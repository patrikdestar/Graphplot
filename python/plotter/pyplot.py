# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
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

Supported: figure, subplots, fig.add_subplot, gcf, gca, sca, close, show,
pause, savefig, plot (with format strings like "r--o" or "C1--"), scatter,
errorbar, hist (one or several datasets), stairs, fill_between,
contour/contourf (geometric contour levels), axhline, axvline (full-height
markers), text, annotate, xlabel, ylabel, title, xlim, ylim, xscale, yscale,
legend, grid, tight_layout (no-op), and the matching Axes methods
(set_xlabel, set_xlim, set_yscale, ax.set(...), ...).
Images: imshow, pcolormesh and colorbar (2-D arrays, uniform grids).
Not supported: RGB images, polar/3D axes, twin axes.
Differences: legend position / font are figure-wide (last call wins);
contour levels are geometric (base * factor**k) as in NMR
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
    if fmt.lower() == "none":  # errorbar(fmt="none"): bars only
        return None, "none", None
    s = fmt
    import re
    m = re.search(r"C\d", s)  # cycle colours "C0".."C9"
    if m:
        color = m.group(0)
        s = s.replace(color, "", 1)
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


_TAB_COLORS = {  # matplotlib's "tab:" (Tableau 10) colours
    "blue": "#1f77b4", "orange": "#ff7f0e", "green": "#2ca02c", "red": "#d62728",
    "purple": "#9467bd", "brown": "#8c564b", "pink": "#e377c2", "gray": "#7f7f7f",
    "grey": "#7f7f7f", "olive": "#bcbd22", "cyan": "#17becf"}


def _mpl_color(c):
    """matplotlib colour forms that plotter does not parse itself."""
    if isinstance(c, str) and c.startswith("tab:") and c[4:] in _TAB_COLORS:
        return _TAB_COLORS[c[4:]]
    if isinstance(c, str):
        try:
            v = float(c)  # grayscale "0.5"
        except ValueError:
            return c
        if 0.0 <= v <= 1.0:
            return (v, v, v)
    return c


_DRAWSTYLES = {"default": None, "steps": "pre", "steps-pre": "pre", "steps-post": "post",
               "steps-mid": "mid"}


