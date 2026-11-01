# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""Style control: every visual setting comes from a style, never from code.

Global default (for figures created afterwards)::

    import plotter
    plotter.style.use("science")              # built-in: default, science, nmr, presentation
    plotter.style.use("~/styles/thesis.gpstyle")
    plotter.style.set({"font.size": 12, "xtick.minor.visible": True})
    plotter.style.get("axes.linewidth")
    plotter.style.save("my.gpstyle")          # complete, commented style file
    plotter.style.save_as_default()           # loaded automatically in every session

Per figure (undoable)::

    fig.use_style("science")
    fig.set_style({"label.top.left": "**CMS** *Preliminary*",
                   "label.top.right": "138 fb$^{-1}$ (13 TeV)"})

Style files are plain text, one ``key: value`` per line, '#' comments, and an
optional ``inherit: <built-in or file>`` line. Run ``plotter.style.keys()``
for all keys with descriptions. Your default style file lives at
``plotter.style.default_path()`` (override with $GRAPHPLOT_STYLE).
"""
from __future__ import annotations

import os as _os
from collections.abc import Iterable, Mapping

from . import _plotter


def _to_text(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (list, tuple)) and not (len(value) in (3, 4) and all(
            isinstance(v, (int, float)) for v in value)):
        return ", ".join(_to_text(v) for v in value)
    if isinstance(value, (tuple, list)):  # (r, g, b[, a]) colour
        scale = 255 if all(v <= 1 for v in value) else 1
        vals = [int(round(v * scale)) for v in value]
        return "#" + "".join(f"{v:02x}" for v in ([vals[3]] if len(vals) == 4 else []) + vals[:3])
    return str(value)


def available() -> list[str]:
    """Names of the built-in styles."""
    return list(_plotter._style_builtins())


def keys() -> dict[str, str]:
    """All style keys with their descriptions."""
    return dict(_plotter._style_keys())


def use(name_or_path: str) -> list[str]:
    """Make a built-in style or a .gpstyle file the default for new figures.

    Returns the list of ignored/invalid entries (empty when all were valid)."""
    if name_or_path not in available():
        name_or_path = _os.path.expanduser(name_or_path)
    return list(_plotter._style_use(name_or_path))


def get(key: str) -> str:
    return _plotter._style_get(key)


def set(params: Mapping | None = None, /, **kwargs) -> None:  # noqa: A001
    """Change default-style keys: set({"font.size": 12}) or set(font_size=12).

    In keyword form, '_' is read as '.' (use the dict form for keys that
    contain underscores, e.g. "axes.unicode_minus")."""
    items = dict(params or {})
    items.update({k.replace("_", "."): v for k, v in kwargs.items()})
    for k, v in items.items():
        _plotter._style_set(k, _to_text(v))


def text() -> str:
    """The current default style as file text."""
    return _plotter._style_text()


def save(path: str) -> None:
    _plotter._style_save(_os.path.expanduser(path))


def default_path() -> str:
    """Location of the style file loaded automatically at start-up."""
    return _plotter._style_default_path()


def save_as_default() -> str:
    """Save the current default style so every new session starts with it."""
    path = default_path()
    _os.makedirs(_os.path.dirname(path), exist_ok=True)
    save(path)
    return path


def reload() -> list[str]:
    """Reset to the built-in default plus your default style file."""
    return list(_plotter._style_reload())


def _figure_set_style(fig, params: Mapping | Iterable | None = None, /, **kwargs) -> None:
    """Change style keys of one figure (one undo step)."""
    items = dict(params or {})
    items.update({k.replace("_", "."): v for k, v in kwargs.items()})
    fig._set_style_params([(k, _to_text(v)) for k, v in items.items()])
