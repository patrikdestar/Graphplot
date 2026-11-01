// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
//
// Timeline: a figure mode for multi-sensor logs (IMU, GPS, CAN, ...).
//
//   +--------------------------------------------+
//   | row 1  (e.g. accelerometer: ax, ay, az)    |  rows share one time axis
//   | row 2  (gyroscope)                         |  (zoom / pan any row)
//   | row 3  (speed)                             |
//   +--------------------------------------------+
//   | navigator: overview of one signal, with    |  drag / resize / wheel the
//   | the visible time window as a shaded band   |  window -> all rows follow
//   +--------------------------------------------+
//
// A vertical cursor line follows the mouse through all rows and shows each
// signal's value at that time. Pins are named, movable, undoable time
// markers drawn through all rows. Rows can be reordered, resized, hidden,
// and signals moved between rows. With auto-fit, each row rescales Y to the
// data inside the visible window.
//
// Rows are ordinary figure panels (PlotModel + PlotEditor + PlotRenderer),
// so styles, export, undo and the Python API keep working.
#pragma once

#include "model/PlotTypes.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QSharedPointer>
#include <optional>
#include <vector>

class QCPAxisRect;
class QCPAbstractItem;
class QCPAxisTicker;
class QCPItemStraightLine;
class QCPLayer;
class QCustomPlot;
class QTimer;

namespace gp {

class FigureWindow;
class PlotModel;
class PlotRenderer;
class RichTextItem;

struct TimePin {
    ObjectId id = kInvalidId;
    double t = 0.0;  // timeline time (seconds since the origin)
    QString label;
    QColor color = QColor(0xe3, 0x49, 0x48);
    bool operator==(const TimePin&) const = default;
};

// A signal reference: panel (row) index + dataset id.
struct SignalRef {
    int row = -1;
    ObjectId id = kInvalidId;
    bool isValid() const { return row >= 0 && id != kInvalidId; }
    bool operator==(const SignalRef&) const = default;
};

class Timeline : public QObject {
    Q_OBJECT
public:
    explicit Timeline(FigureWindow* window);
    ~Timeline() override;

    // ---- rows -----------------------------------------------------------
    // A row is a figure panel; ids are panel indices (stable).
    int addRow(const QString& name);
    // Adds a signal (time in seconds since the origin) to a row; undoable.
    ObjectId addSignal(int row, const QString& name, QVector<double> t, QVector<double> y,
                       const QColor& color = {});
    QList<int> rows() const { return m_order; }  // display order (top to bottom)
    void setRowOrder(const QList<int>& order);
    bool rowVisible(int row) const;
    void setRowVisible(int row, bool visible);
    double rowHeight(int row) const;
    void setRowHeight(int row, double stretch);  // relative height (default 1)
    // Removes a row from the layout if it has no signals left (its panel
    // stays allocated but hidden, so panel indices remain stable).
    bool removeRowIfEmpty(int row);
    QString rowName(int row) const;
    void setRowName(int row, const QString& name);
    // Moves a signal to another row (one undo step); returns its new id.
    ObjectId moveSignal(const SignalRef& from, int toRow);

    // Loads CSV logs: one row per file, or one row per column.
    bool loadCsv(const QStringList& files, bool rowPerColumn, QString* error = nullptr);

    // ---- time window ------------------------------------------------------
    QPair<double, double> window() const;
    void setWindow(double t0, double t1);
    QPair<double, double> fullRange() const;
    PlotModel* navigatorModel() const { return m_navModel; }
    PlotRenderer* navigatorRenderer() const { return m_navRenderer; }
    void showAll();

    // ---- navigator --------------------------------------------------------
    SignalRef navigatorSignal() const { return m_navSignal; }
    void setNavigatorSignal(const SignalRef& s);
    QList<SignalRef> signalList() const;     // all signals, in row display order
    QString signalName(const SignalRef& s) const;

