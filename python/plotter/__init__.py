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
    fig.show(block=False)

    region = fig.wait_for_region()         # let the user drag a region
    if region is not None:                 # None if the window was closed
        xmin, xmax = region
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


def _prepare_windows_dlls() -> None:
    """Let Windows find Qt's DLLs and plugins before _plotter is loaded.

    Since Python 3.8, extension modules do not see DLLs on PATH. The release
    wheel bundles Qt next to this file (with platforms/, styles/, ... plugin
    folders); a self-built module uses the Qt found on PATH instead.
    """
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    if os.path.isfile(os.path.join(here, "Qt6Core.dll")):
        os.add_dll_directory(here)
        if os.path.isdir(os.path.join(here, "platforms")):
            old = os.environ.get("QT_PLUGIN_PATH")
            os.environ["QT_PLUGIN_PATH"] = here + (os.pathsep + old if old else "")
        return
    for d in os.environ.get("PATH", "").split(os.pathsep):
        if d and os.path.isfile(os.path.join(d, "Qt6Core.dll")):
            os.add_dll_directory(d)
            break


if _sys.platform == "win32":
    _prepare_windows_dlls()

from ._plotter import Annotation, Contour, Figure, Line, Marker, Peak
from ._plotter import process_events as _process_events
from ._plotter import _run_until_closed
from . import _plotter
from . import style

__all__ = [
    "style", "figure", "Axes", "timeline", "Timeline", "TimelineRow", "TimePin", "show", "pause", "figures", "close_all",
    "theme", "themes", "theme_options", "tooltips", "load",
    "Figure", "Line", "Contour", "Marker", "Annotation", "Peak",
]

_figures: list[Figure] = []


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
    _forget_closed()
    fig = Figure(title, int(size[0]), int(size[1]))
    _figures.append(fig)
    return fig


def _forget_closed() -> None:
    # Figures the user (or code) closed: show() must not re-open them, and the
    # module must not keep them alive (their data is freed once no handle
    # refers to them). Figures not shown yet are kept.
    _figures[:] = [f for f in _figures if not f._closed]


def figures() -> list[Figure]:
    """All figures of this session that have not been closed."""
    _forget_closed()
    return list(_figures)


_headless_warned = False


def _no_blocking_without_screen() -> bool:
    """True (after a one-time warning) when no screen is available
    (QT_QPA_PLATFORM=offscreen): blocking until the user closes a window would
    never end, so show() returns at once - like matplotlib's Agg backend."""
    global _headless_warned
    if not _plotter._headless():
        return False
    if not _headless_warned:
        import warnings
        warnings.warn("plotter: no screen (offscreen platform); show() does not wait. "
                      "Use savefig() / save() to keep results.", stacklevel=3)
        _headless_warned = True
    return True


def show(block: bool | None = None, timeout: float | None = None) -> None:
    """Show all figures. If blocking, run the event loop until all are closed."""
    _forget_closed()  # never re-open windows the user already closed
    for f in list(_figures):
        f._show()
    if block is None:
        block = not _interactive()
    if block and timeout is None and _no_blocking_without_screen():
        _process_events(0)
        return
    if block:
        _run_until_closed(list(_figures), -1.0 if timeout is None else float(timeout))


def themes() -> dict:
    """Available display themes: {id: description}."""
    return {tid: f"{name} - {desc}" for tid, name, desc, _dark in _plotter._themes()}


def theme_options() -> dict:
    """{"plots": bool, "exports": bool}: whether dark themes colour the plots
    on screen and the exports (see theme())."""
    plots, exports = _plotter._theme_options()
    return {"plots": plots, "exports": exports}


def theme(name: str | None = None, *, plots: bool | None = None, exports: bool | None = None,
          save: bool = False) -> str:
    """Get or set the display theme of all windows; returns the current theme id.

    name     "system", "light", "dark", "dim", "midnight", "solarized-dark",
             "high-contrast" (see themes()).
    plots    dark themes also colour the plots on screen (default True).
    exports  exports use the theme colours too (default False: savefig and the
             clipboard keep the figure's own, usually light, style).
    save     remember the choice for future sessions (like the Appearance menu).
    The theme only changes how things look on screen; figure styles are untouched."""
    if name is not None:
        _plotter._set_theme(str(name), bool(save))
    if plots is not None or exports is not None:
        _plotter._set_theme_options(plots, exports, bool(save))
    return _plotter._theme()