def _common(kw: dict, ax=None):
    """Translate matplotlib keyword aliases to plotter keywords.

    ``ax`` (a plotter Axes) is needed to give ``alpha`` a colour when none was
    specified (the next colour of the cycle)."""
    out = {}
    if "c" in kw:
        kw.setdefault("color", kw.pop("c"))
    if "drawstyle" in kw or "ds" in kw:
        ds = kw.pop("drawstyle", None) or kw.pop("ds", None)
        if ds not in _DRAWSTYLES:
            raise ValueError(f"unsupported drawstyle {ds!r}")
        if _DRAWSTYLES[ds]:
            out["step"] = _DRAWSTYLES[ds]
    for mpl, ours in (("linewidth", "width"), ("lw", "width"), ("linestyle", "style"),
                      ("ls", "style"), ("markersize", "markersize"), ("ms", "markersize"),
                      ("label", "label"), ("color", "color"), ("marker", "marker")):
        if mpl in kw:
            out[ours] = kw.pop(mpl)
    if out.get("color") is not None:
        out["color"] = _mpl_color(out["color"])
    if "label" in out and out["label"] is not None:
        out["label"] = str(out["label"])
        if out["label"].startswith("_"):  # "_nolegend_", "_child0": hidden from the legend
            out["label"] = None
    if kw.get("alpha") is not None:
        if out.get("color") is None and ax is not None:
            out["color"] = ax._take_color()
        if out.get("color") is not None:
            r, g, b, _ = _plotter._plotter._rgba(out["color"])
            out["color"] = (r, g, b, float(kw["alpha"]))
    kw.pop("alpha", None)
    # Purely cosmetic matplotlib keywords without an equivalent: accepted, ignored.
    for k in ("zorder", "ecolor", "mfc", "markerfacecolor", "mec", "markeredgecolor", "mew",
              "markeredgewidth", "markevery", "antialiased", "rasterized", "solid_capstyle",
              "dash_capstyle", "picker", "gid", "clip_on"):
        kw.pop(k, None)
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
        self._fixed_limits = set()  # axes whose limits the script set explicitly
        # As in matplotlib: a legend only where legend() is called.
        if not axes.lines() and not axes.contours():
            axes.legend(False)

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
            y = _np.asarray(y)
            # A 2-D y plots one line per column (as in matplotlib).
            for col in (y.T if y.ndim == 2 else [y]):
                lines.append(self._ax.plot(x, col, **_common(dict(opts), self._ax)))
        return lines

    def scatter(self, x, y, s=None, c=None, marker="o", **kw):
        if c is not None:
            arr = _np.asarray(c)
            if arr.dtype.kind in "fiu" and arr.ndim == 1 and arr.size not in (3, 4):
                # One value per point (colour-mapped in matplotlib) is not supported.
                import warnings
                warnings.warn("plotter.pyplot.scatter: per-point colours are not supported; "
                              "using a single colour", stacklevel=2)
            else:
                kw["color"] = c
        size = None if s is None else float(_np.sqrt(_np.mean(s)))
        opts = _common(kw, self._ax)
        return self._ax.plot(x, y, style="none", marker=_FMT_MARKERS.get(marker, "o"),
                             markersize=size, **{k: v for k, v in opts.items()
                                                 if k not in ("style", "marker")})

    def errorbar(self, x, y, yerr=None, xerr=None, fmt="", **kw):
        kw.pop("capsize", None)  # cap size comes from the style ('errorbar.capsize')
        kw.pop("elinewidth", None)
        kw.pop("capthick", None)
        # Like matplotlib: without fmt the points are joined by a line.
        color, ls, marker = _parse_fmt(fmt) if fmt else (None, "-", None)
        if color and "color" not in kw and "c" not in kw:
            kw["color"] = color
        opts = _common(kw, self._ax)
        opts.setdefault("style", ls or "none")
        opts.setdefault("marker", marker)
        return self._ax.plot(x, y, xerr=xerr, yerr=yerr, **opts)

    def hist(self, x, bins=10, range=None, density=False, weights=None,  # noqa: A002
             histtype="step", **kw):
        # Several datasets (a list of arrays or a 2-D array, one per column):
        # one histogram each, with shared bins, like matplotlib.
        many = (isinstance(x, (list, tuple)) and len(x) > 0 and _np.ndim(x[0]) > 0) or _np.ndim(x) == 2
        if many:
            data = list(_np.asarray(x).T) if _np.ndim(x) == 2 and not isinstance(x, (list, tuple)) else list(x)
            if _np.ndim(bins) == 0:
                finite = _np.concatenate([_np.asarray(d, dtype=float).ravel() for d in data])
                finite = finite[_np.isfinite(finite)]
                bins = _np.histogram_bin_edges(finite, bins=int(bins), range=range)
            labels = kw.pop("label", None)
            colors = kw.pop("color", None)
            results = []
            for i, d in enumerate(data):
                k = dict(kw)
                if isinstance(labels, (list, tuple)):
                    k["label"] = labels[i] if i < len(labels) else None
                elif labels is not None and i == 0:
                    k["label"] = labels
                if isinstance(colors, (list, tuple)) and not isinstance(colors[0], (int, float)):
                    k["color"] = colors[i % len(colors)]
                elif colors is not None:
                    k["color"] = colors
                w = None if weights is None else weights[i]
                results.append(self.hist(d, bins, range, density=density, weights=w, histtype=histtype, **k))
            return (_np.array([r[0] for r in results]), results[0][1], [r[2] for r in results])
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

    def fill_between(self, x, y1, y2=0, where=None, alpha=0.3, color=None, label=None, step=None,
                     **kw):
        kw.pop("linewidth", None)
        kw.pop("edgecolor", None)
        kw.pop("interpolate", None)
        if "facecolor" in kw:
            color = kw.pop("facecolor")
        if kw:
            raise TypeError(f"unsupported keyword(s): {', '.join(sorted(kw))}")
        y1 = _np.asarray(y1, dtype=float) + 0 * _np.asarray(x, dtype=float)
        y2 = _np.broadcast_to(_np.asarray(y2, dtype=float), y1.shape)
        if where is not None:
            # Outside `where` the band collapses to zero height.
            y1 = _np.where(_np.asarray(where, dtype=bool), y1, y2)
        return self._ax.fill_between(x, y1, y2, label=None if label is None else str(label),
                                     color=_mpl_color(color), alpha=alpha, step=step)

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
        import numbers

        n = base = factor = None
        if isinstance(levels, numbers.Integral):
            n = int(levels)
        elif levels is not None:
            # Explicit levels: geometric levels are base * factor**k, so use
            # the positive ones to choose base, factor and count.
            lv = _np.sort(_np.abs(_np.asarray(levels, dtype=float)))
            lv = _np.unique(lv[lv > 0])
            if lv.size:
                base, n = float(lv[0]), int(lv.size)
                factor = float((lv[-1] / lv[0]) ** (1 / (lv.size - 1))) if lv.size > 1 else 2.0
        return self._ax.contour(z, x, y, levels=n, factor=factor, base=base, color=colors,
                                label=label)

    def contourf(self, *args, **kw):
        cmap = kw.pop("cmap", None)
        c = self.contour(*args, **kw)
        c.image = True
        if cmap is not None:
            c.cmap = cmap
        self._last_image = c
        return c

    # ---- images ------------------------------------------------------------------------
    @staticmethod
    def _centres(edges_or_centres, n):
        """Cell-centre (start, stop) from n+1 edges or n centres (1-D or 2-D grids)."""
        a = _np.asarray(edges_or_centres, dtype=float)
        if a.size == n + 1:
            a = 0.5 * (a[:-1] + a[1:])
        return float(a[0]), float(a[-1])

    def imshow(self, X, cmap=None, vmin=None, vmax=None, origin=None, extent=None, aspect=None,
               interpolation=None, label=None, **kw):
        """Colour-mapped image of a 2-D array (matplotlib conventions: row 0 at the
        top unless origin='lower'; extent = (left, right, bottom, top) pixel edges)."""
        z = _np.asarray(X, dtype=float)
        if z.ndim != 2:
            raise ValueError("imshow: only 2-D arrays (RGB images are not supported)")
        ny, nx = z.shape
        upper = (origin or "upper") == "upper"
        if extent is None:
            x, y = (0.0, nx - 1.0), (0.0, ny - 1.0)
            if upper:
                self._ax.y_reversed = True  # row 0 at the top, y grows downwards
        else:
            left, right, bottom, top = (float(v) for v in extent)
            dx, dy = (right - left) / nx, (top - bottom) / ny
            x = (left + dx / 2, right - dx / 2)
            # Row 0 is at the top for origin='upper', at the bottom for 'lower'.
            y = (top - dy / 2, bottom + dy / 2) if upper else (bottom + dy / 2, top - dy / 2)
        c = self._ax.image(z, x, y, cmap=cmap or "viridis", colorbar=False, vmin=vmin, vmax=vmax,
                           label=label)
        self._last_image = c
        return c

    def pcolormesh(self, *args, cmap=None, vmin=None, vmax=None, shading=None, label=None, **kw):
        """pcolormesh(C) or pcolormesh(X, Y, C) on a uniform grid (X, Y: edges or centres)."""
        if len(args) == 1:
            c_arr, x, y = _np.asarray(args[0], dtype=float), None, None
        elif len(args) == 3:
            X, Y, c_arr = (_np.asarray(a, dtype=float) for a in args)
            ny, nx = c_arr.shape
            X = X[0] if X.ndim == 2 else X
            Y = Y[:, 0] if Y.ndim == 2 else Y
            x, y = self._centres(X, nx), self._centres(Y, ny)
        else:
            raise TypeError("pcolormesh(C) or pcolormesh(X, Y, C)")
        c = self._ax.image(c_arr, x, y, cmap=cmap or "viridis", colorbar=False, vmin=vmin, vmax=vmax,
                           label=label)
        self._last_image = c
        return c

    def colorbar(self, mappable=None, label=None, **kw):
        """Show the colour bar of an image (default: the last one drawn in this panel)."""
        m = mappable if mappable is not None else getattr(self, "_last_image", None)
        if m is None:
            raise RuntimeError("colorbar: no image (imshow / pcolormesh / contourf) to show it for")
        m.colorbar = True
        if label is not None:
            m.label = str(label)
        return m

    def axvline(self, x=0, ymin=0, ymax=1, **kw):
        # ymin / ymax (partial lines) are accepted; markers always span the panel.
        opts = _common(kw)
        return self._ax.marker(x, "v", show_value=False, **{k: v for k, v in opts.items()
                                                            if k in ("color", "label", "style", "width")})

    def axhline(self, y=0, xmin=0, xmax=1, **kw):
        opts = _common(kw)
        return self._ax.marker(y, "h", show_value=False, **{k: v for k, v in opts.items()
                                                            if k in ("color", "label", "style", "width")})

    @property
    def transAxes(self):  # noqa: N802 - matplotlib name
        """Marker for axes-fraction coordinates in text(..., transform=ax.transAxes)."""
        return ("axes", self)

    def text(self, x, y, s, fontsize=None, color=None, ha=None, va=None, horizontalalignment=None,
             verticalalignment=None, **kw):
        transform = kw.pop("transform", None)
        if isinstance(transform, tuple) and transform[:1] == ("axes",):
            # Axes fraction -> data coordinates of the current view.
            (x0, x1), (y0, y1) = self.get_xlim(), self.get_ylim()
            x, y = x0 + x * (x1 - x0), y0 + y * (y1 - y0)
        ha = ha or horizontalalignment or "left"
        va = va or verticalalignment or "baseline"
        return self._ax.annotate(str(s), x, y, fontsize=fontsize, color=_mpl_color(color), ha=ha, va=va)

    def annotate(self, text, xy, xytext=None, arrowprops=None, ha=None, va=None, **kw):
        """Text at xytext (or xy); with arrowprops an arrow from the text to xy."""
        x, y = xytext if xytext is not None else xy
        fontsize = kw.get("fontsize", kw.get("size"))
        color = _mpl_color(kw.get("color"))
        arrow = tuple(float(v) for v in xy) if arrowprops is not None and xytext is not None else None
        return self._ax.annotate(str(text), x, y, fontsize=fontsize, color=color,
                                 ha=ha or kw.get("horizontalalignment") or "left",
                                 va=va or kw.get("verticalalignment") or "baseline", arrow_to=arrow)

    def bar(self, x, height, width=0.8, bottom=0.0, align="center", color=None, label=None,
            edgecolor=None, **kw):
        """Vertical bars (drawn as one filled outline)."""
        x = _np.atleast_1d(_np.asarray(x, dtype=float))
        h = _np.broadcast_to(_np.asarray(height, dtype=float), x.shape)
        w = _np.broadcast_to(_np.asarray(width, dtype=float), x.shape)
        b = _np.broadcast_to(_np.asarray(bottom, dtype=float), x.shape)
        left = x - w / 2 if align == "center" else x
        # Each bar is a closed rectangle; NaN separates them (filled one by one).
        xs, ys = [], []
        for l0, wi, hi, bi in zip(left, w, h, b):
            xs += [l0, l0, l0 + wi, l0 + wi, l0, _np.nan]
            ys += [bi, bi + hi, bi + hi, bi, bi, _np.nan]
        opts = _common(dict(kw, **({"label": label} if label is not None else {}),
                            **({"color": color} if color is not None else {})), self._ax)
        return self._ax.plot(xs, ys, fill=True, **opts)

    def barh(self, y, width, height=0.8, left=0.0, color=None, label=None, **kw):
        """Horizontal bars (drawn as one filled outline)."""
        y = _np.atleast_1d(_np.asarray(y, dtype=float))
        wd = _np.broadcast_to(_np.asarray(width, dtype=float), y.shape)
        ht = _np.broadcast_to(_np.asarray(height, dtype=float), y.shape)
        lf = _np.broadcast_to(_np.asarray(left, dtype=float), y.shape)
        xs, ys = [], []
        for yi, wi, hi, li in zip(y - ht / 2, wd, ht, lf):
            xs += [li, li + wi, li + wi, li, li]
            ys += [yi, yi, yi + hi, yi + hi, yi]
            xs.append(_np.nan)
            ys.append(_np.nan)
        opts = _common(dict(kw, **({"label": label} if label is not None else {}),
                            **({"color": color} if color is not None else {})), self._ax)
        return self._ax.plot(xs, ys, fill=True, **opts)

    def step(self, x, y, *args, where="pre", **kw):
        """Step plot (where: 'pre', 'post', 'mid')."""
        lines = self.plot(x, y, *args, **kw)
        for ln in lines:
            ln.step = where
        return lines

    def semilogx(self, *args, **kw):
        lines = self.plot(*args, **kw)
        self.set_xscale("log")
        return lines

    def semilogy(self, *args, **kw):
        lines = self.plot(*args, **kw)
        self.set_yscale("log")
        return lines

    def loglog(self, *args, **kw):
        lines = self.plot(*args, **kw)
        self.set_xscale("log")
        self.set_yscale("log")
        return lines

    def cla(self):
        """Remove everything from this panel."""
        self._ax.clear()

    def get_title(self):
        return self._ax.title

    def get_lines(self):
        return self._ax.lines()

    def axis(self, arg=None, **_):
        """axis([xmin, xmax, ymin, ymax]) sets the limits; axis() returns them.
        'off' / 'equal' / 'tight' are accepted ('tight' autoscales)."""
        if isinstance(arg, (list, tuple)) and len(arg) == 4:
            self.set_xlim(arg[0], arg[1])
            self.set_ylim(arg[2], arg[3])
        elif arg == "tight":
            self._ax.autoscale()
        return (*self.get_xlim(), *self.get_ylim())

    def margins(self, *_, **__):
        pass  # autoscale chooses the margins

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

    # Limits in matplotlib's convention: (left, right) / (bottom, top) in
    # display order, so set_xlim(8, 2) draws a reversed (ppm-style) axis and
    # get_xlim() then returns (8, 2).
    def _set_lim(self, axis, first, second):
        if isinstance(first, (tuple, list)):
            first, second = first
        cur = self._get_lim(axis)
        first = cur[0] if first is None else float(first)
        second = cur[1] if second is None else float(second)
        setattr(self._ax, axis + "_reversed", first > second)
        getattr(self._ax, "set_" + axis + "lim")(min(first, second), max(first, second))
        self._fixed_limits.add(axis)  # later plots no longer re-fit this axis

    def _get_lim(self, axis):
        lo, hi = getattr(self._ax, axis + "lim")()
        return (hi, lo) if getattr(self._ax, axis + "_reversed") else (lo, hi)

    def set_xlim(self, left=None, right=None):
        self._set_lim("x", left, right)

    def set_ylim(self, bottom=None, top=None):
        self._set_lim("y", bottom, top)

    def get_xlim(self):
        return self._get_lim("x")

    def get_ylim(self):
        return self._get_lim("y")

    # ---- ticks ----------------------------------------------------------------------
    def set_xticks(self, ticks, labels=None, **_):
        self._ax.set_xticks(None if ticks is None else list(ticks), labels)

    def set_yticks(self, ticks, labels=None, **_):
        self._ax.set_yticks(None if ticks is None else list(ticks), labels)

    def set_xticklabels(self, labels, **_):
        current = self._ax.get_xticks()
        if current is None:
            raise RuntimeError("set_xticklabels: set the tick positions first (set_xticks)")
        self._ax.set_xticks(list(current[0]), [str(t) for t in labels])

    def set_yticklabels(self, labels, **_):
        current = self._ax.get_yticks()
        if current is None:
            raise RuntimeError("set_yticklabels: set the tick positions first (set_yticks)")
        self._ax.set_yticks(list(current[0]), [str(t) for t in labels])

    def get_xticks(self):
        t = self._ax.get_xticks()
        return _np.asarray([]) if t is None else t[0]

    def get_yticks(self):
        t = self._ax.get_yticks()
        return _np.asarray([]) if t is None else t[0]

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

    _LEGEND_CODES = {1: "upper right", 2: "upper left", 3: "lower left", 4: "lower right",
                     5: "right", 6: "center left", 7: "center right", 8: "lower center",
                     9: "upper center", 10: "center"}

    def legend(self, *args, loc=None, frameon=None, fontsize=None, labels=None, **__):
        """legend(), legend(labels), legend(handles, labels).

        Note: the legend position and font are figure-wide style settings, so
        the last call sets them for every panel."""
        if len(args) == 2:
            handles, labels = args
            for h, lab in zip(handles, labels):
                h.label = str(lab)
        elif len(args) == 1 or labels is not None:
            labels = labels if labels is not None else args[0]
            for h, lab in zip(self._ax.lines(), labels):
                h.label = str(lab)
        self._ax.legend(True)
        params = {}
        if isinstance(loc, int):
            loc = self._LEGEND_CODES.get(loc)  # 0 = "best": keep the current position
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

    def set(self, **kw):
        """ax.set(xlabel=..., ylabel=..., title=..., xlim=..., ylim=..., xscale=..., yscale=...)"""
        for key, value in kw.items():
            setter = getattr(self, "set_" + key, None)
            if setter is None:
                raise AttributeError(f"Axes.set() got an unsupported property {key!r}")
            setter(*value) if key in ("xlim", "ylim") and isinstance(value, (tuple, list)) else setter(value)

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

    @property
    def closed(self) -> bool:
        """True once the window was closed (by code or by the user)."""
        return self._fig._closed

    def gca(self):
        return next(a for a in self.axes if a.panel == self._fig.gca())

    def savefig(self, fname, dpi=None, format=None, **_):  # noqa: A002
        """Save as png / svg / pdf. ``fname`` may be a path or a binary file object.

        dpi defaults to 100 (the figure-size convention), as in matplotlib;
        bbox_inches, transparent, facecolor, ... are accepted and ignored."""
        scale = (dpi or _DPI) / _DPI
        if hasattr(fname, "write"):  # file object: export to a temporary file, copy the bytes
            import os
            import tempfile
            ext = (format or "png").lower()
            fd, tmp = tempfile.mkstemp(suffix="." + ext)
            os.close(fd)
            try:
                self._fig.savefig(tmp, width=self._size[0], height=self._size[1], scale=scale)
                with open(tmp, "rb") as f:
                    fname.write(f.read())
            finally:
                os.remove(tmp)
            return
        fname = str(fname)
        if format and not fname.lower().endswith("." + format.lower()):
            fname += "." + format.lower()
        self._fig.savefig(fname, width=self._size[0], height=self._size[1], scale=scale)

    def add_subplot(self, *args, **_):
        """add_subplot(111), add_subplot(2, 1, 2), add_subplot((2, 1, 2))."""
        if len(args) == 1 and isinstance(args[0], int) and 111 <= args[0] <= 999:
            args = tuple(int(c) for c in str(args[0]))
        elif len(args) == 1 and isinstance(args[0], tuple):
            args = args[0]
        rows, cols, index = (args + (1, 1, 1))[:3] if args else (1, 1, 1)
        if rows * cols != len(self.axes):
            grid = self._fig.subplots(rows, cols, squeeze=False)
            self.axes = [PltAxes(self, a) for row in grid for a in row]
        ax = self.axes[index - 1]
        self._fig.sca(ax.panel)
        return ax

    def suptitle(self, s, **_):
        self._fig.suptitle = str(s)

    def set_size_inches(self, w, h=None, forward=True):
        if h is None:
            w, h = w
        self._size = (int(w * _DPI), int(h * _DPI))

    def get_size_inches(self):
        return _np.array([self._size[0] / _DPI, self._size[1] / _DPI])

    def clf(self):
        """Clear every panel (data and objects)."""
        for ax in self.axes:
            ax.cla()

    def legend(self, *args, **kw):
        """Figure legend: shown on the first panel (plotter legends live inside a panel)."""
        return self.axes[0].legend(*args, **kw)

    def subplots_adjust(self, *_, **__):
        pass  # layout is automatic; use subplots(gridspec_kw=...) for spacing

    def tight_layout(self, *_, **__):
        pass  # layout is automatic

    def show(self):
        self._fig.show()

    @property
    def native(self):
        """The plotter.Figure (window, undo, peaks, styles, ...)."""
        return self._fig


