# Python API (`plotter`)

```python
import plotter
fig = plotter.figure(title=None, size=(1000, 650))
```

## Module functions

| Function | Description |
|---|---|
| `figure(title=None, size=(w, h))` | Create a figure window. Call it from the main thread. |
| `show(block=None, timeout=None)` | Show all figures. Blocks until all are closed unless running interactively. |
| `pause(seconds=0.05)` | Run the event loop for a while (keeps windows responsive in loops). |
| `figures()` | Open figures. |
| `close_all()` | Close every figure. |

## `Figure`

### Data

| Method | Returns |
|---|---|
| `plot(x, y=None, *, label, color, width, style='-', marker=None, markersize, visible=True, replace=False, step=None, fill=None, fill_to=None, xerr=None, yerr=None)` | `Line` |
| `errorbar(x, y, yerr=None, xerr=None, *, label, color, marker='o', ...)` | `Line` |
| `hist(data, bins=10, range=None, *, density=False, weights=None, histtype='step', label, color, fill)` | `(counts, edges, Line)` |
| `fill_between(x, y1, y2=0, *, label, color, alpha=0.3, step=None, line=False)` | `Line` (one undo step; `line=True` also draws the edges) |
| `contour(z, x=None, y=None, *, label, levels, factor, base=None, color, negative_color, negative=True, image=False, linewidth)` | `Contour` |
| `open_bruker(path)` | `Contour` (Bruker processed 2D spectrum, ppm axes) |
| `marker(position, orientation='v', *, color, label='', style='--', width=1.5, show_value=True)` | `Marker` |
| `vline(x, **kw)`, `hline(y, **kw)` | `Marker` |
| `annotate(text, x, y, *, color=None, fontsize=None)` | `Annotation` (unset colour/size follow the style) |
| `lines()`, `contours()`, `markers()`, `annotations()` | lists of handles |
| `line(label)` | the `Line` with that label |
| `get_marker_positions()` | `[(label, 'v'/'h', position), …]` |
| `remove(handle)`, `clear()` | remove objects (undoable) |

Argument formats:

- **Colours:** a name (`'blue'`, `'red'`, `'k'`, `'C0'`…`'C7'`, any SVG colour
  name), `'#rrggbb'` / `'#aarrggbb'`, or an `(r, g, b[, a])` tuple with 0–1
  floats or 0–255 ints. `None` means the next palette colour.
- **Line styles:** `'-'`, `'--'`, `':'`, `'-.'`, `'none'`.
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

### Peaks (2D spectra)

| Method | Description |
|---|---|
| `pick_peaks(*, threshold=None, min_distance=3, negative=False, exclude_diagonal=0.0, region='view', contour=None)` | automatic picking; returns the *new* peaks (one undo step) |
| `add_peak(x, y, *, label='', comment='', snap=True)` | one peak (snaps to the nearest maximum within 3 points) |
| `peaks()` | all `Peak` handles |
| `peak_table()` | list of dicts (`x`, `y`, `intensity`, `label`, `comment`), e.g. for `pandas.DataFrame` |
| `export_peaks(csv)`, `import_peaks(csv)` | CSV with header `x,y,intensity,label,comment` |
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
| `set_style({key: value, ...}, **kw)` | change keys (in keyword form, `_` stands for `.`, e.g. `font_size=12`) |
| `get_style_param(key)`, `save_style(path)` | |

Module `plotter.style` (the default for *new* figures):

| Function | Description |
|---|---|
| `use`, `set`, `get`, `text`, `save` | like the figure methods |
| `available()` | built-in style names |
| `keys()` | all keys with descriptions |
| `save_as_default()` | writes your default style file |
| `default_path()` | where that file lives |
| `reload()` | re-reads the default style |

All keys: [STYLE_REFERENCE.md](STYLE_REFERENCE.md). In texts, `$...$` math
(`^{}`, `_{}`, Greek letters, `\to`, `\pm`, `\sqrt{}`, ...), `**bold**` and
`*italic*` are supported.

### Multi-panel figures

| Method | Description |
|---|---|
| `subplots(nrows=1, ncols=1, *, sharex, sharey, height_ratios, width_ratios, hspace, wspace, squeeze=True)` | list or grid of `Axes`; existing content stays in panel 0 |
| `ratio_panel(ratio=0.3, *, ylabel='Ratio')` | `(main, lower)` with shared x and touching frames |
| `axes`, `gca()`, `sca(ax)` | panels / current panel |

How panels work:

- An `Axes` offers the same plotting methods and properties as `Figure`
  (`ax.plot`, `ax.hist`, `ax.xlabel = ...`, `ax.set_ylim`, `ax.pick_peaks`, …).
- Handles stay bound to the panel they were created in.
- Figure-level calls act on the current panel. That is the one used last, or
  the one clicked in the window.
- Styles and the header labels are figure-wide.

### Selection and interaction

