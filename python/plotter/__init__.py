# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""plotter - interactive scientific plotting (C++/Qt) controlled from Python.

Rendering and mouse interaction run natively in C++/Qt; Python creates
figures, sends NumPy data and reads back what the user selected::

    import numpy as np
    import plotter

    x = np.linspace(0, 100, 2000)
    fig = plotter.figure()
    fig.plot(x, np.sin(x), label="Signal 1", color="blue")
    fig.marker(25.0, color="green", label="Marker 1")
    fig.show()

    xmin, xmax = fig.wait_for_region()     # let the user drag a region
    m = (x >= xmin) & (x <= xmax)

Event loop
----------
* Interactive prompt (python -i, the REPL, IPython with ``%gui plotter``):
  windows stay live while the prompt waits for input; ``fig.show()`` returns
  immediately.
* Scripts: ``fig.show()`` blocks until the window is closed (pass
  ``block=False`` to continue, and call ``plotter.show()`` /
  ``plotter.pause()`` later).

Threading
---------
Create figures from the main thread. Methods may be called from other Python
threads; such calls are forwarded to the GUI thread and wait until the main
thread runs the event loop (prompt idle, ``show()``, ``pause()``, ...).
"""
from __future__ import annotations

__author__ = "Justinas Minkevicius <oompagoo@gmail.com>"
__copyright__ = "Copyright (c) 2026 Justinas Minkevicius. All rights reserved."
__license__ = "Proprietary - no use without the author's written permission (see LICENSE)"

import sys as _sys
import threading as _threading

from ._plotter import Annotation, Contour, Figure, Line, Marker, Peak
from ._plotter import process_events as _process_events
from ._plotter import _run_until_closed
from . import style

__all__ = [
    "style", "figure", "Axes", "timeline", "Timeline", "TimelineRow", "TimePin", "show", "pause", "figures", "close_all",
    "Figure", "Line", "Contour", "Marker", "Annotation", "Peak",
]

_figures: list[Figure] = []
# Serialises "make panel current + call" sequences of Axes proxies, so two
# threads using different panels of one figure cannot interleave.
_panel_lock = _threading.RLock()


def _interactive() -> bool:
    """True when running under an interactive prompt (REPL, python -i, IPython)."""
    if hasattr(_sys, "ps1") or _sys.flags.interactive:
        return True
    return "IPython" in _sys.modules and _ipython_active()


def _ipython_active() -> bool:
    try:
        from IPython import get_ipython  # noqa: PLC0415
    except ImportError:
        return False
    return get_ipython() is not None


def figure(title: str | None = None, size: tuple[int, int] = (1000, 650)) -> Figure:
    """Create a new figure window (shown with ``fig.show()``)."""
    fig = Figure(title, int(size[0]), int(size[1]))
    _figures.append(fig)
    return fig


def _forget_closed() -> None:
    # Closed figures stay usable through their handles, but show() must not
    # re-open them and the module must not keep them alive forever.
    _figures[:] = [f for f in _figures if f.is_open]


def figures() -> list[Figure]:
    """All figures created in this session that are still open."""
    return [f for f in _figures if f.is_open]


def show(block: bool | None = None, timeout: float | None = None) -> None:
    """Show all figures. If blocking, run the event loop until all are closed."""
    _forget_closed()  # never re-open windows the user already closed
    for f in list(_figures):
        f._show()
    if block is None:
        block = not _interactive()
    if block:
        _run_until_closed(list(_figures), -1.0 if timeout is None else float(timeout))


def pause(seconds: float = 0.05) -> None:
    """Keep the windows responsive for `seconds` (useful inside loops)."""
    _process_events(int(seconds * 1000))


def close_all() -> None:
    for f in _figures:
        f.close()
    _figures.clear()


def _figure_show(self: Figure, block: bool | None = None, timeout: float | None = None) -> Figure:
    """Show this figure. Blocks until it is closed when not running interactively."""
    self._show()
    if block is None:
        block = not _interactive()
    if block:
        _run_until_closed([self], -1.0 if timeout is None else float(timeout))
    else:
        _process_events(0)  # paint the window right away
    return self


Figure.show = _figure_show
Figure.vline = lambda self, x, **kw: self.marker(x, "v", **kw)
Figure.vline.__doc__ = "Vertical marker at x (same keywords as marker())."
Figure.hline = lambda self, y, **kw: self.marker(y, "h", **kw)
Figure.hline.__doc__ = "Horizontal marker at y (same keywords as marker())."


def _region_mask(self: Figure, x):
    """Boolean mask of the selected region for an x array (None if no selection)."""
    import numpy as np  # noqa: PLC0415

    r = self.get_selected_region()
    if r is None:
        return None
    x = np.asarray(x)
    return (x >= r[0]) & (x <= r[1])


Figure.region_mask = _region_mask


def _errorbar(self: Figure, x, y, yerr=None, xerr=None, *, label=None, color=None,
              marker="o", markersize=None, style="none", width=None, **kw) -> Line:
    """Points with error bars (data-point style). yerr/xerr: scalar, array or (low, high)."""
    return self.plot(x, y, label=label, color=color, marker=marker, markersize=markersize,
                     style=style, width=width, xerr=xerr, yerr=yerr, **kw)


def _hist(self: Figure, data, bins=10, range=None, *, density=False, weights=None,  # noqa: A002
          histtype="step", label=None, color=None, fill=None, width=None, style="-"):
    """Histogram of `data` (numpy.histogram semantics).

    histtype  'step'        outline (default)
              'stepfilled'  filled area (alpha 0.3 of the line colour, or `fill`)
              'errorbar'    points at bin centres with Poisson errors sqrt(N)
                            (scaled consistently when density=True or weights are given)
    Returns (counts, edges, Line)."""
    import numpy as np

    data = np.asarray(data, dtype=float).ravel()
    counts, edges = np.histogram(data, bins=bins, range=range, weights=weights)
    widths = np.diff(edges)
    if histtype == "errorbar":
        # Poisson errors: sqrt(sum of w^2) per bin (= sqrt(N) when unweighted).
        w2 = None if weights is None else np.square(np.asarray(weights, dtype=float).ravel())
        err = np.sqrt(np.histogram(data, bins=edges, weights=w2)[0])
    if density:
        norm = counts.sum() * widths
        counts = counts / norm
        if histtype == "errorbar":
            err = err / norm
    if histtype == "errorbar":
        centres = 0.5 * (edges[:-1] + edges[1:])
        line = self.plot(centres, counts, label=label, color=color, marker="o", style="none",
                         width=width, yerr=err, xerr=0.5 * widths)
        return counts, edges, line
    if histtype not in ("step", "stepfilled"):
        raise ValueError("histtype must be 'step', 'stepfilled' or 'errorbar'")
    # Closed outline: drop to zero at both outer edges.
    xs = np.concatenate([[edges[0]], edges])
    ys = np.concatenate([[0.0], counts, [0.0]])
    if fill is None and histtype == "stepfilled":
        fill = True
    line = self.plot(xs, ys, label=label, color=color, step="post", fill=fill, width=width,
                     style=style)
    return counts, edges, line


def _fill_between(self: Figure, x, y1, y2=0.0, *, label=None, color=None, alpha=0.3,
                  line=False, step=None) -> Line:
    """Fill the area between y1 and y2 (scalar or array), e.g. uncertainty bands.

    step: None or 'pre'/'post'/'mid' (use 'mid' for bands on histogram bin centres).
    One undo step. Returns the Line of y1 (y2 is an unlabelled helper curve)."""
    import numpy as np

    from ._plotter import _rgba

    x = np.asarray(x, dtype=float)
    y2 = np.broadcast_to(np.asarray(y2, dtype=float), x.shape)
    if color is None:
        color = self._take_color()
    r, g, b, _ = _rgba(color)
    fill = (r, g, b, float(alpha))
    style = "-" if line else "none"
    self._begin_macro("Fill between")
    try:
        lower = self.plot(x, y2, color=color, style=style, step=step)
        return self.plot(x, y1, label=label, color=color, style=style, fill=fill, fill_to=lower,
                         step=step)
    finally:
        self._end_macro()


class Axes:
    """One panel of a multi-panel figure (see Figure.subplots / Figure.ratio_panel).

    It offers the same plotting methods and properties as Figure (plot, hist,
    errorbar, contour, marker, annotate, pick_peaks, xlabel, set_xlim, ...),
    applied to this panel. Handles returned by these methods stay bound to
    the panel they were created in."""

    def __init__(self, figure: Figure, index: int):
        object.__setattr__(self, "figure", figure)
        object.__setattr__(self, "index", index)

    def _activate(self):
        if self.figure._current() != self.index:
            self.figure._set_current(self.index)

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        # Properties (xlabel, x_log, ...) are read from this panel.
        if isinstance(getattr(type(self.figure), name, None), property):
            with _panel_lock:
                self._activate()
                return getattr(self.figure, name)
        attr = getattr(self.figure, name)
        if not callable(attr):
            return attr

        def call(*args, **kwargs):
            with _panel_lock:
                self._activate()
                return attr(*args, **kwargs)

        call.__name__ = name
        call.__doc__ = getattr(attr, "__doc__", None)
        return call

    def __setattr__(self, name, value):
        with _panel_lock:
            self._activate()
            setattr(self.figure, name, value)

    def __repr__(self):
        return f"<Axes {self.index} of {self.figure!r}>"

    def __eq__(self, other):
        return isinstance(other, Axes) and other.figure is self.figure and other.index == self.index

    def __hash__(self):
        return hash((id(self.figure), self.index))


def _subplots(self: Figure, nrows: int = 1, ncols: int = 1, *, sharex: bool = False,
              sharey: bool = False, height_ratios=None, width_ratios=None, hspace=None,
              wspace=None, squeeze: bool = True):
    """Arrange the figure as nrows x ncols panels and return their Axes.

    Existing content stays in panel 0 (top-left). sharex/sharey link the axis
    ranges and keep tick labels only on the outer panels. hspace/wspace are
    the gaps in pixels (0 = frames touch, as in ratio plots).
    Returns a list (one row or column, like matplotlib's squeeze) or a list of rows."""
    self._subplots(int(nrows), int(ncols), bool(sharex), bool(sharey),
                   list(height_ratios or []), list(width_ratios or []),
                   -1 if hspace is None else int(hspace), -1 if wspace is None else int(wspace))
    grid = [[Axes(self, r * ncols + c) for c in range(ncols)] for r in range(nrows)]
    if squeeze and (nrows == 1 or ncols == 1):
        return [a for row in grid for a in row]
    return grid


def _ratio_panel(self: Figure, ratio: float = 0.3, *, ylabel: str = "Ratio"):
    """Main plot with a ratio/residual panel below (shared x, touching frames).

    Returns (main, lower) Axes. `ratio` is the lower panel's height fraction."""
    main, lower = _subplots(self, 2, 1, sharex=True, height_ratios=[1 - ratio, ratio], hspace=0)
    lower.ylabel = ylabel
    xlabel = main.xlabel
    if xlabel:
        lower.xlabel, main.xlabel = xlabel, ""
    return main, lower


def _axes_list(self: Figure):
    return [Axes(self, i) for i in range(self._axes_count())]


def _gca(self: Figure):
    """The current panel (last created/used, or clicked in the window)."""
    return Axes(self, self._current())


def _to_seconds(t):
    """Time values as float seconds; returns (array, is_absolute_epoch).

    Accepts numbers (seconds), numpy datetime64 / timedelta64 and sequences of
    datetime-like objects (pandas Timestamps, datetime.datetime)."""
    import numpy as np

    a = np.asarray(t)
    if a.dtype.kind == "M":  # datetime64 -> Unix epoch seconds
        return a.astype("datetime64[ns]").astype(np.int64) / 1e9, True
    if a.dtype.kind == "m":  # timedelta64 -> seconds
        return a.astype("timedelta64[ns]").astype(np.int64) / 1e9, False
    if a.dtype == object and a.size and hasattr(a.flat[0], "timestamp"):
        return np.array([x.timestamp() for x in a.ravel()], dtype=float), True
    return a.astype(float), False


class TimelineRow:
    """One row of a Timeline (a figure panel). Plot into it with ``row.plot``."""

    def __init__(self, timeline: "Timeline", index: int):
        self._tl = timeline
        self.index = index

    def plot(self, t, y, label: str = "", color=None) -> Line:
        """Add a signal. ``t``: seconds since the timeline origin, or absolute
        times (datetime64 / pandas timestamps), which set the origin on first use."""
        secs, absolute = _to_seconds(t)
        if absolute:
            if self._tl.time_origin is None:
                self._tl.time_origin = float(secs[0])
            secs = secs - self._tl.time_origin
        return self._tl.figure._tl_add_signal(self.index, "" if label is None else str(label),
                                              secs, y, color)

    def lines(self):
        return Axes(self._tl.figure, self.index).lines()

    @property
    def axes(self) -> "Axes":
        """Full panel API (markers, annotations, styles, y limits, ...)."""
        return Axes(self._tl.figure, self.index)

    name = property(lambda self: self._tl.figure._tl_row_name(self.index),
                    lambda self, v: self._tl.figure._tl_set_row_name(self.index, str(v)))
    visible = property(lambda self: self._tl.figure._tl_row_visible(self.index),
                       lambda self, v: self._tl.figure._tl_set_row_visible(self.index, bool(v)))
    height = property(lambda self: self._tl.figure._tl_row_height(self.index),
                      lambda self, v: self._tl.figure._tl_set_row_height(self.index, float(v)),
                      doc="Relative row height (default 1).")

    def __eq__(self, other):
        return isinstance(other, TimelineRow) and other._tl is self._tl and other.index == self.index

    def __hash__(self):
        return hash((id(self._tl), self.index))

    def __repr__(self):
        return f"<TimelineRow {self.index} {self.name!r}>"


class TimePin:
    """A time marker drawn through all rows of a Timeline (undoable)."""

    def __init__(self, timeline: "Timeline", pin_id: int):
        self._tl = timeline
        self.id = pin_id

    def _get(self):
        for pid, t, label, color in self._tl.figure._tl_pins():
            if pid == self.id:
                return t, label, color
        raise KeyError("this time marker no longer exists")

    t = property(lambda self: self._get()[0],
                 lambda self, v: self._tl.figure._tl_update_pin(self.id, float(v), self._get()[1]))
    label = property(lambda self: self._get()[1],
                     lambda self, v: self._tl.figure._tl_update_pin(self.id, self._get()[0], str(v)))
    color = property(lambda self: self._get()[2])

    def remove(self):
        self._tl.figure._tl_remove_pin(self.id)

    def jump(self):
        """Centre the visible window on this marker."""
        self._tl.figure._tl_jump_to_pin(self.id)

    def __repr__(self):
        try:
            t, label, _ = self._get()
            return f"<TimePin {label!r} at {self._tl.format_time(t)}>"
        except KeyError:
            return "<TimePin (removed)>"


class Timeline:
    """Stacked sensor rows sharing one time axis, with a navigator strip,
    a cursor line through all rows (with value readouts), time markers,
    row rearranging and auto-fit Y. Create with plotter.timeline().

    Times are seconds relative to ``time_origin`` (Unix epoch seconds) when
    the data has absolute timestamps; ``clock_time = True`` then labels the
    axis with clock times.

    Anything not defined here is forwarded to the underlying Figure
    (show, savefig, use_style, set_style, undo, on, ...)."""

    def __init__(self, figure: Figure):
        object.__setattr__(self, "figure", figure)
        figure._tl_enable()

    # ---- rows ---------------------------------------------------------
    def add_row(self, name: str = "") -> TimelineRow:
        return TimelineRow(self, self.figure._tl_add_row(str(name)))

    def add_signals(self, row_name: str, t, signals: dict, colors=None) -> TimelineRow:
        """Add a row with several signals: add_signals("IMU", t, {"ax": ax, "ay": ay})."""
        row = self.add_row(row_name)
        for i, (name, y) in enumerate(signals.items()):
            row.plot(t, y, label=name, color=None if colors is None else colors[i])
        return row

    @property
    def rows(self):
        """Rows in display order (top to bottom)."""
        return [TimelineRow(self, i) for i in self.figure._tl_rows()]

    def set_row_order(self, rows) -> None:
        self.figure._tl_set_order([r.index if isinstance(r, TimelineRow) else int(r) for r in rows])

    def move_signal(self, line: Line, row: TimelineRow) -> Line:
        """Move a signal to another row (one undo step); returns the new Line."""
        return self.figure._tl_move_signal(line, row.index)

    def remove_row(self, row: TimelineRow) -> bool:
        """Remove an empty row from the layout."""
        return self.figure._tl_remove_row(row.index)

    def load_csv(self, *files, row_per_column: bool = False) -> None:
        """Load CSV logs (time column + signal columns); one row per file by default."""
        import os
        self.figure._tl_load_csv([os.path.expanduser(str(f)) for f in files], bool(row_per_column))

    # ---- time window / cursor ------------------------------------------------
    window = property(lambda self: self.figure._tl_window(),
                      lambda self, w: self.figure._tl_set_window(float(w[0]), float(w[1])),
                      doc="Visible time window (t0, t1).")

    @property
    def full_range(self):
        return self.figure._tl_full_range()

    def show_all(self):
        self.figure._tl_show_all()

    cursor_time = property(lambda self: self.figure._tl_cursor(),
                           lambda self, t: self.figure._tl_set_cursor(t),
                           doc="Time under the cursor line (None when the mouse is outside).")

    def values_at(self, t: float) -> dict:
        """Interpolated value of every visible signal: {(row_name, signal): value}."""
        out = {}
        for row, name, value in self.figure._tl_values_at(float(t)):
            out[(self.figure._tl_row_name(row), name)] = value
        return out

    def format_time(self, t: float) -> str:
        return self.figure._tl_format_time(float(t))

    # ---- navigator / options ------------------------------------------------------
    @property
    def navigator(self) -> str:
        row, sid = self.figure._tl_nav()
        for r, i, name in self.figure._tl_signals():
            if (r, i) == (row, sid):
                return name
        return ""

    @navigator.setter
    def navigator(self, value):
        """A Line, or a name "row / signal" (or just the signal name)."""
        if isinstance(value, Line):
            self.figure._tl_set_nav(*self._line_ref(value))
            return
        for r, i, name in self.figure._tl_signals():
            if name == value or name.split(" / ", 1)[-1] == value:
                self.figure._tl_set_nav(r, i)
                return
        raise KeyError(f"no signal {value!r}")

    def _line_ref(self, line: Line):
        return line._panel, line._id

    auto_fit_y = property(lambda self: self.figure._tl_autofit(),
                          lambda self, v: self.figure._tl_set_autofit(bool(v)))
    clock_time = property(lambda self: self.figure._tl_clock(),
                          lambda self, v: self.figure._tl_set_clock(bool(v)))
    time_origin = property(lambda self: self.figure._tl_origin(),
                           lambda self, v: self.figure._tl_set_origin(float(v)),
                           doc="Unix epoch seconds of t = 0 (None if the data has no absolute time).")

    # ---- time markers ---------------------------------------------------------------
    def pin(self, t: float, label: str = "", color=None) -> TimePin:
        return TimePin(self, self.figure._tl_add_pin(float(t), str(label), color))

    def pins(self):
        return [TimePin(self, pid) for pid, *_ in self.figure._tl_pins()]

    def remove_pin(self, pin) -> None:
        self.figure._tl_remove_pin(pin.id if isinstance(pin, TimePin) else int(pin))

    def clear_pins(self) -> None:
        for pid, *_ in self.figure._tl_pins():
            self.figure._tl_remove_pin(pid)

    def next_pin(self):
        """Centres the window on the next time marker (keyboard: ]); returns it or None."""
        pid = self.figure._tl_step_pin(1)
        return TimePin(self, pid) if pid else None

    def prev_pin(self):
        """Centres the window on the previous time marker (keyboard: [); returns it or None."""
        pid = self.figure._tl_step_pin(-1)
        return TimePin(self, pid) if pid else None

    # ---- signals and data -------------------------------------------------------------
    def signals(self) -> dict:
        """All signals as {"row / signal": Line}, in display order."""
        out = {}
        for row in self.rows:
            for line in row.lines():
                out[f"{row.name} / {line.label}"] = line
        return out

    def signal(self, name: str) -> Line:
        """A signal by "row / signal" or by its own name."""
        sigs = self.signals()
        if name in sigs:
            return sigs[name]
        matches = [l for key, l in sigs.items() if key.split(" / ", 1)[-1] == name]
        if len(matches) != 1:
            raise KeyError(f"{name!r} matches {len(matches)} signals")
        return matches[0]

    def window_data(self, window=None) -> dict:
        """Samples inside the time window (default: the visible one):
        {"row / signal": (t, y)} as numpy arrays."""
        import numpy as np

        t0, t1 = window if window is not None else self.window
        out = {}
        for key, line in self.signals().items():
            x, y = line.x, line.y
            i0, i1 = np.searchsorted(x, t0, "left"), np.searchsorted(x, t1, "right")
            out[key] = (x[i0:i1], y[i0:i1])
        return out

    def export_csv(self, path: str, window=True, resample: float | None = None) -> None:
        """Write signals to CSV.

        window    True = visible window, False = whole recording, or (t0, t1)
        resample  None: long format (signal, time, value) - every sample as recorded.
                  dt (seconds): wide format on a common time grid, linearly
                  interpolated (NaN outside a signal's range or in gaps).
        Time is written as seconds and, with absolute data, as ISO time too."""
        import csv
        from datetime import datetime, timezone

        import numpy as np

        if window is True:
            win = self.window
        elif window is False:
            win = self.full_range
        else:
            win = tuple(window)
        data = self.window_data(win)
        origin = self.time_origin

        def iso(t):
            return datetime.fromtimestamp(origin + t, tz=timezone.utc).isoformat(timespec="milliseconds")

        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            if resample is None:
                w.writerow(["signal", "time_s"] + (["time_utc"] if origin is not None else []) + ["value"])
                for key, (t, y) in data.items():
                    for ti, yi in zip(t, y):
                        w.writerow([key, repr(float(ti))] + ([iso(ti)] if origin is not None else []) + [repr(float(yi))])
                return
            grid = np.arange(win[0], win[1] + 0.5 * resample, resample)
            cols = []
            for key, (t, y) in data.items():
                v = np.interp(grid, t, y, left=np.nan, right=np.nan) if len(t) else np.full(grid.size, np.nan)
                cols.append(v)
            w.writerow(["time_s"] + (["time_utc"] if origin is not None else []) + list(data))
            for i, ti in enumerate(grid):
                w.writerow([repr(float(ti))] + ([iso(ti)] if origin is not None else [])
                           + [repr(float(c[i])) for c in cols])

    # ---- forwarding ------------------------------------------------------------------
    def __getattr__(self, name):
        return getattr(self.figure, name)

    def __setattr__(self, name, value):
        # Timeline properties are set here; everything else (xlabel, title...) on the figure.
        if isinstance(getattr(type(self), name, None), property):
            object.__setattr__(self, name, value)
        else:
            setattr(self.figure, name, value)

    def __repr__(self):
        return f"<Timeline rows={[r.name for r in self.rows]}>"


def timeline(title: str | None = None, size: tuple[int, int] = (1200, 800)) -> Timeline:
    """Create a timeline window: stacked sensor rows sharing one time axis."""
    return Timeline(figure(title, size))


Figure.subplots = _subplots
Figure.ratio_panel = _ratio_panel
Figure.axes = property(_axes_list)
Figure.gca = _gca
Figure.sca = lambda self, ax: self._set_current(ax.index if isinstance(ax, Axes) else int(ax))
Figure.sca.__doc__ = "Make an Axes (or panel index) the current panel for Figure-level calls."
Figure.errorbar = _errorbar
Figure.hist = _hist
Figure.fill_between = _fill_between
Figure.set_style = style._figure_set_style

# IPython: "%gui plotter" keeps windows live in the terminal IPython shell.
try:  # pragma: no cover - only when IPython is installed
    from . import _ipython

    _ipython.register()
except Exception:  # noqa: BLE001
    pass
