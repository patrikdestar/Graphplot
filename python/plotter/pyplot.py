"""matplotlib.pyplot-like interface on top of plotter.

Covers the commonly used part of pyplot so that many existing scripts work
after changing one import::

    import plotter.pyplot as plt          # instead of: import matplotlib.pyplot as plt

    plt.style.use("science")
    fig, (ax, rax) = plt.subplots(2, 1, sharex=True,
                                  gridspec_kw={"height_ratios": [3, 1], "hspace": 0})
    ax.hist(mc, bins=40, histtype="stepfilled", label="MC")
    ax.errorbar(x, y, yerr=dy, fmt="ko", label="Data")
    ax.set_ylabel("Events"); ax.legend(); ax.set_yscale("log")
    rax.plot(x, y / mc_y, "k.")
    plt.savefig("plot.pdf"); plt.show()

Supported: figure, subplots, gcf, gca, sca, close, show, pause, savefig,
plot (with format strings like "r--o"), scatter, errorbar, hist, bar-like
step histograms, fill_between, contour/contourf (geometric contour levels),
axhline, axvline, axhspan-free markers, text, annotate, xlabel, ylabel,
title, xlim, ylim, xscale, yscale, legend, grid, tight_layout (no-op),
and the matching Axes methods (set_xlabel, set_xlim, set_yscale, ...).
Not supported: images (imshow), polar/3D axes, twin axes, colorbars.
Differences: contour levels are geometric (base * factor**k) as in NMR
software; figure sizes are converted at 100 pixels per inch.
"""
from __future__ import annotations

import numpy as _np

import plotter as _plotter
from plotter import style  # noqa: F401  (plt.style.use works as in matplotlib)

_DPI = 100  # pixels per inch for figsize conversions
_current: "_PltFigure | None" = None

_FMT_COLORS = set("bgrcmykw")
_FMT_MARKERS = {".": "o", "o": "o", "s": "s", "^": "^", "v": "^", "D": "d", "d": "d",
                "x": "x", "+": "+", "*": "d", "p": "s", "h": "d"}
_FMT_LINES = ["--", "-.", "-", ":"]


def _parse_fmt(fmt: str):
    """'r--o' -> (color, linestyle, marker); missing parts are None."""
    color = linestyle = marker = None
    s = fmt
    for ls in _FMT_LINES:
        if ls in s:
            linestyle = ls
            s = s.replace(ls, "", 1)
            break
    for ch in s:
        if ch in _FMT_COLORS and color is None:
            color = ch
        elif ch in _FMT_MARKERS and marker is None:
            marker = _FMT_MARKERS[ch]
        else:
            raise ValueError(f"unsupported format string {fmt!r}")
    if marker and linestyle is None:
        linestyle = "none"
    return color, linestyle, marker


def _common(kw: dict):
    """Translate matplotlib keyword aliases to plotter keywords."""
    out = {}
    if "c" in kw:
        kw.setdefault("color", kw.pop("c"))
    for mpl, ours in (("linewidth", "width"), ("lw", "width"), ("linestyle", "style"),
                      ("ls", "style"), ("markersize", "markersize"), ("ms", "markersize"),
                      ("label", "label"), ("color", "color"), ("marker", "marker")):
        if mpl in kw:
            out[ours] = kw.pop(mpl)
    if "alpha" in kw and out.get("color") is not None:
        r, g, b, _ = _plotter._plotter._rgba(out["color"])
        out["color"] = (r, g, b, float(kw.pop("alpha")))
    kw.pop("alpha", None)
    kw.pop("zorder", None)
    if out.get("style") in ("", " ", "None"):
        out["style"] = "none"
    if out.get("marker") in ("", " ", "None"):
        out["marker"] = None
    if isinstance(out.get("marker"), str) and out["marker"] in _FMT_MARKERS:
        out["marker"] = _FMT_MARKERS[out["marker"]]
    if kw:
        raise TypeError(f"unsupported keyword(s): {', '.join(sorted(kw))}")
    return out