| Method | Description |
|---|---|
| `get_selected_region()` | `(xmin, xmax)` or `None` |
| `set_selected_region(xmin, xmax)`, `clear_selection()` | change it (undoable) |
| `region_mask(x)` | boolean mask of `x` inside the region (or `None`) |
| `region_stats(xmin=None, xmax=None)` | statistics of every visible line in the region (or the given range): `{label: {n, mean, std, min, max, x_at_min, x_at_max, rms, integral}}`. `std` is the sample standard deviation, `integral` the trapezoid rule; NaN gaps are skipped. Raises `ValueError` if no range is given and nothing is selected. |
| `wait_for_region(timeout=-1)` | keep the window interactive until the user finishes a selection; return it. Returns `None` on timeout or close. |
| `scale_contour_levels(factor)` | same as the mouse wheel / `*` `/` keys |
| `on(event, callback)`, `off(event, callback)` | register / remove a callback (see below) |
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
| `xlim()`, `ylim()` | current limits |
| `set_xlim(a, b)`, `set_ylim(a, b)` | set limits |
| `autoscale(x=True, y=True)` | fit the data |
| `reset_view()`, `set_home_view()` | home view (also the `Home` key) |
| `title`, `xlabel`, `ylabel` | read/write properties |
| `legend(visible=True)`, `grid(visible=True)` | toggles |
| `undo()`, `redo()` | the same history as Ctrl+Z / Ctrl+Shift+Z in the window |
| `savefig(filename, *, width=0, height=0, scale=2.0)` | `.png` / `.svg` / `.pdf` |
| `screenshot(filename)` | image of the whole window |
| `show(block=None)`, `close()`, `is_open` | window control |

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
| `rows`, `set_row_order(rows)`, `remove_row(row)` | display order (top to bottom) |
| `move_signal(line, row)` | move a signal to another row (one undo step); returns the new `Line` |
| `window` | `(t0, t1)`, read/write |
| `full_range`, `show_all()` | |
| `cursor_time` | time under the cursor line, or `None` |
| `values_at(t)` | `{(row name, signal): value}`, linearly interpolated (NaN in gaps) |
| `navigator` | signal shown in the bottom strip: a name (`"speed"` or `"gps / speed"`) or a `Line` |
| `auto_fit_y` | each row fits Y to the data in the window |
| `clock_time` | clock-time axis labels (needs absolute timestamps) |
| `time_origin` | Unix epoch seconds of t = 0 (`None` for relative data) |
| `pin(t, label="", color=None)` | add a time marker; returns a `TimePin` |
| `pins()`, `remove_pin(pin)`, `clear_pins()` | time markers |
| `next_pin()`, `prev_pin()` | centre the window on the next / previous marker (keys `]` / `[`); return it or `None` |
| `signals()`, `signal(name)` | `{"row / signal": Line}`; one signal by full or short name |
| `window_data(window=None)` | `{"row / signal": (t, y)}` inside the visible (or given) window |
| `export_csv(path, window=True, resample=None)` | CSV of the visible window (`False`: everything; or `(t0, t1)`). `resample=None`: long format, every sample; `resample=dt`: wide format on a common grid (linear interpolation) |
| `format_time(t)` | time as displayed |

`TimelineRow` members:

- `plot(t, y, label="", color=None)`: `t` must be sorted. Seconds since the
  origin, or absolute `datetime64` / pandas timestamps (the first absolute
  time sets `time_origin`).
- `lines()`.
- `name`, `visible`, `height` (relative).
- `axes`: the full panel API.

`TimePin` members:

- `t`, `label`: read/write and undoable.
- `color`.
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
| `figure`, `subplots` (incl. `gridspec_kw` height ratios / hspace), `gcf`, `gca`, `sca`, `close`, `show`, `pause`, `savefig(dpi=)` | `imshow`, colour bars |
| `plot` with format strings (`'r--o'`, several x/y/fmt groups), `scatter`, `errorbar(fmt=)`, `hist` (`step`, `stepfilled`/`bar`, `errorbar`), `stairs` | polar and 3D axes |
| `fill_between`, `contour`/`contourf` (X, Y, Z), `axhline`, `axvline`, `text`, `annotate` | twin axes |
| `xlabel`, `ylabel`, `title`, `xlim`, `ylim`, `xscale`, `yscale`, `legend(loc=, frameon=)`, `grid`, `plt.style.use` | `rcParams` (use `plotter.style.set`) |

Axes methods use the matplotlib names (`set_xlabel`, `set_xlim`,
`set_yscale`, `invert_xaxis`, …). `ax.panel` and `fig.native` give access to
the full plotter API (peaks, regions, undo, styles).

Differences from matplotlib:

- Figure sizes are converted at 100 px per inch.
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
- `set_errors(xerr=None, yerr=None)`: like `set_data`, not recorded in the undo history.
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
- **Jupyter notebooks:** the windows need a Qt event loop. In a notebook
  kernel, call `plotter.show()` (blocking) or `plotter.pause()`; there is no
  automatic kernel integration (terminal IPython uses `%gui plotter`).
- **Data copies:** arrays are copied on the way in and on the way out, so
  later NumPy changes do not affect the plot until you send the data again.
