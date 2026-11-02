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

import os as _os
import re as _re
import numpy as _np

import plotter as _plotter
from plotter import style as _style


class _CallableList(list):
    """A list that can also be called (plotter.style.available() vs matplotlib's attribute)."""

    def __call__(self):
        return list(self)


class _StyleModule:
    """plt.style as in matplotlib: style.use(name or list), style.available (a list)."""

    def __getattr__(self, name):
        return getattr(_style, name)

    @property
    def available(self):
        return _CallableList(_style.available())

    def use(self, style):
        names = [style] if isinstance(style, (str, _os.PathLike)) else list(style)
        ignored = []
        for name in names:
            if name == "default":
                name = "default" if "default" in _style.available() else _style.available()[0]
            looks_like_file = isinstance(name, _os.PathLike) or _os.sep in str(name) or "." in str(name)
            if str(name) not in _style.available() and not looks_like_file:
                raise ValueError(f"unknown style {name!r}; available: {', '.join(_style.available())} "
                                 "(or a .gpstyle file)")
            ignored += _style.use(str(name))
        return ignored


style = _StyleModule()

_DPI = 100  # pixels per inch for figsize conversions
_current: "_PltFigure | None" = None

_FMT_COLORS = set("bgrcmykw")
_FMT_MARKERS = {".": ".", "o": "o", "s": "s", "^": "^", "v": "v", "<": "<", ">": ">", "D": "d", "d": "d",
                "x": "x", "+": "+", "*": "*", "p": "p", "h": "h", "H": "h", "8": "h"}
_FMT_LINES = ["--", "-.", "-", ":"]


def _is_dates(v):
    """datetime64 / datetime / pandas Timestamp values (scalars or arrays)."""
    if hasattr(v, "dtype") and getattr(v.dtype, "kind", "") == "M":
        return True
    try:
        a = _np.asarray(v)
    except Exception:  # noqa: BLE001
        return False
    if a.dtype.kind == "M":
        return True
    if a.dtype != object or a.size == 0:
        return False
    first = a.flat[0]
    return hasattr(first, "timestamp") or (hasattr(first, "year") and hasattr(first, "toordinal"))


def _unmask(v):
    """Masked arrays -> float arrays with NaN at masked points (drawn as gaps)."""
    if _np.ma.isMaskedArray(v):
        return _np.ma.filled(_np.ma.asarray(v, dtype=float), _np.nan)
    return v


def _nums(v):
    """Numbers as a float array; dates become Unix seconds (as plot() does)."""
    if _is_dates(v):
        shape = _np.shape(_np.asarray(v))
        return _plotter._to_seconds(v)[0].reshape(shape)
    return _np.asarray(v, dtype=float)


def _num(v):
    """One number; a date becomes Unix seconds."""
    return float(_nums(v).reshape(-1)[0]) if _is_dates(v) else float(v)


def _legend_label(label):
    """matplotlib's rule: None or a label starting with '_' is not in the legend."""
    if label is None:
        return None
    label = str(label)
    return None if label.startswith("_") else label


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


# matplotlib's exact values for the names plotter maps to its own palette tones
# (other CSS names are parsed with the same values by Qt).
_EXACT_COLORS = {"b": "#0000ff", "g": "#008000", "r": "#ff0000", "c": "#00bfbf", "m": "#bf00bf",
                 "y": "#bfbf00", "k": "#000000", "w": "#ffffff",
                 "blue": "#0000ff", "orange": "#ffa500", "aqua": "#00ffff", "teal": "#008080",
                 "yellow": "#ffff00", "magenta": "#ff00ff", "green": "#008000", "violet": "#ee82ee",
                 "purple": "#800080", "red": "#ff0000", "black": "#000000", "cyan": "#00ffff",
                 "gray": "#808080", "grey": "#808080"}


