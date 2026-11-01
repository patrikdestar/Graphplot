// pybind11 module "_plotter" (wrapped by the pure-Python package "plotter").
//
// Python objects:
//   Figure      - one FigureWindow (toolbar, plot, properties panel)
//   Line        - handle to a 1D dataset
//   Contour     - handle to a 2D dataset drawn as contours
//   Marker      - handle to a vertical/horizontal marker
//   Annotation  - handle to a text annotation
//
// Handles store (figure, object id). They stay valid across undo/redo,
// because commands restore objects with their original id; using a handle
// whose object no longer exists raises LookupError.
//
// All C++ state is touched on the GUI thread only (see GuiThread.h).
#include "GuiThread.h"

#include "commands/PlotEditor.h"
#include "gui/FigureWindow.h"
#include "gui/PeakTable.h"
#include "model/Contour.h"
#include "model/PeakPicker.h"
#include "model/Style.h"
#include "model/PlotModel.h"
#include "qcustomplot.h"
#include "render/PlotRenderer.h"

#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <QApplication>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUndoStack>
#include <cmath>
#include <memory>

namespace py = pybind11;
using namespace gp;
using gp::pyglue::inGui;

namespace {

// ---- conversions ------------------------------------------------------------------

QVector<double> toVector(const py::handle& obj, const char* what)
{
    auto arr = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(obj);
    if (!arr) throw py::type_error(std::string(what) + " must be convertible to a float array");
    if (arr.ndim() != 1)
        throw py::value_error(std::string(what) + " must be one-dimensional");
    QVector<double> v(arr.size());
    std::copy(arr.data(), arr.data() + arr.size(), v.begin());
    return v;
}

py::array_t<double> toNumpy(const QVector<double>& v)
{
    py::array_t<double> a(v.size());
    std::copy(v.begin(), v.end(), a.mutable_data());
    return a;
}

// Error specification: None, scalar, length-n array, or (2, n) array / pair (low, high).
std::pair<QVector<double>, QVector<double>> toErrors(const py::object& obj, qsizetype n, const char* what)
{
    if (obj.is_none()) return {};
    if (py::isinstance<py::float_>(obj) || py::isinstance<py::int_>(obj)) {
        QVector<double> v(n, std::abs(obj.cast<double>()));
        return {v, v};
    }
    auto arr = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(obj);
    if (!arr) throw py::type_error(std::string(what) + " must be a number or an array");
    auto copy = [&](const double* p) {
        QVector<double> v(n);
        for (qsizetype i = 0; i < n; ++i) v[i] = std::abs(p[i]);
        return v;
    };
    if (arr.ndim() == 1 && arr.size() == n) {
        QVector<double> v = copy(arr.data());
        return {v, v};
    }
    if (arr.ndim() == 2 && arr.shape(0) == 2 && arr.shape(1) == n)
        return {copy(arr.data()), copy(arr.data() + n)};
    throw py::value_error(std::string(what) + " must be a scalar, length-n, or shape (2, n) (low, high)");
}

QColor toColor(const py::handle& obj, const char* what = "color")
{
    if (obj.is_none()) return {};
    if (py::isinstance<py::str>(obj)) {
        const QColor c = parseColor(QString::fromStdString(obj.cast<std::string>()));
        if (!c.isValid())
            throw py::value_error(std::string("unknown ") + what + " '" + obj.cast<std::string>() + "'");
        return c;
    }
    if (py::isinstance<py::sequence>(obj)) {
        auto seq = obj.cast<std::vector<double>>();
        if (seq.size() == 3 || seq.size() == 4) {
            const bool unit = std::all_of(seq.begin(), seq.end(), [](double v) { return v <= 1.0; });
            const double k = unit ? 255.0 : 1.0;
            QColor c;
            c.setRgb(int(std::lround(seq[0] * k)), int(std::lround(seq[1] * k)),
                     int(std::lround(seq[2] * k)),
                     seq.size() == 4 ? int(std::lround(seq[3] * k)) : 255);
            return c;
        }
    }
    throw py::type_error(std::string(what) + " must be a name, '#rrggbb' or an (r, g, b[, a]) tuple");
}

std::string colorName(const QColor& c)
{
    return c.alpha() == 255 ? c.name().toStdString() : c.name(QColor::HexArgb).toStdString();
}

LineStyle toLineStyle(const std::string& s)
{
    bool ok = false;
    const LineStyle ls = parseLineStyle(QString::fromStdString(s), &ok);
    if (!ok) throw py::value_error("unknown line style '" + s + "' (use '-', '--', ':', '-.', 'none')");
    return ls;
}

ScatterShape toScatter(const py::handle& obj)
{
    if (obj.is_none()) return ScatterShape::None;
    const std::string s = obj.cast<std::string>();
    bool ok = false;
    const ScatterShape sh = parseScatterShape(QString::fromStdString(s), &ok);
    if (!ok) throw py::value_error("unknown marker symbol '" + s + "' (use o, s, ^, d, x, +)");
    return sh;
}

Orientation toOrientation(const std::string& s)
{
    if (s == "v" || s == "vertical" || s == "x") return Orientation::Vertical;
    if (s == "h" || s == "horizontal" || s == "y") return Orientation::Horizontal;
    throw py::value_error("orientation must be 'v' or 'h'");
}

// ---- Figure ---------------------------------------------------------------------------

class Figure : public std::enable_shared_from_this<Figure> {
public:
    explicit Figure(QPointer<FigureWindow> w) : m_w(std::move(w)) {}

    // Returns the window or raises if it has been closed/destroyed.
    FigureWindow* w() const
    {
        if (!m_w) throw std::runtime_error("the figure has been closed");
        return m_w;
    }
    bool alive() const { return !m_w.isNull(); }
    PlotModel* model() const { return w()->model(); }
    PlotEditor* editor() const { return w()->editor(); }

    // Callbacks ------------------------------------------------------------------------
    // Events are delivered once an edit is complete (not on every intermediate
    // mouse-move during a drag), coalesced per event loop iteration.
    void connectCallbacks();  // also picks up panels created later
    void connectAxes(int ax);
    void addCallback(const std::string& event, py::function fn)
    {
        if (event != "region_changed" && event != "marker_changed" && event != "levels_changed" &&
            event != "closed")
            throw py::value_error("unknown event '" + event +
                                  "' (region_changed, marker_changed, levels_changed, closed)");
        m_callbacks.emplace_back(event, std::move(fn));
    }
    void clearCallbacks() { m_callbacks.clear(); }
    void fire(const std::string& event, const std::function<py::tuple()>& makeArgs);

    // Monotonic counters used by wait_for_*().
    quint64 regionVersion = 0;
    quint64 markerVersion = 0;
    bool closed = false;

private:
    void scheduleFlush();
    void flush();