class PltAxes:
    """matplotlib-style Axes wrapping a plotter Axes/panel."""

    def __init__(self, fig: "_PltFigure", axes: "_plotter.Axes"):
        self.figure = fig
        self._ax = axes

    # ---- plotting ----------------------------------------------------------
    def plot(self, *args, **kw):
        """plot(y), plot(x, y), plot(x, y, fmt), plot(x1, y1, fmt1, x2, y2, ...)"""
        lines = []
        args = list(args)
        while args:
            if len(args) == 1 or isinstance(args[1], str):
                y = args.pop(0)
                x = _np.arange(len(_np.asarray(y)))
            else:
                x, y = args.pop(0), args.pop(0)
            fmt = args.pop(0) if args and isinstance(args[0], str) else ""
            opts = dict(kw)
            color, ls, marker = _parse_fmt(fmt) if fmt else (None, None, None)
            if color and "color" not in opts and "c" not in opts:
                opts["color"] = color
            if ls and "linestyle" not in opts and "ls" not in opts:
                opts["linestyle"] = ls
            if marker and "marker" not in opts:
                opts["marker"] = marker
            lines.append(self._ax.plot(x, y, **_common(opts)))
        return lines

    def scatter(self, x, y, s=None, c=None, marker="o", **kw):
        if c is not None:
            kw["color"] = c
        size = None if s is None else float(_np.sqrt(_np.mean(s)))
        opts = _common(kw)
        return self._ax.plot(x, y, style="none", marker=_FMT_MARKERS.get(marker, "o"),
                             markersize=size, **{k: v for k, v in opts.items()
                                                 if k not in ("style", "marker")})

    def errorbar(self, x, y, yerr=None, xerr=None, fmt="", **kw):
        kw.pop("capsize", None)  # cap size comes from the style ('errorbar.capsize')
        kw.pop("elinewidth", None)
        kw.pop("capthick", None)
        color, ls, marker = _parse_fmt(fmt) if fmt else (None, "none", "o")
        if color and "color" not in kw and "c" not in kw:
            kw["color"] = color
        opts = _common(kw)
        opts.setdefault("style", ls or "none")
        opts.setdefault("marker", marker)
        return self._ax.plot(x, y, xerr=xerr, yerr=yerr, **opts)

    def hist(self, x, bins=10, range=None, density=False, weights=None,  # noqa: A002
             histtype="step", **kw):
        kw.pop("edgecolor", None)
        fill = kw.pop("facecolor", None)
        opts = _common(kw)
        if histtype == "bar":
            histtype = "stepfilled"
        counts, edges, line = self._ax.hist(x, bins, range, density=density, weights=weights,
                                            histtype=histtype, fill=fill,
                                            **{k: v for k, v in opts.items() if k in ("label", "color", "width", "style")})
        return counts, edges, line

    def stairs(self, values, edges=None, fill=False, **kw):
        values = _np.asarray(values, dtype=float)
        edges = _np.arange(len(values) + 1) if edges is None else _np.asarray(edges, dtype=float)
        xs = _np.concatenate([[edges[0]], edges])
        ys = _np.concatenate([[0.0], values, [0.0]])
        return self._ax.plot(xs, ys, step="post", fill=True if fill else None, **_common(kw))

    def fill_between(self, x, y1, y2=0, alpha=0.3, color=None, label=None, step=None, **kw):
        kw.pop("linewidth", None)
        kw.pop("edgecolor", None)
        if "facecolor" in kw:
            color = kw.pop("facecolor")
        if kw:
            raise TypeError(f"unsupported keyword(s): {', '.join(sorted(kw))}")
        return self._ax.fill_between(x, y1, y2, label=label, color=color, alpha=alpha, step=step)

    def contour(self, *args, levels=None, colors=None, **kw):
        """contour(Z) or contour(X, Y, Z); levels: number of (geometric) levels."""
        kw.pop("linewidths", None)
        label = kw.pop("label", None)
        if kw:
            raise TypeError(f"unsupported keyword(s): {', '.join(sorted(kw))}")
        if len(args) == 1:
            z, x, y = args[0], None, None
        elif len(args) == 3:
            x, y, z = (_np.asarray(a) for a in args)
            x = x[0] if x.ndim == 2 else x
            y = y[:, 0] if y.ndim == 2 else y
        else:
            raise TypeError("contour(Z) or contour(X, Y, Z)")
        n = levels if isinstance(levels, int) else None
        return self._ax.contour(z, x, y, levels=n, color=colors, label=label)

    def contourf(self, *args, **kw):
        c = self.contour(*args, **kw)
        c.image = True
        return c

    def axvline(self, x=0, **kw):
        opts = _common(kw)
        return self._ax.marker(x, "v", show_value=False, **{k: v for k, v in opts.items()
                                                            if k in ("color", "label", "style", "width")})

    def axhline(self, y=0, **kw):
        opts = _common(kw)
        return self._ax.marker(y, "h", show_value=False, **{k: v for k, v in opts.items()
                                                            if k in ("color", "label", "style", "width")})

    def text(self, x, y, s, fontsize=None, color=None, **kw):
        kw.pop("transform", None)
        kw.pop("ha", None)
        kw.pop("va", None)
        return self._ax.annotate(str(s), x, y, fontsize=fontsize, color=color)

    def annotate(self, text, xy, xytext=None, **kw):
        x, y = xytext if xytext is not None else xy
        return self.text(x, y, text, **{k: v for k, v in kw.items() if k in ("fontsize", "color")})

    # ---- decorations --------------------------------------------------------------
    def set_xlabel(self, s, **_):
        self._ax.xlabel = str(s)

    def set_ylabel(self, s, **_):
        self._ax.ylabel = str(s)

    def set_title(self, s, **_):
        self._ax.title = str(s)

    def get_xlabel(self):
        return self._ax.xlabel

    def get_ylabel(self):
        return self._ax.ylabel

    def set_xlim(self, left=None, right=None):
        if isinstance(left, (tuple, list)):
            left, right = left
        lo, hi = self._ax.xlim()
        self._ax.set_xlim(lo if left is None else left, hi if right is None else right)

    def set_ylim(self, bottom=None, top=None):
        if isinstance(bottom, (tuple, list)):
            bottom, top = bottom
        lo, hi = self._ax.ylim()
        self._ax.set_ylim(lo if bottom is None else bottom, hi if top is None else top)

    def get_xlim(self):
        return self._ax.xlim()

    def get_ylim(self):
        return self._ax.ylim()

    def set_xscale(self, scale):
        self._ax.x_log = scale == "log"

    def set_yscale(self, scale):
        self._ax.y_log = scale == "log"
        if scale == "log":
            self._ax.autoscale(x=False, y=True)

    def invert_xaxis(self):
        self._ax.x_reversed = not self._ax.x_reversed

    def invert_yaxis(self):
        self._ax.y_reversed = not self._ax.y_reversed

    def legend(self, *_, loc=None, frameon=None, fontsize=None, **__):
        self._ax.legend(True)
        params = {}
        if isinstance(loc, str) and loc != "best":
            params["legend.loc"] = loc
        if frameon is not None:
            params["legend.frameon"] = bool(frameon)
        if fontsize is not None and not isinstance(fontsize, str):
            params["legend.fontsize"] = fontsize
        if params:
            self.figure._fig.set_style(params)

    def grid(self, visible=True, **_):
        self._ax.grid(bool(visible))

    def autoscale(self, enable=True, axis="both", **_):
        if enable:
            self._ax.autoscale(x=axis in ("both", "x"), y=axis in ("both", "y"))

    def relim(self):
        pass

    def tick_params(self, **_):
        pass  # tick appearance comes from the style ('xtick.*', 'ytick.*')

    @property
    def panel(self):
        """The underlying plotter Axes (full plotter API: peaks, regions, ...)."""
        return self._ax