def _mpl_color(c):
    """matplotlib colour forms that plotter does not parse itself, and the
    exact matplotlib values of the basic colour names."""
    if isinstance(c, str) and c.startswith("tab:") and c[4:] in _TAB_COLORS:
        return _TAB_COLORS[c[4:]]
    if isinstance(c, str) and c.strip().lower() in _EXACT_COLORS:
        return _EXACT_COLORS[c.strip().lower()]
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
        face = kw.pop("markerfacecolor", kw.pop("mfc", None))
        edge = kw.pop("markeredgecolor", kw.pop("mec", None))
        for k in ("markeredgewidth", "mew", "markevery", "solid_capstyle", "dash_capstyle", "antialiased"):
            kw.pop(k, None)  # accepted; drawn with plotter's defaults
        while args:
            if len(args) == 1 or isinstance(args[1], str):
                y = args.pop(0)
                # A pandas Series / DataFrame is plotted against its index (as matplotlib).
                x = _np.arange(len(_np.asarray(y)))
                index = getattr(y, "index", None)
                if index is not None and hasattr(y, "values") and not callable(index):
                    idx = index.to_numpy() if hasattr(index, "to_numpy") else _np.asarray(index)
                    if idx.dtype.kind in "fiuM" or _is_dates(idx):
                        x = idx  # numbers or dates: the index is x (as matplotlib)
            else:
                x, y = args.pop(0), args.pop(0)
            x, y = _unmask(x), _unmask(y)
            if _np.ndim(x) == 0:  # plot(1, 2, 'ro'): one point
                x = [x]
            if _np.ndim(y) == 0:
                y = [y]
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
                line = self._ax.plot(x, col, **_common(dict(opts), self._ax))
                if face is not None:
                    line.markerfacecolor = "none" if face == "none" else _mpl_color(face)
                if edge is not None:
                    line.markeredgecolor = "none" if edge == "none" else _mpl_color(edge)
                lines.append(line)
        return lines

    def scatter(self, x, y, s=None, c=None, marker="o", cmap=None, vmin=None, vmax=None, **kw):
        """Points; c may be one colour, a colour per point, or values to colour-map;
        s may be one area or one area per point (in points², as matplotlib)."""
        kw.pop("linewidths", None)  # the outline is 1 px
        edge = kw.pop("edgecolors", kw.pop("edgecolor", None))
        edge = None if edge in (None, "face") else ("none" if edge == "none" else _mpl_color(edge))
        if "color" in kw and c is None:
            c = kw.pop("color")
        x, y = _unmask(x), _unmask(y)
        if c is not None and not isinstance(c, str):
            c = _unmask(c)
        x = _np.atleast_1d(self._xs(x)).astype(float).ravel()
        y = _np.atleast_1d(_np.asarray(y, dtype=float)).ravel()
        npts = x.size
        sym = _FMT_MARKERS.get(marker, marker if marker else "o")
        label = kw.pop("label", None)
        opts = _common(kw, self._ax)
        opts = {k: v for k, v in opts.items() if k not in ("style", "marker", "color")}

        # Per-point colours (keys into `palette`), or one colour for all.
        keys, palette, single = None, None, None
        if c is not None:
            arr = _np.asarray(c, dtype=object) if not isinstance(c, str) else None
            numeric = arr is not None and _np.asarray(c).dtype.kind in "fiu"
            if numeric and _np.asarray(c).ndim == 1 and _np.size(c) == npts and not isinstance(c, tuple):
                v = _np.asarray(c, dtype=float)
                keep = _np.isfinite(v)  # points without a value are not drawn (matplotlib: "bad" colour)
                x, y, v = x[keep], y[keep], v[keep]
                if s is not None and _np.ndim(s) > 0:
                    s = _np.asarray(s, dtype=float)[keep]
                if v.size == 0:
                    return None
                lo = _np.nanmin(v) if vmin is None else vmin
                hi = _np.nanmax(v) if vmax is None else vmax
                steps = 32  # colour-mapped in 32 steps
                palette = _plotter._plotter._colormap_colors(cmap or "viridis", steps)
                keys = _np.clip(((v - lo) / ((hi - lo) or 1) * (steps - 1)).round(), 0, steps - 1).astype(int)
            elif numeric and _np.asarray(c).ndim == 2 and _np.asarray(c).shape[0] == 1:
                single = tuple(float(t) for t in _np.asarray(c, dtype=float)[0])
            elif (isinstance(c, (list, tuple)) and len(c) == 1 and isinstance(c[0], str)):
                single = _mpl_color(c[0])
            elif numeric and _np.asarray(c).ndim == 2 and _np.asarray(c).shape[0] == npts:
                rgba = [tuple(float(t) for t in row) for row in _np.asarray(c, dtype=float)]
                palette = sorted(set(rgba))
                index = {col: i for i, col in enumerate(palette)}
                keys = _np.array([index[col] for col in rgba])
            elif (isinstance(c, (list, tuple, _np.ndarray)) and len(c) == npts and npts > 1
                  and all(isinstance(t, str) for t in c)):
                names = [_mpl_color(t) for t in c]
                palette = sorted(set(names))
                index = {col: i for i, col in enumerate(palette)}
                keys = _np.array([index[col] for col in names])
            else:
                single = _mpl_color(c)
        mapped = keys is not None  # per-point colours (a colour map or a list)
        # Per-point sizes: grouped into at most 12 sizes.
        size_keys, sizes = None, None
        if s is not None and _np.ndim(s) > 0 and _np.size(s) == x.size and x.size > 1:
            diam = _np.sqrt(_np.maximum(_np.asarray(s, dtype=float), 0))
            lo, hi = _np.nanmin(diam), _np.nanmax(diam)
            nsz = 12 if hi > lo else 1
            size_keys = _np.clip(((diam - lo) / ((hi - lo) or 1) * (nsz - 1)).round(), 0, nsz - 1).astype(int)
            sizes = [float(lo + (hi - lo) * k / max(1, nsz - 1)) for k in range(nsz)]
        size = None if s is None or sizes is not None else float(_np.sqrt(_np.mean(s)))

        def draw(mask, color, msize, lab):
            line = self._ax.plot(x[mask], y[mask], style="none", marker=sym, markersize=msize,
                                 color=color, label=lab, **opts)
            if mapped:
                line.exact_color = True  # colours from a colour map / per point: no theme recolouring
            if edge is not None:
                line.markeredgecolor = edge
            return line

        if keys is None and size_keys is None:
            color = single if single is not None else self._ax._take_color()
            return draw(_np.ones(x.size, bool), color, size, None if label is None else str(label))
        if keys is None:
            keys, palette = _np.zeros(x.size, int), [single if single is not None else self._ax._take_color()]
        if size_keys is None:
            size_keys = _np.zeros(x.size, int)
        first = None
        for ck in sorted(set(keys.tolist())):
            for sk in sorted(set(size_keys[keys == ck].tolist())):
                m = (keys == ck) & (size_keys == sk)
                line = draw(m, palette[ck], sizes[sk] if sizes else size,
                            str(label) if (first is None and label is not None) else None)
                first = first or line
        return first

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
        return _ErrorbarContainer(self._ax.plot(x, y, xerr=xerr, yerr=yerr, **opts))

    def hist(self, x, bins=10, range=None, density=False, weights=None,  # noqa: A002
             histtype="bar", log=False, **kw):
        """Histogram(s); log=True puts the count axis on a log scale."""
        out = self._hist(x, bins, range, density, weights, histtype, **kw)
        if log:
            (self.set_xscale if kw.get("orientation") == "horizontal" else self.set_yscale)("log")
        return out

    def _hist(self, x, bins, range, density, weights, histtype, **kw):  # noqa: A002
        # Several datasets (a list of arrays or a 2-D array, one per column):
        # one histogram each, with shared bins, like matplotlib.
        many = (isinstance(x, (list, tuple)) and len(x) > 0 and _np.ndim(x[0]) > 0) or (
            not isinstance(x, (list, tuple)) and _np.ndim(x) == 2)
        if many:
            data = list(x) if isinstance(x, (list, tuple)) else list(_np.asarray(x).T)
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
            return (_np.array([r[0] for r in results]), results[0][1], [r[2] for r in results])  # one _Patches each
        kw_edge = kw.pop("edgecolor", kw.pop("ec", None))
        fill = kw.pop("facecolor", None)
        opts = _common(kw)
        if histtype == "bar":
            histtype = "stepfilled"
        counts, edges, line = self._ax.hist(x, bins, range, density=density, weights=weights,
                                            histtype=histtype, fill=fill,
                                            **{k: v for k, v in opts.items() if k in ("label", "color", "width", "style")})
        edges_a = _np.asarray(edges, dtype=float)
        rects = [(float(edges_a[i]), 0.0, float(edges_a[i + 1] - edges_a[i]), float(c))
                 for i, c in enumerate(_np.asarray(counts, dtype=float))]
        if kw_edge is not None and kw_edge != "none":
            line.color = _mpl_color(kw_edge)  # the outline
        return counts, edges, _RectGroup(self, line, rects, None, label=_legend_label(opts.get("label")))

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
        x = self._xs(x)
        y1 = _np.asarray(y1, dtype=float) + 0 * x
        y2 = _np.broadcast_to(_np.asarray(y2, dtype=float), y1.shape)
        if where is not None:
            # As matplotlib: only the runs where `where` is True are filled
            # (one band per run, sharing the colour and the label).
            mask = _np.broadcast_to(_np.asarray(where, dtype=bool), y1.shape)
            idx = _np.flatnonzero(mask)
            runs = _np.split(idx, _np.flatnonzero(_np.diff(idx) > 1) + 1) if idx.size else []
            runs = [r for r in runs if r.size >= 2]
            if not runs:  # nothing to fill: an empty band (still a Line, as documented)
                runs = [_np.array([], dtype=int)]
            col = _mpl_color(color) or self._ax._take_color()
            first = None
            self._ax._begin_macro("Fill between")  # one undo step
            try:
                for run in runs:
                    band = self._ax.fill_between(x[run], y1[run], y2[run], color=col, alpha=alpha, step=step,
                                                 label=None if (label is None or first is not None) else str(label))
                    first = first or band
            finally:
                self._ax._end_macro()
            return first
        return self._ax.fill_between(x, y1, y2, label=None if label is None else str(label),
                                     color=_mpl_color(color), alpha=alpha, step=step)

    def contour(self, *args, levels=None, colors=None, **kw):
        """contour(Z), contour(X, Y, Z), with levels as a count or a list of exact
        levels (also positional: contour(X, Y, Z, 10) / contour(Z, [0.1, 0.5]))."""
        kw.pop("linewidths", None)
        kw.pop("linestyles", None)
        kw.pop("alpha", None)
        cmap = kw.pop("cmap", None)
        label = kw.pop("label", None)
        if kw:
            raise TypeError(f"unsupported keyword(s): {', '.join(sorted(kw))}")
        args = list(args)
        if len(args) in (2, 4) and levels is None:  # trailing positional levels
            levels = args.pop()
        if len(args) == 1:
            z, x, y = _unmask(args[0]), None, None
        elif len(args) == 3:
            x, y, z = (_np.asarray(_unmask(a)) for a in args)
            x = x[0] if x.ndim == 2 else x
            y = y[:, 0] if y.ndim == 2 else y
        else:
            raise TypeError("contour(Z) or contour(X, Y, Z) (optionally followed by levels)")
        import numbers

        if levels is not None and not isinstance(levels, numbers.Integral):
            levels = [float(v) for v in _np.atleast_1d(levels)]  # exact levels, as given
        color = _mpl_color(colors) if isinstance(colors, (str, tuple)) else None
        c = self._ax.contour(z, x, y, levels=levels, color=color, negative_color=color, label=label,
                             negative=not isinstance(levels, list), cmap=cmap)
        if color is not None:
            c.negative_dashed = True  # single-colour contours: negative levels dashed, as matplotlib
        self._last_image = c
        return c

    @staticmethod
    def _nice_levels(z, n):
        """About n+1 round level values spanning the data (like matplotlib's MaxNLocator)."""
        z = _np.asarray(_unmask(z), dtype=float)  # masked values do not set the levels
        lo, hi = _np.nanmin(z), _np.nanmax(z)
        if not (_np.isfinite(lo) and _np.isfinite(hi)) or lo == hi:
            return [float(lo) - 0.5, float(lo) + 0.5]
        raw = (hi - lo) / max(1, n)
        mag = 10.0 ** _np.floor(_np.log10(raw))
        step = next(m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw)
        first = _np.floor(lo / step) * step
        last = _np.ceil(hi / step) * step
        return [float(v) for v in _np.arange(first, last + step / 2, step)]

    def contourf(self, *args, **kw):
        """Filled contours: flat colour bands between the levels (no lines)."""
        import numbers

        cmap = kw.pop("cmap", None)
        args = list(args)
        levels = kw.pop("levels", None)
        if len(args) in (2, 4):
            levels = args.pop()
        z = args[0] if len(args) == 1 else args[2]
        if levels is None or isinstance(levels, numbers.Integral):
            levels = self._nice_levels(z, (int(levels) if levels is not None else 7) + 1)
        c = self.contour(*args, levels=list(levels), **kw)
        c.filled = True
        c.contours = False
        if cmap is not None:
            c.cmap = cmap
        elif c.cmap == "auto":
            c.cmap = "viridis"
        self._last_image = c
        return c

    # ---- images ------------------------------------------------------------------------
    @staticmethod
    def _centres(edges_or_centres, n):
        """Cell-centre (start, stop) from n+1 edges or n centres (1-D or 2-D grids)."""
        a = _np.asarray(edges_or_centres, dtype=float)
        if a.size == n + 1:
            if n >= 2:  # uneven edges (e.g. [0, 1, 10]) cannot be drawn as an even grid
                step = _np.diff(a)
                if not _np.allclose(step, step[0], rtol=1e-3, atol=0):
                    raise ValueError("plotter draws images on evenly spaced grids only; "
                                     "these coordinates are not evenly spaced (resample the data first)")
            a = 0.5 * (a[:-1] + a[1:])
        if a.size != n:
            raise ValueError(f"grid coordinates: expected {n} centres or {n + 1} edges, got {a.size}")
        if n > 2:
            step = _np.diff(a)
            if not _np.allclose(step, step[0], rtol=1e-3, atol=0):
                raise ValueError("plotter draws images on evenly spaced grids only; "
                                 "these coordinates are not evenly spaced (resample the data first)")
        return float(a[0]), float(a[-1])

    def imshow(self, X, cmap=None, vmin=None, vmax=None, origin=None, extent=None, aspect=None,
               interpolation=None, label=None, **kw):
        """Colour-mapped image of a 2-D array (matplotlib conventions: row 0 at the
        top unless origin='lower'; extent = (left, right, bottom, top) pixel edges)."""
        z = _np.asarray(_unmask(X), dtype=float)
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
            # A descending extent shows the axis that way round (as matplotlib).
            if left > right:
                self._ax.x_reversed = True
            if bottom > top:
                self._ax.y_reversed = True
            dx, dy = (right - left) / nx, (top - bottom) / ny
            x = (left + dx / 2, right - dx / 2)
            y = (bottom + dy / 2, top - dy / 2)
            # Row 0 is at the top for origin='upper' (the extent's top edge),
            # at the bottom for 'lower'.
            if upper:
                z = z[::-1]
        smooth = interpolation not in (None, "none", "nearest", "antialiased")
        c = self._ax.image(z, x, y, cmap=cmap or "viridis", colorbar=False, vmin=vmin, vmax=vmax,
                           label=label, smooth=smooth)
        self._last_image = c
        return c

    def pcolormesh(self, *args, cmap=None, vmin=None, vmax=None, shading=None, label=None, **kw):
        """pcolormesh(C) or pcolormesh(X, Y, C) on a uniform grid (X, Y: edges or centres)."""
        if len(args) == 1:
            c_arr, x, y = _np.asarray(_unmask(args[0]), dtype=float), None, None
        elif len(args) == 3:
            X, Y, c_arr = (_np.asarray(_unmask(a), dtype=float) for a in args)
            ny, nx = c_arr.shape
            X = X[0] if X.ndim == 2 else X
            Y = Y[:, 0] if Y.ndim == 2 else Y
            x, y = self._centres(X, nx), self._centres(Y, ny)
        else:
            raise TypeError("pcolormesh(C) or pcolormesh(X, Y, C)")
        c = self._ax.image(c_arr, x, y, cmap=cmap or "viridis", colorbar=False, vmin=vmin, vmax=vmax,
                           label=label, smooth=shading == "gouraud")
        self._last_image = c
        return c

    def colorbar(self, mappable=None, label=None, **kw):
        """Show the colour bar of an image (default: the last one drawn in this panel)."""
        m = mappable if mappable is not None else getattr(self, "_last_image", None)
        if m is not None and hasattr(type(m), "image") and not m.image:
            raise RuntimeError("colorbar: this contour is drawn as lines only, without a colour scale; "
                               "use contourf / imshow for a colour bar")
        if m is None or not hasattr(type(m), "colorbar"):
            raise RuntimeError("colorbar: colour bars are drawn for images (imshow / pcolormesh / "
                               "contourf); a scatter's colours have no colour bar here")
        m.colorbar = True
        if label is not None:
            m.label = str(label)

        return m

    def hlines(self, y, xmin, xmax, colors=None, linestyles="-", label=None, **kw):
        """Horizontal segments from xmin to xmax at each y (one dataset)."""
        y = _np.atleast_1d(_np.asarray(y, dtype=float))
        x0 = _np.broadcast_to(self._xs(xmin), y.shape)
        x1 = _np.broadcast_to(self._xs(xmax), y.shape)
        xs = _np.column_stack([x0, x1, _np.full_like(y, _np.nan)]).ravel()
        ys = _np.column_stack([y, y, _np.full_like(y, _np.nan)]).ravel()
        return self.plot(xs, ys, color=colors, linestyle=linestyles, label=label, **kw)[0]

    def vlines(self, x, ymin, ymax, colors=None, linestyles="-", label=None, **kw):
        """Vertical segments from ymin to ymax at each x (one dataset)."""
        x = _np.atleast_1d(self._xs(x))
        y0 = _np.broadcast_to(_np.asarray(ymin, dtype=float), x.shape)
        y1 = _np.broadcast_to(_np.asarray(ymax, dtype=float), x.shape)
        xs = _np.column_stack([x, x, _np.full_like(x, _np.nan)]).ravel()
        ys = _np.column_stack([y0, y1, _np.full_like(x, _np.nan)]).ravel()
        return self.plot(xs, ys, color=colors, linestyle=linestyles, label=label, **kw)[0]

    def axvspan(self, xmin, xmax, ymin=0, ymax=1, alpha=0.25, color=None, label=None, **kw):
        """Shaded vertical band from xmin to xmax, always the full panel height
        (not data: it does not change the autoscaled view)."""
        return self._span(self._x1(xmin), self._x1(xmax), "v", alpha, color, label, kw)

    def axhspan(self, ymin, ymax, xmin=0, xmax=1, alpha=0.25, color=None, label=None, **kw):
        """Shaded horizontal band from ymin to ymax, always the full panel width."""
        return self._span(ymin, ymax, "h", alpha, color, label, kw)

    def _span(self, a, b, orientation, alpha, color, label, kw):
        color = kw.pop("facecolor", kw.pop("fc", color))
        for k in ("edgecolor", "ec", "linewidth", "lw", "zorder", "hatch"):
            kw.pop(k, None)
        if kw:
            raise TypeError(f"unsupported keyword(s): {', '.join(sorted(kw))}")
        col = _mpl_color(color) if color is not None else "#9c9ca1"
        r, g, bl, _ = _plotter._plotter._rgba(col)
        rgba = (r, g, bl, min(float(alpha), 254 / 255))  # fully opaque would mean "default 25 %"
        label = _legend_label(label)
        return self._ax.marker(float(a), orientation, color=rgba, span_to=float(b),
                               label=label or "", legend=True)

    def fill_betweenx(self, y, x1, x2=0, alpha=0.3, color=None, label=None, **kw):
        """Area between two curves x1(y) and x2(y) (a closed, filled outline)."""
        y = _np.asarray(y, dtype=float)
        x1 = _np.broadcast_to(_np.asarray(x1, dtype=float), y.shape)
        x2 = _np.broadcast_to(_np.asarray(x2, dtype=float), y.shape)
        # Non-finite rows split the area into pieces; one path visits each
        # piece and returns to the start the same way (no NaN in the path).
        ok = _np.isfinite(y) & _np.isfinite(x1) & _np.isfinite(x2)
        xs, ys = [], []
        start = None
        idx = _np.flatnonzero(ok)
        pieces = _np.split(idx, _np.flatnonzero(_np.diff(idx) > 1) + 1) if idx.size else []
        for piece in pieces:
            if piece.size < 2:
                continue
            px = _np.concatenate([x1[piece], x2[piece][::-1], x1[piece][:1]])
            py = _np.concatenate([y[piece], y[piece][::-1], y[piece][:1]])
            if start is None:
                start = (px[0], py[0])
            xs += list(px) + [start[0]]
            ys += list(py) + [start[1]]
        col = _mpl_color(color) or self._ax._take_color()
        r, g, b, _ = _plotter._plotter._rgba(col)
        return self._ax.plot(xs, ys, color=col, fill=(r, g, b, alpha), style="none",
                             label=None if label is None else str(label))

    def _xs(self, v):
        """x values as numbers; dates switch the panel to date tick labels."""
        if _is_dates(v) and not self._ax.x_time:
            self._ax.x_time = True
        return _nums(v)

    def _x1(self, v):
        return float(self._xs(v).reshape(-1)[0]) if _is_dates(v) else float(v)

    def twinx(self):
        """A second y axis on the right, sharing this panel's x axis."""
        twin = PltAxes(self.figure, self._ax.twinx())
        twin._twin_of = self
        self.figure.axes.append(twin)
        return twin

    def set_aspect(self, *_, **__):
        pass  # the aspect ratio follows the window / figure size

    def minorticks_on(self):
        self.figure._fig.set_style({"xtick.minor.visible": True, "ytick.minor.visible": True})

    def minorticks_off(self):
        self.figure._fig.set_style({"xtick.minor.visible": False, "ytick.minor.visible": False})

    @staticmethod
    def _line_opts(kw):
        # As matplotlib: solid, in 'C0' unless given, without using up a cycle colour.
        opts = {k: v for k, v in _common(kw).items() if k in ("color", "label", "style", "width")}
        opts.setdefault("style", "-")
        if opts.get("color") is None:
            opts["color"] = "C0"
        return opts

    def _ref_line(self, pos, orientation, kw):
        # matplotlib lists a labelled reference line in the legend (no tag on the plot).
        opts = self._line_opts(kw)
        opts["label"] = opts.get("label") or ""
        return self._ax.marker(pos, orientation, show_value=False, legend=True, **opts)

    def axvline(self, x=0, ymin=0, ymax=1, **kw):
        # ymin / ymax (partial lines) are accepted; markers always span the panel.
        return self._ref_line(self._x1(x), "v", kw)

    def axhline(self, y=0, xmin=0, xmax=1, **kw):
        return self._ref_line(float(y), "h", kw)

    @property
    def transAxes(self):  # noqa: N802 - matplotlib name
        """Marker for axes-fraction coordinates in text(..., transform=ax.transAxes)."""
        return ("axes", self)

    def text(self, x, y, s, fontsize=None, color=None, ha=None, va=None, horizontalalignment=None,
             verticalalignment=None, **kw):
        transform = kw.pop("transform", None)
        # transform=ax.transAxes: axes fractions, kept when the view changes.
        coords = "axes" if isinstance(transform, tuple) and transform[:1] == ("axes",) else "data"
        ha = ha or horizontalalignment or "left"
        va = va or verticalalignment or "baseline"
        x = float(x) if coords == "axes" else self._x1(x)
        return self._ax.annotate(str(s), x, float(y), fontsize=fontsize or kw.get("size"),
                                 color=_mpl_color(color), ha=ha, va=va, coords=coords)

    def annotate(self, text, xy, xytext=None, arrowprops=None, ha=None, va=None, **kw):
        """Text at xytext (or xy); with arrowprops an arrow from the text to xy.
        xycoords / textcoords: 'data' or 'axes fraction' (the arrow target xy is data)."""
        x, y = xytext if xytext is not None else xy
        fontsize = kw.get("fontsize", kw.get("size"))
        color = _mpl_color(kw.get("color"))
        place = kw.get("textcoords") if xytext is not None else kw.get("xycoords")
        place = place or kw.get("xycoords") or "data"
        if place in ("offset points", "offset pixels"):
            # A small offset in points from xy: the text goes at xy itself.
            x, y = xy
            xytext, place = None, kw.get("xycoords") or "data"
        if place not in ("data", "axes fraction"):
            import warnings
            warnings.warn(f"plotter: coordinates {place!r} are not supported; using data coordinates",
                          stacklevel=2)
            place = "data"
        arrow = (self._x1(xy[0]), float(xy[1])) if arrowprops is not None and xytext is not None else None
        if place == "data":
            x = self._x1(x)
        if arrow is not None and kw.get("xycoords", "data") != "data":
            raise ValueError("the arrow target xy must be in 'data' coordinates")
        return self._ax.annotate(str(text), float(x), float(y), fontsize=fontsize, color=color,
                                 coords="axes" if place == "axes fraction" else "data",
                                 ha=ha or kw.get("horizontalalignment") or "left",
                                 va=va or kw.get("verticalalignment") or "baseline", arrow_to=arrow)

    @staticmethod
    def _bar_outline(starts, sizes, lengths, bases, vertical):
        """One path through all bars without NaN (a filled curve with gaps is
        not drawn): it starts at one point, and for every bar goes there,
        around the rectangle and back the same way, so only the rectangles
        enclose area (stacked bars with different bottoms included). Bars
        with non-finite values are left out."""
        a, b = [], []  # along / across the bars
        start = None
        for s0, sz, ln, bs in zip(starts, sizes, lengths, bases):
            if not all(_np.isfinite(v) for v in (s0, sz, ln, bs)):
                continue
            if start is None:
                start = (s0, bs)
                a.append(s0)
                b.append(bs)
            a += [s0, s0, s0 + sz, s0 + sz, s0, start[0]]
            b += [bs, bs + ln, bs + ln, bs, bs, start[1]]
        return (a, b) if vertical else (b, a)

    def bar(self, x, height, width=0.8, bottom=0.0, align="center", color=None, label=None,
            edgecolor=None, yerr=None, capsize=None, tick_label=None, **kw):
        """Vertical bars (filled, no edge line, as matplotlib draws them by default).
        x may be numbers or category names (placed at 0, 1, 2, ... with those tick labels)."""
        xs = list(_np.atleast_1d(x))
        if xs and isinstance(xs[0], str):
            tick_label = tick_label or xs
            xs = list(range(len(xs)))
        dates = _is_dates(x)
        x = _np.atleast_1d(self._xs(x)) if dates else _np.asarray(xs, dtype=float)
        import datetime as _dt
        if isinstance(width, _dt.timedelta) or _np.asarray(width).dtype.kind == "m" or (
                _np.asarray(width).dtype == object and _np.size(width)
                and isinstance(_np.asarray(width).flat[0], _dt.timedelta)):
            width = _np.asarray(width).astype("timedelta64[ns]").astype(_np.int64) / 1e9
        elif dates:
            width = _np.asarray(width, dtype=float) * 86400.0  # numbers are days on a date axis
        h = _np.broadcast_to(_np.asarray(height, dtype=float), x.shape)
        w = _np.broadcast_to(_np.asarray(width, dtype=float), x.shape)
        b = _np.broadcast_to(_np.asarray(bottom, dtype=float), x.shape)
        left = x - w / 2 if align == "center" else x
        if isinstance(color, (list, tuple)) and color and not isinstance(color[0], (int, float)):
            # One colour per bar: one filled shape per bar.
            out = [self.bar(xi, hi, wi, bi, align, color=ci, label=label if i == 0 else None, **kw)._line
                   for i, (xi, hi, wi, bi, ci) in enumerate(zip(x, h, w, b, color))]
            line = out[0]
        else:
            xs_, ys_ = self._bar_outline(left, w, h, b, True)
            opts = _common(dict(kw, **({"label": label} if label is not None else {}),
                                **({"color": color} if color is not None else {})), self._ax)
            opts["style"] = "none"
            opts["color"] = opts.get("color") or self._ax._take_color()
            line = self._ax.plot(xs_, ys_, fill=opts["color"], **opts)  # opaque, as matplotlib
            out = None
        if yerr is not None:
            self._ax.plot(x, b + h, yerr=yerr, style="none", color="k")
        if tick_label is not None:
            self._ax.set_xticks(list(x), [str(t) for t in _np.atleast_1d(tick_label)])
        group = _RectGroup(self, line, [(float(l0), float(b0), float(w0), float(h0))
                                        for l0, b0, w0, h0 in zip(_np.atleast_1d(left), b, w, h)], out,
                           label=_legend_label(label))
        if yerr is not None:  # bar_label puts values beyond the error bars
            e = _np.asarray(yerr, dtype=float)
            lo, hi = (e[0], e[1]) if e.ndim == 2 else (e, e)
            # (lower, upper) per bar: a negative bar's label goes past the lower error.
            object.__setattr__(group, "_errors", list(zip(_np.broadcast_to(lo, x.shape), _np.broadcast_to(hi, x.shape))))
        return group

    def barh(self, y, width, height=0.8, left=0.0, color=None, label=None, tick_label=None, xerr=None,
             align="center", edgecolor=None, capsize=None, **kw):
        """Horizontal bars (filled). y may be numbers or category names."""
        ys0 = list(_np.atleast_1d(y))
        if ys0 and isinstance(ys0[0], str):
            tick_label = tick_label or ys0
            ys0 = list(_np.arange(len(ys0)))
        y = _np.asarray(ys0, dtype=float)
        wd = _np.broadcast_to(_np.asarray(width, dtype=float), y.shape)
        ht = _np.broadcast_to(_np.asarray(height, dtype=float), y.shape)
        lf = _np.broadcast_to(_np.asarray(left, dtype=float), y.shape)
        bottom = y - ht / 2 if align == "center" else y
        if isinstance(color, (list, tuple)) and color and not isinstance(color[0], (int, float)):
            # One colour per bar: one filled shape per bar.
            out = [self.barh(yi, wi, hi, li, color=ci, label=label if i == 0 else None, align=align, **kw)._line
                   for i, (yi, wi, hi, li, ci) in enumerate(zip(y, wd, ht, lf, color))]
            line = out[0]
        else:
            xs, ys = self._bar_outline(bottom, ht, wd, lf, False)
            opts = _common(dict(kw, **({"label": label} if label is not None else {}),
                                **({"color": color} if color is not None else {})), self._ax)
            opts["style"] = "none"
            opts["color"] = opts.get("color") or self._ax._take_color()
            line = self._ax.plot(xs, ys, fill=opts["color"], **opts)
            out = None
        if xerr is not None:
            self._ax.plot(lf + wd, bottom + ht / 2, xerr=xerr, style="none", color="k")
        if tick_label is not None:
            self._ax.set_yticks(list(y), [str(t) for t in _np.atleast_1d(tick_label)])
        group = _RectGroup(self, line, [(float(l0), float(b0), float(w0), float(h0))
                                        for l0, b0, w0, h0 in zip(lf, bottom, wd, ht)], out,
                           label=_legend_label(label), horizontal=True)
        if xerr is not None:
            e = _np.asarray(xerr, dtype=float)
            lo, hi = (e[0], e[1]) if e.ndim == 2 else (e, e)
            # (lower, upper) per bar: a negative bar's label goes past the lower error.
            object.__setattr__(group, "_errors", list(zip(_np.broadcast_to(lo, y.shape), _np.broadcast_to(hi, y.shape))))
        return group

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
        """Clear this panel: data, objects, title, labels, ticks and scales."""
        ax = self._ax
        ax.clear()
        ax.legend_with_twin = False
        ax.title = ax.xlabel = ax.ylabel = ""
        ax.x_log = ax.y_log = ax.x_reversed = ax.y_reversed = False
        ax.set_xticks(None)
        ax.set_yticks(None)
        ax.legend(False)
        self._fixed_limits.clear()

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
        conv = self._x1 if axis == "x" else float
        first = cur[0] if first is None else conv(first)
        second = cur[1] if second is None else conv(second)
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
    def _rotate(self, axis, kw):
        rot = kw.get("rotation", kw.get("labelrotation"))
        if rot is not None:
            rot = {"horizontal": 0, "vertical": 90}.get(rot, rot)
            setattr(self._ax, axis + "_tick_rotation", float(rot))

    def _set_ticks(self, axis, ticks, labels, minor, kw):
        if minor:
            # Custom minor tick positions are not supported: show automatic minor ticks.
            self.figure._fig.set_style({axis + "tick.minor.visible": True})
            return
        getattr(self._ax, "set_" + axis + "ticks")(None if ticks is None else list(ticks), labels)
        self._rotate(axis, kw)

    def set_xticks(self, ticks, labels=None, *, minor=False, **kw):
        self._set_ticks("x", ticks, labels, minor, kw)

    def set_yticks(self, ticks, labels=None, *, minor=False, **kw):
        self._set_ticks("y", ticks, labels, minor, kw)

    def _set_ticklabels(self, axis, labels, kw):
        current = getattr(self._ax, "get_" + axis + "ticks")()
        if current is None:
            raise RuntimeError(f"set_{axis}ticklabels: set the tick positions first (set_{axis}ticks)")
        getattr(self._ax, "set_" + axis + "ticks")(list(current[0]), [str(t) for t in labels])
        self._rotate(axis, kw)

    def set_xticklabels(self, labels, **kw):
        self._set_ticklabels("x", labels, kw)

    def set_yticklabels(self, labels, **kw):
        self._set_ticklabels("y", labels, kw)

    def get_xticks(self, minor=False):
        t = self._ax.get_xticks()
        return _np.asarray(self._ax._drawn_ticks(True, *self.figure._size)[0]) if t is None else t[0]

    def get_yticks(self, minor=False):
        t = self._ax.get_yticks()
        return _np.asarray(self._ax._drawn_ticks(False, *self.figure._size)[0]) if t is None else t[0]

    def get_xscale(self):
        return "log" if self._ax.x_log else "linear"

    def get_yscale(self):
        return "log" if self._ax.y_log else "linear"

    def get_legend(self):
        """The legend (an object with set_visible / remove), or None if hidden."""
        return _Legend(self._ax) if self._ax.legend_visible else None

    def label_outer(self):
        """Only the outer panels of a grid keep their axis labels."""
        if not hasattr(self, "_grid_pos"):
            return
        rows, cols, index = self._grid_pos
        if index // cols < rows - 1:
            self._ax.xlabel = ""
            self._ax.x_tick_labels = False  # as matplotlib: inner tick numbers go too
        if index % cols > 0:
            self._ax.ylabel = ""
            self._ax.y_tick_labels = False

    def set_xscale(self, scale):
        self._ax.x_log = scale == "log"

    def set_yscale(self, scale):
        self._ax.y_log = scale == "log"  # re-fitted by plotter if it follows the data

    def invert_xaxis(self):
        self._ax.x_reversed = not self._ax.x_reversed

    def invert_yaxis(self):
        self._ax.y_reversed = not self._ax.y_reversed

    _LEGEND_CODES = {1: "upper right", 2: "upper left", 3: "lower left", 4: "lower right",
                     5: "right", 6: "center left", 7: "center right", 8: "lower center",
                     9: "upper center", 10: "center"}

    def legend(self, *args, loc=None, frameon=None, fontsize=None, labels=None, handles=None, **__):
        """legend(), legend(labels), legend(handles, labels), legend(handles=[...]).

        Handles from another panel (e.g. a twinx axis) are listed in this
        panel's legend and left out of their own. The legend font and frame
        are figure-wide style settings; the position is per panel."""
        if len(args) == 2:
            handles, labels = args
        elif (len(args) == 1 and labels is None and len(args[0]) > 0
              and not isinstance(_np.atleast_1d(_np.asarray(args[0], dtype=object))[0], str)):
            handles = args[0]
        if handles is not None:
            def unwrap(h):
                if isinstance(h, _RectGroup):
                    return h._line
                if isinstance(h, _Rect):
                    return h._group._line
                return h[0] if isinstance(h, (list, tuple)) else h
            handles = [unwrap(h) for h in handles]
            for old in getattr(self, "_legend_standins", []):  # from an earlier legend() call
                old.remove()
            self._legend_standins = []
            labels = list(labels) if labels is not None else [h.label for h in handles]
            host = getattr(self, "_twin_of", None)
            if host is not None and any(hasattr(h, "_panel") and h in host._ax.lines() for h in handles):
                # Called on the twin with the host's lines: the merged legend lives on the host.
                return host.legend(handles=handles, labels=labels, loc=loc, frameon=frameon, fontsize=fontsize)
            own = self._ax.lines()
            twin = next((a for a in self.figure.axes if getattr(a, "_twin_of", None) is self), None)
            for h, lab in zip(handles, labels):
                h.label = str(lab)
                if hasattr(h, "_panel") and h not in own:
                    if twin is not None and h in twin._ax.lines():
                        # A line of this panel's twinx: one legend lists both panels' lines.
                        self._ax.legend_with_twin = True
                    else:
                        # A line of another panel: an entry with the same look here.
                        self._legend_standins.append(
                            self._ax.plot([], [], label=str(lab), color=h.color, style=h.style, width=h.width,
                                          marker=None if h.marker == "none" else h.marker))
        elif len(args) == 1 or labels is not None:
            labels = labels if labels is not None else args[0]
            for h, lab in zip(self._ax.lines(), labels):
                h.label = str(lab)
        self._ax.legend(True)
        params = {}
        if isinstance(loc, int):
            loc = self._LEGEND_CODES.get(loc, "best")
        if loc is None and self._ax.legend_loc is None:
            loc = "best"  # matplotlib's default
        if isinstance(loc, str):
            self._ax.legend_loc = loc  # this panel only, as in matplotlib
        if frameon is not None:
            params["legend.frameon"] = bool(frameon)
        if isinstance(fontsize, str):
            fontsize = rcParams._convert("legend.fontsize", fontsize)
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

    def relim(self, *_, **__):
        pass

    def autoscale_view(self, tight=None, scalex=True, scaley=True):
        self._ax.autoscale(x=bool(scalex), y=bool(scaley))

    def get_legend_handles_labels(self):
        standins = getattr(self, "_legend_standins", [])
        lines = [l for l in self._ax.lines()
                 if l.in_legend and not l.label.startswith("_") and l not in standins]
        return lines, [l.label for l in lines]

    def get_xticklabels(self, *_, **__):
        return [_TickLabel(self, "x", t) for t in self._tick_texts("x")]

    def get_yticklabels(self, *_, **__):
        return [_TickLabel(self, "y", t) for t in self._tick_texts("y")]

    def _tick_texts(self, axis):
        t = getattr(self._ax, "get_" + axis + "ticks")()
        if t is not None and len(t[1]):
            return list(t[1])
        if t is None:  # automatic ticks: the labels as drawn
            return list(self._ax._drawn_ticks(axis == "x", *self.figure._size)[1])
        return [str(v) for v in t[0]]

    def clabel(self, *_, **__):
        """Contour labels are not drawn (accepted for compatibility)."""
        return []

    def bar_label(self, container, labels=None, fmt="%g", padding=0, **kw):
        """Value labels at the end of each bar of a bar() result."""
        out = []
        horizontal = getattr(container, "_horizontal", False)
        errors = container.__dict__.get("_errors") if hasattr(container, "__dict__") else None
        style = {k: v for k, v in kw.items() if k in ("fontsize", "color")}
        for i, r in enumerate(container):
            v = r.get_width() if horizontal else r.get_height()
            text = labels[i] if labels is not None else (fmt % v if "%" in fmt else fmt.format(v))
            lo_err, hi_err = (float(errors[i][0]), float(errors[i][1])) if errors is not None and i < len(errors) \
                else (0.0, 0.0)
            end = v + hi_err if v >= 0 else v - lo_err  # beyond the error bar, as matplotlib
            if horizontal:  # at the end of the bar, vertically centred
                out.append(self.text(r.get_x() + end, r.get_y() + r.get_height() / 2, " " + text if v >= 0 else text + " ",
                                     ha="left" if v >= 0 else "right", va="center", **style))
            else:
                out.append(self.text(r.get_x() + r.get_width() / 2, r.get_y() + end, text, ha="center",
                                     va="bottom" if v >= 0 else "top", **style))
        return out

    def tick_params(self, axis="both", **kw):
        """Label rotation (rotation= / labelrotation=) is applied; other tick
        appearance comes from the style ('xtick.*', 'ytick.*')."""
        for a in ("x", "y"):
            if axis in (a, "both"):
                self._rotate(a, kw)

    @property
    def panel(self):
        """The underlying plotter Axes (full plotter API: peaks, regions, ...)."""
        return self._ax