def tooltips(on: bool | None = None, *, save: bool = False) -> bool:
    """Get or switch the explanations shown when the mouse rests on a button,
    field or menu entry (View > Appearance > Show explanations on hover)."""
    if on is not None:
        _plotter._set_tooltips(bool(on), bool(save))
    return _plotter._tooltips()


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
    on_gui_thread = _plotter._is_gui_thread()
    if block and timeout is None and on_gui_thread and _no_blocking_without_screen():
        block = False
    if block and not on_gui_thread:
        raise RuntimeError("show(block=True) must be called from the main thread; "
                           "from a worker thread use show(block=False)")
    if block:
        _run_until_closed([self], -1.0 if timeout is None else float(timeout))
    elif on_gui_thread:
        _process_events(0)  # paint the window right away
    return self


Figure.show = _figure_show


def _figure_repr_png(self) -> bytes:
    """PNG snapshot for Jupyter / IPython rich display (a figure shown as the
    last expression of a notebook cell appears inline)."""
    import os
    import tempfile

    fd, path = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    try:
        self.savefig(path, scale=1.0)
        with open(path, "rb") as f:
            return f.read()
    finally:
        os.remove(path)


Figure._repr_png_ = _figure_repr_png


def _accept_pathlike(name: str, keyword: str) -> None:
    """Let a Figure method take pathlib.Path (and other os.PathLike) paths,
    given positionally or by its keyword name."""
    import functools
    import os

    native = getattr(Figure, name)

    @functools.wraps(native)
    def method(self, *args, **kwargs):
        if args:
            args = (os.fspath(args[0]),) + args[1:]
        elif keyword in kwargs:
            kwargs[keyword] = os.fspath(kwargs[keyword])
        return native(self, *args, **kwargs)

    setattr(Figure, name, method)


