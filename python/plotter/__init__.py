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

from ._plotter import Annotation, Contour, Figure, Line, Marker, Peak
from ._plotter import process_events as _process_events
from ._plotter import _run_until_closed
from . import style

__all__ = [
    "style", "figure", "Axes", "show", "pause", "figures", "close_all",
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
    fig = Figure(title, int(size[0]), int(size[1]))
    _figures.append(fig)
    return fig


def figures() -> list[Figure]:
    """All figures created in this session that are still open."""
    return [f for f in _figures if f.is_open]


def show(block: bool | None = None, timeout: float | None = None) -> None:
    """Show all figures. If blocking, run the event loop until all are closed."""
    for f in _figures:
        try:
            f._show()
        except RuntimeError:
            pass
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

    _PROPERTIES = {"title", "xlabel", "ylabel", "x_reversed", "y_reversed", "diagonal",
                   "peak_labels"}

    def __init__(self, figure: Figure, index: int):
        object.__setattr__(self, "figure", figure)
        object.__setattr__(self, "index", index)

    def _activate(self):
        if self.figure._current() != self.index:
            self.figure._set_current(self.index)

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        if name in Axes._PROPERTIES:
            self._activate()
            return getattr(self.figure, name)
        attr = getattr(self.figure, name)
        if not callable(attr):
            return attr

        def call(*args, **kwargs):
            self._activate()
            return attr(*args, **kwargs)

        call.__name__ = name
        call.__doc__ = getattr(attr, "__doc__", None)
        return call

    def __setattr__(self, name, value):
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