    QPointer<FigureWindow> m_w;
    std::vector<std::pair<std::string, py::function>> m_callbacks;
    QSet<int> m_dirtyRegions;
    QSet<QPair<int, ObjectId>> m_dirtyMarkers;
    QSet<QPair<int, ObjectId>> m_dirtyMatrices;
    int m_connectedAxes = 0;
    bool m_baseConnected = false;
    bool m_flushScheduled = false;
};

using FigurePtr = std::shared_ptr<Figure>;

// Handle base: (figure, id)
struct Handle {
    // Remembers the panel (axes) that was current when the object was created.
    Handle(FigurePtr f, ObjectId i) : fig(std::move(f)), id(i), ax(fig->w()->currentAxes()) {}
    Handle(FigurePtr f, ObjectId i, int a) : fig(std::move(f)), id(i), ax(a) {}
    FigurePtr fig;
    ObjectId id;
    int ax;
    PlotModel* model() const
    {
        FigureWindow* w = fig->w();
        if (ax < 0 || ax >= w->axesCount()) throw py::key_error("the panel no longer exists");
        return w->axes(ax).model;
    }
    PlotEditor* editor() const { return fig->w()->axes(ax).editor; }
};

struct Line : Handle {
    const Dataset& get() const
    {
        const Dataset* d = model()->dataset(id);
        if (!d) throw py::key_error("this dataset no longer exists");
        return *d;
    }
    void setStyle(const std::function<void(QString&, DatasetStyle&)>& f)
    {
        inGui([&] {
            const Dataset& d = get();
            QString name = d.name;
            DatasetStyle s = d.style;
            f(name, s);
            editor()->setDatasetAppearance(id, name, s);
        });
    }
};
struct Contour : Handle {
    const Matrix& get() const
    {
        const Matrix* m = model()->matrix(id);
        if (!m) throw py::key_error("this 2D dataset no longer exists");
        return *m;
    }
    void setStyle(const std::function<void(QString&, ContourStyle&)>& f)
    {
        inGui([&] {
            const Matrix& m = get();
            QString name = m.name;
            ContourStyle s = m.style;
            f(name, s);
            editor()->setMatrixAppearance(id, name, s);
        });
    }
};
struct MarkerH : Handle {
    Marker get() const
    {
        const Marker* m = model()->marker(id);
        if (!m) throw py::key_error("this marker no longer exists");
        return *m;
    }
    void edit(const std::function<void(Marker&)>& f)
    {
        inGui([&] {
            Marker m = get();
            f(m);
            editor()->updateMarker(m);
        });
    }
};
struct PeakH : Handle {
    Peak get() const
    {
        const Peak* p = model()->peak(id);
        if (!p) throw py::key_error("this peak no longer exists");
        return *p;
    }
    void edit(const std::function<void(Peak&)>& f, const QString& text = "Edit peak")
    {
        inGui([&] {
            Peak p = get();
            f(p);
            editor()->updatePeak(p, std::nullopt, false, text);
        });
    }
};

struct AnnotationH : Handle {
    Annotation get() const
    {
        const Annotation* a = model()->annotation(id);
        if (!a) throw py::key_error("this annotation no longer exists");
        return *a;
    }
    void edit(const std::function<void(Annotation&)>& f)
    {
        inGui([&] {
            Annotation a = get();
            f(a);
            editor()->updateAnnotation(a);
        });
    }
};

void Figure::connectAxes(int ax)
{
    FigureWindow* win = w();
    PlotModel* m = win->axes(ax).model;
    std::weak_ptr<Figure> weak = weak_from_this();
    auto self = [weak]() { return weak.lock(); };
    QObject::connect(m, &PlotModel::regionChanged, win, [self, ax] {
        if (auto f = self()) {
            f->m_dirtyRegions.insert(ax);
            f->scheduleFlush();
        }
    });
    QObject::connect(m, &PlotModel::markerChanged, win, [self, ax](ObjectId id) {
        if (auto f = self()) {
            f->m_dirtyMarkers.insert({ax, id});
            f->scheduleFlush();
        }
    });
    QObject::connect(m, &PlotModel::matrixAppearanceChanged, win, [self, ax](ObjectId id) {
        if (auto f = self()) {
            f->m_dirtyMatrices.insert({ax, id});
            f->scheduleFlush();
        }
    });
    m_connectedAxes = std::max(m_connectedAxes, ax + 1);
}

void Figure::connectCallbacks()
{
    FigureWindow* win = w();
    std::weak_ptr<Figure> weak = weak_from_this();
    auto self = [weak]() { return weak.lock(); };
    for (int i = m_connectedAxes; i < win->axesCount(); ++i) connectAxes(i);
    if (m_baseConnected) return;
    m_baseConnected = true;
    // A drag ends with a command push (or a plain release): flush then.
    QObject::connect(win->editor()->undoStack(), &QUndoStack::indexChanged, win, [self] {
        if (auto f = self()) f->scheduleFlush();
    });
    QObject::connect(win, &FigureWindow::closed, win, [self] {
        if (auto f = self()) {
            f->closed = true;
            f->fire("closed", []() -> py::tuple { return py::make_tuple(); });
        }
    });
}

void Figure::scheduleFlush()
{
    if (m_flushScheduled) return;
    m_flushScheduled = true;
    QTimer::singleShot(0, m_w, [weak = weak_from_this()] {
        if (auto f = weak.lock()) f->flush();
    });
}

void Figure::flush()
{
    m_flushScheduled = false;
    if (!m_w) return;
    // Wait until the mouse is released: mid-drag states are previews.
    if (QApplication::mouseButtons() != Qt::NoButton) {
        QTimer::singleShot(50, m_w, [weak = weak_from_this()] {
            if (auto f = weak.lock()) f->scheduleFlush();
        });
        return;
    }
    auto self = shared_from_this();
    FigureWindow* win = m_w;
    for (int ax : std::exchange(m_dirtyRegions, {})) {
        if (ax >= win->axesCount()) continue;
        ++regionVersion;
        const Region r = win->axes(ax).model->region();
        fire("region_changed", [r]() -> py::tuple {
            if (r.active) return py::make_tuple(r.xmin, r.xmax);
            return py::make_tuple(py::none(), py::none());
        });
    }
    for (const auto& [ax, id] : std::exchange(m_dirtyMarkers, {})) {
        if (ax >= win->axesCount() || !win->axes(ax).model->marker(id)) continue;
        ++markerVersion;
        fire("marker_changed", [self, id, ax]() -> py::tuple { return py::make_tuple(MarkerH{{self, id, ax}}); });
    }
    for (const auto& [ax, id] : std::exchange(m_dirtyMatrices, {})) {
        if (ax >= win->axesCount() || !win->axes(ax).model->matrix(id)) continue;
        fire("levels_changed", [self, id, ax]() -> py::tuple { return py::make_tuple(Contour{{self, id, ax}}); });
    }
}

void Figure::fire(const std::string& event, const std::function<py::tuple()>& makeArgs)
{
    bool any = false;
    for (const auto& [ev, fn] : m_callbacks) any |= ev == event;
    if (!any) return;
    py::gil_scoped_acquire gil;  // the event loop runs with the GIL released
    const auto callbacks = m_callbacks;  // callbacks may add/remove callbacks
    for (const auto& [ev, fn] : callbacks) {
        if (ev != event) continue;
        try {
            fn(*makeArgs());
        } catch (py::error_already_set& e) {
            // Never let a Python exception unwind through Qt's event loop.
            e.discard_as_unraisable(py::str("plotter callback for '" + event + "'"));
        }
    }
}

// ---- Figure creation ---------------------------------------------------------------------

FigurePtr makeFigure(const py::object& title, int width, int height)
{
    pyglue::ensureApp();
    if (!pyglue::isGuiThread())
        throw std::runtime_error("figures must be created on the GUI (main) thread");
    auto* win = new FigureWindow;
    win->setAttribute(Qt::WA_DeleteOnClose, false);  // keep data accessible after closing
    win->resize(width, height);
    if (!title.is_none()) {
        const QString t = QString::fromStdString(title.cast<std::string>());
        win->setWindowTitle(t);
        AxesInfo info = win->model()->axesInfo();
        info.title = t;
        win->model()->setAxesInfo(info);
    }
    auto fig = std::make_shared<Figure>(QPointer<FigureWindow>(win));
    fig->connectCallbacks();
    return fig;
}

// Validates and returns (x0, x1) for a regular grid axis.
std::pair<double, double> gridAxis(const py::object& obj, int n, const char* name)
{
    if (obj.is_none()) return {0.0, double(n - 1)};
    const QVector<double> v = toVector(obj, name);
    if (v.size() == 2 && n != 2) return {v[0], v[1]};  // (start, stop)
    if (v.size() != n)
        throw py::value_error(std::string(name) + " must have one value per grid column/row or be (start, stop)");
    const double step = (v.last() - v.first()) / (n - 1);
    for (int i = 1; i < n; ++i)
        if (std::abs((v[i] - v[i - 1]) - step) > 1e-6 * std::max(std::abs(step), 1e-300))
            throw py::value_error(std::string(name) + " must be uniformly spaced");
    return {v.first(), v.last()};
}

Matrix matrixFromNumpy(const py::object& zobj, const py::object& x, const py::object& y)
{
    auto z = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(zobj);
    if (!z || z.ndim() != 2) throw py::value_error("z must be a 2D array (rows = y, columns = x)");
    Matrix m;
    m.ny = int(z.shape(0));
    m.nx = int(z.shape(1));
    if (m.nx < 2 || m.ny < 2) throw py::value_error("z must be at least 2 x 2");
    m.z.resize(z.size());
    std::copy(z.data(), z.data() + z.size(), m.z.begin());
    std::tie(m.x0, m.x1) = gridAxis(x, m.nx, "x");
    std::tie(m.y0, m.y1) = gridAxis(y, m.ny, "y");
    return m;
}

}  // namespace

// ============================================================================================