# ---- module-level (stateful) API ------------------------------------------------------

_numbered: dict = {}


def figure(num=None, figsize=None, **_):
    """New figure, or the existing (still open) figure with this number/name."""
    global _current
    for key in [k for k, f in _numbered.items() if f.closed]:
        del _numbered[key]  # let closed figures be freed
    existing = _numbered.get(num) if num is not None else None
    if existing is not None and not existing.closed:
        _current = existing
        return existing
    _current = _PltFigure(figsize=figsize, num=num)
    if num is not None:
        _numbered[num] = _current
    return _current


def gcf():
    """The current figure; a new one if there is none or it was closed (as in matplotlib)."""
    return _current if _current is not None and not _current.closed else figure()


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
    # matplotlib gives hspace as a fraction of the panel height; plotter uses
    # pixels: approximated as fraction * 100 px (0 = frames touch).
    px = lambda v: None if v is None else int(round(v * 100))  # noqa: E731
    grid = fig._fig.subplots(nrows, ncols, sharex=bool(sharex), sharey=bool(sharey),
                             height_ratios=hr, width_ratios=wr, hspace=px(hspace),
                             wspace=px(wspace), squeeze=False)
    axes = [[PltAxes(fig, a) for a in row] for row in grid]
    fig.axes = [a for row in axes for a in row]
    fig._fig.sca(grid[0][0])
    # Like matplotlib: an object ndarray (axs[0, 1], axs.flat, axs.ravel()).
    arr = _np.empty((nrows, ncols), dtype=object)
    for r in range(nrows):
        for c in range(ncols):
            arr[r, c] = axes[r][c]
    if squeeze:
        if nrows == 1 and ncols == 1:
            return fig, arr[0, 0]
        if nrows == 1 or ncols == 1:
            return fig, arr.ravel()
    return fig, arr