class _PltFigure:
    def __init__(self, figsize=None, num=None):
        w, h = figsize if figsize is not None else (8.0, 6.0)
        self._fig = _plotter.figure(None if num is None else str(num),
                                    size=(int(w * _DPI), int(h * _DPI)))
        self._size = (int(w * _DPI), int(h * _DPI))
        self.axes = [PltAxes(self, self._fig.axes[0])]

    def gca(self):
        return next(a for a in self.axes if a.panel == self._fig.gca())

    def savefig(self, fname, dpi=None, **_):
        scale = (dpi or _DPI * 2) / _DPI
        self._fig.savefig(str(fname), width=self._size[0], height=self._size[1], scale=scale)

    def suptitle(self, s, **_):
        self._fig.axes[0].title = str(s)

    def tight_layout(self, *_, **__):
        pass  # layout is automatic

    def show(self):
        self._fig.show()

    @property
    def native(self):
        """The plotter.Figure (window, undo, peaks, styles, ...)."""
        return self._fig


# ---- module-level (stateful) API ------------------------------------------------------

def figure(num=None, figsize=None, **_):
    global _current
    _current = _PltFigure(figsize=figsize, num=num)
    return _current


def gcf():
    return _current if _current is not None else figure()


def gca():
    return gcf().gca()