class _PltFigure:
    def __init__(self, figsize=None, num=None):
        w, h = figsize if figsize is not None else _DEFAULT_FIGSIZE
        self._fig = _plotter.figure(size=(int(w * _DPI), int(h * _DPI)))
        self.number = num
        if num is not None:  # matplotlib: the number / name is the window title only
            self._fig.window_title = f"Figure {num}" if isinstance(num, int) else str(num)
        self._size = (int(w * _DPI), int(h * _DPI))
        self.axes = [PltAxes(self, self._fig.axes[0])]

    @property
    def closed(self) -> bool:
        """True once the window was closed (by code or by the user)."""
        return self._fig._closed

    def gca(self):
        return next(a for a in self.axes if a.panel == self._fig.gca())

    def savefig(self, fname, dpi=None, format=None, **_):  # noqa: A002
        """Save as png / jpg / svg / pdf. ``fname`` may be a path or a binary file object;
        a name without an extension gets ``.png`` (matplotlib's default).

        dpi defaults to 100 (the figure-size convention), as in matplotlib;
        bbox_inches, transparent, facecolor, ... are accepted and ignored."""
        scale = (dpi or _DPI) / _DPI
        if hasattr(fname, "write"):  # file object: export to a temporary file, copy the bytes
            import io
            import os
            import tempfile
            ext = (format or "png").lower()
            fd, tmp = tempfile.mkstemp(suffix="." + ext)
            os.close(fd)
            try:
                self._fig.savefig(tmp, width=self._size[0], height=self._size[1], scale=scale)
                with open(tmp, "rb") as f:
                    data = f.read()
                # Text streams (io.StringIO for SVG) get text, binary ones bytes.
                fname.write(data.decode("utf-8") if isinstance(fname, io.TextIOBase) else data)
            finally:
                os.remove(tmp)
            return
        fname = str(fname)
        if format and not fname.lower().endswith("." + format.lower()):
            # Exactly this path, in the given format (as matplotlib): export
            # with the format's extension, then move into place.
            tmp = fname + "." + format.lower()
            self._fig.savefig(tmp, width=self._size[0], height=self._size[1], scale=scale)
            _os.replace(tmp, fname)
            return
        if not _os.path.splitext(fname)[1]:
            fname += ".png"
        self._fig.savefig(fname, width=self._size[0], height=self._size[1], scale=scale)

    def add_subplot(self, *args, **_):
        """add_subplot(111), add_subplot(2, 1, 2), add_subplot((2, 1, 2))."""
        if len(args) == 1 and isinstance(args[0], int) and 111 <= args[0] <= 999:
            args = tuple(int(c) for c in str(args[0]))
        elif len(args) == 1 and isinstance(args[0], tuple):
            args = args[0]
        rows, cols, index = (args + (1, 1, 1))[:3] if args else (1, 1, 1)
        current = getattr(self, "_grid", (1, 1))
        if (rows, cols) != current:
            used = any(ax.get_lines() or ax.panel.contours() for ax in self.axes)
            if used and current != (1, 1):
                raise ValueError(f"subplot({rows}, {cols}, ...): this figure already uses a "
                                 f"{current[0]}x{current[1]} grid; mixing grid shapes (e.g. 221 with "
                                 f"212) is not supported - use plt.subplots() or one shape")
            grid = self._fig.subplots(rows, cols, squeeze=False)
            self.axes = [PltAxes(self, a) for row in grid for a in row]
            for i, a in enumerate(self.axes):
                a._grid_pos = (rows, cols, i)
            self._grid = (rows, cols)
        if not 1 <= index <= rows * cols:
            raise ValueError(f"subplot index {index} is outside a {rows}x{cols} grid")
        ax = self.axes[index - 1]
        self._fig.sca(ax.panel)
        return ax

    def suptitle(self, s, **_):
        self._fig.suptitle = str(s)  # size from the style (axes.titlesize)

    def autofmt_xdate(self, bottom=0.2, rotation=30, ha="right", which=None):
        """Rotate the x tick labels of the bottom panels (date labels)."""
        for a in self.axes:
            rows, cols, i = getattr(a, "_grid_pos", (1, 1, 0))
            if i // cols == rows - 1 and not hasattr(a, "_twin_of"):
                a._ax.x_tick_rotation = float(rotation)

    def get_axes(self):
        return list(self.axes)

    def supxlabel(self, s, **_):
        """x label shared by the panels: one label under the bottom row (centred
        on the figure); the panels' own labels stay."""
        self._fig.supxlabel = str(s)

    def supylabel(self, s, **_):
        """y label shared by the panels: one label left of the left column."""
        self._fig.supylabel = str(s)

    @property
    def canvas(self):
        """matplotlib's fig.canvas for animation loops: draw / draw_idle / flush_events."""
        return _Canvas()

    def set_size_inches(self, w, h=None, forward=True):
        if h is None:
            w, h = w
        self._size = (int(w * _DPI), int(h * _DPI))

    def get_size_inches(self):
        return _np.array([self._size[0] / _DPI, self._size[1] / _DPI])

    def clf(self):
        """Clear the figure: back to one empty panel without titles, labels,
        custom ticks or log scales (as matplotlib's clf)."""
        for ax in self.axes:
            ax.cla()
        self._fig.supxlabel = self._fig.supylabel = ""
        if getattr(self, "_grid", (1, 1)) != (1, 1):
            self._fig.subplots(1, 1)
            self._grid = (1, 1)
        self._fig.suptitle = ""
        self.axes = [PltAxes(self, self._fig.axes[0])]
        self._fig.sca(self._fig.axes[0])

    def legend(self, *args, **kw):
        """Figure legend: shown on the first panel (plotter legends live inside a
        panel); it also lists the lines of that panel's twinx axis."""
        host = self.axes[0]
        twins = [a for a in self.axes[1:] if getattr(a, "_twin_of", None) is host]
        if twins and not args and "handles" not in kw:
            host._ax.legend_with_twin = True
            for t in twins:
                t._ax.legend(False)
        return host.legend(*args, **kw)

    def colorbar(self, mappable=None, ax=None, label=None, **kw):
        target = ax if ax is not None else self.gca()
        return target.colorbar(mappable, label=label, **kw)

    def text(self, x, y, s, **kw):
        """Text at figure fraction (x, y) (approximated on the first panel)."""
        ax = self.axes[0]
        return ax.text(x, y, s, transform=ax.transAxes, **kw)

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
    if num is None:  # matplotlib numbers every figure: the next free number
        num = max([k for k in _numbered if isinstance(k, int)], default=0) + 1
    _current = _PltFigure(figsize=figsize, num=num)
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
    share = lambda v: v if isinstance(v, str) else bool(v)  # noqa: E731 - 'col', 'row', 'all', 'none' pass through
    grid = fig._fig.subplots(nrows, ncols, sharex=share(sharex), sharey=share(sharey),
                             height_ratios=hr, width_ratios=wr, hspace=px(hspace),
                             wspace=px(wspace), squeeze=False)
    axes = [[PltAxes(fig, a) for a in row] for row in grid]
    fig.axes = [a for row in axes for a in row]
    for i, a in enumerate(fig.axes):
        a._grid_pos = (nrows, ncols, i)  # for label_outer / supxlabel
    fig._grid = (nrows, ncols)
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
    """Numbers / names of the open pyplot figures."""
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
        # Axes with limits set by the script (here or on a shared partner) stay.
        self._ax.fit_following()
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


