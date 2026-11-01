// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
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
#include "gui/Appearance.h"
#include "gui/FigureWindow.h"
#include "gui/PeakTable.h"
#include "gui/Timeline.h"
#include "model/Contour.h"
#include "model/DataExport.h"
#include "model/PeakPicker.h"
#include "model/Statistics.h"
#include "model/Style.h"
#include "model/PlotModel.h"
#include "qcustomplot.h"
#include "render/ColorBar.h"
#include "render/DisplayTheme.h"
#include "render/PlotRenderer.h"

#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <QApplication>
#include <QGuiApplication>
#include <QDir>
#include <QEventLoop>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <cmath>
#include <memory>
#include <atomic>
#include <set>

namespace py = pybind11;
using namespace gp;

// A text argument (labels, titles, comments): None means "" and any other
// object is converted with str(), as plot(label=3) always did. Keeps every
// text parameter consistent.
struct Text {
    std::string value;
    operator const std::string&() const { return value; }
    bool empty() const { return value.empty(); }
};
namespace pybind11::detail {
template <>
struct type_caster<Text> {
    PYBIND11_TYPE_CASTER(Text, const_name("str"));
    bool load(handle src, bool)
    {
        if (!src) return false;
        if (src.is_none()) {
            value.value.clear();
        } else if (PyBytes_Check(src.ptr())) {
            value.value = src.cast<std::string>();  // UTF-8 bytes, not "b'...'"
        } else if (PyUnicode_Check(src.ptr()) || PyNumber_Check(src.ptr())) {
            value.value = str(src).cast<std::string>();  // str, or a number such as label=3
        } else {
            return false;  // lists etc.: a TypeError, not "['a']"
        }
        return true;
    }
    static handle cast(const Text& t, return_value_policy, handle) { return str(t.value).release(); }
};
}  // namespace pybind11::detail
using gp::pyglue::inGui;