def sca(ax: PltAxes):
    global _current
    _current = ax.figure
    ax.figure._fig.sca(ax.panel)


def subplots(nrows=1, ncols=1, sharex=False, sharey=False, figsize=None, squeeze=True,
             gridspec_kw=None, height_ratios=None, width_ratios=None, **_):
    fig = figure(figsize=figsize)
    gk = dict(gridspec_kw or {})
    hr = height_ratios or gk.get("height_ratios")
    wr = width_ratios or gk.get("width_ratios")
    hspace = gk.get("hspace")
    wspace = gk.get("wspace")
    # matplotlib's hspace is a fraction of the axes height; 0 means touching.
    px = lambda v: None if v is None else int(round(v * 100))  # noqa: E731
    grid = fig._fig.subplots(nrows, ncols, sharex=bool(sharex), sharey=bool(sharey),
                             height_ratios=hr, width_ratios=wr, hspace=px(hspace),
                             wspace=px(wspace), squeeze=False)
    axes = [[PltAxes(fig, a) for a in row] for row in grid]
    fig.axes = [a for row in axes for a in row]
    fig._fig.sca(grid[0][0])
    if squeeze:
        if nrows == 1 and ncols == 1:
            return fig, axes[0][0]
        if nrows == 1 or ncols == 1:
            return fig, [a for row in axes for a in row]
    return fig, axes


def _ax_call(name):
    def f(*args, **kw):
        return getattr(gca(), name)(*args, **kw)
    f.__name__ = name
    f.__doc__ = getattr(PltAxes, name).__doc__
    return f


plot = _ax_call("plot")
scatter = _ax_call("scatter")
errorbar = _ax_call("errorbar")
hist = _ax_call("hist")
stairs = _ax_call("stairs")
fill_between = _ax_call("fill_between")
contour = _ax_call("contour")
contourf = _ax_call("contourf")
axvline = _ax_call("axvline")
axhline = _ax_call("axhline")
text = _ax_call("text")
annotate = _ax_call("annotate")
legend = _ax_call("legend")
grid = _ax_call("grid")
autoscale = _ax_call("autoscale")


def xlabel(s, **kw):
    gca().set_xlabel(s, **kw)


def ylabel(s, **kw):
    gca().set_ylabel(s, **kw)


def title(s, **kw):
    gca().set_title(s, **kw)


def xlim(*args):
    if args:
        gca().set_xlim(*args)
    return gca().get_xlim()


def ylim(*args):
    if args:
        gca().set_ylim(*args)
    return gca().get_ylim()


def xscale(s):
    gca().set_xscale(s)


def yscale(s):
    gca().set_yscale(s)


def tight_layout(*_, **__):
    pass


def savefig(fname, dpi=None, **kw):
    gcf().savefig(fname, dpi=dpi, **kw)


def show(block=None):
    _plotter.show(block=block)


def pause(interval):
    _plotter.pause(interval)


def close(fig=None):
    global _current
    if fig is None or fig == "all":
        if fig == "all":
            _plotter.close_all()
        elif _current is not None:
            _current._fig.close()
        _current = None
    else:
        fig._fig.close()
        if fig is _current:
            _current = None