hlines = _ax_call("hlines")
vlines = _ax_call("vlines")
axvspan = _ax_call("axvspan")
axhspan = _ax_call("axhspan")
fill_betweenx = _ax_call("fill_betweenx")
minorticks_on = _ax_call("minorticks_on")
minorticks_off = _ax_call("minorticks_off")


def twinx(ax=None):
    return (ax or gca()).twinx()


def figtext(x, y, s, **kw):
    return gcf().text(x, y, s, **kw)


class _RcParams(dict):
    """Minimal matplotlib.rcParams: the common keys map to plotter style keys
    (applied to new figures and the current one); others are kept and ignored."""

    _MAP = {"font.size": "font.size", "font.family": "font.family", "axes.labelsize": "axes.labelsize",
            "axes.titlesize": "axes.titlesize", "axes.linewidth": "axes.linewidth",
            "axes.grid": "axes.grid", "lines.linewidth": "lines.linewidth",
            "lines.markersize": "lines.markersize", "legend.fontsize": "legend.fontsize",
            "legend.frameon": "legend.frameon", "xtick.labelsize": "xtick.labelsize",
            "ytick.labelsize": "ytick.labelsize", "xtick.direction": "xtick.direction",
            "ytick.direction": "ytick.direction", "xtick.minor.visible": "xtick.minor.visible",
            "ytick.minor.visible": "ytick.minor.visible", "axes.prop_cycle": "axes.prop_cycle"}

    # matplotlib's named font sizes, relative to font.size.
    _SIZES = {"xx-small": 0.579, "x-small": 0.694, "small": 0.833, "medium": 1.0,
              "large": 1.2, "x-large": 1.44, "xx-large": 1.728, "smaller": 0.833, "larger": 1.2}

    def _convert(self, key, value):
        if key == "axes.prop_cycle":  # cycler(color=[...]) or a list of colours
            colors = value.by_key().get("color") if hasattr(value, "by_key") else value
            if not colors:
                raise ValueError("axes.prop_cycle: only colour cycles are supported")
            out = []
            for c in colors:
                c = _mpl_color(c)
                if not isinstance(c, str):
                    r, g, b, a = _plotter._plotter._rgba(c)
                    c = "#%02x%02x%02x" % (round(r * 255), round(g * 255), round(b * 255))
                out.append(c)
            return ",".join(out)
        if key == "font.family" and not isinstance(value, str):
            value = list(value)[0] if len(value) else "sans-serif"  # first family of a list
        if key.endswith("size") and isinstance(value, str) and value.lower() in self._SIZES:
            try:
                base = float(style.get("font.size"))
            except (KeyError, ValueError):
                base = 10.0
            value = round(base * self._SIZES[value.lower()], 2)
        return value

    def __getitem__(self, key):
        if key == "axes.prop_cycle" and not dict.__contains__(self, key):
            return _ColorCycle(c.strip() for c in style.get("axes.prop_cycle").split(","))
        if not dict.__contains__(self, key) and key in self._MAP:
            try:
                return style.get(self._MAP[key])
            except KeyError:
                pass
        return super().__getitem__(key)

    def get(self, key, default=None):
        try:
            return self[key]
        except KeyError:
            return default

    def __setitem__(self, key, value):
        if key == "figure.figsize":
            global _DEFAULT_FIGSIZE
            w, h = value
            _DEFAULT_FIGSIZE = (float(w), float(h))
        elif key == "font.size":
            # As in matplotlib, the other text sizes follow font.size (unless
            # set explicitly here): scale them by the same factor.
            new = float(self._convert(key, value))
            try:
                old = float(style.get("font.size"))
            except (KeyError, ValueError):
                old = 10.0
            changes = {"font.size": new}
            if old > 0:
                for k in ("axes.labelsize", "axes.titlesize", "xtick.labelsize", "ytick.labelsize",
                          "legend.fontsize"):
                    if not dict.__contains__(self, k):
                        changes[k] = round(float(style.get(k)) * new / old, 2)
            style.set(changes)
            if _current is not None and not _current.closed:
                _current._fig.set_style(changes)
            value = new
        elif key in self._MAP:
            original = value
            value = self._convert(key, value)
            style.set({self._MAP[key]: value})  # raises for invalid values: nothing is stored
            if _current is not None and not _current.closed:
                _current._fig.set_style({self._MAP[key]: value})
            if key == "axes.prop_cycle":
                value = original  # read back as given (e.g. a cycler)
        super().__setitem__(key, value)

    def update(self, *args, **kw):
        for k, v in dict(*args, **kw).items():
            self[k] = v