def subplot(*args, **kw):
    """subplot(211) / subplot(2, 1, 1): the panel of a grid on the current figure."""
    return gcf().add_subplot(*args, **kw)


def clf():
    gcf().clf()


def cla():
    gca().cla()


def suptitle(s, **kw):
    gcf().suptitle(s, **kw)


def get_fignums():
    """Numbers / names of the open pyplot figures created with figure(num=...)."""
    return [k for k, f in _numbered.items() if not f.closed]


def ion():
    pass  # windows are always interactive at the prompt


def ioff():
    pass


def draw():
    _plotter.pause(0)  # process pending updates


def _refits(method):
    """Like matplotlib, re-fit the view to all data after each plotting call
    (plotter itself fits only once, so interactive zooming is kept), except
    for axes whose limits the script has set."""
    import functools

    @functools.wraps(method)
    def wrapper(self, *args, **kw):
        result = method(self, *args, **kw)
        fit_x, fit_y = "x" not in self._fixed_limits, "y" not in self._fixed_limits
        if fit_x or fit_y:
            self._ax.autoscale(x=fit_x, y=fit_y)
        return result

    return wrapper


for _name in ("plot", "scatter", "errorbar", "hist", "stairs", "fill_between", "bar", "barh", "step",
              "semilogx", "semilogy", "loglog", "contour", "contourf", "imshow", "pcolormesh"):
    setattr(PltAxes, _name, _refits(getattr(PltAxes, _name)))