    // ---- cursor -------------------------------------------------------------
    std::optional<double> cursorTime() const { return m_cursor; }
    void setCursorTime(std::optional<double> t);  // also used by the mouse
    // Interpolated value of every visible signal at time t: (row, name, value).
    struct Reading {
        int row;
        QString name;
        double value;
        QColor color;
    };
    QList<Reading> valuesAt(double t) const;

    // ---- options ------------------------------------------------------------
    bool autoFitY() const { return m_autoFit; }
    void setAutoFitY(bool on);
    bool clockTime() const { return m_clock; }
    void setClockTime(bool on);  // only possible when the data has absolute times
    bool hasAbsoluteTime() const { return m_absolute; }
    double timeOrigin() const { return m_origin; }  // Unix epoch seconds of t = 0
    void setTimeOrigin(double epochSeconds);         // marks the data as absolute
    QString formatTime(double t, double resolution = 0) const;

    // ---- pins (undoable) ------------------------------------------------------
    const std::vector<TimePin>& pins() const { return m_pins; }
    ObjectId addPin(double t, const QString& label = {}, const QColor& color = {});
    void updatePin(const TimePin& after, std::optional<TimePin> before = std::nullopt);
    void removePin(ObjectId id);
    void jumpToPin(ObjectId id);  // centres the window on the pin
    // Centres the window on the next (+1) or previous (-1) pin relative to
    // the window centre; returns the pin id (kInvalidId if there is none).
    ObjectId stepPin(int direction);
    // Used by the undo commands.
    void applyPin(ObjectId id, const std::optional<TimePin>& state);

signals:
    void windowChanged(double t0, double t1);
    void cursorChanged(std::optional<double> t);
    void rowsChanged();
    void pinsChanged();
    void signalsChanged();
    void optionsChanged();  // auto-fit, clock time, origin

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct RowVisual {
        QCPItemStraightLine* cursor = nullptr;
        RichTextItem* readout = nullptr;
        double height = 1.0;
        bool visible = true;
        QSharedPointer<QCPAxisTicker> linearTicker;
    };

    void relayout();
    void attachRow(int row);
    void updateCursorVisuals();
    void rebuildPinVisuals();
    void updateNavigatorData();
    void scheduleAutoFit();
    void autoFitNow();
    void applyTimeAxis();
    int rowAt(const QPointF& px) const;           // visible row under px, or -1
    int firstVisibleRow() const;
    void applyThemeColors();
    bool inNavigator(const QPointF& px) const;
    ObjectId pinAt(const QPointF& px, int row) const;
    PlotRenderer* rowRenderer(int row) const;
    void onPrimaryRangeChanged();
    void setWindowFromNavigator(double a, double b);
    void bindToRect(QCPAbstractItem* item, PlotRenderer* r);

    FigureWindow* m_win;
    QCustomPlot* m_plot;
    QList<int> m_order;
    QHash<int, RowVisual> m_rows;

    // navigator
    PlotModel* m_navModel = nullptr;
    PlotRenderer* m_navRenderer = nullptr;
    QCPItemStraightLine* m_navCursor = nullptr;
    RichTextItem* m_navTimeLabel = nullptr;
    SignalRef m_navSignal;
    ObjectId m_navDataset = kInvalidId;

    // state
    std::optional<double> m_cursor;
    bool m_autoFit = true;
    bool m_clock = false;
    bool m_absolute = false;
    double m_origin = 0.0;
    bool m_syncing = false;
    bool m_firstRowUsed = false;
    bool m_fitScheduled = false;
    QCPLayer* m_cursorLayer = nullptr;

    // pins
    std::vector<TimePin> m_pins;
    ObjectId m_lastPinId = 0;
    std::vector<QCPAbstractItem*> m_pinItems;

    // mouse drags handled by the timeline
    enum class Drag { None, NavMove, NavMin, NavMax, NavCreate, Pin };
    Drag m_drag = Drag::None;
    double m_pressT = 0;
    QPoint m_pressPx;
    QPair<double, double> m_pressWindow;
    TimePin m_dragPin;
    bool m_dragMoved = false;
};

}  // namespace gp