class _ColorCycle(list):
    """The colour cycle as a list, with cycler's by_key()."""

    def by_key(self):
        return {"color": list(self)}


rcParams = _RcParams()
_DEFAULT_FIGSIZE = (8.0, 6.0)


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
    ax._rotate("x", kw)
    t = ax.panel.get_xticks()
    return (ax.get_xticks(), [] if t is None else t[1])


def yticks(ticks=None, labels=None, **kw):
    """yticks() -> (positions, labels); yticks(ticks, labels=None) sets them."""
    ax = gca()
    if ticks is not None or labels is not None:
        ax.set_yticks(ticks if ticks is not None else ax.get_yticks(), labels)
    ax._rotate("y", kw)
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


# ---- matplotlib Line2D methods on plotter's Line handles ------------------------------
_pending_data = {}  # line -> (axis, values) waiting for the other array of a new length


def _set_one(line, axis, values):
    """set_xdata / set_ydata: a new length waits for the matching other array
    (matplotlib checks only when drawing: set_xdata(x3); set_ydata(y3) works)."""
    values = _np.asarray(values)
    other = line.y if axis == "x" else line.x
    pending = _pending_data.pop(line, None)
    if pending is not None and pending[0] != axis and _np.size(pending[1]) == values.size:
        other = pending[1]
    if _np.size(other) != values.size:
        _pending_data[line] = (axis, values)
        return
    x, y = (values, other) if axis == "x" else (other, values)
    line.set_data_native(x, y)