namespace {

// ---- conversions ------------------------------------------------------------------

// A file path from Python: "~" / "~/..." means the home folder (as in the shell).
QString toPath(const std::string& p)
{
    QString s = QString::fromStdString(p);
    if (s == "~" || s.startsWith("~/")) s = QDir::homePath() + s.mid(1);
    return s;
}

// Absolute times (datetime64, datetime / pandas Timestamp objects), which
// toVector turns into Unix seconds; plotting them gives date tick labels.
bool isDateLike(const py::handle& obj)
{
    // pandas (also tz-aware) datetime data has a dtype of kind 'M'; timedeltas ('m') are durations.
    if (py::hasattr(obj, "dtype") && py::hasattr(obj.attr("dtype"), "kind") &&
        py::str(obj.attr("dtype").attr("kind")).cast<std::string>() == "M")
        return true;
    try {
        py::array a = py::module_::import("numpy").attr("asarray")(obj);
        if (a.dtype().kind() == 'M') return true;
        if (a.dtype().kind() == 'O' && a.size() > 0) {
            py::object first = a.attr("ravel")()[py::int_(0)];
            return py::hasattr(first, "timestamp") || (py::hasattr(first, "year") && py::hasattr(first, "toordinal"));
        }
    } catch (const py::error_already_set&) {
    }
    return false;
}

QVector<double> toVector(const py::handle& obj, const char* what)
{
    // Masked arrays: masked points become NaN (gaps), like matplotlib.
    if (py::hasattr(obj, "mask") && py::hasattr(obj, "filled"))
        return toVector(obj.attr("astype")("float64").attr("filled")(std::nan("")), what);
    // Time-zone aware pandas data: convert to UTC datetime64 first.
    if (py::hasattr(obj, "dt") && py::hasattr(obj.attr("dt"), "tz") && !obj.attr("dt").attr("tz").is_none())
        return toVector(obj.attr("dt").attr("tz_convert")(py::none()).attr("to_numpy")(), what);
    if (py::hasattr(obj, "tz") && py::hasattr(obj, "tz_convert") && !obj.attr("tz").is_none())
        return toVector(obj.attr("tz_convert")(py::none()).attr("to_numpy")(), what);
    // datetime64 / timedelta64 become (epoch) seconds; a plain float cast
    // would yield nanoseconds. NaT becomes NaN.
    if (auto raw = py::array::ensure(obj); raw && (raw.dtype().kind() == 'M' || raw.dtype().kind() == 'm')) {
        const char* unit = raw.dtype().kind() == 'M' ? "datetime64[ns]" : "timedelta64[ns]";
        py::array ns = raw.attr("astype")(unit);
        py::object secs = ns.attr("astype")("int64").attr("__truediv__")(1e9);
        py::module_::import("numpy").attr("putmask")(secs, ns.attr("__ne__")(ns), std::nan(""));
        return toVector(secs, what);
    }
    // Object arrays of datetime objects (e.g. tz-aware timestamps): use timestamp().
    if (auto raw = py::array::ensure(obj); raw && raw.dtype().kind() == 'O' && raw.size() > 0) {
        py::object first = raw.attr("ravel")()[py::int_(0)];
        // datetime.date (no time of day): midnight UTC, via numpy.
        if (!py::hasattr(first, "timestamp") && py::hasattr(first, "year") && py::hasattr(first, "toordinal"))
            return toVector(raw.attr("astype")("datetime64[ns]"), what);
        if (py::hasattr(first, "timestamp")) {
            QVector<double> v;
            for (py::handle item : raw.attr("ravel")()) {
                const bool nat = item.is_none() || item.attr("__ne__")(item).cast<bool>();
                if (nat) {
                    v << std::nan("");
                    continue;
                }
                py::object when = py::reinterpret_borrow<py::object>(item);
                // Naive datetimes are UTC (as numpy / pandas do), not local time.
                if (py::hasattr(when, "tzinfo") && when.attr("tzinfo").is_none()) {
                    py::object utc = py::module_::import("datetime").attr("timezone").attr("utc");
                    when = when.attr("replace")(py::arg("tzinfo") = utc);
                }
                v << when.attr("timestamp")().cast<double>();
            }
            if (raw.ndim() != 1) throw py::value_error(std::string(what) + " must be one-dimensional");
            return v;
        }
    }
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
// Negative sizes are rejected like in matplotlib (NaN is allowed: no bar).
std::pair<QVector<double>, QVector<double>> toErrors(const py::object& obj, qsizetype n, const char* what)
{
    auto checked = [what](double v) {
        if (v < 0) throw py::value_error(std::string(what) + " must not contain negative values");
        return v;
    };
    if (obj.is_none()) return {};
    if (py::isinstance<py::float_>(obj) || py::isinstance<py::int_>(obj)) {
        QVector<double> v(n, checked(obj.cast<double>()));
        return {v, v};
    }
    auto arr = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(obj);
    if (!arr) throw py::type_error(std::string(what) + " must be a number or an array");
    if (arr.ndim() == 0) {  // numpy scalar (np.float32(0.1), np.int64(2), ...)
        QVector<double> v(n, checked(*arr.data()));
        return {v, v};
    }
    auto copy = [&](const double* p) {
        QVector<double> v(n);
        for (qsizetype i = 0; i < n; ++i) v[i] = checked(p[i]);
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
            // RGB in 0..1 or 0..255 (decided on the three colour components);
            // alpha in 0..1 or 0..255 on its own, e.g. (255, 0, 0, 0.5).
            const bool unit = std::all_of(seq.begin(), seq.begin() + 3, [](double v) { return v <= 1.0; });
            const double k = unit ? 255.0 : 1.0;
            const double a = seq.size() == 4 ? (seq[3] <= 1.0 ? seq[3] * 255.0 : seq[3]) : 255.0;
            int c[4] = {int(std::lround(seq[0] * k)), int(std::lround(seq[1] * k)),
                        int(std::lround(seq[2] * k)), int(std::lround(a))};
            for (int v : c)
                if (v < 0 || v > 255) throw py::value_error(std::string(what) + " components out of range");
            return QColor(c[0], c[1], c[2], c[3]);
        }
    }
    throw py::type_error(std::string(what) + " must be a name, '#rrggbb' or an (r, g, b[, a]) tuple");
}

// A colour that must be given (property setters: None is not "no colour").
QColor requiredColor(const py::object& obj, const char* what)
{
    if (obj.is_none()) throw py::value_error(std::string(what) + " cannot be None");
    return toColor(obj, what);
}

// sharex / sharey as in matplotlib: False / True / 'none' / 'all' / 'col' / 'row'.
GridSpec::Share toShare(const py::object& v)
{
    if (v.is_none()) return GridSpec::Share::None;
    if (py::isinstance<py::str>(v)) {
        const std::string s = v.cast<std::string>();
        if (s == "all") return GridSpec::Share::All;
        if (s == "col") return GridSpec::Share::Column;
        if (s == "row") return GridSpec::Share::Row;
        if (s == "none") return GridSpec::Share::None;
        throw py::value_error("share must be True/False, 'all', 'col', 'row' or 'none'");
    }
    return py::bool_(v) ? GridSpec::Share::All : GridSpec::Share::None;
}

// matplotlib-style horizontal / vertical alignment -> Qt::Alignment flags.
int toAlignment(const std::string& ha, const std::string& va)
{
    int h = 0, v = 0;
    if (ha == "left") h = Qt::AlignLeft;
    else if (ha == "center" || ha == "centre") h = Qt::AlignHCenter;
    else if (ha == "right") h = Qt::AlignRight;
    else throw py::value_error("ha must be 'left', 'center' or 'right'");
    if (va == "bottom" || va == "baseline") v = Qt::AlignBottom;
    else if (va == "center" || va == "centre" || va == "center_baseline") v = Qt::AlignVCenter;
    else if (va == "top") v = Qt::AlignTop;
    else throw py::value_error("va must be 'bottom', 'center' or 'top'");
    return h | v;
}

// Colour map name for 2D images (None = automatic).
QString checkedColormap(const py::object& v)
{
    if (v.is_none()) return {};
    const QString name = QString::fromStdString(py::str(v).cast<std::string>()).trimmed().toLower();
    QCPColorGradient g;
    if (!colormapGradient(name, false, &g))
        throw py::value_error("unknown colour map '" + name.toStdString() + "' (" +
                              colormapNames().join(", ").toStdString() + "; '_r' reverses)");
    return name == "auto" ? QString() : name;
}

// (fixed, vmin, vmax) from optional limits; a missing limit (stored as
// -inf / +inf) keeps the data's.
std::tuple<bool, double, double> colorLimits(const py::object& vmin, const py::object& vmax)
{
    if (vmin.is_none() && vmax.is_none()) return {false, 0.0, 1.0};
    const double inf = std::numeric_limits<double>::infinity();
    const double lo = vmin.is_none() ? -inf : vmin.cast<double>();
    const double hi = vmax.is_none() ? inf : vmax.cast<double>();
    if ((!vmin.is_none() && !std::isfinite(lo)) || (!vmax.is_none() && !std::isfinite(hi)) || !(hi > lo))
        throw py::value_error("vmin must be smaller than vmax (finite numbers)");
    return {true, lo, hi};
}

void checkWidth(double v)
{
    if (!(v >= 0) || !std::isfinite(v)) throw py::value_error("width must be a finite number >= 0");
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
    if (!ok) throw py::value_error("unknown marker symbol '" + s + "' (use o s ^ v < > d p h * . x +)");
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
    // The last reference can be dropped from a Qt slot (GIL released):
    // destroying Python callables requires the GIL.
    ~Figure()
    {
        if (!m_callbacks.empty() && Py_IsInitialized()) {
            py::gil_scoped_acquire gil;
            m_callbacks.clear();
        }
        // Nothing in Python refers to this figure any more: free the window
        // and its data (deferred: this may run inside one of its own slots).
        if (m_w) m_w->deleteLater();
    }

    // Returns the window or raises if it has been closed/destroyed.
    FigureWindow* w() const
    {
        if (!m_w) throw std::runtime_error("the figure has been closed");
        return m_w;
    }
    bool alive() const { return !m_w.isNull(); }
    // The panel figure-level calls act on: the one an Axes proxy names for
    // the calling Python thread (see pyglue::CallContext), else the window's
    // current panel. Read on the GUI thread, inside the caller's context.
    int panel() const
    {
        FigureWindow* win = w();
        auto& ctx = pyglue::callContext();
        const int t = ctx.panelTargets.value(this, -1);
        const int p = t >= 0 && t < win->axesCount() ? t : win->currentAxes();
        ctx.lastPanel.insert(this, p);  // see CallContext::lastPanel
        return p;
    }
    PlotModel* model() const { return w()->axes(panel()).model; }
    PlotEditor* editor() const { return w()->axes(panel()).editor; }
    PlotRenderer* renderer() const { return w()->axes(panel()).renderer; }

    // Callbacks ------------------------------------------------------------------------
    // Events are delivered once an edit is complete (not on every intermediate
    // mouse-move during a drag), coalesced per event loop iteration.
    void connectCallbacks();  // also picks up panels created later
    void connectAxes(int ax);
    void connectTimeline();  // timeline events -> Python callbacks
    bool timelineConnected = false;
    void addCallback(const std::string& event, py::function fn)
    {
        static const std::set<std::string> known = {"region_changed", "marker_changed", "levels_changed",
                                                    "closed", "cursor_moved", "window_changed",
                                                    "pins_changed"};
        if (!known.count(event))
            throw py::value_error("unknown event '" + event +
                                  "' (region_changed, marker_changed, levels_changed, closed, "
                                  "cursor_moved, window_changed, pins_changed)");
        m_callbacks.emplace_back(event, std::move(fn));
    }
    void clearCallbacks() { m_callbacks.clear(); }
    // Removes one callback (compared with ==, so a bound method obj.cb given
    // again matches); returns whether it existed.
    bool removeCallback(const std::string& event, const py::function& fn)
    {
        const auto before = m_callbacks.size();
        m_callbacks.erase(std::remove_if(m_callbacks.begin(), m_callbacks.end(),
                                         [&](const auto& c) { return c.first == event && c.second.equal(fn); }),
                          m_callbacks.end());
        return m_callbacks.size() != before;
    }
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
    // Remembers the panel (axes) that was current when the object was created
    // (read on the GUI thread, which owns that state).
    // The panel is the one the creating call used (recorded in the calling
    // thread's context), not whichever panel is current now.
    Handle(FigurePtr f, ObjectId i) : fig(std::move(f)), id(i)
    {
        ax = pyglue::callContext().lastPanel.value(fig.get(), -1);
        if (ax < 0) ax = inGui([this] { return fig->panel(); });
    }
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
    PlotEditor* editor() const
    {
        model();  // validates the panel index
        return fig->w()->axes(ax).editor;
    }
    // Handles only make sense for the figure that created them.
    void checkFigure(const FigurePtr& f) const
    {
        if (fig != f) throw py::value_error("this handle belongs to another figure");
    }
};

// A handle argument that must belong to `self` (and, optionally, to its current panel).
template <typename H>
const H& ownHandle(const FigurePtr& self, const py::handle& obj, bool currentPanel, const char* what)
{
    const H& h = obj.cast<const H&>();
    h.checkFigure(self);
    if (currentPanel && inGui([&] { return self->panel(); }) != h.ax)
        throw py::value_error(std::string(what) + " must be in the same panel");
    return h;
}

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

void Figure::connectTimeline()
{
    if (timelineConnected) return;
    timelineConnected = true;
    Timeline* tl = w()->timeline();
    std::weak_ptr<Figure> weak = weak_from_this();
    // Cursor and window events are delivered immediately (they are continuous).
    QObject::connect(tl, &Timeline::cursorChanged, tl, [weak](std::optional<double> t) {
        if (auto f = weak.lock())
            f->fire("cursor_moved", [t]() -> py::tuple {
                if (t) return py::make_tuple(*t);
                return py::make_tuple(py::none());
            });
    });
    QObject::connect(tl, &Timeline::windowChanged, tl, [weak](double a, double b) {
        if (auto f = weak.lock()) f->fire("window_changed", [a, b]() -> py::tuple { return py::make_tuple(a, b); });
    });
    QObject::connect(tl, &Timeline::pinsChanged, tl, [weak] {
        if (auto f = weak.lock()) f->fire("pins_changed", []() -> py::tuple { return py::make_tuple(); });
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
    // The callback list is only touched with the GIL held (on() and
    // disconnect_all() run from Python, i.e. with the GIL).
    py::gil_scoped_acquire gil;  // the event loop runs with the GIL released
    bool any = false;
    for (const auto& [ev, fn] : m_callbacks) any |= ev == event;
    if (!any) return;
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

// Undoable "start like a new panel" after Figure.clear: fitting and colour
// cycle restart; undo restores the previous fitting state, view and colours.
class FreshStartCommand : public QUndoCommand {
public:
    FreshStartCommand(PlotRenderer* r, PlotModel* m)
        : QUndoCommand("Clear figure"), m_r(r), m_m(m), m_before(r->fitState()), m_colors(m->colorCounter())
    {
    }
    void redo() override
    {
        m_r->startFresh();
        m_m->resetColorCycle();
    }
    void undo() override
    {
        m_r->setFitState(m_before);
        m_m->setColorCounter(m_colors);
    }

private:
    QPointer<PlotRenderer> m_r;
    PlotModel* m_m;
    PlotRenderer::FitState m_before;
    int m_colors;
};

FigurePtr makeFigure(const py::object& title, int width, int height)
{
    pyglue::ensureApp();
    if (!pyglue::isGuiThread())
        throw std::runtime_error("figures must be created on the GUI (main) thread");
    auto* win = new FigureWindow;
    win->setAttribute(Qt::WA_DeleteOnClose, false);  // keep data accessible after closing
    win->setFigureSize(QSize(width, height));
    if (!title.is_none()) {
        const QString t = QString::fromStdString(py::str(title).cast<std::string>());
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
        // Tolerance allows float32 coordinates (rounding ~1e-7 of the values).
        if (std::abs((v[i] - v[i - 1]) - step) >
            1e-4 * std::max(std::abs(step), 1e-300) + 1e-6 * std::max(std::abs(v[i]), std::abs(v[i - 1])))
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

    // ---- appearance (themes, hover explanations); wrapped by plotter.theme() etc.
    mod.def("_themes", [] {
        std::vector<std::tuple<std::string, std::string, std::string, bool>> out;
        for (const ThemeDef& t : DisplayTheme::themes())
            out.emplace_back(t.id.toStdString(), t.name.toStdString(), t.description.toStdString(), t.dark);
        return out;
    });
    mod.def("_theme", [] {
        pyglue::ensureApp();
        return inGui([] { return Appearance::instance()->theme().toStdString(); });
    });
    mod.def("_set_theme", [](const std::string& id, bool save) {
        pyglue::ensureApp();
        if (!inGui([&] { return Appearance::instance()->setTheme(QString::fromStdString(id), save); }))
            throw py::value_error("unknown theme '" + id + "'");
    });
    mod.def("_theme_options", [] {
        pyglue::ensureApp();
        return inGui([] {
            return std::make_pair(DisplayTheme::instance()->themePlots(), DisplayTheme::instance()->themeExports());
        });
    });
    mod.def("_set_theme_options", [](const py::object& plots, const py::object& exports, bool save) {
        pyglue::ensureApp();
        // Python truthiness (bool(x)), like every other switch.
        const std::optional<bool> p = plots.is_none() ? std::nullopt : std::optional<bool>(py::bool_(plots));
        const std::optional<bool> e = exports.is_none() ? std::nullopt : std::optional<bool>(py::bool_(exports));
        inGui([&] {
            if (p) Appearance::instance()->setThemePlots(*p, save);
            if (e) Appearance::instance()->setThemeExports(*e, save);
        });
    });
    mod.def("_tooltips", [] {
        pyglue::ensureApp();
        return inGui([] { return Appearance::instance()->tooltipsEnabled(); });
    });
    mod.def("_set_tooltips", [](bool on, bool save) {
        pyglue::ensureApp();
        inGui([&] { Appearance::instance()->setTooltipsEnabled(on, save); });
    });

    mod.def("_colormap_colors", [](const std::string& name, int n) {
        // n colours sampled evenly from a colour map (for per-point colours).
        QCPColorGradient g;
        if (!colormapGradient(QString::fromStdString(name).toLower(), false, &g))
            throw py::value_error("unknown colour map '" + name + "'");
        std::vector<std::string> out;
        for (int i = 0; i < n; ++i) {
            const double v = n > 1 ? double(i) / (n - 1) : 0.5;
            out.push_back(QColor::fromRgba(g.color(v, QCPRange(0, 1))).name().toStdString());
        }
        return out;
    });
    mod.def("_headless", [] {
        // No screen: windows can never be closed by a user (CI, servers).
        const QString p = QGuiApplication::platformName();
        return p == "offscreen" || p == "minimal";
    });
    mod.def("_is_gui_thread", [] { return pyglue::isGuiThread() || !QCoreApplication::instance(); });
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
    mod.def("_style_set_many", [](const std::vector<std::pair<std::string, std::string>>& items) {
        Style s = defaultStyle();  // applied to a copy: all or nothing
        for (const auto& [key, value] : items) {
            QString err;
            if (!s.set(QString::fromStdString(key), QString::fromStdString(value), &err))
                throw py::value_error(err.toStdString());
        }
        setDefaultStyle(s);
    });
    mod.def("_style_text", [] { return defaultStyle().toText().toStdString(); });
    mod.def("_style_save", [](const std::string& fn) {
        QString err;
        if (!saveStyle(toPath(fn), defaultStyle(), &err)) throw std::runtime_error(err.toStdString());
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
            const bool xIsDate = !y.is_none() && isDateLike(x);
            if (xv.size() != yv.size())
                throw py::value_error("x and y must have the same length");
            Dataset d;
            if (!label.is_none()) d.name = QString::fromStdString(py::str(label).cast<std::string>());
            d.style.inLegend = !label.is_none() && !d.name.isEmpty();  // like matplotlib: only labelled lines
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
            // fill=True: the line colour, translucent; otherwise any colour spec.
            const bool fillWithLineColor = py::isinstance<py::bool_>(fill) && fill.cast<bool>();
            if (!fill.is_none() && !py::isinstance<py::bool_>(fill)) d.style.fillColor = toColor(fill, "fill");
            if (!fillTo.is_none()) d.style.fillTo = ownHandle<Line>(self, fillTo, true, "fill_to").id;
            std::tie(d.xErrLow, d.xErrHigh) = toErrors(xerr, xv.size(), "xerr");
            std::tie(d.yErrLow, d.yErrHigh) = toErrors(yerr, xv.size(), "yerr");
            d.x = std::move(xv);
            d.y = std::move(yv);
            const ObjectId id = inGui([&] {
                if (fillWithLineColor) {
                    // Translucent line colour (assign the line colour now).
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
                        ed->setDatasetDataAndErrors(oid, d);
                        if (!d.style.color.isValid()) d.style.color = old->style.color;
                        if (!(d.style.lineWidth > 0)) d.style.lineWidth = old->style.lineWidth;
                        if (!(d.style.scatterSize > 0)) d.style.scatterSize = old->style.scatterSize;
                        ed->setDatasetAppearance(oid, d.name, d.style);
                        ed->undoStack()->endMacro();
                        return oid;
                    }
                }
                const bool first = self->model()->datasets().empty() && self->model()->matrices().empty();
                // The first data decides between date and number labels (as in
                // matplotlib); one undo step together with the data.
                // (Plain numbers never switch it off: they may be dates already
                // converted to seconds; clear() resets the panel.)
                const bool switchTime = first && xIsDate && !self->model()->axesInfo().xTime;
                if (switchTime) {
                    ed->undoStack()->beginMacro("Plot");
                    AxesInfo info = self->model()->axesInfo();
                    info.xTime = true;
                    ed->setAxesInfo(info);
                }
                const ObjectId nid = ed->addDataset(d);
                if (switchTime) ed->undoStack()->endMacro();
                if (first) {
                    self->renderer()->fitFollowing();
                    self->renderer()->captureHomeView();
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
marker    point symbol: 'o', 's', '^', 'v', '<', '>', 'd', 'p', 'h', '*', '.', 'x', '+' or None
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
           const py::object& linewidthObj, const py::object& cmap, bool contours, bool colorbar,
           const py::object& vmin, const py::object& vmax, bool smooth) {
            // levels: a count (geometric levels) or a sequence of exact levels.
            QVector<double> explicitLevels;
            const bool isSequence = !levelsObj.is_none() && !py::isinstance<py::int_>(levelsObj) &&
                                    py::hasattr(levelsObj, "__len__");
            if (isSequence) {
                explicitLevels = toVector(levelsObj, "levels");
                if (explicitLevels.isEmpty() || explicitLevels.size() > 1000)
                    throw py::value_error("give 1 to 1000 contour levels");
            }
            const int levels = levelsObj.is_none() || isSequence ? 0 : levelsObj.cast<int>();
            const double factor = factorObj.is_none() ? 0.0 : factorObj.cast<double>();
            const double linewidth = linewidthObj.is_none() ? 0.0 : linewidthObj.cast<double>();
            Matrix m = matrixFromNumpy(z, x, y);
            if (!label.is_none()) m.name = QString::fromStdString(py::str(label).cast<std::string>());
            if (!levelsObj.is_none() && !isSequence && (levels < 1 || levels > 1000))
                throw py::value_error("levels must be between 1 and 1000");
            if (!factorObj.is_none() && !(factor > 1.0)) throw py::value_error("factor must be > 1");
            m.style.levelCount = levels;
            m.style.factor = factor;
            m.style.baseLevel = base.is_none() ? 0.0 : base.cast<double>();
            m.style.positiveColor = toColor(color);
            if (!negativeColor.is_none()) m.style.negativeColor = toColor(negativeColor, "negative_color");
            m.style.showNegative = negative;
            m.style.showImage = image || !contours;  // no contours: show the image
            m.style.lineWidth = linewidth;
            m.style.showContours = contours;
            m.style.levels = explicitLevels;
            m.style.smooth = smooth;
            m.style.colorBar = colorbar;
            m.style.colormap = checkedColormap(cmap);
            std::tie(m.style.fixedRange, m.style.vmin, m.style.vmax) = colorLimits(vmin, vmax);
            const ObjectId id = inGui([&] {
                const bool first = self->model()->datasets().empty() && self->model()->matrices().empty();
                const ObjectId nid = self->editor()->addMatrix(m);
                if (first) {
                    self->renderer()->fitFollowing();
                    self->renderer()->captureHomeView();
                }
                return nid;
            });
            return Contour{{self, id}};
        },
        py::arg("z"), py::arg("x") = py::none(), py::arg("y") = py::none(), py::kw_only(),
        py::arg("label") = py::none(), py::arg("levels") = py::none(), py::arg("factor") = py::none(),
        py::arg("base") = py::none(), py::arg("color") = py::none(),
        py::arg("negative_color") = py::none(), py::arg("negative") = true,
        py::arg("image") = false, py::arg("linewidth") = py::none(), py::arg("cmap") = py::none(),
        py::arg("contours") = true, py::arg("colorbar") = false, py::arg("vmin") = py::none(),
        py::arg("vmax") = py::none(), py::arg("smooth") = true,
        R"doc(Show 2D data z[row=y, column=x] as contour lines on a regular grid.

x, y      grid coordinates: full uniformly spaced arrays or (start, stop); default: indices
levels    number of positive levels: base * factor**k, k = 0..levels-1;
          or a list of exact levels (drawn as given, any sign)
smooth    image: interpolate between cells (False: blocky cells)
base      lowest level; default chooses the top level just below max |z|
negative  also draw the mirrored negative levels (in negative_color)
image     draw a colour-map image underneath the contours
cmap      image colours: 'auto', 'viridis', 'magma', 'inferno', 'plasma', 'cividis',
          'gray', 'blues', 'rdbu' ('_r' reverses); contours=False: image only
colorbar  colour bar beside the panel; vmin, vmax: fixed colour limits
In the window, Alt+mouse wheel (or the wheel in 'Levels' mode) scrolls the
levels; '*' and '/' halve / double them. Returns a Contour handle.)doc");

    fig.def(
        "open_bruker",
        [](FigurePtr self, const std::string& path) {
            QString err;
            const ObjectId id = inGui([&] {
                return self->w()->openBruker(toPath(path), &err);
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
            if (!contour.is_none()) only = ownHandle<Contour>(self, contour, true, "contour").id;
            const QList<ObjectId> ids = inGui([&] {
                PlotRenderer* r = self->renderer();
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
                return self->editor()->pickPeaks(targets, o);
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
        [](FigurePtr self, double x, double y, const Text& label, const Text& comment,
           bool snap) {
            const ObjectId id = inGui([&] {
                Peak p;
                p.x = x;
                p.y = y;
                if (const Matrix* m = self->renderer()->matrixAt({x, y})) {
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
            if (!inGui([&] { return self->w()->peakTable()->exportCsv(toPath(fn), &err); }))
                throw std::runtime_error(err.toStdString());
        },
        py::arg("filename"), "Write the peak list as CSV (x, y, intensity, label, comment).");
    fig.def(
        "import_peaks",
        [](FigurePtr self, const std::string& fn) {
            QString err;
            qsizetype added = 0;
            if (!inGui([&] {
                    const qsizetype before = qsizetype(self->model()->peaks().size());
                    const bool ok = self->w()->peakTable()->importCsv(toPath(fn), &err);
                    added = qsizetype(self->model()->peaks().size()) - before;
                    return ok;
                }))
                throw std::runtime_error(err.toStdString());
            return added;
        },
        py::arg("filename"), "Add peaks from a CSV written by export_peaks (one undo step); returns how many.");
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
           const Text& label, const std::string& style, double width, bool showValue, const py::object& spanTo,
           bool legend) {
            Marker m;
            m.inLegend = legend;
            m.orientation = toOrientation(orientation);
            m.position = pos;
            m.label = QString::fromStdString(label);
            const QColor c = toColor(color);
            m.lineStyle = toLineStyle(style);
            checkWidth(width);
            m.lineWidth = width;
            m.showValue = showValue;
            if (!spanTo.is_none()) {  // a band from position to span_to
                m.span = true;
                m.spanEnd = spanTo.cast<double>();
                m.showValue = false;
            }
            const ObjectId id = inGui([&] {
                m.color = c.isValid() ? c : paletteColor(7 + int(self->model()->markers().size()));
                return self->editor()->addMarker(m);
            });
            return MarkerH{{self, id}};
        },
        py::arg("position"), py::arg("orientation") = "v", py::kw_only(),
        py::arg("color") = py::none(), py::arg("label") = "", py::arg("style") = "--",
        py::arg("width") = 1.5, py::arg("show_value") = true, py::arg("span_to") = py::none(),
        py::arg("legend") = false,
        "Add a vertical ('v', at x = position) or horizontal ('h', at y = position) marker.\n"
        "legend=True lists the label in the legend instead of drawing it on the plot.");

    fig.def(
        "annotate",
        [](FigurePtr self, const Text& text, double x, double y, const py::object& color,
           const py::object& fontsizeObj, const std::string& ha, const std::string& va, const py::object& arrowTo,
           const std::string& coords) {
            if (coords != "data" && coords != "axes")
                throw py::value_error("coords must be 'data' or 'axes'");
            const double fontsize = fontsizeObj.is_none() ? 0.0 : fontsizeObj.cast<double>();
            if (!fontsizeObj.is_none() && !(fontsize > 0 && std::isfinite(fontsize)))
                throw py::value_error("fontsize must be a positive number");
            Annotation a;
            a.text = QString::fromStdString(text);
            a.x = x;
            a.y = y;
            const QColor c = toColor(color);
            if (c.isValid()) a.color = c;
            a.fontSize = fontsize;
            a.alignment = toAlignment(ha, va);
            a.axesCoords = coords == "axes";
            if (!arrowTo.is_none()) {
                const auto to = arrowTo.cast<std::pair<double, double>>();
                a.arrow = true;
                a.arrowX = to.first;
                a.arrowY = to.second;
            }
            return AnnotationH{{self, inGui([&] { return self->editor()->addAnnotation(a); })}};
        },
        py::arg("text"), py::arg("x"), py::arg("y"), py::kw_only(), py::arg("color") = py::none(),
        py::arg("fontsize") = py::none(), py::arg("ha") = "left", py::arg("va") = "bottom",
        py::arg("arrow_to") = py::none(), py::arg("coords") = "data",
        "Add a text annotation at data (x, y), or at a fraction of the axes box with\n"
        "coords='axes' ((0, 0) bottom-left, (1, 1) top-right; it stays put when the view changes). ha / va: which point of the text sits there\n"
        "('left', 'center', 'right' / 'bottom', 'center', 'top'); arrow_to=(x, y) draws an arrow\n"
        "from the text to that point."); 

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
    fig.def(
        "save",
        [](FigurePtr self, const std::string& filename) {
            const QString fn = toPath(filename);
            QString err;
            if (!inGui([&] { return self->w()->saveFigure(fn, &err); })) throw std::runtime_error(err.toStdString());
        },
        py::arg("filename"),
        "Save everything (data, peaks with labels/comments, markers, annotations, region, panels,\n"
        "styles, timeline) as a .gpfig file; reopen with plotter.load() or File > Open figure.");
    fig.def("_load", [](FigurePtr self, const std::string& filename) {
        const QString fn = toPath(filename);
        QString err;
        if (!inGui([&] {
                const bool ok = self->w()->loadFigure(fn, &err);
                self->connectCallbacks();  // panels created by the file
                return ok;
            }))
            throw std::runtime_error(err.toStdString());
    });
    fig.def("_has_timeline", [](FigurePtr self) { return inGui([&] { return self->w()->hasTimeline(); }); });
    fig.def(
        "export_data",
        [](FigurePtr self, const std::string& filename, bool region) {
            const QString fn = toPath(filename);
            QString err;
            const bool ok = inGui([&] {
                const Region r = self->model()->region();
                if (region && !r.active) {
                    err = "no region is selected";
                    return false;
                }
                return writeDataCsv(*self->model(), fn, region ? std::optional<Region>(r) : std::nullopt, &err);
            });
            if (!ok) throw std::runtime_error(err.toStdString());
        },
        py::arg("filename"), py::kw_only(), py::arg("region") = false,
        "Write the visible 1D data of the current panel as CSV (region=True: only the selected X region).");
    fig.def(
        "region_stats",
        [](FigurePtr self, const py::object& xmin, const py::object& xmax) {
            // Convert the Python arguments here, while the GIL is held.
            if (xmin.is_none() != xmax.is_none())
                throw py::value_error("pass both xmin and xmax, or neither (to use the selected region)");
            const bool useRegion = xmin.is_none();
            const double a = useRegion ? 0.0 : xmin.cast<double>();
            const double b = useRegion ? 0.0 : xmax.cast<double>();
            const auto stats = inGui([&] {
                if (!useRegion) return rangeStats(*self->model(), a, b);
                const Region r = self->model()->region();
                if (!r.active) throw py::value_error("no region is selected (or pass xmin, xmax)");
                return rangeStats(*self->model(), r.xmin, r.xmax);
            });
            py::dict out;
            for (const SignalStats& s : stats) {
                py::dict d;
                d["n"] = s.n;
                d["mean"] = s.mean;
                d["std"] = s.std;
                d["min"] = s.min;
                d["max"] = s.max;
                d["x_at_min"] = s.xAtMin;
                d["x_at_max"] = s.xAtMax;
                d["rms"] = s.rms;
                d["integral"] = s.integral;
                // Datasets may share a name: keep every entry by suffixing the id.
                QString key = s.name.isEmpty() ? QString("dataset %1").arg(s.id) : s.name;
                if (out.contains(py::str(key.toStdString()))) key += QString(" #%1").arg(s.id);
                out[py::str(key.toStdString())] = d;
            }
            return out;
        },
        py::arg("xmin") = py::none(), py::arg("xmax") = py::none(),
        "Statistics of every visible dataset inside the selected region (or [xmin, xmax]):\n"
        "{name: {n, mean, std, min, max, x_at_min, x_at_max, rms, integral}}.");
    fig.def("clear_selection", [](FigurePtr self) { inGui([&] { self->editor()->clearRegion(); }); });
    fig.def(
        "wait_for_region",
        [](FigurePtr self, double timeout) -> py::object {
            // Waiting forever on a window nobody can see would never return.
            if (timeout < 0 && !inGui([&] { return self->w()->isVisible(); }))
                throw std::runtime_error("the figure is not shown: call fig.show(block=False) first");
            const QString platform = QGuiApplication::platformName();
            if (timeout < 0 && (platform == "offscreen" || platform == "minimal")) {
                // No screen: nobody can select anything, so do not wait forever.
                py::module_::import("warnings").attr("warn")(
                    "plotter: no screen (offscreen platform); wait_for_region() returns None");
                return py::none();
            }
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
        // Remove from the handle's own panel (ids are only unique per panel).
        auto removeIn = [&](const Handle& hd, ObjectKind kind) {
            hd.checkFigure(self);
            inGui([&] { hd.editor()->removeObject({kind, hd.id}); });
        };
        if (py::isinstance<Line>(h)) removeIn(h.cast<Line&>(), ObjectKind::Dataset);
        else if (py::isinstance<Contour>(h)) removeIn(h.cast<Contour&>(), ObjectKind::Matrix);
        else if (py::isinstance<MarkerH>(h)) removeIn(h.cast<MarkerH&>(), ObjectKind::Marker);
        else if (py::isinstance<AnnotationH>(h)) removeIn(h.cast<AnnotationH&>(), ObjectKind::Annotation);
        else if (py::isinstance<PeakH>(h)) removeIn(h.cast<PeakH&>(), ObjectKind::Peak);
        else throw py::type_error("expected a Line, Contour, Marker, Annotation or Peak");
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
            if (m->axesInfo().xTime) {  // a cleared panel starts with number labels again
                AxesInfo info = m->axesInfo();
                info.xTime = false;
                ed->setAxesInfo(info);
            }
            // Like a new panel: the next data is fitted and gets the first colour.
            // Part of the undo step, so undo also brings back colours and view.
            ed->undoStack()->push(new FreshStartCommand(self->renderer(), m));
            ed->undoStack()->endMacro();
        });
    }, "Remove all datasets, markers, annotations and the selection (undoable).");

    // -- view
    auto getLim = [](FigurePtr self, bool x) {
        const ViewRange r = inGui([&] { return self->renderer()->viewRange(); });
        return x ? py::tuple(py::make_tuple(r.xmin, r.xmax)) : py::tuple(py::make_tuple(r.ymin, r.ymax));
    };
    fig.def("xlim", [getLim](FigurePtr s) { return getLim(s, true); }, "Current x axis limits.");
    fig.def("ylim", [getLim](FigurePtr s) { return getLim(s, false); }, "Current y axis limits.");
    fig.def(
        "set_xlim",
        [](FigurePtr self, double a, double b) {
            if (!std::isfinite(a) || !std::isfinite(b) || a == b)
                throw py::value_error("limits must be two different finite numbers");
            inGui([&] {
                // (high, low) reverses the axis, as in matplotlib; (low, high)
                // keeps the current direction (e.g. ppm axes stay reversed).
                if (a > b) {
                    AxesInfo info = self->model()->axesInfo();
                    if (!info.xReversed) {
                        info.xReversed = true;
                        self->editor()->setAxesInfo(info);
                    }
                }
                ViewRange r = self->renderer()->viewRange();
                r.xmin = std::min(a, b);
                r.xmax = std::max(a, b);
                self->renderer()->setViewRange(r);
            });
        },
        py::arg("xmin"), py::arg("xmax"));
    fig.def(
        "set_ylim",
        [](FigurePtr self, double a, double b) {
            if (!std::isfinite(a) || !std::isfinite(b) || a == b)
                throw py::value_error("limits must be two different finite numbers");
            inGui([&] {
                // (high, low) reverses the axis, as in matplotlib; (low, high)
                // keeps the current direction (e.g. ppm axes stay reversed).
                if (a > b) {
                    AxesInfo info = self->model()->axesInfo();
                    if (!info.yReversed) {
                        info.yReversed = true;
                        self->editor()->setAxesInfo(info);
                    }
                }
                ViewRange r = self->renderer()->viewRange();
                r.ymin = std::min(a, b);
                r.ymax = std::max(a, b);
                self->renderer()->setViewRange(r);
            });
        },
        py::arg("ymin"), py::arg("ymax"));
    fig.def(
        "autoscale",
        [](FigurePtr self, bool x, bool y) { inGui([&] { self->renderer()->autoscale(x, y); }); },
        py::arg("x") = true, py::arg("y") = true);
    fig.def("reset_view", [](FigurePtr self) { inGui([&] { self->renderer()->resetView(); }); });
    fig.def("fit_following", [](FigurePtr self) { inGui([&] { self->renderer()->fitFollowing(); }); },
            "Fit the axes that still follow the data (not zoomed, no limits set; shared axes included).");
    fig.def("set_home_view", [](FigurePtr self) {
        inGui([&] { self->renderer()->captureHomeView(); });
    }, "Make the current view the one restored by reset_view() / the Home key.");

    // -- labels
    auto axesProp = [](QString AxesInfo::*field) {
        return std::make_pair(
            [field](FigurePtr self) {
                return inGui([&] { return (self->model()->axesInfo().*field).toStdString(); });
            },
            [field](FigurePtr self, const Text& v) {  // None / any object, as documented
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
    fig.def_property(
        "window_title",
        [](FigurePtr self) { return inGui([&] { return self->w()->windowTitle().toStdString(); }); },
        [](FigurePtr self, const Text& t) { inGui([&] { self->w()->setWindowTitle(QString::fromStdString(t)); }); },
        "Title of the window only (not drawn on the plot).");
    fig.def_property("xlabel", xg, xs);
    fig.def_property(
        "suptitle",
        [](FigurePtr self) {
            return inGui([&] { return self->w()->axes(0).model->axesInfo().suptitle.toStdString(); });
        },
        [](FigurePtr self, const Text& t) {
            inGui([&] {
                AxesInfo a = self->w()->axes(0).model->axesInfo();
                a.suptitle = QString::fromStdString(t);
                self->w()->axes(0).editor->setAxesInfo(a);
            });
        },
        "Figure-wide title above all panels (each panel's own title is `title`).");
    // Custom ticks: positions and optional labels; None = automatic ticks.
    for (const bool isX : {true, false}) {
        fig.def(
            isX ? "set_xticks" : "set_yticks",
            [isX](FigurePtr self, const py::object& ticks, const py::object& labels) {
                bool custom = !ticks.is_none();
                QVector<double> pos;
                QStringList text;
                if (custom) {
                    pos = toVector(ticks, "ticks");
                    for (double v : pos)
                        if (!std::isfinite(v)) throw py::value_error("tick positions must be finite");
                    if (!labels.is_none()) {
                        for (const py::handle& l : labels) text << QString::fromStdString(py::str(l).cast<std::string>());
                        if (text.size() != pos.size())
                            throw py::value_error("give one label per tick position");
                    }
                }
                inGui([&] {
                    AxesInfo a = self->model()->axesInfo();
                    (isX ? a.customXTicks : a.customYTicks) = custom;
                    (isX ? a.xTicks : a.yTicks) = pos;
                    (isX ? a.xTickLabels : a.yTickLabels) = text;
                    self->editor()->setAxesInfo(a);
                });
            },
            py::arg("ticks"), py::arg("labels") = py::none(),
            "Tick positions (and optional text labels); [] hides the ticks, None restores automatic ticks.");
        fig.def(isX ? "get_xticks" : "get_yticks", [isX](FigurePtr self) -> py::object {
            const AxesInfo a = inGui([&] { return self->model()->axesInfo(); });
            if (!(isX ? a.customXTicks : a.customYTicks)) return py::none();
            std::vector<std::string> labels;
            for (const QString& t : isX ? a.xTickLabels : a.yTickLabels) labels.push_back(t.toStdString());
            return py::make_tuple(toNumpy(isX ? a.xTicks : a.yTicks), labels);
        }, "(positions, labels) of custom ticks, or None for automatic ticks.");
    }
    fig.def_property(
        "supylabel",
        [](FigurePtr self) {
            return inGui([&] { return self->w()->axes(0).model->axesInfo().supYLabel.toStdString(); });
        },
        [](FigurePtr self, const Text& t) {
            inGui([&] {
                AxesInfo a = self->w()->axes(0).model->axesInfo();
                a.supYLabel = QString::fromStdString(t);
                self->w()->axes(0).editor->setAxesInfo(a);
            });
        },
        "Figure-wide y label left of all panels (each panel keeps its own ylabel).");
    fig.def_property(
        "supxlabel",
        [](FigurePtr self) {
            return inGui([&] { return self->w()->axes(0).model->axesInfo().supXLabel.toStdString(); });
        },
        [](FigurePtr self, const Text& t) {
            inGui([&] {
                AxesInfo a = self->w()->axes(0).model->axesInfo();
                a.supXLabel = QString::fromStdString(t);
                self->w()->axes(0).editor->setAxesInfo(a);
            });
        },
        "Figure-wide x label below all panels (each panel keeps its own xlabel).");
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
    fig.def_property_readonly(
        "legend_visible", [](FigurePtr self) { return inGui([&] { return self->model()->axesInfo().legendVisible; }); },
        "Whether legend() turned the legend on (it shows when there are labelled items).");
    fig.def_property(
        "legend_with_twin",
        [](FigurePtr self) { return inGui([&] { return self->model()->axesInfo().legendWithTwin; }); },
        [](FigurePtr self, bool on) {
            inGui([&] {
                AxesInfo a = self->model()->axesInfo();
                a.legendWithTwin = on;
                self->editor()->setAxesInfo(a);
            });
        },
        "The legend also lists the lines of this panel's twinx panel (one legend for both).");
    fig.def_property(
        "legend_loc",
        [](FigurePtr self) -> py::object {
            const QString loc = inGui([&] { return self->model()->axesInfo().legendLoc; });
            if (loc.isEmpty()) return py::none();
            return py::str(loc.toStdString());
        },
        [](FigurePtr self, const py::object& v) {
            QString loc = v.is_none() ? QString() : QString::fromStdString(py::str(v).cast<std::string>()).trimmed();
            if (loc == "right") loc = "center right";  // matplotlib's alias
            static const QStringList ok = {"upper right", "upper left", "lower right", "lower left", "upper center",
                                           "lower center", "center left", "center right", "center", "best"};
            if (!loc.isEmpty() && !ok.contains(loc))
                throw py::value_error("legend_loc must be None or one of: " + ok.join(", ").toStdString());
            inGui([&] {
                AxesInfo a = self->model()->axesInfo();
                a.legendLoc = loc;
                self->editor()->setAxesInfo(a);
            });
        },
        "Legend position of this panel ('upper left', 'lower right', 'center', ...);\n"
        "None = the style's legend.loc.");
    for (const char* axis : {"x", "y"}) {
        const bool isX = axis[0] == 'x';
        fig.def_property(
            (std::string(axis) + "_tick_rotation").c_str(),
            [isX](FigurePtr self) {
                return inGui([&] { const AxesInfo& a = self->model()->axesInfo(); return isX ? a.xTickRotation : a.yTickRotation; });
            },
            [isX](FigurePtr self, double deg) {
                if (!std::isfinite(deg)) throw py::value_error("rotation must be a number of degrees");
                deg = std::clamp(std::remainder(deg, 360.0), -90.0, 90.0);
                inGui([&] {
                    AxesInfo a = self->model()->axesInfo();
                    (isX ? a.xTickRotation : a.yTickRotation) = deg;
                    self->editor()->setAxesInfo(a);
                });
            },
            "Tick label rotation in degrees (counter-clockwise, -90..90).");
    }
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
        auto [g5, s5] = axesFlag(&AxesInfo::xLog);
        auto [g6, s6] = axesFlag(&AxesInfo::yLog);
        auto [g7, s7] = axesFlag(&AxesInfo::xTime);
        fig.def_property("x_time", g7, s7,
                         "x values are Unix times (seconds, UTC): date / time tick labels.\n"
                         "Set automatically when the first data plotted has datetime x values.");
        fig.def_property("x_log", g5, s5, "Logarithmic x axis.");
        fig.def_property("y_log", g6, s6, "Logarithmic y axis.");
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
                return self->w()->applyStyle(toPath(nameOrPath), &errors);
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
                // The style is figure-wide: apply the changed keys to every panel.
                FigureWindow* w = self->w();
                std::vector<Style> styles;
                for (int i = 0; i < w->axesCount(); ++i) {
                    Style s = w->axes(i).model->style();
                    for (const auto& [k, v] : kv) {
                        QString err;
                        if (!s.set(QString::fromStdString(k), QString::fromStdString(v), &err))
                            throw py::value_error(err.toStdString());
                    }
                    styles.push_back(s);
                }
                w->undoStack()->beginMacro("Change style");
                for (int i = 0; i < w->axesCount(); ++i) w->axes(i).editor->setStyle(styles[size_t(i)], "Change style");
                w->undoStack()->endMacro();
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
            if (!inGui([&] { return saveStyle(toPath(fn), self->model()->style(), &err); }))
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
    fig.def("_twinx", [](FigurePtr self, int host) {
        const int i = inGui([&] {
            if (self->w()->hasTimeline()) throw py::value_error("timeline rows have no twin axes");
            const int t = self->w()->createTwinPanel(host);
            self->connectCallbacks();
            return t;
        });
        if (i < 0) throw py::value_error("this panel cannot get a twin (it is one, or already has one)");
        return i;
    });
    fig.def("_twin_of", [](FigurePtr self, int i) {
        return inGui([&] { return i >= 0 && i < self->w()->axesCount() ? self->w()->axes(i).twinOf : -1; });
    });
    fig.def("_current", [](FigurePtr self) { return inGui([&] { return self->w()->currentAxes(); }); });
    // Target panel of this figure for calls from the current Python thread
    // (-1: the current panel). Thread-local: no locking needed.
    fig.def_property(
        "_target", [](FigurePtr self) { return pyglue::callContext().panelTargets.value(self.get(), -1); },
        [](FigurePtr self, int i) {
            auto& targets = pyglue::callContext().panelTargets;
            if (i < 0) targets.remove(self.get());
            else targets.insert(self.get(), i);
        });
    fig.def_property_readonly("_closed", [](FigurePtr self) { return self->closed || !self->alive(); });
    fig.def("_set_current", [](FigurePtr self, int i) {
        inGui([&] {
            if (i < 0 || i >= self->w()->axesCount()) throw py::index_error("no such panel");
            self->w()->setCurrentAxes(i);
        });
    });
    fig.def(
        "_subplots",
        [](FigurePtr self, int rows, int cols, const py::object& sharex, const py::object& sharey, std::vector<double> hr,
           std::vector<double> wr, int hspace, int wspace) {
            inGui([&] {
                if (self->w()->hasTimeline())
                    throw py::value_error("a timeline figure arranges its own rows (use add_row)");
                GridSpec g;
                g.rows = rows;
                g.cols = cols;
                g.sharex = toShare(sharex);
                g.sharey = toShare(sharey);
                g.heightRatios = QList<double>(hr.begin(), hr.end());
                g.widthRatios = QList<double>(wr.begin(), wr.end());
                g.hspace = hspace;
                g.wspace = wspace;
                self->w()->setGrid(g);
                self->connectCallbacks();
            });
        });
    // ---- timeline (wrapped by plotter.Timeline) ----------------------------------------
    auto tl = [](FigurePtr& self) {
        Timeline* t = self->w()->timeline();
        self->connectTimeline();
        return t;
    };
    fig.def("_tl_enable", [tl](FigurePtr self) { inGui([&] { tl(self); }); });
    fig.def("_tl_add_row", [tl](FigurePtr self, const std::string& name) {
        return inGui([&] {
            const int row = tl(self)->addRow(QString::fromStdString(name));
            self->connectCallbacks();
            return row;
        });
    });
    fig.def(
        "_tl_add_signal",
        [tl](FigurePtr self, int row, const std::string& name, const py::object& t, const py::object& y,
             const py::object& color) {
            QVector<double> tv = toVector(t, "t"), yv = toVector(y, "y");
            if (tv.size() != yv.size()) throw py::value_error("t and y must have the same length");
            for (qsizetype i = 1; i < tv.size(); ++i)
                if (tv[i] < tv[i - 1]) throw py::value_error("t must be sorted (increasing)");
            const QColor c = toColor(color);
            const ObjectId id = inGui([&] {
                if (!tl(self)->rows().contains(row)) throw py::index_error("no such row");
                return tl(self)->addSignal(row, QString::fromStdString(name), tv, yv, c);
            });
            return Line{{self, id, row}};
        },
        py::arg("row"), py::arg("name"), py::arg("t"), py::arg("y"), py::arg("color") = py::none());
    fig.def("_tl_rows", [tl](FigurePtr self) {
        return inGui([&] {
            const QList<int> rows = tl(self)->rows();  // one copy: begin/end from the same list
            return std::vector<int>(rows.begin(), rows.end());
        });
    });
    fig.def("_tl_set_order", [tl](FigurePtr self, const std::vector<int>& order) {
        inGui([&] { tl(self)->setRowOrder(QList<int>(order.begin(), order.end())); });
    });
    fig.def("_tl_row_visible", [tl](FigurePtr self, int row) { return inGui([&] { return tl(self)->rowVisible(row); }); });
    fig.def("_tl_set_row_visible", [tl](FigurePtr self, int row, bool v) { inGui([&] { tl(self)->setRowVisible(row, v); }); });
    fig.def("_tl_row_height", [tl](FigurePtr self, int row) { return inGui([&] { return tl(self)->rowHeight(row); }); });
    fig.def("_tl_set_row_height", [tl](FigurePtr self, int row, double h) { inGui([&] { tl(self)->setRowHeight(row, h); }); });
    fig.def("_tl_row_name", [tl](FigurePtr self, int row) { return inGui([&] { return tl(self)->rowName(row).toStdString(); }); });
    fig.def("_tl_set_row_name", [tl](FigurePtr self, int row, const std::string& n) {
        inGui([&] { tl(self)->setRowName(row, QString::fromStdString(n)); });
    });
    fig.def("_tl_remove_row", [tl](FigurePtr self, int row) { return inGui([&] { return tl(self)->removeRowIfEmpty(row); }); });
    fig.def("_tl_move_signal", [tl](FigurePtr self, const Line& line, int toRow) {
        line.checkFigure(self);
        inGui([&] { line.model(); });  // validates the panel
        const ObjectId id = inGui([&] { return tl(self)->moveSignal({line.ax, line.id}, toRow); });
        if (id == kInvalidId) throw py::value_error("cannot move this signal to that row");
        return Line{{self, id, toRow}};
    });
    fig.def("_tl_load_csv", [tl](FigurePtr self, const std::vector<std::string>& files, bool perColumn) {
        QStringList l;
        for (const auto& f : files) l << QString::fromStdString(f);
        QString err;
        const bool ok = inGui([&] {
            const bool r = tl(self)->loadCsv(l, perColumn, &err);
            self->connectCallbacks();
            return r;
        });
        if (!ok) throw std::runtime_error(err.toStdString());
    });
    fig.def("_tl_window", [tl](FigurePtr self) {
        const auto w = inGui([&] { return tl(self)->window(); });
        return py::make_tuple(w.first, w.second);
    });
    fig.def("_tl_set_window", [tl](FigurePtr self, double a, double b) { inGui([&] { tl(self)->setWindow(a, b); }); });
    fig.def("_tl_full_range", [tl](FigurePtr self) {
        const auto w = inGui([&] { return tl(self)->fullRange(); });
        return py::make_tuple(w.first, w.second);
    });
    fig.def("_tl_show_all", [tl](FigurePtr self) { inGui([&] { tl(self)->showAll(); }); });
    fig.def("_tl_cursor", [tl](FigurePtr self) -> py::object {
        const auto c = inGui([&] { return tl(self)->cursorTime(); });
        return c ? py::object(py::float_(*c)) : py::object(py::none());
    });
    fig.def("_tl_set_cursor", [tl](FigurePtr self, const py::object& t) {
        const std::optional<double> v = t.is_none() ? std::nullopt : std::optional<double>(t.cast<double>());
        inGui([&] { tl(self)->setCursorTime(v); });
    });
    fig.def("_tl_values_at", [tl](FigurePtr self, double t) {
        const auto r = inGui([&] { return tl(self)->valuesAt(t); });
        std::vector<std::tuple<int, std::string, double>> out;
        for (const auto& x : r) out.emplace_back(x.row, x.name.toStdString(), x.value);
        return out;
    });
    fig.def("_tl_signals", [tl](FigurePtr self) {
        return inGui([&] {
            std::vector<std::tuple<int, ObjectId, std::string>> out;
            for (const SignalRef& s : tl(self)->signalList())
                out.emplace_back(s.row, s.id, tl(self)->signalName(s).toStdString());
            return out;
        });
    });
    fig.def("_tl_nav", [tl](FigurePtr self) {
        const SignalRef s = inGui([&] { return tl(self)->navigatorSignal(); });
        return py::make_tuple(s.row, s.id);
    });
    fig.def("_tl_set_nav", [tl](FigurePtr self, int row, ObjectId id) { inGui([&] { tl(self)->setNavigatorSignal({row, id}); }); });
    fig.def("_tl_autofit", [tl](FigurePtr self) { return inGui([&] { return tl(self)->autoFitY(); }); });
    fig.def("_tl_set_autofit", [tl](FigurePtr self, bool on) { inGui([&] { tl(self)->setAutoFitY(on); }); });
    fig.def("_tl_clock", [tl](FigurePtr self) { return inGui([&] { return tl(self)->clockTime(); }); });
    fig.def("_tl_set_clock", [tl](FigurePtr self, bool on) {
        const bool ok = inGui([&] {
            tl(self)->setClockTime(on);
            return tl(self)->clockTime() == on;
        });
        if (!ok) throw py::value_error("clock time needs absolute timestamps (set time_origin)");
    });
    fig.def("_tl_origin", [tl](FigurePtr self) -> py::object {
        const auto o = inGui([&] { return std::make_pair(tl(self)->hasAbsoluteTime(), tl(self)->timeOrigin()); });
        return o.first ? py::object(py::float_(o.second)) : py::object(py::none());
    });
    fig.def("_tl_set_origin", [tl](FigurePtr self, double o) { inGui([&] { tl(self)->setTimeOrigin(o); }); });
    fig.def("_tl_format_time", [tl](FigurePtr self, double t) { return inGui([&] { return tl(self)->formatTime(t).toStdString(); }); });
    fig.def("_tl_pins", [tl](FigurePtr self) {
        return inGui([&] {
            std::vector<std::tuple<ObjectId, double, std::string, std::string>> out;
            for (const TimePin& p : tl(self)->pins())
                out.emplace_back(p.id, p.t, p.label.toStdString(), colorName(p.color));
            return out;
        });
    });
    fig.def("_tl_add_pin", [tl](FigurePtr self, double t, const Text& label, const py::object& color) {
        const QColor c = toColor(color);
        return inGui([&] { return tl(self)->addPin(t, QString::fromStdString(label), c); });
    });
    fig.def("_tl_update_pin", [tl](FigurePtr self, ObjectId id, double t, const Text& label) {
        inGui([&] {
            for (const TimePin& p : tl(self)->pins())
                if (p.id == id) {
                    TimePin q = p;
                    q.t = t;
                    q.label = QString::fromStdString(label);
                    tl(self)->updatePin(q);
                    return;
                }
            throw py::key_error("this time marker no longer exists");
        });
    });
    fig.def("_tl_remove_pin", [tl](FigurePtr self, ObjectId id) { inGui([&] { tl(self)->removePin(id); }); });
    fig.def("_tl_jump_to_pin", [tl](FigurePtr self, ObjectId id) { inGui([&] { tl(self)->jumpToPin(id); }); });
    fig.def("_tl_step_pin", [tl](FigurePtr self, int direction) {
        return inGui([&] { return tl(self)->stepPin(direction); });
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
            if (width < 0 || height < 0) throw py::value_error("width and height must be >= 0 (0: figure size)");
            if (!(scale > 0) || !std::isfinite(scale) || scale > 20)
                throw py::value_error("scale must be > 0 and <= 20");
            QString err;
            const bool ok = inGui([&] {
                return self->w()->exportTo(toPath(filename), width, height, scale, &err);
            });
            if (!ok) throw std::runtime_error(err.toStdString());
        },
        py::arg("filename"), py::kw_only(), py::arg("width") = 0, py::arg("height") = 0,
        py::arg("scale") = 2.0,
        "Export to .png, .jpg, .svg or .pdf (format from the extension). width/height in pixels\n"
        "(0 = the figure size given to plotter.figure); scale multiplies the PNG resolution.");
    fig.def(
        "screenshot",
        [](FigurePtr self, const std::string& filename) {
            const bool ok = inGui([&] {
                // Finish pending updates (auto-fit, queued replots, panel
                // lists) so the image shows the current state.
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                return self->w()->grab().save(toPath(filename));
            });
            if (!ok) throw std::runtime_error("could not save screenshot");
        },
        py::arg("filename"), "Save an image of the whole window (toolbar and panel included).");
    fig.def("on", &Figure::addCallback, py::arg("event"), py::arg("callback"),
            R"doc(Register a callback, run on the GUI thread after an edit completes.

'region_changed'  callback(xmin, xmax)  - both None when the selection is cleared
'marker_changed'  callback(marker)      - after a marker was moved/edited
'levels_changed'  callback(contour)     - after contour settings/levels changed
'closed'          callback()            - the window was closed
Timeline figures (plotter.timeline) also have:
'cursor_moved'    callback(t or None)   - the cursor line moved
'window_changed'  callback(t0, t1)      - the visible time window changed
'pins_changed'    callback()            - time markers added/moved/removed
Remove a callback with off(event, callback).)doc");
    fig.def("disconnect_all", &Figure::clearCallbacks);
    fig.def("off", &Figure::removeCallback, py::arg("event"), py::arg("callback"),
            "Remove one callback registered with on(); returns True if it was registered.");

    // ---- Line ------------------------------------------------------------------------------
    py::class_<Line>(mod, "Line", "Handle to a 1D dataset.")
        // Two handles to the same dataset compare equal (e.g. in legend(handles=...)).
        .def("__eq__", [](const Line& a, const py::object& b) {
            if (!py::isinstance<Line>(b)) return false;
            const Line& o = b.cast<const Line&>();
            return a.fig == o.fig && a.ax == o.ax && a.id == o.id;
        })
        .def("__hash__", [](const Line& h) {
            return std::hash<const void*>()(h.fig.get()) ^ (std::hash<qint64>()(qint64(h.id)) * 31) ^ size_t(h.ax);
        })
        .def_property_readonly("_panel", [](const Line& h) { return h.ax; })
        .def_property_readonly("in_legend", [](Line& h) {
            return inGui([&] { const Dataset& d = h.get(); return d.style.inLegend && !d.name.isEmpty(); });
        }, "Whether the line is listed in the legend (it has a label).")
        .def_property_readonly("x", [](Line& h) { return toNumpy(inGui([&] { return h.get().x; })); })
        .def_property_readonly("y", [](Line& h) { return toNumpy(inGui([&] { return h.get().y; })); })
        .def_property("label",
                      [](Line& h) { return inGui([&] { return h.get().name.toStdString(); }); },
                      [](Line& h, const Text& v) {
                          h.setStyle([&](QString& n, DatasetStyle& s) {
                              n = QString::fromStdString(v);
                              s.inLegend = !v.empty();
                          });
                      })
        .def_property("color",
                      [](Line& h) { return colorName(inGui([&] { return h.get().style.color; })); },
                      [](Line& h, const py::object& v) {
                          const QColor c = requiredColor(v, "color");
                          h.setStyle([&](QString&, DatasetStyle& s) { s.color = c; });
                      })
        .def_property("width",
                      [](Line& h) { return inGui([&] { return h.get().style.lineWidth; }); },
                      [](Line& h, double v) {
                          checkWidth(v);
                          h.setStyle([&](QString&, DatasetStyle& s) { s.lineWidth = v; });
                      })
        .def_property("style",
                      [](Line& h) { return lineStyleName(inGui([&] { return h.get().style.lineStyle; })).toStdString(); },
                      [](Line& h, const Text& v) {
                          const LineStyle ls = toLineStyle(v);
                          h.setStyle([&](QString&, DatasetStyle& s) { s.lineStyle = ls; });
                      })
        .def_property("marker",
                      [](Line& h) { return scatterShapeName(inGui([&] { return h.get().style.scatter; })).toStdString(); },
                      [](Line& h, const py::object& v) {
                          const ScatterShape sh = toScatter(v);
                          h.setStyle([&](QString&, DatasetStyle& s) { s.scatter = sh; });
                      })
        .def_property(
            "markeredgecolor",
            [](Line& h) -> py::object {
                const QColor c = inGui([&] { return h.get().style.markerEdge; });
                if (!c.isValid()) return py::none();
                return py::str(colorName(c));
            },
            [](Line& h, const py::object& v) {
                // None = the line colour; 'none' = no outline.
                const bool none = py::isinstance<py::str>(v) && v.cast<std::string>() == "none";
                const QColor c = none ? QColor(Qt::transparent) : toColor(v, "markeredgecolor");
                h.setStyle([&](QString&, DatasetStyle& s) { s.markerEdge = c; });
            },
            "Outline colour of the symbols: None = the line colour, 'none' = no outline.")
        .def_property(
            "markerfacecolor",
            [](Line& h) -> py::object {
                const QColor c = inGui([&] { return h.get().style.markerFace; });
                if (!c.isValid()) return py::none();
                return py::str(colorName(c));
            },
            [](Line& h, const py::object& v) {
                // None = the line colour; 'none' = hollow.
                const bool none = py::isinstance<py::str>(v) && v.cast<std::string>() == "none";
                const QColor c = none ? QColor(Qt::transparent) : toColor(v, "markerfacecolor");
                h.setStyle([&](QString&, DatasetStyle& s) { s.markerFace = c; });
            },
            "Fill colour of the symbols: None = the line colour, 'none' = hollow symbols.")
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
                inGui([&] {
                    Dataset d = h.get();
                    if (d.x.size() != n) throw py::value_error("the data changed size meanwhile");
                    d.xErrLow = xl, d.xErrHigh = xh, d.yErrLow = yl, d.yErrHigh = yh;
                    h.editor()->setDatasetDataAndErrors(h.id, d);
                });
            },
            py::arg("xerr") = py::none(), py::arg("yerr") = py::none(),
            "Replace the error bars (None removes them; undoable).")
        .def(
            "append",
            [](Line& h, const py::object& x, const py::object& y, const py::object& maxPoints) {
                // One point (two numbers) or sequences of points.
                const auto vec = [](const py::object& v, const char* what) {
                    // A single value (number, date, 0-d array): a one-element list,
                    // so dates are converted like everywhere else.
                    const bool scalar = !py::hasattr(v, "__len__") ||
                                        (py::hasattr(v, "ndim") && v.attr("ndim").cast<int>() == 0);
                    if (scalar) {
                        py::list one;
                        one.append(v);
                        return toVector(one, what);
                    }
                    return toVector(v, what);
                };
                const QVector<double> xv = vec(x, "x"), yv = vec(y, "y");
                if (xv.size() != yv.size()) throw py::value_error("x and y must have the same length");
                const qsizetype keep = maxPoints.is_none() ? -1 : maxPoints.cast<qsizetype>();
                if (!maxPoints.is_none() && keep < 1) throw py::value_error("max_points must be >= 1");
                inGui([&] {
                    const Dataset& d = h.get();
                    QVector<double> nx = d.x, ny = d.y;
                    nx += xv;
                    ny += yv;
                    if (keep > 0 && nx.size() > keep) {  // rolling window: drop the oldest points
                        nx.remove(0, nx.size() - keep);
                        ny.remove(0, ny.size() - keep);
                    }
                    h.editor()->setDatasetData(h.id, std::move(nx), std::move(ny), false);
                    h.model()->setDatasetErrors(h.id, {}, {}, {}, {});  // as documented: no stale error bars
                });
            },
            py::arg("x"), py::arg("y"), py::kw_only(), py::arg("max_points") = py::none(),
            "Append points (live data). max_points keeps only the newest points (a rolling window).\n"
            "Like set_data, not recorded in the undo history; error bars are dropped.")
        .def_property_readonly("xerr", [](Line& h) {
            const Dataset d = inGui([&] { return h.get(); });
            if (d.xErrLow.isEmpty()) return py::object(py::none());
            return py::object(py::make_tuple(toNumpy(d.xErrLow), toNumpy(d.xErrHigh)));
        }, "(low, high) arrays or None")
        .def_property_readonly("yerr", [](Line& h) {
            const Dataset d = inGui([&] { return h.get(); });
            if (d.yErrLow.isEmpty()) return py::object(py::none());
            return py::object(py::make_tuple(toNumpy(d.yErrLow), toNumpy(d.yErrHigh)));
        }, "(low, high) arrays or None")
        .def_property("step",
                      [](Line& h) { return stepModeName(inGui([&] { return h.get().style.step; })).toStdString(); },
                      [](Line& h, const Text& v) {
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
        .def("remove", [](Line& h) { inGui([&] { h.get(); h.editor()->removeDataset(h.id); }); })
        .def_property_readonly("_id", [](Line& h) { return h.id; })
        .def_property_readonly("_panel", [](Line& h) { return h.ax; })
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
                      [](Contour& h, const Text& v) {
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
                          if (v < 1 || v > 1000) throw py::value_error("n_levels must be between 1 and 1000");
                          h.setStyle([&](QString&, ContourStyle& s) { s.levelCount = v; });
                      })
        .def_property("negative",
                      [](Contour& h) { return inGui([&] { return h.get().style.showNegative; }); },
                      [](Contour& h, bool v) { h.setStyle([&](QString&, ContourStyle& s) { s.showNegative = v; }); })
        .def_property("filled",
                      [](Contour& h) { return inGui([&] { return h.get().style.filled; }); },
                      [](Contour& h, bool v) {
                          h.setStyle([&](QString&, ContourStyle& s) {
                              s.filled = v;
                              if (v) s.showImage = true;
                          });
                      },
                      "Image in flat colour bands between the contour levels (contourf).")
        .def_property("negative_dashed",
                      [](Contour& h) { return inGui([&] { return h.get().style.negativeDashed; }); },
                      [](Contour& h, bool v) { h.setStyle([&](QString&, ContourStyle& s) { s.negativeDashed = v; }); },
                      "Draw negative levels dashed.")
        .def_property("image",
                      [](Contour& h) { return inGui([&] { return h.get().style.showImage; }); },
                      [](Contour& h, bool v) { h.setStyle([&](QString&, ContourStyle& s) { s.showImage = v; }); })
        .def_property("cmap",
                      [](Contour& h) {
                          const QString c = inGui([&] { return h.get().style.colormap; });
                          return (c.isEmpty() ? QString("auto") : c).toStdString();
                      },
                      [](Contour& h, const py::object& v) {
                          const QString c = checkedColormap(v);
                          h.setStyle([&](QString&, ContourStyle& s) { s.colormap = c; });
                      })
        .def_property("contours",
                      [](Contour& h) { return inGui([&] { return h.get().style.showContours; }); },
                      [](Contour& h, bool v) {
                          h.setStyle([&](QString&, ContourStyle& s) {
                              s.showContours = v;
                              if (!v) s.showImage = true;  // never invisible
                          });
                      })
        .def_property("colorbar",
                      [](Contour& h) { return inGui([&] { return h.get().style.colorBar; }); },
                      [](Contour& h, bool v) { h.setStyle([&](QString&, ContourStyle& s) { s.colorBar = v; }); })
        .def_property("clim",
                      [](Contour& h) -> py::object {
                          const ContourStyle s = inGui([&] { return h.get().style; });
                          if (!s.fixedRange) return py::none();
                          auto lim = [](double v) { return std::isfinite(v) ? py::object(py::float_(v)) : py::object(py::none()); };
                          return py::make_tuple(lim(s.vmin), lim(s.vmax));
                      },
                      [](Contour& h, const py::object& v) {
                          bool fixed = false;
                          double lo = 0, hi = 1;
                          if (!v.is_none()) {
                              const auto pair = v.cast<std::pair<py::object, py::object>>();
                              std::tie(fixed, lo, hi) = colorLimits(pair.first, pair.second);
                          }
                          h.setStyle([&](QString&, ContourStyle& s) {
                              s.fixedRange = fixed;
                              s.vmin = lo;
                              s.vmax = hi;
                          });
                      },
                      "Image colour limits (vmin, vmax), or None for the data range.")
        .def_property("visible",
                      [](Contour& h) { return inGui([&] { return h.get().style.visible; }); },
                      [](Contour& h, bool v) { h.setStyle([&](QString&, ContourStyle& s) { s.visible = v; }); })
        .def_property("color",
                      [](Contour& h) { return colorName(inGui([&] { return h.get().style.positiveColor; })); },
                      [](Contour& h, const py::object& v) {
                          const QColor c = requiredColor(v, "color");
                          h.setStyle([&](QString&, ContourStyle& s) { s.positiveColor = c; });
                      })
        .def_property("negative_color",
                      [](Contour& h) { return colorName(inGui([&] { return h.get().style.negativeColor; })); },
                      [](Contour& h, const py::object& v) {
                          const QColor c = requiredColor(v, "negative_color");
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
                    const Matrix& old = h.get();
                    // Coordinates not given: the grid keeps its extent (ppm axes stay).
                    if (x.is_none()) m.x0 = old.x0, m.x1 = old.x1;
                    if (y.is_none()) m.y0 = old.y0, m.y1 = old.y1;
                    h.editor()->setMatrixData(h.id, m.nx, m.ny, m.z, m.x0, m.x1, m.y0, m.y1);
                });
            },
            py::arg("z"), py::arg("x") = py::none(), py::arg("y") = py::none(),
            "Replace the grid (keeps the contour settings).")
        .def("remove", [](Contour& h) { inGui([&] { h.get(); h.editor()->removeMatrix(h.id); }); })
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
        .def_property("in_legend",
                      [](MarkerH& h) { return inGui([&] { return h.get().inLegend; }); },
                      [](MarkerH& h, bool v) { h.edit([&](Marker& m) { m.inLegend = v; }); },
                      "List the label in the legend instead of drawing it on the plot.")
        .def_property(
            "span_end",
            [](MarkerH& h) -> py::object {
                const Marker m = inGui([&] { return h.get(); });
                if (!m.span) return py::none();
                return py::float_(m.spanEnd);
            },
            [](MarkerH& h, const py::object& v) {
                const bool on = !v.is_none();
                const double end = on ? v.cast<double>() : 0.0;
                if (on && !std::isfinite(end)) throw py::value_error("span_end must be finite");
                h.edit([&](Marker& m) {
                    m.span = on;
                    if (on) m.spanEnd = end;
                });
            },
            "Other edge of a band (axvspan / axhspan), or None for a plain line.")
        .def_property_readonly("orientation", [](MarkerH& h) {
            return inGui([&] { return h.get().orientation; }) == Orientation::Vertical ? "v" : "h";
        })
        .def_property("label",
                      [](MarkerH& h) { return inGui([&] { return h.get().label.toStdString(); }); },
                      [](MarkerH& h, const Text& v) {
                          h.edit([&](Marker& m) { m.label = QString::fromStdString(v); });
                      })
        .def_property("color",
                      [](MarkerH& h) { return colorName(inGui([&] { return h.get().color; })); },
                      [](MarkerH& h, const py::object& v) {
                          const QColor c = requiredColor(v, "color");
                          h.edit([&](Marker& m) { m.color = c; });
                      })
        .def_property("style",
                      [](MarkerH& h) { return lineStyleName(inGui([&] { return h.get().lineStyle; })).toStdString(); },
                      [](MarkerH& h, const Text& v) {
                          const LineStyle ls = toLineStyle(v);
                          h.edit([&](Marker& m) { m.lineStyle = ls; });
                      })
        .def_property("width",
                      [](MarkerH& h) { return inGui([&] { return h.get().lineWidth; }); },
                      [](MarkerH& h, double v) {
                          checkWidth(v);
                          h.edit([&](Marker& m) { m.lineWidth = v; });
                      })
        .def("remove", [](MarkerH& h) { inGui([&] { h.get(); h.editor()->removeMarker(h.id); }); })
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
                          h.edit(
                              [&](Peak& p) {
                                  p.x = v.first;
                                  p.y = v.second;
                                  // Intensity follows the new position (as when dragging).
                                  PlotRenderer* r = h.fig->w()->axes(h.ax).renderer;
                                  if (const Matrix* m = r->matrixAt({p.x, p.y}))
                                      p.intensity = sampleMatrix(*m, p.x, p.y);
                              },
                              "Move peak");
                      })
        .def_property_readonly("x", [](PeakH& h) { return inGui([&] { return h.get().x; }); })
        .def_property_readonly("y", [](PeakH& h) { return inGui([&] { return h.get().y; }); })
        .def_property_readonly("intensity", [](PeakH& h) { return inGui([&] { return h.get().intensity; }); })
        .def_property("label",
                      [](PeakH& h) { return inGui([&] { return h.get().label.toStdString(); }); },
                      [](PeakH& h, const Text& v) {
                          h.edit([&](Peak& p) { p.label = QString::fromStdString(v); }, "Edit peak label");
                      })
        .def_property("comment",
                      [](PeakH& h) { return inGui([&] { return h.get().comment.toStdString(); }); },
                      [](PeakH& h, const Text& v) {
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
        .def("remove", [](PeakH& h) { inGui([&] { h.get(); h.editor()->removePeak(h.id); }); })
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
                      [](AnnotationH& h) {
                          // Unset: the style's annotation colour (what is drawn).
                          return colorName(inGui([&] {
                              const QColor c = h.get().color;
                              return c.isValid() ? c : h.model()->style().annotationColor;
                          }));
                      },
                      [](AnnotationH& h, const py::object& v) {
                          const QColor c = toColor(v);
                          h.edit([&](Annotation& a) { a.color = c; });
                      })
        .def_property(
            "coords",
            [](AnnotationH& h) { return std::string(inGui([&] { return h.get().axesCoords; }) ? "axes" : "data"); },
            [](AnnotationH& h, const std::string& v) {
                if (v != "data" && v != "axes") throw py::value_error("coords must be 'data' or 'axes'");
                h.edit([&](Annotation& a) { a.axesCoords = v == "axes"; });
            },
            "'data': the position is in data coordinates; 'axes': fractions of the axes box\n"
            "(switching re-reads the same numbers in the other coordinates).")
        .def_property(
            "arrow_to",
            [](AnnotationH& h) -> py::object {
                const Annotation a = inGui([&] { return h.get(); });
                if (!a.arrow) return py::none();
                return py::make_tuple(a.arrowX, a.arrowY);
            },
            [](AnnotationH& h, const py::object& v) {
                const bool on = !v.is_none();
                const auto to = on ? v.cast<std::pair<double, double>>() : std::pair<double, double>{0, 0};
                h.edit([&](Annotation& a) {
                    a.arrow = on;
                    a.arrowX = to.first;
                    a.arrowY = to.second;
                });
            },
            "Point the arrow goes to, (x, y), or None for no arrow.")
        .def_property("fontsize",
                      [](AnnotationH& h) {
                          return inGui([&] {
                              const double f = h.get().fontSize;
                              return f > 0 ? f : h.model()->style().annotationSize;
                          });
                      },
                      [](AnnotationH& h, const py::object& v) {
                          // None: follow the style again.
                          const double size = v.is_none() ? 0.0 : v.cast<double>();
                          if (!v.is_none() && !(size > 0 && std::isfinite(size)))
                              throw py::value_error("fontsize must be a positive number (or None for the style default)");
                          h.edit([&](Annotation& a) { a.fontSize = size; });
                      })
        .def("remove", [](AnnotationH& h) { inGui([&] { h.get(); h.editor()->removeAnnotation(h.id); }); })
        .def("__repr__", [](AnnotationH& h) {
            return inGui([&]() -> std::string {
                if (!h.fig->alive() || !h.model()->annotation(h.id)) return "<Annotation (removed)>";
                return "<Annotation '" + h.get().text.toStdString() + "'>";
            });
        });
}