PYBIND11_MODULE(_plotter, mod)
{
    mod.doc() = "Native part of the 'plotter' package (Graphplot C++/Qt bindings).";
    gp::reloadDefaultStyle();  // the user's default style file, if any (once, at import)

    mod.def("process_events", &pyglue::processEvents, py::arg("ms") = 0,
            "Run the Qt event loop for `ms` milliseconds (keeps windows responsive in scripts).");
    mod.def(
        "_run_until_closed",
        [](const std::vector<FigurePtr>& figs, double timeout) {
            return pyglue::runEventLoopUntil(
                [figs] {
                    for (const auto& f : figs)
                        if (f->alive() && f->w()->isVisible()) return false;
                    return true;
                },
                timeout < 0 ? -1 : int(timeout * 1000));
        },
        py::arg("figures"), py::arg("timeout") = -1.0);
    mod.def("_rgba", [](const py::object& c) {
        const QColor q = toColor(c);
        return py::make_tuple(q.redF(), q.greenF(), q.blueF(), q.alphaF());
    }, "Colour spec -> (r, g, b, a) floats.");
    mod.def("_has_app", [] { return QCoreApplication::instance() != nullptr; });

    // ---- global (default) style: used for figures created afterwards -----------
    mod.def("_style_builtins", [] {
        std::vector<std::string> out;
        for (const QString& n : builtinStyleNames()) out.push_back(n.toStdString());
        return out;
    });
    mod.def("_style_keys", [] {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& k : Style::keys()) out.emplace_back(k.key.toStdString(), k.description.toStdString());
        return out;
    });
    mod.def("_style_use", [](const std::string& nameOrPath) {
        Style s;
        QStringList errors;
        if (!loadStyle(QString::fromStdString(nameOrPath), &s, &errors))
            throw std::runtime_error(errors.join("; ").toStdString());
        setDefaultStyle(s);
        std::vector<std::string> out;
        for (const QString& e : errors) out.push_back(e.toStdString());
        return out;
    });
    mod.def("_style_get", [](const std::string& key) {
        const QString v = defaultStyle().value(QString::fromStdString(key));
        if (v.isNull()) throw py::key_error("unknown style key '" + key + "'");
        return v.toStdString();
    });
    mod.def("_style_set", [](const std::string& key, const std::string& value) {
        Style s = defaultStyle();
        QString err;
        if (!s.set(QString::fromStdString(key), QString::fromStdString(value), &err))
            throw py::value_error(err.toStdString());
        setDefaultStyle(s);
    });
    mod.def("_style_text", [] { return defaultStyle().toText().toStdString(); });
    mod.def("_style_save", [](const std::string& fn) {
        QString err;
        if (!saveStyle(QString::fromStdString(fn), defaultStyle(), &err)) throw std::runtime_error(err.toStdString());
    });
    mod.def("_style_default_path", [] { return userDefaultStylePath().toStdString(); });
    mod.def("_style_reload", [] {
        QStringList errors;
        reloadDefaultStyle(&errors);
        std::vector<std::string> out;
        for (const QString& e : errors) out.push_back(e.toStdString());
        return out;
    });

    // ---- Figure ------------------------------------------------------------------------
    py::class_<Figure, FigurePtr> fig(mod, "Figure", "A plot window.");
    fig.def(py::init(&makeFigure), py::arg("title") = py::none(), py::arg("width") = 1000,
            py::arg("height") = 650);

    fig.def(
        "plot",
        [](FigurePtr self, const py::object& x, const py::object& y, const py::object& label,
           const py::object& color, const py::object& widthObj, const std::string& style,
           const py::object& marker, const py::object& markersizeObj, bool visible, bool replace,
           const py::object& step, const py::object& fill, const py::object& fillTo,
           const py::object& xerr, const py::object& yerr) {
            const double width = widthObj.is_none() ? 0.0 : widthObj.cast<double>();
            const double markersize = markersizeObj.is_none() ? 0.0 : markersizeObj.cast<double>();
            QVector<double> xv, yv;
            if (y.is_none()) {  // plot(y)
                yv = toVector(x, "y");
                xv.resize(yv.size());
                for (qsizetype i = 0; i < xv.size(); ++i) xv[i] = double(i);
            } else {
                xv = toVector(x, "x");
                yv = toVector(y, "y");
            }
            if (xv.size() != yv.size())
                throw py::value_error("x and y must have the same length");
            Dataset d;
            if (!label.is_none()) d.name = QString::fromStdString(label.cast<std::string>());
            d.style.inLegend = !label.is_none();  // like matplotlib: only labelled lines
            d.style.color = toColor(color);
            d.style.lineWidth = width;
            d.style.lineStyle = toLineStyle(style);
            d.style.scatter = toScatter(marker);
            d.style.scatterSize = markersize;
            d.style.visible = visible;
            if (!step.is_none()) {
                bool ok = false;
                d.style.step = parseStepMode(QString::fromStdString(py::str(step).cast<std::string>()), &ok);
                if (!ok) throw py::value_error("step must be None, 'pre', 'post' or 'mid'");
            }
            if (!fill.is_none() && !(py::isinstance<py::bool_>(fill) && !fill.cast<bool>())) {
                // True: the line colour, translucent; otherwise any colour spec.
                d.style.fillColor = py::isinstance<py::bool_>(fill) ? QColor() : toColor(fill, "fill");
                if (!d.style.fillColor.isValid()) d.style.fillColor = QColor(0, 0, 0, 0);  // resolved below
            }
            if (!fillTo.is_none()) d.style.fillTo = fillTo.cast<Line&>().id;
            std::tie(d.xErrLow, d.xErrHigh) = toErrors(xerr, xv.size(), "xerr");
            std::tie(d.yErrLow, d.yErrHigh) = toErrors(yerr, xv.size(), "yerr");
            d.x = std::move(xv);
            d.y = std::move(yv);
            const ObjectId id = inGui([&] {
                if (d.style.fillColor == QColor(0, 0, 0, 0)) {
                    // fill=True: translucent line colour (assign the colour now).
                    if (!d.style.color.isValid()) d.style.color = self->model()->takeNextDatasetColor();
                    QColor c = d.style.color;
                    c.setAlphaF(0.3);
                    d.style.fillColor = c;
                }
                PlotEditor* ed = self->editor();
                if (replace && !d.name.isEmpty()) {
                    if (const Dataset* old = self->model()->datasetByName(d.name)) {
                        // Update in place: keep identity, update data and the given style.
                        const ObjectId oid = old->id;
                        ed->undoStack()->beginMacro("Update dataset");
                        ed->setDatasetData(oid, d.x, d.y, true);
                        if (!d.style.color.isValid()) d.style.color = old->style.color;
                        if (!(d.style.lineWidth > 0)) d.style.lineWidth = old->style.lineWidth;
                        if (!(d.style.scatterSize > 0)) d.style.scatterSize = old->style.scatterSize;
                        ed->setDatasetAppearance(oid, d.name, d.style);
                        ed->undoStack()->endMacro();
                        return oid;
                    }
                }
                const bool first = self->model()->datasets().empty() && self->model()->matrices().empty();
                const ObjectId nid = ed->addDataset(d);
                if (first) {
                    self->w()->renderer()->autoscale();
                    self->w()->renderer()->captureHomeView();
                }
                return nid;
            });
            return Line{{self, id}};
        },
        py::arg("x"), py::arg("y") = py::none(), py::kw_only(), py::arg("label") = py::none(),
        py::arg("color") = py::none(), py::arg("width") = py::none(), py::arg("style") = "-",
        py::arg("marker") = py::none(), py::arg("markersize") = py::none(), py::arg("visible") = true,
        py::arg("replace") = false, py::arg("step") = py::none(), py::arg("fill") = py::none(),
        py::arg("fill_to") = py::none(), py::arg("xerr") = py::none(), py::arg("yerr") = py::none(),
        R"doc(Plot y against x (or y against its index if only one array is given).

label     dataset name shown in the legend
color     name ('blue', 'C3', '#ff8800') or (r, g, b[, a]) tuple; default: next palette colour
width     line width in pixels (default: style 'lines.linewidth')
style     '-', '--', ':', '-.' or 'none'
marker    point symbol: 'o', 's', '^', 'd', 'x', '+' or None
replace   if True and a dataset with the same label exists, update it in place
step      None, 'pre', 'post' (histograms from bin edges) or 'mid'
fill      True (line colour, translucent) or a colour: fill down to y = 0, or to fill_to
fill_to   another Line: fill the band between the two curves (uncertainty bands)
xerr/yerr error bars: scalar, array, or shape (2, n) for (low, high)
Returns a Line handle.)doc");

    fig.def(
        "contour",
        [](FigurePtr self, const py::object& z, const py::object& x, const py::object& y,
           const py::object& label, const py::object& levelsObj, const py::object& factorObj,
           const py::object& base,
           const py::object& color, const py::object& negativeColor, bool negative, bool image,
           const py::object& linewidthObj) {
            const int levels = levelsObj.is_none() ? 0 : levelsObj.cast<int>();
            const double factor = factorObj.is_none() ? 0.0 : factorObj.cast<double>();
            const double linewidth = linewidthObj.is_none() ? 0.0 : linewidthObj.cast<double>();
            Matrix m = matrixFromNumpy(z, x, y);
            if (!label.is_none()) m.name = QString::fromStdString(label.cast<std::string>());
            if (!levelsObj.is_none() && levels < 1) throw py::value_error("levels must be >= 1");
            if (!factorObj.is_none() && !(factor > 1.0)) throw py::value_error("factor must be > 1");
            m.style.levelCount = levels;
            m.style.factor = factor;
            m.style.baseLevel = base.is_none() ? 0.0 : base.cast<double>();
            m.style.positiveColor = toColor(color);
            if (!negativeColor.is_none()) m.style.negativeColor = toColor(negativeColor, "negative_color");
            m.style.showNegative = negative;
            m.style.showImage = image;
            m.style.lineWidth = linewidth;
            const ObjectId id = inGui([&] {
                const bool first = self->model()->datasets().empty() && self->model()->matrices().empty();
                const ObjectId nid = self->editor()->addMatrix(m);
                if (first) {
                    self->w()->renderer()->autoscale();
                    self->w()->renderer()->captureHomeView();
                }
                return nid;
            });
            return Contour{{self, id}};
        },
        py::arg("z"), py::arg("x") = py::none(), py::arg("y") = py::none(), py::kw_only(),
        py::arg("label") = py::none(), py::arg("levels") = py::none(), py::arg("factor") = py::none(),
        py::arg("base") = py::none(), py::arg("color") = py::none(),
        py::arg("negative_color") = py::none(), py::arg("negative") = true,
        py::arg("image") = false, py::arg("linewidth") = py::none(),
        R"doc(Show 2D data z[row=y, column=x] as contour lines on a regular grid.

x, y      grid coordinates: full uniformly spaced arrays or (start, stop); default: indices
levels    number of positive levels: base * factor**k, k = 0..levels-1
base      lowest level; default chooses the top level just below max |z|
negative  also draw the mirrored negative levels (in negative_color)
image     draw a colour-map image underneath the contours
In the window, Alt+mouse wheel (or the wheel in 'Levels' mode) scrolls the
levels; '*' and '/' halve / double them. Returns a Contour handle.)doc");

    fig.def(
        "open_bruker",
        [](FigurePtr self, const std::string& path) {
            QString err;
            const ObjectId id = inGui([&] {
                return self->w()->openBruker(QString::fromStdString(path), &err);
            });
            if (id == kInvalidId) throw std::runtime_error(err.toStdString());
            return Contour{{self, id}};
        },
        py::arg("path"),
        R"doc(Open a Bruker TopSpin processed 2D spectrum (pdata/N folder, 2rr file or
experiment folder). Axes are in ppm, drawn high-to-low as in TopSpin, labelled
with the nuclei; the diagonal is shown for homonuclear spectra. Returns a Contour.)doc");

    fig.def(
        "pick_peaks",
        [](FigurePtr self, const py::object& threshold, int minDistance, bool negative,
           double excludeDiagonal, const std::string& region, const py::object& contour) {
            if (region != "view" && region != "all" && region != "selection")
                throw py::value_error("region must be 'view', 'all' or 'selection'");
            PeakPickOptions o;
            o.threshold = threshold.is_none() ? 0.0 : threshold.cast<double>();
            o.minDistance = minDistance;
            o.negative = negative;
            o.diagonalExclusion = excludeDiagonal;
            ObjectId only = kInvalidId;
            if (!contour.is_none()) only = contour.cast<Contour&>().id;
            const QList<ObjectId> ids = inGui([&] {
                PlotRenderer* r = self->w()->renderer();
                if (region == "view") {
                    const ViewRange v = r->viewRange();
                    o.restrictToRect = true;
                    o.xmin = v.xmin; o.xmax = v.xmax; o.ymin = v.ymin; o.ymax = v.ymax;
                } else if (region == "selection") {
                    const Region s = self->model()->region();
                    if (!s.active) throw py::value_error("no region is selected");
                    const ViewRange v = r->viewRange();
                    o.restrictToRect = true;
                    o.xmin = s.xmin; o.xmax = s.xmax; o.ymin = v.ymin; o.ymax = v.ymax;
                }
                QList<ObjectId> targets;
                if (only != kInvalidId) {
                    if (!self->model()->matrix(only)) throw py::key_error("contour no longer exists");
                    targets << only;
                } else {
                    for (const auto& m : self->model()->matrices())
                        if (m.style.visible) targets << m.id;
                }
                if (targets.isEmpty()) throw py::value_error("peak picking needs 2D data (fig.contour / open_bruker)");
                QList<ObjectId> all;
                self->editor()->undoStack()->beginMacro("Pick peaks");
                for (ObjectId t : targets) all += self->editor()->pickPeaks(t, o);
                self->editor()->undoStack()->endMacro();
                return all;
            });
            std::vector<PeakH> out;
            for (ObjectId id : ids) out.push_back(PeakH{{self, id}});
            return out;
        },
        py::kw_only(), py::arg("threshold") = py::none(), py::arg("min_distance") = 3,
        py::arg("negative") = false, py::arg("exclude_diagonal") = 0.0,
        py::arg("region") = "view", py::arg("contour") = py::none(),
        R"doc(Automatic peak picking on the 2D data; returns the *new* peaks (one undo step).

threshold         minimum height; default = the lowest contour level shown
min_distance      minimum separation in data points
negative          also pick negative peaks
exclude_diagonal  skip peaks with |x - y| < value (pick only off-diagonal cross peaks)
region            'view' (visible area), 'all', or 'selection' (selected X region)
Peaks closer than min_distance to existing peaks are not added again.)doc");
    fig.def(
        "add_peak",
        [](FigurePtr self, double x, double y, const std::string& label, const std::string& comment,
           bool snap) {
            const ObjectId id = inGui([&] {
                Peak p;
                p.x = x;
                p.y = y;
                if (const Matrix* m = self->w()->renderer()->matrixAt({x, y})) {
                    p.matrixId = m->id;
                    p.intensity = sampleMatrix(*m, x, y);
                    if (snap)
                        if (auto s = snapToPeak(*m, x, y, 3)) p = *s;
                }
                p.label = QString::fromStdString(label);
                p.comment = QString::fromStdString(comment);
                return self->editor()->addPeak(p);
            });
            return PeakH{{self, id}};
        },
        py::arg("x"), py::arg("y"), py::kw_only(), py::arg("label") = "", py::arg("comment") = "",
        py::arg("snap") = true,
        "Add a peak at (x, y); with snap=True it moves to the nearest maximum (within 3 points).");
    fig.def("peaks", [](FigurePtr self) {
        std::vector<PeakH> out;
        inGui([&] {
            for (const auto& p : self->model()->peaks()) out.push_back(PeakH{{self, p.id}});
        });
        return out;
    });
    fig.def(
        "peak_table",
        [](FigurePtr self) {
            // Plain data for analysis / pandas: list of dicts.
            py::list rows;
            const std::vector<Peak> ps = inGui([&] { return self->model()->peaks(); });
            for (const Peak& p : ps) {
                py::dict d;
                d["x"] = p.x;
                d["y"] = p.y;
                d["intensity"] = p.intensity;
                d["label"] = p.label.toStdString();
                d["comment"] = p.comment.toStdString();
                rows.append(d);
            }
            return rows;
        },
        "All peaks as a list of dicts (x, y, intensity, label, comment) - e.g. pandas.DataFrame(fig.peak_table()).");
    fig.def(
        "export_peaks",
        [](FigurePtr self, const std::string& fn) {
            QString err;
            if (!inGui([&] { return self->w()->peakTable()->exportCsv(QString::fromStdString(fn), &err); }))
                throw std::runtime_error(err.toStdString());
        },
        py::arg("filename"), "Write the peak list as CSV (x, y, intensity, label, comment).");
    fig.def(
        "import_peaks",
        [](FigurePtr self, const std::string& fn) {
            QString err;
            if (!inGui([&] { return self->w()->peakTable()->importCsv(QString::fromStdString(fn), &err); }))
                throw std::runtime_error(err.toStdString());
        },
        py::arg("filename"), "Add peaks from a CSV written by export_peaks (one undo step).");
    fig.def("show_peak_table", [](FigurePtr self) { inGui([&] { self->w()->showPeakTable(); }); },
            "Bring the 'Peaks' panel (peak list) to the front.");
    fig.def("clear_peaks", [](FigurePtr self) {
        inGui([&] {
            QList<ObjectId> ids;
            for (const auto& p : self->model()->peaks()) ids << p.id;
            self->editor()->removePeaks(ids, "Delete all peaks");
        });
    });

    fig.def(
        "marker",
        [](FigurePtr self, double pos, const std::string& orientation, const py::object& color,
           const std::string& label, const std::string& style, double width, bool showValue) {
            Marker m;
            m.orientation = toOrientation(orientation);
            m.position = pos;
            m.label = QString::fromStdString(label);
            const QColor c = toColor(color);
            m.lineStyle = toLineStyle(style);
            m.lineWidth = width;
            m.showValue = showValue;
            const ObjectId id = inGui([&] {
                m.color = c.isValid() ? c : paletteColor(7 + int(self->model()->markers().size()));
                return self->editor()->addMarker(m);
            });
            return MarkerH{{self, id}};
        },
        py::arg("position"), py::arg("orientation") = "v", py::kw_only(),
        py::arg("color") = py::none(), py::arg("label") = "", py::arg("style") = "--",
        py::arg("width") = 1.5, py::arg("show_value") = true,
        "Add a vertical ('v', at x = position) or horizontal ('h', at y = position) marker.");

    fig.def(
        "annotate",
        [](FigurePtr self, const std::string& text, double x, double y, const py::object& color,
           const py::object& fontsizeObj) {
            const double fontsize = fontsizeObj.is_none() ? 0.0 : fontsizeObj.cast<double>();
            Annotation a;
            a.text = QString::fromStdString(text);
            a.x = x;
            a.y = y;
            const QColor c = toColor(color);
            if (c.isValid()) a.color = c;
            a.fontSize = fontsize;
            return AnnotationH{{self, inGui([&] { return self->editor()->addAnnotation(a); })}};
        },
        py::arg("text"), py::arg("x"), py::arg("y"), py::kw_only(), py::arg("color") = py::none(),
        py::arg("fontsize") = py::none(), "Add a text annotation anchored (bottom-left) at data (x, y).");

    // -- region
    fig.def(
        "get_selected_region",
        [](FigurePtr self) -> py::object {
            const Region r = inGui([&] { return self->model()->region(); });
            if (!r.active) return py::none();
            return py::make_tuple(r.xmin, r.xmax);
        },
        "Return the selected X region as (xmin, xmax), or None if nothing is selected.");
    fig.def(
        "set_selected_region",
        [](FigurePtr self, double xmin, double xmax) {
            inGui([&] { self->editor()->setRegion(Region{true, xmin, xmax}); });
        },
        py::arg("xmin"), py::arg("xmax"));
    fig.def("clear_selection", [](FigurePtr self) { inGui([&] { self->editor()->clearRegion(); }); });
    fig.def(
        "wait_for_region",
        [](FigurePtr self, double timeout) -> py::object {
            const quint64 start = self->regionVersion;
            const bool ok = pyglue::runEventLoopUntil(
                [self, start] { return self->regionVersion != start || self->closed; },
                timeout < 0 ? -1 : int(timeout * 1000));
            const Region r = self->alive() ? self->model()->region() : Region{};
            if (!ok || !r.active) return py::none();
            return py::make_tuple(r.xmin, r.xmax);
        },
        py::arg("timeout") = -1.0,
        "Keep the window interactive until the user finishes selecting a region; return it.\n"
        "Returns None on timeout, when the window is closed or the selection was cleared.");

    // -- object lists
    fig.def("lines", [](FigurePtr self) {
        std::vector<Line> out;
        inGui([&] {
            for (const auto& d : self->model()->datasets()) out.push_back(Line{{self, d.id}});
        });
        return out;
    });
    fig.def("contours", [](FigurePtr self) {
        std::vector<Contour> out;
        inGui([&] {
            for (const auto& m : self->model()->matrices()) out.push_back(Contour{{self, m.id}});
        });
        return out;
    });
    fig.def("markers", [](FigurePtr self) {
        std::vector<MarkerH> out;
        inGui([&] {
            for (const auto& m : self->model()->markers()) out.push_back(MarkerH{{self, m.id}});
        });
        return out;
    });
    fig.def("annotations", [](FigurePtr self) {
        std::vector<AnnotationH> out;
        inGui([&] {
            for (const auto& a : self->model()->annotations()) out.push_back(AnnotationH{{self, a.id}});
        });
        return out;
    });
    fig.def(
        "get_marker_positions",
        [](FigurePtr self) {
            std::vector<std::tuple<std::string, std::string, double>> out;
            inGui([&] {
                for (const auto& m : self->model()->markers())
                    out.emplace_back(m.label.toStdString(),
                                     m.orientation == Orientation::Vertical ? "v" : "h", m.position);
            });
            return out;
        },
        "List of (label, orientation, position) for all markers.");
    fig.def(
        "line",
        [](FigurePtr self, const std::string& label) {
            const ObjectId id = inGui([&] {
                const Dataset* d = self->model()->datasetByName(QString::fromStdString(label));
                if (!d) throw py::key_error("no dataset named '" + label + "'");
                return d->id;
            });
            return Line{{self, id}};
        },
        py::arg("label"), "Return the Line handle of the dataset with this label.");
    fig.def("remove", [](FigurePtr self, const py::object& h) {
        ObjectRef ref;
        if (py::isinstance<Line>(h)) ref = {ObjectKind::Dataset, h.cast<Line&>().id};
        else if (py::isinstance<Contour>(h)) ref = {ObjectKind::Matrix, h.cast<Contour&>().id};
        else if (py::isinstance<MarkerH>(h)) ref = {ObjectKind::Marker, h.cast<MarkerH&>().id};
        else if (py::isinstance<AnnotationH>(h)) ref = {ObjectKind::Annotation, h.cast<AnnotationH&>().id};
        else if (py::isinstance<PeakH>(h)) ref = {ObjectKind::Peak, h.cast<PeakH&>().id};
        else throw py::type_error("expected a Line, Contour, Marker or Annotation");
        inGui([&] { self->editor()->removeObject(ref); });
    });
    fig.def("clear", [](FigurePtr self) {
        inGui([&] {
            PlotEditor* ed = self->editor();
            PlotModel* m = self->model();
            ed->undoStack()->beginMacro("Clear figure");
            while (!m->peaks().empty()) ed->removePeak(m->peaks().back().id);
            while (!m->annotations().empty()) ed->removeAnnotation(m->annotations().back().id);
            while (!m->markers().empty()) ed->removeMarker(m->markers().back().id);
            while (!m->matrices().empty()) ed->removeMatrix(m->matrices().back().id);
            while (!m->datasets().empty()) ed->removeDataset(m->datasets().back().id);
            ed->clearRegion();
            ed->undoStack()->endMacro();
        });
    }, "Remove all datasets, markers, annotations and the selection (undoable).");

    // -- view
    auto getLim = [](FigurePtr self, bool x) {
        const ViewRange r = inGui([&] { return self->w()->renderer()->viewRange(); });
        return x ? py::tuple(py::make_tuple(r.xmin, r.xmax)) : py::tuple(py::make_tuple(r.ymin, r.ymax));
    };
    fig.def("xlim", [getLim](FigurePtr s) { return getLim(s, true); }, "Current x axis limits.");
    fig.def("ylim", [getLim](FigurePtr s) { return getLim(s, false); }, "Current y axis limits.");
    fig.def(
        "set_xlim",
        [](FigurePtr self, double a, double b) {
            inGui([&] {
                ViewRange r = self->w()->renderer()->viewRange();
                r.xmin = a;
                r.xmax = b;
                self->w()->renderer()->setViewRange(r);
            });
        },
        py::arg("xmin"), py::arg("xmax"));
    fig.def(
        "set_ylim",
        [](FigurePtr self, double a, double b) {
            inGui([&] {
                ViewRange r = self->w()->renderer()->viewRange();
                r.ymin = a;
                r.ymax = b;
                self->w()->renderer()->setViewRange(r);
            });
        },
        py::arg("ymin"), py::arg("ymax"));
    fig.def(
        "autoscale",
        [](FigurePtr self, bool x, bool y) { inGui([&] { self->w()->renderer()->autoscale(x, y); }); },
        py::arg("x") = true, py::arg("y") = true);
    fig.def("reset_view", [](FigurePtr self) { inGui([&] { self->w()->renderer()->resetView(); }); });
    fig.def("set_home_view", [](FigurePtr self) {
        inGui([&] { self->w()->renderer()->captureHomeView(); });
    }, "Make the current view the one restored by reset_view() / the Home key.");

    // -- labels
    auto axesProp = [](QString AxesInfo::*field) {
        return std::make_pair(
            [field](FigurePtr self) {
                return inGui([&] { return (self->model()->axesInfo().*field).toStdString(); });
            },
            [field](FigurePtr self, const std::string& v) {
                inGui([&] {
                    AxesInfo a = self->model()->axesInfo();
                    a.*field = QString::fromStdString(v);
                    self->editor()->setAxesInfo(a);
                });
            });
    };
    auto [tg, ts] = axesProp(&AxesInfo::title);
    auto [xg, xs] = axesProp(&AxesInfo::xLabel);
    auto [yg, ys] = axesProp(&AxesInfo::yLabel);
    fig.def_property("title", tg, ts);
    fig.def_property("xlabel", xg, xs);
    fig.def_property("ylabel", yg, ys);
    fig.def(
        "legend",
        [](FigurePtr self, bool visible) {
            inGui([&] {
                AxesInfo a = self->model()->axesInfo();
                a.legendVisible = visible;
                self->editor()->setAxesInfo(a);
            });
        },
        py::arg("visible") = true);
    auto axesFlag = [](bool AxesInfo::*field) {
        return std::make_pair(
            [field](FigurePtr self) { return inGui([&] { return self->model()->axesInfo().*field; }); },
            [field](FigurePtr self, bool v) {
                inGui([&] {
                    AxesInfo a = self->model()->axesInfo();
                    a.*field = v;
                    self->editor()->setAxesInfo(a);
                });
            });
    };
    {
        auto [g1, s1] = axesFlag(&AxesInfo::xReversed);
        auto [g2, s2] = axesFlag(&AxesInfo::yReversed);
        auto [g3, s3] = axesFlag(&AxesInfo::showDiagonal);
        auto [g4, s4] = axesFlag(&AxesInfo::showPeakLabels);
        fig.def_property("peak_labels", g4, s4, "Show peak labels on the plot.");
        fig.def_property("x_reversed", g1, s1, "High x values on the left (NMR convention).");
        fig.def_property("y_reversed", g2, s2);
        fig.def_property("diagonal", g3, s3, "Show the dashed y = x diagonal.");
    }
    fig.def(
        "grid",
        [](FigurePtr self, bool visible) {
            inGui([&] {
                AxesInfo a = self->model()->axesInfo();
                a.gridVisible = visible;
                self->editor()->setAxesInfo(a);
            });
        },
        py::arg("visible") = true);

    // -- per-figure style (undoable)
    fig.def(
        "use_style",
        [](FigurePtr self, const std::string& nameOrPath) {
            QStringList errors;
            const bool ok = inGui([&] {
                return self->w()->applyStyle(QString::fromStdString(nameOrPath), &errors);
            });
            if (!ok) throw std::runtime_error(errors.join("; ").toStdString());
            std::vector<std::string> out;
            for (const QString& e : errors) out.push_back(e.toStdString());
            return out;
        },
        py::arg("name_or_path"),
        "Apply a built-in style ('default', 'science', 'nmr', 'presentation') or a .gpstyle file\n"
        "to this figure. Returns a list of ignored entries (empty when everything was valid).");
    fig.def(
        "_set_style_params",
        [](FigurePtr self, const std::vector<std::pair<std::string, std::string>>& kv) {
            inGui([&] {
                Style s = self->model()->style();
                for (const auto& [k, v] : kv) {
                    QString err;
                    if (!s.set(QString::fromStdString(k), QString::fromStdString(v), &err))
                        throw py::value_error(err.toStdString());
                }
                self->editor()->setStyle(s, "Change style");
            });
        });
    fig.def(
        "get_style_param",
        [](FigurePtr self, const std::string& key) {
            const QString v = inGui([&] { return self->model()->style().value(QString::fromStdString(key)); });
            if (v.isNull()) throw py::key_error("unknown style key '" + key + "'");
            return v.toStdString();
        },
        py::arg("key"));
    fig.def(
        "save_style",
        [](FigurePtr self, const std::string& fn) {
            QString err;
            if (!inGui([&] { return saveStyle(QString::fromStdString(fn), self->model()->style(), &err); }))
                throw std::runtime_error(err.toStdString());
        },
        py::arg("filename"), "Write this figure's complete style to a .gpstyle file.");

    // -- contour level scrolling (same as the mouse wheel / keys in the window)
    fig.def(
        "scale_contour_levels",
        [](FigurePtr self, double factor) {
            inGui([&] { self->w()->controller()->scaleContourLevels(factor); });
        },
        py::arg("factor"),
        "Multiply the base level of the selected (or all visible) contour plots by factor.");

    // -- window / history / export
    fig.def(
        "_show",
        [](FigurePtr self) {
            inGui([&] {
                FigureWindow* w = self->w();
                self->closed = false;
                w->show();
                w->raise();
                w->activateWindow();
            });
        });
    fig.def("close", [](FigurePtr self) {
        if (self->alive()) inGui([&] { self->w()->close(); });
    });
    fig.def_property_readonly("is_open", [](FigurePtr self) {
        return self->alive() && inGui([&] { return self->w()->isVisible(); });
    });
    fig.def("_axes_count", [](FigurePtr self) { return inGui([&] { return self->w()->axesCount(); }); });
    fig.def("_current", [](FigurePtr self) { return inGui([&] { return self->w()->currentAxes(); }); });
    fig.def("_set_current", [](FigurePtr self, int i) {
        inGui([&] {
            if (i < 0 || i >= self->w()->axesCount()) throw py::index_error("no such panel");
            self->w()->setCurrentAxes(i);
        });
    });
    fig.def(
        "_subplots",
        [](FigurePtr self, int rows, int cols, bool sharex, bool sharey, std::vector<double> hr,
           std::vector<double> wr, int hspace, int wspace) {
            inGui([&] {
                GridSpec g;
                g.rows = rows;
                g.cols = cols;
                g.sharex = sharex;
                g.sharey = sharey;
                g.heightRatios = QList<double>(hr.begin(), hr.end());
                g.widthRatios = QList<double>(wr.begin(), wr.end());
                g.hspace = hspace;
                g.wspace = wspace;
                self->w()->setGrid(g);
                self->connectCallbacks();
            });
        });
    fig.def("_take_color", [](FigurePtr self) {
        return colorName(inGui([&] { return self->model()->takeNextDatasetColor(); }));
    });
    fig.def("_begin_macro", [](FigurePtr self, const std::string& text) {
        inGui([&] { self->editor()->undoStack()->beginMacro(QString::fromStdString(text)); });
    });
    fig.def("_end_macro", [](FigurePtr self) { inGui([&] { self->editor()->undoStack()->endMacro(); }); });
    fig.def("undo", [](FigurePtr self) { inGui([&] { self->editor()->undo(); }); });
    fig.def("redo", [](FigurePtr self) { inGui([&] { self->editor()->redo(); }); });
    fig.def(
        "savefig",
        [](FigurePtr self, const std::string& filename, int width, int height, double scale) {
            QString err;
            const bool ok = inGui([&] {
                return self->w()->exportTo(QString::fromStdString(filename), width, height, scale, &err);
            });
            if (!ok) throw std::runtime_error(err.toStdString());
        },
        py::arg("filename"), py::kw_only(), py::arg("width") = 0, py::arg("height") = 0,
        py::arg("scale") = 2.0,
        "Export to .png, .svg or .pdf (format from the extension). width/height in pixels\n"
        "(0 = window size); scale multiplies the PNG resolution.");
    fig.def(
        "screenshot",
        [](FigurePtr self, const std::string& filename) {
            const bool ok = inGui([&] {
                return self->w()->grab().save(QString::fromStdString(filename));
            });
            if (!ok) throw std::runtime_error("could not save screenshot");
        },
        py::arg("filename"), "Save an image of the whole window (toolbar and panel included).");
    fig.def("on", &Figure::addCallback, py::arg("event"), py::arg("callback"),
            R"doc(Register a callback, run on the GUI thread after an edit completes.

'region_changed'  callback(xmin, xmax)  - both None when the selection is cleared
'marker_changed'  callback(marker)      - after a marker was moved/edited
'levels_changed'  callback(contour)     - after contour settings/levels changed
'closed'          callback()            - the window was closed)doc");
    fig.def("disconnect_all", &Figure::clearCallbacks);

    // ---- Line ------------------------------------------------------------------------------
    py::class_<Line>(mod, "Line", "Handle to a 1D dataset.")
        .def_property_readonly("x", [](Line& h) { return toNumpy(inGui([&] { return h.get().x; })); })
        .def_property_readonly("y", [](Line& h) { return toNumpy(inGui([&] { return h.get().y; })); })
        .def_property("label",
                      [](Line& h) { return inGui([&] { return h.get().name.toStdString(); }); },
                      [](Line& h, const std::string& v) {
                          h.setStyle([&](QString& n, DatasetStyle& s) {
                              n = QString::fromStdString(v);
                              s.inLegend = !v.empty();
                          });
                      })
        .def_property("color",
                      [](Line& h) { return colorName(inGui([&] { return h.get().style.color; })); },
                      [](Line& h, const py::object& v) {
                          const QColor c = toColor(v);
                          h.setStyle([&](QString&, DatasetStyle& s) { s.color = c; });
                      })
        .def_property("width",
                      [](Line& h) { return inGui([&] { return h.get().style.lineWidth; }); },
                      [](Line& h, double v) { h.setStyle([&](QString&, DatasetStyle& s) { s.lineWidth = v; }); })
        .def_property("style",
                      [](Line& h) { return lineStyleName(inGui([&] { return h.get().style.lineStyle; })).toStdString(); },
                      [](Line& h, const std::string& v) {
                          const LineStyle ls = toLineStyle(v);
                          h.setStyle([&](QString&, DatasetStyle& s) { s.lineStyle = ls; });
                      })
        .def_property("marker",
                      [](Line& h) { return scatterShapeName(inGui([&] { return h.get().style.scatter; })).toStdString(); },
                      [](Line& h, const py::object& v) {
                          const ScatterShape sh = toScatter(v);
                          h.setStyle([&](QString&, DatasetStyle& s) { s.scatter = sh; });
                      })
        .def_property("visible",
                      [](Line& h) { return inGui([&] { return h.get().style.visible; }); },
                      [](Line& h, bool v) { h.setStyle([&](QString&, DatasetStyle& s) { s.visible = v; }); })
        .def(
            "set_data",
            [](Line& h, const py::object& x, const py::object& y, bool undoable) {
                QVector<double> xv = toVector(x, "x"), yv = toVector(y, "y");
                if (xv.size() != yv.size()) throw py::value_error("x and y must have the same length");
                inGui([&] {
                    h.get();
                    h.editor()->setDatasetData(h.id, xv, yv, undoable);
                });
            },
            py::arg("x"), py::arg("y"), py::kw_only(), py::arg("undoable") = false,
            "Replace the data. Not recorded in the undo history unless undoable=True.")
        .def(
            "set_errors",
            [](Line& h, const py::object& xerr, const py::object& yerr) {
                const qsizetype n = inGui([&] { return h.get().x.size(); });
                auto [xl, xh] = toErrors(xerr, n, "xerr");
                auto [yl, yh] = toErrors(yerr, n, "yerr");
                inGui([&] { h.model()->setDatasetErrors(h.id, xl, xh, yl, yh); });
            },
            py::arg("xerr") = py::none(), py::arg("yerr") = py::none(),
            "Replace the error bars (None removes them).")
        .def_property_readonly("yerr", [](Line& h) {
            const Dataset d = inGui([&] { return h.get(); });
            if (d.yErrLow.isEmpty()) return py::object(py::none());
            return py::object(py::make_tuple(toNumpy(d.yErrLow), toNumpy(d.yErrHigh)));
        }, "(low, high) arrays or None")
        .def_property("step",
                      [](Line& h) { return stepModeName(inGui([&] { return h.get().style.step; })).toStdString(); },
                      [](Line& h, const std::string& v) {
                          bool ok = false;
                          const StepMode m = parseStepMode(QString::fromStdString(v), &ok);
                          if (!ok) throw py::value_error("step must be 'none', 'pre', 'post' or 'mid'");
                          h.setStyle([&](QString&, DatasetStyle& s) { s.step = m; });
                      })
        .def_property("fill",
                      [](Line& h) -> py::object {
                          const QColor c = inGui([&] { return h.get().style.fillColor; });
                          if (!c.isValid()) return py::none();
                          return py::str(colorName(c));
                      },
                      [](Line& h, const py::object& v) {
                          QColor c;
                          if (!v.is_none() && !(py::isinstance<py::bool_>(v) && !v.cast<bool>())) {
                              if (py::isinstance<py::bool_>(v)) {
                                  c = inGui([&] { return h.get().style.color; });
                                  c.setAlphaF(0.3);
                              } else {
                                  c = toColor(v, "fill");
                              }
                          }
                          h.setStyle([&](QString&, DatasetStyle& s) { s.fillColor = c; });
                      }, "Fill colour (None = no fill); True = translucent line colour")
        .def("remove", [](Line& h) { inGui([&] { h.editor()->removeDataset(h.id); }); })
        .def_property_readonly("exists", [](Line& h) {
            return h.fig->alive() && inGui([&] { return h.model()->dataset(h.id) != nullptr; });
        })
        .def("__repr__", [](Line& h) {
            return inGui([&]() -> std::string {
                if (!h.fig->alive() || !h.model()->dataset(h.id)) return "<Line (removed)>";
                const Dataset& d = h.get();
                return "<Line '" + d.name.toStdString() + "' n=" + std::to_string(d.x.size()) + ">";
            });
        });

    // ---- Contour -----------------------------------------------------------------------------
    py::class_<Contour>(mod, "Contour", "Handle to a 2D dataset shown as contours.")
        .def_property_readonly("z", [](Contour& h) {
            const Matrix m = inGui([&] { return h.get(); });
            py::array_t<double> a({m.ny, m.nx});
            std::copy(m.z.begin(), m.z.end(), a.mutable_data());
            return a;
        })
        .def_property_readonly("extent", [](Contour& h) {
            const Matrix m = inGui([&] { return h.get(); });
            return py::make_tuple(m.x0, m.x1, m.y0, m.y1);
        }, "(x0, x1, y0, y1) of the grid")
        .def_property("label",
                      [](Contour& h) { return inGui([&] { return h.get().name.toStdString(); }); },
                      [](Contour& h, const std::string& v) {
                          h.setStyle([&](QString& n, ContourStyle&) { n = QString::fromStdString(v); });
                      })
        .def_property("base_level",
                      [](Contour& h) { return inGui([&] { return h.get().style.baseLevel; }); },
                      [](Contour& h, double v) {
                          if (!(v > 0)) throw py::value_error("base_level must be > 0");
                          h.setStyle([&](QString&, ContourStyle& s) { s.baseLevel = v; });
                      })
        .def_property("factor",
                      [](Contour& h) { return inGui([&] { return h.get().style.factor; }); },
                      [](Contour& h, double v) {
                          if (!(v > 1)) throw py::value_error("factor must be > 1");
                          h.setStyle([&](QString&, ContourStyle& s) { s.factor = v; });
                      })
        .def_property("n_levels",
                      [](Contour& h) { return inGui([&] { return h.get().style.levelCount; }); },
                      [](Contour& h, int v) {
                          if (v < 1) throw py::value_error("n_levels must be >= 1");
                          h.setStyle([&](QString&, ContourStyle& s) { s.levelCount = v; });
                      })
        .def_property("negative",
                      [](Contour& h) { return inGui([&] { return h.get().style.showNegative; }); },
                      [](Contour& h, bool v) { h.setStyle([&](QString&, ContourStyle& s) { s.showNegative = v; }); })
        .def_property("image",
                      [](Contour& h) { return inGui([&] { return h.get().style.showImage; }); },
                      [](Contour& h, bool v) { h.setStyle([&](QString&, ContourStyle& s) { s.showImage = v; }); })
        .def_property("visible",
                      [](Contour& h) { return inGui([&] { return h.get().style.visible; }); },
                      [](Contour& h, bool v) { h.setStyle([&](QString&, ContourStyle& s) { s.visible = v; }); })
        .def_property("color",
                      [](Contour& h) { return colorName(inGui([&] { return h.get().style.positiveColor; })); },
                      [](Contour& h, const py::object& v) {
                          const QColor c = toColor(v);
                          h.setStyle([&](QString&, ContourStyle& s) { s.positiveColor = c; });
                      })
        .def_property("negative_color",
                      [](Contour& h) { return colorName(inGui([&] { return h.get().style.negativeColor; })); },
                      [](Contour& h, const py::object& v) {
                          const QColor c = toColor(v, "negative_color");
                          h.setStyle([&](QString&, ContourStyle& s) { s.negativeColor = c; });
                      })
        .def("levels", [](Contour& h) {
            const ContourStyle s = inGui([&] { return h.get().style; });
            std::vector<double> l = contourLevels(s);
            std::sort(l.begin(), l.end());
            return l;
        }, "All contour levels currently drawn, sorted ascending.")
        .def("scale_levels", [](Contour& h, double factor) {
            if (!(factor > 0)) throw py::value_error("factor must be > 0");
            inGui([&] { h.get(); h.editor()->scaleContourLevels({h.id}, factor); });
        }, py::arg("factor"), "Multiply the base level (factor < 1 shows weaker features).")
        .def(
            "set_data",
            [](Contour& h, const py::object& z, const py::object& x, const py::object& y) {
                Matrix m = matrixFromNumpy(z, x, y);
                inGui([&] {
                    h.get();
                    h.editor()->setMatrixData(h.id, m.nx, m.ny, m.z, m.x0, m.x1, m.y0, m.y1);
                });
            },
            py::arg("z"), py::arg("x") = py::none(), py::arg("y") = py::none(),
            "Replace the grid (keeps the contour settings).")
        .def("remove", [](Contour& h) { inGui([&] { h.editor()->removeMatrix(h.id); }); })
        .def("__repr__", [](Contour& h) {
            return inGui([&]() -> std::string {
                if (!h.fig->alive() || !h.model()->matrix(h.id)) return "<Contour (removed)>";
                const Matrix& m = h.get();
                return "<Contour '" + m.name.toStdString() + "' " + std::to_string(m.nx) + "x" +
                       std::to_string(m.ny) + " base=" + QString::number(m.style.baseLevel, 'g', 4).toStdString() + ">";
            });
        });

    // ---- Marker ------------------------------------------------------------------------------
    py::class_<MarkerH>(mod, "Marker", "Handle to a vertical or horizontal marker.")
        .def_property("position",
                      [](MarkerH& h) { return inGui([&] { return h.get().position; }); },
                      [](MarkerH& h, double v) { h.edit([&](Marker& m) { m.position = v; }); })
        .def_property_readonly("orientation", [](MarkerH& h) {
            return inGui([&] { return h.get().orientation; }) == Orientation::Vertical ? "v" : "h";
        })
        .def_property("label",
                      [](MarkerH& h) { return inGui([&] { return h.get().label.toStdString(); }); },
                      [](MarkerH& h, const std::string& v) {
                          h.edit([&](Marker& m) { m.label = QString::fromStdString(v); });
                      })
        .def_property("color",
                      [](MarkerH& h) { return colorName(inGui([&] { return h.get().color; })); },
                      [](MarkerH& h, const py::object& v) {
                          const QColor c = toColor(v);
                          h.edit([&](Marker& m) { m.color = c; });
                      })
        .def_property("style",
                      [](MarkerH& h) { return lineStyleName(inGui([&] { return h.get().lineStyle; })).toStdString(); },
                      [](MarkerH& h, const std::string& v) {
                          const LineStyle ls = toLineStyle(v);
                          h.edit([&](Marker& m) { m.lineStyle = ls; });
                      })
        .def_property("width",
                      [](MarkerH& h) { return inGui([&] { return h.get().lineWidth; }); },
                      [](MarkerH& h, double v) { h.edit([&](Marker& m) { m.lineWidth = v; }); })
        .def("remove", [](MarkerH& h) { inGui([&] { h.editor()->removeMarker(h.id); }); })
        .def("__repr__", [](MarkerH& h) {
            return inGui([&]() -> std::string {
                if (!h.fig->alive() || !h.model()->marker(h.id)) return "<Marker (removed)>";
                const Marker m = h.get();
                return "<Marker '" + m.label.toStdString() + "' " +
                       (m.orientation == Orientation::Vertical ? "x=" : "y=") +
                       QString::number(m.position, 'g', 6).toStdString() + ">";
            });
        });

    // ---- Peak ----------------------------------------------------------------------------------
    py::class_<PeakH>(mod, "Peak", "Handle to a picked peak.")
        .def_property("position",
                      [](PeakH& h) {
                          const Peak p = inGui([&] { return h.get(); });
                          return py::make_tuple(p.x, p.y);
                      },
                      [](PeakH& h, std::pair<double, double> v) {
                          h.edit([&](Peak& p) { p.x = v.first; p.y = v.second; }, "Move peak");
                      })
        .def_property_readonly("x", [](PeakH& h) { return inGui([&] { return h.get().x; }); })
        .def_property_readonly("y", [](PeakH& h) { return inGui([&] { return h.get().y; }); })
        .def_property_readonly("intensity", [](PeakH& h) { return inGui([&] { return h.get().intensity; }); })
        .def_property("label",
                      [](PeakH& h) { return inGui([&] { return h.get().label.toStdString(); }); },
                      [](PeakH& h, const std::string& v) {
                          h.edit([&](Peak& p) { p.label = QString::fromStdString(v); }, "Edit peak label");
                      })
        .def_property("comment",
                      [](PeakH& h) { return inGui([&] { return h.get().comment.toStdString(); }); },
                      [](PeakH& h, const std::string& v) {
                          h.edit([&](Peak& p) { p.comment = QString::fromStdString(v); }, "Edit peak comment");
                      }, "Free-text notes; never drawn on the plot.")
        .def_property("label_offset",
                      [](PeakH& h) {
                          const Peak p = inGui([&] { return h.get(); });
                          return py::make_tuple(p.labelOffset.x(), p.labelOffset.y());
                      },
                      [](PeakH& h, std::pair<double, double> v) {
                          h.edit([&](Peak& p) { p.labelOffset = QPointF(v.first, v.second); }, "Move peak label");
                      }, "Label position relative to the peak, in pixels (x right, y down).")
        .def("remove", [](PeakH& h) { inGui([&] { h.editor()->removePeak(h.id); }); })
        .def("__repr__", [](PeakH& h) {
            return inGui([&]() -> std::string {
                if (!h.fig->alive() || !h.model()->peak(h.id)) return "<Peak (removed)>";
                const Peak p = h.get();
                return "<Peak (" + QString::number(p.x, 'f', 4).toStdString() + ", " +
                       QString::number(p.y, 'f', 4).toStdString() + ")" +
                       (p.label.isEmpty() ? "" : " '" + p.label.toStdString() + "'") + ">";
            });
        });

    // ---- Annotation ----------------------------------------------------------------------------
    py::class_<AnnotationH>(mod, "Annotation", "Handle to a text annotation.")
        .def_property("text",
                      [](AnnotationH& h) { return inGui([&] { return h.get().text.toStdString(); }); },
                      [](AnnotationH& h, const std::string& v) {
                          h.edit([&](Annotation& a) { a.text = QString::fromStdString(v); });
                      })
        .def_property("position",
                      [](AnnotationH& h) {
                          const Annotation a = inGui([&] { return h.get(); });
                          return py::make_tuple(a.x, a.y);
                      },
                      [](AnnotationH& h, std::pair<double, double> p) {
                          h.edit([&](Annotation& a) { a.x = p.first; a.y = p.second; });
                      })
        .def_property("color",
                      [](AnnotationH& h) { return colorName(inGui([&] { return h.get().color; })); },
                      [](AnnotationH& h, const py::object& v) {
                          const QColor c = toColor(v);
                          h.edit([&](Annotation& a) { a.color = c; });
                      })
        .def_property("fontsize",
                      [](AnnotationH& h) {
                          return inGui([&] {
                              const double f = h.get().fontSize;
                              return f > 0 ? f : h.model()->style().annotationSize;
                          });
                      },
                      [](AnnotationH& h, double v) { h.edit([&](Annotation& a) { a.fontSize = v; }); })
        .def("remove", [](AnnotationH& h) { inGui([&] { h.editor()->removeAnnotation(h.id); }); })
        .def("__repr__", [](AnnotationH& h) {
            return inGui([&]() -> std::string {
                if (!h.fig->alive() || !h.model()->annotation(h.id)) return "<Annotation (removed)>";
                return "<Annotation '" + h.get().text.toStdString() + "'>";
            });
        });
}