def _add_line2d_methods():
    Line = _plotter._plotter.Line

    def set_data(self, x, y=None, **kw):  # also matplotlib's set_data((x, y))
        if y is None:
            x, y = x
        self.set_data_native(x, y, **kw)

    methods = {
        "get_xdata": lambda self, orig=True: self.x,
        "get_ydata": lambda self, orig=True: self.y,
        "get_data": lambda self, orig=True: (self.x, self.y),
        "set_xdata": lambda self, x: _set_one(self, "x", x),
        "set_ydata": lambda self, y: _set_one(self, "y", y),
        "get_color": lambda self: self.color,
        "set_color": lambda self, c: setattr(self, "color", _mpl_color(c)),
        "get_label": lambda self: self.label,
        "set_label": lambda self, s: setattr(self, "label", "" if s is None else str(s)),
        "get_linestyle": lambda self: {"solid": "-", "dash": "--", "dot": ":", "dashdot": "-.",
                                       "none": "None"}.get(self.style, self.style),
        "set_linestyle": lambda self, ls: setattr(self, "style", ls),
        "get_linewidth": lambda self: self.width,
        "set_linewidth": lambda self, w: setattr(self, "width", float(w)),
        "get_visible": lambda self: self.visible,
        "set_visible": lambda self, v: setattr(self, "visible", bool(v)),
        "set_marker": lambda self, m: setattr(self, "marker", _FMT_MARKERS.get(m, m)),
        "set_markerfacecolor": lambda self, c: setattr(self, "markerfacecolor",
                                                       "none" if c == "none" else _mpl_color(c)),
        "set_markeredgecolor": lambda self, c: setattr(self, "markeredgecolor",
                                                       "none" if c == "none" else _mpl_color(c)),
    }
    if not hasattr(Line, "set_data_native"):
        Line.set_data_native = Line.set_data
        Line.set_data = set_data
    for name, fn in methods.items():
        if not hasattr(Line, name):
            setattr(Line, name, fn)


