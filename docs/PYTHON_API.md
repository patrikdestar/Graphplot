# Python API (`plotter`)

```python
import plotter
fig = plotter.figure(title=None, size=(1000, 650))
```

Contents: [Module functions](#module-functions) ·
[Figure](#figure) ([data](#data), [peaks](#peaks-2d-spectra),
[styles](#styles-figure-wide-undoable), [panels](#multi-panel-figures-and-axes),
[selection and events](#selection-and-interaction),
[view and output](#view-labels-history-output)) ·
[Timeline](#timeline-sensor-logs) · [pyplot](#matplotlib-compatibility-plotterpyplot) ·
[Handles](#handles) · [Event loop and threads](#event-loop-and-threads)

New to plotter? Start with the tutorial in the
[README](../README.md#getting-started-your-first-plot-from-python).
File paths may be strings with `~` or `pathlib.Path` objects.

## Module functions

| Function | Description |
|---|---|
| `figure(title=None, size=(w, h))` | Create a figure window. Call it from the main thread. |
| `show(block=None, timeout=None)` | Show all figures. Blocks until all are closed unless running interactively. |
| `pause(seconds=0.05)` | Run the event loop for a while (keeps windows responsive in loops). |
| `figures()` | Figures that have not been closed (including ones not shown yet). |
| `close_all()` | Close every figure. |
| `load(path)` | Open a figure saved with `fig.save()` / *File → Save figure* (`.gpfig`); returns a `Figure`, or a `Timeline` for timeline figures (call `show()` to see it). |
| `theme_options()` | `{"plots": bool, "exports": bool}` (see `theme`). |
| `theme(name=None, *, plots=None, exports=None, save=False)` | Get or set the display theme of all windows (`'system'`, `'light'`, `'dark'`, `'dim'`, `'midnight'`, `'solarized-dark'`, `'high-contrast'`); `plots`: dark themes colour the plots on screen (default on); `exports`: exports use the theme too (default off, so files stay light); `save=True` remembers it like the menu does. Returns the theme id. |
| `themes()` | `{id: description}` of the available themes. |
| `tooltips(on=None, *, save=False)` | Get or switch the explanations shown when the mouse rests on a control. |

## `Figure`

### Data

| Method | Returns |
|---|---|
| `plot(x, y=None, *, label, color, width, style='-', marker=None, markersize, visible=True, replace=False, step=None, fill=None, fill_to=None, xerr=None, yerr=None)` | `Line` |
| `errorbar(x, y, yerr=None, xerr=None, *, label, color, marker='o', markersize, style='none', width)` | `Line` (points with error bars; `style='-'` also joins them) |
| `hist(data, bins=10, range=None, *, density=False, weights=None, histtype='step', label, color, fill, width, style)` | `(counts, edges, Line)` |
| `fill_between(x, y1, y2=0, *, label, color, alpha=0.3, step=None, line=False)` | `Line` (one undo step; `line=True` also draws the edges) |
| `contour(z, x=None, y=None, *, label, levels, factor, base=None, color, negative_color, negative=True, image=False, linewidth)` | `Contour` |
| `image(z, x=None, y=None, *, cmap='viridis', colorbar=True, vmin=None, vmax=None, label=None)` | `Contour` drawn as a colour-mapped image only (heat map) |
| `open_bruker(path)` | `Contour` (Bruker processed 2D spectrum, ppm axes) |
| `marker(position, orientation='v', *, color, label='', style='--', width=1.5, show_value=True)` | `Marker` |
| `vline(x, **kw)`, `hline(y, **kw)` | `Marker` |
| `annotate(text, x, y, *, color=None, fontsize=None)` | `Annotation` (unset colour/size follow the style) |
| `lines()`, `contours()`, `markers()`, `annotations()` | lists of handles |
| `line(label)` | the `Line` with that label |
| `get_marker_positions()` | `[(label, 'v'/'h', position), …]` |
| `remove(handle)`, `clear()` | remove objects (undoable) |

Argument formats:

- **Colours:**
  - Short and basic names map to the (colour-blind-safe) palette tones, so
    that they match the default colours: `'b'`/`'blue'`, `'orange'`,
    `'teal'`/`'aqua'`, `'y'`/`'yellow'`, `'m'`/`'magenta'`, `'g'`/`'green'`,
    `'purple'`/`'violet'`, `'r'`/`'red'`, `'k'`/`'black'`, `'w'`, `'c'`/`'cyan'`,
    `'gray'`. So `'red'` is `#e34948`, not pure `#ff0000`; write the hex code
    for an exact colour.
  - `'C0'`, `'C1'`, …: the n-th palette colour.
  - Every other SVG colour name (`'navy'`, `'crimson'`, …) as defined by SVG.
  - `'#rrggbb'` or `'#aarrggbb'`: **alpha comes first** (Qt convention), unlike
    matplotlib's `#rrggbbaa`. For transparency, a tuple is clearer.
  - An `(r, g, b[, a])` tuple: RGB as 0–1 floats or 0–255 ints; alpha 0–1.
  - `None` (when creating an object) means the next palette colour.
- **Line styles:** `'-'`/`'solid'`, `'--'`/`'dashed'`, `':'`/`'dotted'`,
  `'-.'`/`'dashdot'`, `'none'`.
- **Symbols:** `'o'`, `'s'`, `'^'`, `'d'`, `'x'`, `'+'`.

`plot(..., replace=True)` updates the dataset with the same label in place,
or adds a new one if no such dataset exists.
Unset sizes and colours come from the style (`lines.linewidth`,
`axes.prop_cycle`, …). Only labelled lines appear in the legend, as in
matplotlib.

Plotting options:

- `step`: `'pre'`, `'post'` or `'mid'`.
- `fill`: `True`, which uses the line colour at 30 % opacity, or any colour.
  The area is filled down to y = 0, or towards another Line given as
  `fill_to` (a band).
- `xerr`, `yerr`: a scalar, a length-n array, or a `(2, n)` array for
  (low, high).

`hist`:

- It follows `numpy.histogram` semantics.
- `histtype` is `'step'`, `'stepfilled'` or `'errorbar'`. The last draws
  points at the bin centres with Poisson errors √(Σw²), scaled correctly for
  `density=True`.

`contour`:

- `z` is indexed `z[row = y, column = x]`, as in NumPy and matplotlib.
- `x` and `y` give the grid coordinates, either as full uniformly spaced
  arrays or as `(start, stop)`.
- The positive levels are `base * factor**k` for `k = 0 … levels-1`. With
  `negative=True`, they are mirrored for negative values.
- `base=None` places the top level just below `max |z|`.
- Images: `image=True` draws a colour map under the contours, `contours=False`
  the image alone; `cmap` is `'auto'` (white–blue, or blue–white–red for
  signed data), `'viridis'`, `'magma'`, `'inferno'`, `'plasma'`, `'cividis'`,
  `'gray'`, `'greys'`, `'blues'`, `'reds'`, `'greens'`, `'oranges'`, `'purples'`, `'rdbu'`, `'coolwarm'`, `'bwr'`, `'seismic'`, `'hot'`, `'jet'` or `'turbo'` (`'_r'` reverses); `vmin`/`vmax` fix the
  colour limits (diverging maps are otherwise centred on zero);
  `colorbar=True` shows the scale beside the panel, labelled with the name.
  The `Contour` handle has matching properties: `cmap`, `contours`,
  `colorbar`, `clim` (`(vmin, vmax)` or `None`), `image`.

### Peaks (2D spectra)

| Method | Description |
|---|---|
| `pick_peaks(*, threshold=None, min_distance=3, negative=False, exclude_diagonal=0.0, region='view', contour=None)` | automatic picking; returns the *new* peaks (one undo step) |
| `add_peak(x, y, *, label='', comment='', snap=True)` | one peak (snaps to the nearest maximum within 3 points) |
| `peaks()` | all `Peak` handles |
| `peak_table()` | list of dicts (`x`, `y`, `intensity`, `label`, `comment`), e.g. for `pandas.DataFrame` |
| `export_peaks(csv)`, `import_peaks(csv)` | CSV with header `x,y,intensity,label,comment`; `import_peaks` returns how many peaks were added |
| `clear_peaks()`, `show_peak_table()` | |
| `peak_labels` | property: show labels on the plot |

How `pick_peaks` works:

- It finds local maxima above `threshold`. The default threshold is the
  lowest contour level, so the peaks you can see are the ones picked.
- Candidates closer than `min_distance` data points to a stronger peak are
  dropped.
- `exclude_diagonal` skips peaks with |x − y| below the given value. Use it
  to pick only off-diagonal cross peaks.
- `region` is `'view'` (the visible area), `'all'`, or `'selection'` (the
  selected X region).
- Positions are refined between grid points.
- Peaks that already exist are not added again.

### Axes appearance

| Property | Description |
|---|---|
| `x_reversed`, `y_reversed` | high values on the left / bottom (set automatically by `open_bruker`) |
| `x_log`, `y_log` | logarithmic axes |
| `diagonal` | dashed y = x line |

### Styles (figure-wide, undoable)

| Method | Description |
|---|---|
| `use_style(name_or_path)` | built-in (`default`, `science`, `nmr`, `presentation`) or `.gpstyle` file; returns the list of ignored entries |
| `set_style({key: value, ...}, **kw)` | change keys, all or nothing. In keyword form `_` stands for `.` (`font_size=12`); keys that contain `_` themselves (e.g. `axes.unicode_minus`) need the dict form |
| `get_style_param(key)` | current value as text |
| `save_style(path)` | write the figure's style as a `.gpstyle` file |

Module `plotter.style` (the default for *new* figures):

| Function | Description |
|---|---|
| `use(name_or_path)`, `set(...)` | like `use_style` / `set_style` (all or nothing) |
| `get(key)` | like `get_style_param` |
| `save(path)` | like `save_style` |
| `text()` | the default style as `.gpstyle` text |
| `available()` | built-in style names |
| `keys()` | all keys with descriptions |
| `save_as_default()` | writes your default style file |
| `default_path()` | where that file lives |
| `reload()` | re-reads the default style |

All keys: [STYLE_REFERENCE.md](STYLE_REFERENCE.md). In texts, `$...$` math
(`^{}`, `_{}`, Greek letters, `\to`, `\pm`, `\sqrt{}`, ...), `**bold**` and
`*italic*` are supported.

### Multi-panel figures and Axes

| Method | Description |
|---|---|
| `subplots(nrows=1, ncols=1, *, sharex, sharey, height_ratios, width_ratios, hspace, wspace, squeeze=True)` | list or grid of `Axes`; existing content stays in panel 0 |
| `ratio_panel(ratio=0.3, *, ylabel='Ratio')` | `(main, lower)` with shared x and touching frames |
| `axes`, `gca()`, `sca(ax)` | panels / current panel |
| `twinx()` (on a Figure: the current panel; on an Axes: that panel) | a second y axis on the right: a new panel drawn over the panel, sharing its x axis; returns its `Axes` (pyplot: `ax.twinx()`) |

How panels work:

- An `Axes` (`ax.figure`, `ax.index`) offers the same plotting methods and
  properties as `Figure` (`ax.plot`, `ax.hist`, `ax.xlabel = ...`,
  `ax.set_ylim`, `ax.pick_peaks`, `ax.region_stats`, …), applied to its panel.
- A call through an `Axes` always lands in that panel, even from a worker
  thread while the user clicks another panel; it also makes the panel current.
- Handles stay bound to the panel they were created in.
- Figure-level calls act on the current panel. That is the one used last, or
  the one clicked in the window.
- Styles (including the legend position) and the header labels are
  figure-wide.
- Calling `subplots` again with fewer panels hides the extra ones (their
  content is kept). A timeline figure arranges its own rows and refuses
  `subplots`.

### Selection and interaction

| Method | Description |
|---|---|
| `get_selected_region()` | `(xmin, xmax)` or `None` |
| `set_selected_region(xmin, xmax)`, `clear_selection()` | change it (undoable) |
| `region_mask(x)` | boolean mask of `x` inside the region (or `None`) |
| `region_stats(xmin=None, xmax=None)` | statistics of every visible line in the region (or the given range): `{label: {n, mean, std, min, max, x_at_min, x_at_max, rms, integral}}`. `std` is the sample standard deviation, `integral` the trapezoid rule over |Δx| (so descending ppm axes give positive areas); NaN gaps are skipped; with no point in the range `n` is 0 and the other values are NaN. Raises `ValueError` if no range is given and nothing is selected. |
| `wait_for_region(timeout=-1)` | keep the window interactive until the user finishes a selection; return it. Returns `None` on timeout or close; raises `RuntimeError` if the window is not shown and no timeout is given. |
| `scale_contour_levels(factor)` | same as the mouse wheel / `*` `/` keys |
| `on(event, callback)`, `off(event, callback)` | register / remove a callback (see below); `off` returns whether it was registered and also matches a bound method given again |
| `disconnect_all()` | remove callbacks |

Callbacks run on the GUI thread after an edit is *complete*: after the mouse
is released, not during a drag.

| Event | Callback signature |
|---|---|
| `'region_changed'` | `cb(xmin, xmax)`; both are `None` when the selection is cleared |
| `'marker_changed'` | `cb(marker)` |
| `'levels_changed'` | `cb(contour)` |
| `'closed'` | `cb()` |

### View, labels, history, output

| Member | Description |
|---|---|
| `xlim()`, `ylim()` | current limits (low, high) |
| `set_xlim(a, b)`, `set_ylim(a, b)` | set limits; `a > b` also reverses the axis (as in matplotlib), `a < b` keeps the direction; equal or non-finite limits raise `ValueError` |
| `autoscale(x=True, y=True)` | fit the data |
| `reset_view()`, `set_home_view()` | home view (also the `Home` key) |
| `title`, `xlabel`, `ylabel` | read/write properties; in multi-panel figures each panel draws its own title above its frame |
| `suptitle` | figure-wide title above all panels (`plt.suptitle` / `fig.suptitle` in pyplot) |
| `set_xticks(ticks, labels=None)`, `set_yticks(...)` | custom tick positions and optional text labels; `[]` hides the ticks, `None` restores automatic ticks (saved in figure files) |
| `get_xticks()`, `get_yticks()` | `(positions, labels)` of custom ticks, or `None` |
| `legend(visible=True)`, `grid(visible=True)` | toggles |
| `undo()`, `redo()` | the same history as Ctrl+Z / Ctrl+Shift+Z in the window |
| `save(filename)` | save everything (all panels' data, peaks with labels and comments, markers, annotations, region, styles, panel grid and views, timeline) as `.gpfig`; reopen with `plotter.load()` |
| `export_data(filename, *, region=False)` | the visible 1D data of the current panel as CSV (wide table when all lines share x, else one row per point; `region=True`: only the selected X region) |
| `_repr_png_()` | PNG snapshot: in Jupyter a figure as the last expression of a cell is shown inline |
| `savefig(filename, *, width=0, height=0, scale=2.0)` | `.png` / `.svg` / `.pdf`; size in figure pixels (default: the figure size), 100 px = 1 inch in every format; `scale` multiplies the PNG resolution |
| `screenshot(filename)` | image of the whole window |
| `show(block=None, timeout=None)`, `close()`, `is_open` | window control (`timeout` in seconds for a blocking show) |

## Timeline (sensor logs)

```python
tl = plotter.timeline(title=None, size=(1200, 800))
```

What a timeline is:

- Stacked rows share one time axis.
- A navigator strip with the visible window sits at the bottom.
- A cursor line through all rows shows the values at the cursor.
- Time markers (pins) are drawn through all rows.
- Times are seconds since `time_origin` (Unix epoch seconds) when the data
  has absolute timestamps.
- Anything not listed below is forwarded to the underlying `Figure`
  (`show`, `savefig`, `use_style`, `set_style`, `undo`, `on`, …).

| Member | Description |
|---|---|
| `load_csv(*files, row_per_column=False)` | CSV logs: time column + signal columns; one row per file (or per column) |
| `add_row(name)` | new `TimelineRow` |
| `add_signals(row_name, t, {"ax": ax, ...})` | row with several signals |
| `add_signals(row_name, t, {...}, colors=None)` | optional list of colours, one per signal |
| `rows`, `set_row_order(rows)` | display order (top to bottom); rows as `TimelineRow` or index |
| `remove_row(row)` | remove an **empty** row; returns `False` if it still holds signals |
| `move_signal(line, row)` | move a signal to another row (one undo step); returns the new `Line` |
| `window` | `(t0, t1)`, read/write |
| `full_range`, `show_all()` | |
| `cursor_time` | time under the cursor line, or `None`; writable (moves the line) |
| `values_at(t)` | `{(row name, signal): value}`, linearly interpolated (NaN in gaps); duplicate names get `" #2"`, … |
| `navigator` | signal shown in the bottom strip: a name (`"speed"` or `"gps / speed"`) or a `Line` |
| `auto_fit_y` | each row fits Y to the data in the window |
| `clock_time` | clock-time axis labels (needs absolute timestamps) |
| `time_origin` | Unix epoch seconds of t = 0 (`None` for relative data); writable (marks the data as absolute) |
| `pin(t, label="", color=None)` | add a time marker; returns a `TimePin` |
| `pins()`, `remove_pin(pin)`, `clear_pins()` | time markers (`clear_pins` is one undo step) |
| `next_pin()`, `prev_pin()` | centre the window on the next / previous marker (keys `]` / `[`); return it or `None` |
| `signals()`, `signal(name)` | `{"row / signal": Line}` (duplicates get `" #2"`, …); one signal by full or short name |
| `window_data(window=None, *, neighbours=False)` | `{"row / signal": (t, y)}` inside the visible (or given) window; `neighbours=True` adds one sample on each side |
| `export_csv(path, window=True, resample=None)` | CSV of the visible window (`False`: everything; or `(t0, t1)`). `resample=None`: long format, every sample; `resample=dt`: wide format on a common grid (linear interpolation up to the window edges). With absolute data a `time_utc` column is added |
| `format_time(t)` | time as displayed |

`TimelineRow` members:

- `plot(t, y, label="", color=None)`: `t` must be sorted. Seconds since the
  origin, or absolute times: `datetime64`, pandas timestamps or `datetime`
  objects (the earliest valid time of the first absolute signal sets
  `time_origin`). Naive datetimes are UTC, as in NumPy and pandas; NaT is a
  gap. Adding a signal never moves the visible window.
- `index`: the row's panel index.
- `lines()`.
- `name`, `visible`, `height` (relative).
- `axes`: the full panel API.

`TimePin` members:

- `id`; pins compare equal by id (`pin in tl.pins()`).
- `t`, `label`: read/write and undoable.
- `color` (read-only).
- `jump()`.
- `remove()`.

Events for `tl.on(...)`:

| Event | Callback signature |
|---|---|
| `'cursor_moved'` | `cb(t or None)` |
| `'window_changed'` | `cb(t0, t1)` |
| `'pins_changed'` | `cb()` |

Epoch times stored as double-precision seconds resolve to about 0.24 µs.

## matplotlib compatibility: `plotter.pyplot`

```python
import plotter.pyplot as plt
```

| Supported | Not supported |
|---|---|
| `figure`, `subplots` (incl. `gridspec_kw` height ratios / hspace), `subplot(211)`, `fig.add_subplot`, `clf`, `cla`, `suptitle`, `get_fignums`, `ion`/`ioff`/`draw` (no-ops), `fig.set_size_inches`, `fig.legend`, `gcf`, `gca`, `sca`, `close` (figure, number, name or `'all'`), `show`, `pause`, `savefig(dpi=, format=)` to a path or a binary file object | non-uniform `pcolormesh` grids |
| `plot` with format strings (`'r--o'`, `'C1--'`, several x/y/fmt groups), `step`, `bar` (categories, colour lists, `yerr`), `barh`, `semilogx`/`semilogy`/`loglog`, `axis([...])`, `hlines`/`vlines`, `axvspan`/`axhspan` (cover the range at call time), `fill_betweenx`, `scatter` (incl. `c=values, cmap=`), `errorbar(fmt=)`, `hist` (one or several datasets; `step`, `stepfilled`/`bar`, `errorbar`), `stairs` | polar and 3D axes |
| `fill_between` (incl. `where=`), `contour`/`contourf` (X, Y, Z), `imshow` (origin, extent, cmap, vmin/vmax), `pcolormesh` (uniform grids, edges or centres), `colorbar(mappable, label=)`, `axhline`, `axvline`, `text` (incl. `transform=ax.transAxes`), `annotate` | twin axes, RGB images |
| `xlabel`, `ylabel`, `title`, `xlim`, `ylim`, `xscale`, `yscale`, `legend(labels, loc=name or code, frameon=)`, `ax.set(...)`, `xticks`/`yticks`, `minorticks_on`/`off` (figure-wide), `figtext`, `rcParams` (common keys map to style keys), `ax.set_xticks`/`set_xticklabels`, `grid`, `plt.style.use`; colours `'C0'`…, `'tab:blue'`…, `'0.5'` | `rcParams` (use `plotter.style.set`) |

Axes methods use the matplotlib names (`set_xlabel`, `set_xlim`,
`set_yscale`, `invert_xaxis`, `get_xlim`, `autoscale`, …). `suptitle` sets the
figure-wide title; `tight_layout`, `relim` and `tick_params` are accepted
and do nothing (layout is automatic; ticks come from the style).
`figure(num=...)` returns the open figure with that number or name. `ax.panel` and `fig.native` give access to
the full plotter API (peaks, regions, undo, styles).

Differences from matplotlib:

- Figure sizes are converted at 100 px per inch; exports (PNG metadata, SVG,
  PDF) use the same scale, so `figsize=(8, 6)` is 8 × 6 inches in every format.
- The legend position and font are figure-wide style settings: in a
  multi-panel figure the last `legend(loc=...)` applies to all panels.
- Masked arrays are drawn with gaps; NaT / NaN values are gaps too.
- Contour levels are geometric (NMR convention).
- `hspace` / `wspace` (fractions of the panel size in matplotlib) are converted
  to pixels as fraction × 100; 0 still makes the frames touch.

## Handles

Handles refer to objects by id. They stay valid after undo/redo. Accessing a
removed object raises `KeyError`. Setting a property is an undoable edit.

### `Line`

- **Read-only:** `x`, `y` (NumPy copies), `yerr` (`(low, high)` or `None`),
  `exists`.
- **Properties:** `label`, `color`, `width`, `style`, `marker`, `visible`,
  `step`, `fill`.
- `set_data(x, y, *, undoable=False)`: by default, streaming updates do not
  fill the undo history.
- `append(x, y, *, max_points=None)`: add points (live data); `max_points`
  keeps only the newest points (a rolling window). Not in the undo history;
  drops error bars.
- `xerr`: `(low, high)` arrays or `None` (like `yerr`).
- `set_errors(xerr=None, yerr=None)`: replace the error bars (undoable).
- `remove()`.

### `Peak`

- **Read-only:** `x`, `y`, `intensity`.
- **Properties:**
  - `position` (`(x, y)`);
  - `label`, drawn next to the peak;
  - `comment`, never drawn, but exported with the peak list;
  - `label_offset` (pixels).
- `remove()`.

### `Contour`

- **Read-only:** `z` (2D array copy), `extent` (`(x0, x1, y0, y1)`).
- **Properties:** `label`, `base_level`, `factor`, `n_levels`, `negative`,
  `image`, `visible`, `color`, `negative_color`.
- `levels()`: all drawn levels, sorted.
- `scale_levels(factor)`: `factor < 1` reveals weaker features.
- `set_data(z, x=None, y=None)`: keeps the contour settings.
- `remove()`.

### `Marker`

- **Properties:** `position`, `label`, `color`, `style`, `width`.
- **Read-only:** `orientation` (`'v'` or `'h'`).
- `remove()`.

### `Annotation`

- **Properties:** `text`, `position` (`(x, y)`), `color`, `fontsize`.
- `remove()`.

## Event loop and threads

- **Interactive prompt:** at `python -i`, the plain REPL, or IPython after
  `%gui plotter`, windows stay live while you type, and `fig.show()` returns
  immediately.
- **Scripts:** `fig.show()` blocks until the window is closed. Use
  `show(block=False)` together with `plotter.pause()` or `fig.wait_for_region()`
  to interleave computation with interaction. Ctrl+C interrupts a blocking
  call.
- **Threads:**
  - Create figures on the main thread.
  - Methods may be called from worker threads. They are executed on the GUI
    thread, so they complete only while the main thread runs the event loop
    (prompt idle, `show()`, `pause()`, `wait_for_region()`).
  - Do not block the main thread on such a worker (for example with
    `thread.join()`) without running the event loop.
- **Jupyter notebooks:** a figure that is the last expression of a cell is
  shown inline as an image (`_repr_png_`). Interactive windows need a Qt
  event loop: call `plotter.show()` (blocking) or `plotter.pause()`; there is
  no automatic kernel integration (terminal IPython uses `%gui plotter`).
- **Text arguments** (labels, titles, comments) accept `None` (empty) and any
  object, converted with `str()`.
- **Data copies:** arrays are copied on the way in and on the way out, so
  later NumPy changes do not affect the plot until you send the data again.