def _ax_call(name):
    def f(*args, **kw):
        return getattr(gca(), name)(*args, **kw)
    f.__name__ = name
    f.__doc__ = getattr(PltAxes, name).__doc__
    return f


plot = _ax_call("plot")
bar = _ax_call("bar")
barh = _ax_call("barh")
step = _ax_call("step")
semilogx = _ax_call("semilogx")
semilogy = _ax_call("semilogy")
loglog = _ax_call("loglog")
axis = _ax_call("axis")
margins = _ax_call("margins")
scatter = _ax_call("scatter")
errorbar = _ax_call("errorbar")
hist = _ax_call("hist")
stairs = _ax_call("stairs")
fill_between = _ax_call("fill_between")
contour = _ax_call("contour")
contourf = _ax_call("contourf")
imshow = _ax_call("imshow")
pcolormesh = _ax_call("pcolormesh")


def colorbar(mappable=None, label=None, ax=None, **kw):
    target = ax if ax is not None else gca()
    return target.colorbar(mappable, label=label, **kw)
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


def xlim(*args, left=None, right=None):
    if args or left is not None or right is not None:
        gca().set_xlim(*args, **({"left": left} if left is not None else {}),
                       **({"right": right} if right is not None else {}))
    return gca().get_xlim()


def ylim(*args, bottom=None, top=None):
    if args or bottom is not None or top is not None:
        gca().set_ylim(*args, **({"bottom": bottom} if bottom is not None else {}),
                       **({"top": top} if top is not None else {}))
    return gca().get_ylim()


def xticks(ticks=None, labels=None, **kw):
    """xticks() -> (positions, labels); xticks(ticks, labels=None) sets them."""
    ax = gca()
    if ticks is not None or labels is not None:
        ax.set_xticks(ticks if ticks is not None else ax.get_xticks(), labels)
    t = ax.panel.get_xticks()
    return (ax.get_xticks(), [] if t is None else t[1])


def yticks(ticks=None, labels=None, **kw):
    """yticks() -> (positions, labels); yticks(ticks, labels=None) sets them."""
    ax = gca()
    if ticks is not None or labels is not None:
        ax.set_yticks(ticks if ticks is not None else ax.get_yticks(), labels)
    t = ax.panel.get_yticks()
    return (ax.get_yticks(), [] if t is None else t[1])


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
    """Close the current figure, a figure object, a figure number/name, or "all"."""
    global _current
    if fig == "all":
        _plotter.close_all()
        _numbered.clear()
        _current = None
        return
    if fig is None:
        fig = _current
    elif not isinstance(fig, _PltFigure):
        fig = _numbered.get(fig)  # number or name given to figure(num=...)
    if fig is None:
        return
    fig._fig.close()
    if fig is _current:
        _current = None