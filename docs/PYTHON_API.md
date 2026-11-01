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
| `plot(x, y=None, *, label, color, width=1.5, style='-', marker=None, markersize=6, visible=True, replace=False)` | `Line` |
| `contour(z, x=None, y=None, *, label, levels=12, factor=1.4, base=None, color, negative_color, negative=True, image=False, linewidth=1.0)` | `Contour` |
| `marker(position, orientation='v', *, color, label='', style='--', width=1.5, show_value=True)` | `Marker` |
| `vline(x, **kw)`, `hline(y, **kw)` | `Marker` |
| `annotate(text, x, y, *, color, fontsize=10)` | `Annotation` |
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

`contour`:

- `z` is indexed `z[row = y, column = x]`, as in NumPy and matplotlib.
- `x` and `y` give the grid coordinates, either as full uniformly spaced
  arrays or as `(start, stop)`.
- The positive levels are `base * factor**k` for `k = 0 … levels-1`. With
  `negative=True`, they are mirrored for negative values.
- `base=None` places the top level just below `max |z|`.

### Selection and interaction

| Method | Description |
|---|---|
| `get_selected_region()` | `(xmin, xmax)` or `None` |
| `set_selected_region(xmin, xmax)`, `clear_selection()` | change it (undoable) |
| `region_mask(x)` | boolean mask of `x` inside the region (or `None`) |
| `wait_for_region(timeout=-1)` | keep the window interactive until the user finishes a selection; return it. Returns `None` on timeout or close. |
| `scale_contour_levels(factor)` | same as the mouse wheel / `*` `/` keys |
| `on(event, callback)` | register a callback (see below) |
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

## Handles

Handles refer to objects by id. They stay valid after undo/redo. Accessing a
removed object raises `KeyError`. Setting a property is an undoable edit.

### `Line`

- **Read-only:** `x`, `y` (NumPy copies), `exists`.
- **Properties:** `label`, `color`, `width`, `style`, `marker`, `visible`.
- `set_data(x, y, *, undoable=False)`: by default, streaming updates do not
  fill the undo history.
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
- **Data copies:** arrays are copied on the way in and on the way out, so
  later NumPy changes do not affect the plot until you send the data again.