for _name in ("open_bruker", "export_peaks", "import_peaks", "use_style", "save_style",
              "savefig", "screenshot", "save", "export_data"):
    # The keyword name of the path argument, read from the native signature.
    _doc = getattr(Figure, _name).__doc__ or ""
    _kw = _doc.split("(self: plotter._plotter.Figure, ", 1)[-1].split(":", 1)[0]
    _accept_pathlike(_name, _kw)
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
    # NaN / inf entries are ignored (as in matplotlib), together with their weights.
    finite = np.isfinite(data)
    if not finite.all():
        data = data[finite]
        if weights is not None:
            weights = np.asarray(weights, dtype=float).ravel()[finite]
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
    y1 = np.broadcast_to(np.asarray(y1, dtype=float), x.shape)  # scalars too, as documented
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

    def _call(self, fn):
        # Name this panel as the target of this thread's calls for the
        # duration of one call (thread-local in the native module, so threads
        # never wait for each other), so the call lands here even if the user
        # clicks another panel meanwhile. Using a panel also makes it current.
        previous = self.figure._target
        self.figure._target = self.index
        try:
            if self.figure._current() != self.index:
                self.figure._set_current(self.index)
            return fn()
        finally:
            self.figure._target = previous

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        # Properties (xlabel, x_log, ...) are read from this panel.
        if isinstance(getattr(type(self.figure), name, None), property):
            return self._call(lambda: getattr(self.figure, name))
        attr = getattr(self.figure, name)
        if not callable(attr):
            return attr

        def call(*args, **kwargs):
            return self._call(lambda: attr(*args, **kwargs))

        call.__name__ = name
        call.__doc__ = getattr(attr, "__doc__", None)
        return call

    def __setattr__(self, name, value):
        self._call(lambda: setattr(self.figure, name, value))

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
    self._subplots(int(nrows), int(ncols), sharex if isinstance(sharex, str) else bool(sharex),
                   sharey if isinstance(sharey, str) else bool(sharey),
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


def _twinx(self, ax=None) -> "Axes":
    """A second y axis on the right of a panel (matplotlib's twinx): a new
    panel drawn over it, sharing its x axis. Plot into the returned Axes."""
    fig = self.figure if isinstance(self, Axes) else self
    host = self.index if isinstance(self, Axes) else (ax.index if isinstance(ax, Axes) else fig._current())
    return Axes(fig, fig._twinx(host))


Axes.twinx = _twinx


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

    import datetime as _dt

    a = np.atleast_1d(np.asarray(t))
    if a.dtype.kind in "Mm":  # datetime64 -> Unix epoch seconds, timedelta64 -> seconds
        ns = a.astype("datetime64[ns]" if a.dtype.kind == "M" else "timedelta64[ns]")
        out = ns.astype(np.int64) / 1e9
        out[np.isnat(ns)] = np.nan  # NaT is a gap, not year -292
        return out, a.dtype.kind == "M"
    if a.dtype == object and a.size and not hasattr(a.flat[0], "timestamp") and hasattr(a.flat[0], "toordinal"):
        return _to_seconds(a.astype("datetime64[ns]"))  # datetime.date: midnight UTC
    if a.dtype == object and a.size and hasattr(a.flat[0], "timestamp"):
        def seconds(x):
            if x is None or x != x:  # None / NaT
                return np.nan
            # Naive datetimes are UTC, as for numpy and pandas (not local time).
            if isinstance(x, _dt.datetime) and x.tzinfo is None:
                x = x.replace(tzinfo=_dt.timezone.utc)
            return x.timestamp()
        return np.array([seconds(x) for x in a.ravel()], dtype=float), True
    return a.astype(float), False


class TimelineRow:
    """One row of a Timeline (a figure panel). Plot into it with ``row.plot``."""

    def __init__(self, timeline: "Timeline", index: int):
        self._tl = timeline
        self.index = index

    def plot(self, t, y, label: str = "", color=None) -> Line:
        """Add a signal. ``t``: seconds since the timeline origin, or absolute
        times (datetime64 / pandas timestamps), which set the origin on first use."""
        import numpy as np

        secs, absolute = _to_seconds(t)
        if absolute:
            if self._tl.time_origin is None:
                finite = secs[np.isfinite(secs)]
                if finite.size == 0:
                    raise ValueError("no valid time values")
                self._tl.time_origin = float(finite.min())
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


def _unique(d: dict, key):
    """`key`, or (for a duplicate) the key with " #2", " #3", ... appended.
    Rows or signals may share names; no entry may silently overwrite another."""
    if key not in d:
        return key
    n = 2
    while True:
        k = (key[0], f"{key[1]} #{n}") if isinstance(key, tuple) else f"{key} #{n}"
        if k not in d:
            return k
        n += 1


def _require_number(v, what: str) -> float:
    if v is None:
        raise ValueError(f"{what} must be a number (Unix epoch seconds)")
    return float(v)


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

    def __eq__(self, other):
        return isinstance(other, TimePin) and other.id == self.id and other._tl.figure is self._tl.figure

    def __hash__(self):
        return hash(self.id)

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

    def move_signal(self, line: Line, row) -> Line:
        """Move a signal to another row (a TimelineRow or its index; one undo
        step); returns the new Line."""
        return self.figure._tl_move_signal(line, row.index if isinstance(row, TimelineRow) else int(row))

    def remove_row(self, row) -> bool:
        """Remove an empty row from the layout. Rows that still hold signals
        are kept (returns False); move or remove their signals first."""
        return self.figure._tl_remove_row(row.index if isinstance(row, TimelineRow) else int(row))

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
            out[_unique(out, (self.figure._tl_row_name(row), name))] = value
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
                           lambda self, v: self.figure._tl_set_origin(_require_number(v, "time_origin")),
                           doc="Unix epoch seconds of t = 0 (None if the data has no absolute time).")

    # ---- time markers ---------------------------------------------------------------
    def pin(self, t: float, label: str = "", color=None) -> TimePin:
        return TimePin(self, self.figure._tl_add_pin(float(t), str(label), color))

    def pins(self):
        return [TimePin(self, pid) for pid, *_ in self.figure._tl_pins()]

    def remove_pin(self, pin) -> None:
        self.figure._tl_remove_pin(pin.id if isinstance(pin, TimePin) else int(pin))

    def clear_pins(self) -> None:
        """Delete every time marker (one undo step)."""
        pins = self.figure._tl_pins()
        if not pins:
            return
        self.figure._begin_macro("Delete time markers")
        try:
            for pid, *_ in pins:
                self.figure._tl_remove_pin(pid)
        finally:
            self.figure._end_macro()

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
                out[_unique(out, f"{row.name} / {line.label}")] = line
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

    def window_data(self, window=None, *, neighbours: bool = False) -> dict:
        """Samples inside the time window (default: the visible one):
        {"row / signal": (t, y)} as numpy arrays. ``neighbours=True`` adds
        the sample just before and just after the window (for interpolation)."""
        import numpy as np

        t0, t1 = window if window is not None else self.window
        out = {}
        for key, line in self.signals().items():
            x, y = line.x, line.y
            i0, i1 = np.searchsorted(x, t0, "left"), np.searchsorted(x, t1, "right")
            if neighbours:
                i0, i1 = max(i0 - 1, 0), min(i1 + 1, len(x))
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
        # Resampling interpolates up to the window edges: include one sample
        # on each side so the first / last grid points are not NaN.
        data = self.window_data(win, neighbours=resample is not None)
        origin = self.time_origin

        def iso(t):
            return datetime.fromtimestamp(origin + t, tz=timezone.utc).isoformat(timespec="milliseconds")

        import os

        with open(os.path.expanduser(os.fspath(path)), "w", newline="") as f:
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