_add_line2d_methods()


def subplots_adjust(*args, **kw):
    """Accepted for compatibility (the layout is automatic)."""
    gcf().subplots_adjust(*args, **kw)


class _ErrorbarContainer(tuple):
    """matplotlib's (data line, caplines, barlinecols); attributes go to the line."""

    def __new__(cls, line):
        return super().__new__(cls, (line, (), ()))

    @property
    def lines(self):
        return tuple(self)

    def __getattr__(self, name):
        return getattr(self[0], name)


class _Patches(list):
    """hist's patches: the drawn histogram(s); attributes go to the first."""

    def __getattr__(self, name):
        return getattr(self[0], name)


class _Legend:
    def __init__(self, ax):
        self._ax = ax

    def set_visible(self, on):
        self._ax.legend(bool(on))

    def get_visible(self):
        return self._ax.legend_visible

    def remove(self):
        self._ax.legend(False)


class _Canvas:
    """Drawing is automatic; these only let pending window events run."""

    def draw(self):
        _plotter._plotter.process_events(0)

    draw_idle = draw
    flush_events = draw


def axes(*args, **kw):
    """plt.axes(): the current panel (a new figure if there is none)."""
    return gca()


def _add_handle_methods():
    native = _plotter._plotter
    native.Contour.set_label = lambda self, s: setattr(self, "label", "" if s is None else str(s))
    native.Contour.get_label = lambda self: self.label
    A = native.Annotation
    A.set_text = lambda self, s: setattr(self, "text", str(s))
    A.get_text = lambda self: self.text
    A.set_position = lambda self, xy: setattr(self, "position", (float(xy[0]), float(xy[1])))
    A.get_position = lambda self: self.position
    A.set_x = lambda self, x: setattr(self, "position", (float(x), self.position[1]))
    A.set_y = lambda self, y: setattr(self, "position", (self.position[0], float(y)))
    A.set_color = lambda self, c: setattr(self, "color", _mpl_color(c))
    A.set_fontsize = lambda self, size: setattr(self, "fontsize", float(size))
    native.Line.set_facecolor = lambda self, c: setattr(self, "fill", _mpl_color(c))


