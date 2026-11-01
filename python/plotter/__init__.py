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

import sys as _sys

from ._plotter import Annotation, Contour, Figure, Line, Marker, Peak
from ._plotter import process_events as _process_events
from ._plotter import _run_until_closed
from . import style

__all__ = [
    "style", "figure", "show", "pause", "figures", "close_all",
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
Figure.set_style = style._figure_set_style

# IPython: "%gui plotter" keeps windows live in the terminal IPython shell.
try:  # pragma: no cover - only when IPython is installed
    from . import _ipython

    _ipython.register()
except Exception:  # noqa: BLE001
    pass