def load(path) -> "Figure | Timeline":
    """Open a figure saved with fig.save() / File > Save figure (.gpfig).

    Returns a Timeline for timeline figures, otherwise a Figure. The window is
    not shown until you call show()."""
    import os

    fig = figure()
    try:
        fig._load(os.fspath(path))
    except Exception:
        fig.close()
        raise
    return Timeline(fig) if fig._has_timeline() else fig


def timeline(title: str | None = None, size: tuple[int, int] = (1200, 800)) -> Timeline:
    """Create a timeline window: stacked sensor rows sharing one time axis."""
    return Timeline(figure(title, size))


Figure.subplots = _subplots
Figure.ratio_panel = _ratio_panel
Figure.axes = property(_axes_list)
Figure.gca = _gca
Figure.twinx = _twinx
Figure.sca = lambda self, ax: self._set_current(ax.index if isinstance(ax, Axes) else int(ax))
Figure.sca.__doc__ = "Make an Axes (or panel index) the current panel for Figure-level calls."
Figure.errorbar = _errorbar
Figure.hist = _hist


def _image(self: Figure, z, x=None, y=None, *, cmap="viridis", colorbar=True, vmin=None, vmax=None,
           label=None, smooth=False) -> Contour:
    """Show 2D data z[row=y, column=x] as a colour-mapped image (no contours).

    x, y      cell-centre coordinates: full uniformly spaced arrays or (start, stop)
    cmap      'viridis' (default), 'magma', 'inferno', 'plasma', 'cividis', 'gray',
              'blues', 'rdbu', 'auto' ('_r' reverses)
    colorbar  colour bar beside the panel; vmin, vmax: fixed colour limits (either may be None)
    smooth    interpolate between cells (default False: each cell one colour)
    Returns a Contour handle (contours can be switched on later: c.contours = True)."""
    return self.contour(z, x, y, label=label, image=True, contours=False, cmap=cmap, colorbar=colorbar,
                        vmin=vmin, vmax=vmax, smooth=smooth)


Figure.image = _image
Figure.fill_between = _fill_between
Figure.set_style = style._figure_set_style

# IPython: "%gui plotter" keeps windows live in the terminal IPython shell.
try:  # pragma: no cover - only when IPython is installed
    from . import _ipython

    _ipython.register()
except Exception:  # noqa: BLE001
    pass