_add_handle_methods()


class _Rect:
    """One bar of a bar chart / histogram (matplotlib's Rectangle API subset)."""

    def __init__(self, group, i):
        self._group, self._i = group, i

    def get_x(self):
        return self._group._rects[self._i][0]

    def get_y(self):
        return self._group._rects[self._i][1]

    def get_width(self):
        return self._group._rects[self._i][2]

    def get_height(self):
        return self._group._rects[self._i][3]

    @property
    def label(self):
        return self._group._line.label

    @label.setter
    def label(self, s):
        self._group._line.label = s

    def get_xy(self):
        return (self.get_x(), self.get_y())

    def get_facecolor(self):
        return self._group._line_of(self._i).fill

    def set_facecolor(self, c):
        line = self._group._separate(self._i)
        line.fill = _mpl_color(c)
        line.color = _mpl_color(c)

    set_color = set_facecolor

    def set_edgecolor(self, c):
        pass  # bars are drawn without an outline

    def set_alpha(self, a):
        line = self._group._separate(self._i)
        r, g, b, _ = _plotter._plotter._rgba(line.fill)
        line.fill = (r, g, b, float(a))

    def set_hatch(self, *_):
        pass

    def set_label(self, s):
        self._group._line_of(self._i).label = str(s)


class _RectGroup(list):
    """bar() / hist() result: one _Rect per bar (iterate, index, get_height ...).
    All bars are one shape until a single bar is restyled; then each bar
    becomes its own shape. Other attributes (label, color, remove, ...) act
    on the drawn shape(s), so it can also be used like the Line it was."""

    _own = ("_ax", "_line", "_rects", "_lines", "_label", "_horizontal", "_errors")

    def __init__(self, ax, line, rects, lines=None, label=None, horizontal=False):
        super().__init__(_Rect(self, i) for i in range(len(rects)))
        object.__setattr__(self, "_ax", ax)
        object.__setattr__(self, "_line", line)
        object.__setattr__(self, "_rects", rects)
        object.__setattr__(self, "_lines", lines)
        object.__setattr__(self, "_label", label)
        object.__setattr__(self, "_horizontal", horizontal)

    def _line_of(self, i):
        return self._lines[i] if self._lines else self._line

    def _separate(self, i):
        if self._lines is None:
            src = self._line
            fill, color, style, width = src.fill, src.color, src.style, src.width
            src.remove()
            lines = []
            for k, (x0, y0, w, h) in enumerate(self._rects):
                xs, ys = PltAxes._bar_outline([x0], [w], [h], [y0], True)
                # Only a label the user gave goes to the legend (once).
                lab = self._label if k == 0 and self._label else None
                lines.append(self._ax._ax.plot(xs, ys, fill=fill, color=color, style=style, width=width,
                                               label=lab))
            object.__setattr__(self, "_lines", lines)
            object.__setattr__(self, "_line", lines[0])
        return self._lines[i]

    def _all_lines(self):
        return list(self._lines) if self._lines else [self._line]

    def remove(self, *args):
        """remove(): take the bars off the plot (list.remove(x) with an argument)."""
        if args:
            return list.remove(self, *args)
        for line in self._all_lines():
            line.remove()

    @property
    def patches(self):
        return list(self)

    def __getattr__(self, name):
        return getattr(self._line, name)

    def __setattr__(self, name, value):
        if name in self._own:
            object.__setattr__(self, name, value)
        elif name == "label":
            self._line.label = value
        else:
            for line in self._all_lines():
                setattr(line, name, value)


class _TickLabel:
    """A tick label (matplotlib Text subset): set_rotation / set_ha apply to the axis."""

    def __init__(self, ax, axis, text):
        self._ax, self._axis, self._text = ax, axis, text

    def get_text(self):
        return self._text

    def set_rotation(self, deg):
        self._ax._rotate(self._axis, {"rotation": deg})

    def set_ha(self, *_):
        pass

    set_horizontalalignment = set_ha

    def set_fontsize(self, *_):
        pass


class _Colormap(str):
    """A colour map: its name (plotter takes names everywhere) and, as in
    matplotlib, callable on values in 0..1 -> RGBA rows."""

    def __call__(self, x, alpha=None):
        cols = _plotter._plotter._colormap_colors(str(self), 256)
        v = _np.asarray(x, dtype=float)
        if v.dtype.kind == "f" and not _np.issubdtype(_np.asarray(x).dtype, _np.integer):
            idx = _np.clip((v * 255).round(), 0, 255).astype(int)
        else:
            idx = _np.clip(v, 0, 255).astype(int)
        rgba = _np.array([[int(c[1:3], 16) / 255, int(c[3:5], 16) / 255, int(c[5:7], 16) / 255, 1.0]
                          for c in cols])[idx]
        if alpha is not None:
            rgba[..., 3] = alpha
        return tuple(rgba) if rgba.ndim == 1 else rgba


class _ColormapNames:
    """plt.cm.viridis etc."""

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        return _Colormap(name)


cm = _ColormapNames()


def get_cmap(name=None, lut=None):
    return _Colormap(name or "viridis")


def tick_params(axis="both", **kw):
    gca().tick_params(axis=axis, **kw)
