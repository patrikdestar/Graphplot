# Architecture

## Directory structure

```
Graphplot/
├── CMakeLists.txt              top-level build (libraries, app, options)
├── third_party/qcustomplot/    vendored QCustomPlot 2.1.1 (GPLv3)
├── src/
│   ├── model/                  1. data model: no widgets, no rendering
│   │   ├── PlotTypes.*            value types: Dataset, Matrix, Marker, Annotation, Region, styles
│   │   ├── PlotModel.*            owns all plot content, emits change signals
│   │   ├── Contour.*              marching-squares contour extraction (pure computation)
│   │   ├── PeakPicker.*           peak picking, snapping, peak-list CSV
│   │   ├── BrukerReader.*         Bruker TopSpin pdata (2rr + procs/proc2s) reader/writer
│   │   ├── Style.*                all visual settings, .gpstyle files, built-in styles
│   │   ├── Statistics.*           region statistics (mean, sd, min/max, RMS, integral)
│   │   ├── ModelJson.*            panel content <-> JSON (figure files)
│   │   ├── DataExport.*           data as CSV, statistics as TSV
│   │   ├── TimeSeriesLoader.*     time-column detection for sensor logs
│   │   └── DataLoader.*           column / matrix text file readers
│   ├── commands/               5. history layer
│   │   ├── Commands.*             QUndoCommand subclasses (generic add/remove/edit templates)
│   │   └── PlotEditor.*           single editing API → pushes commands on a QUndoStack
│   ├── render/                 2. rendering layer
│   │   ├── PlotRenderer.*         model → QCustomPlot objects; hit-testing; coordinates; view
│   │   ├── ContourPlottable.*     custom QCustomPlot plottable (QPainter) for contour lines
│   │   ├── PeakOverlay.*          all peaks (crosses, labels, leader lines) in one layerable
│   │   ├── RichText.* / RichElements.*  $math$ / **bold** text: labels, header, legend, items
│   │   ├── Exporter.*             PNG / SVG / PDF
│   │   └── DisplayTheme.*         display themes: widget palette + on-screen plot colours
│   ├── interaction/            3. interaction layer
│   │   └── InteractionController.*  mouse/keyboard → view changes and editor calls
│   ├── gui/                    4. GUI layer
│   │   ├── FigureWindow.*         main window: wiring, toolbar, menus, status bar
│   │   ├── FigureFile.cpp         FigureWindow::saveFigure / loadFigure (.gpfig)
│   │   ├── PropertiesPanel.*      object tree + property editors
│   │   ├── PeakTable.*            peak list, picking controls, CSV import/export
│   │   ├── StyleEditor.*          dialog editing every style key
│   │   ├── Timeline.*             timeline layout: rows, navigator, cursor line, pins, auto-fit
│   │   ├── TimelinePanel.*        "Timeline" dock: rows / signals / markers editing
│   │   ├── Appearance.*           theme / tooltip settings, Appearance menu, persistence
│   │   ├── Explanations.*         one table of hover explanations for every control
│   │   └── Icons.*                toolbar icons drawn with QPainter (follow the palette)
│   ├── app/main.cpp            standalone executable
│   └── python/                 6. Python binding layer
│       ├── bindings.cpp           pybind11 module "_plotter"
│       └── GuiThread.*            event-loop / GIL / thread marshalling glue
├── python/plotter/             pure-Python part: show(), Axes proxies, hist/errorbar/
│                               fill_between, style module, pyplot compatibility layer
├── examples/                   Python example scripts
├── tests/                      QtTest suite (C++) and unittest suite (Python)
├── tools/                      gen_style_reference.py (regenerates docs/STYLE_REFERENCE.md)
└── docs/                       references, file formats, troubleshooting, images
```

CMake builds three static libraries, and the executable and the Python module
link the same code:

| Target | Contents | Qt modules |
|---|---|---|
| `graphplot_model` | model + commands | Core, Gui (QColor, QUndoStack) |
| `graphplot_ui` | render, interaction, gui | + Widgets, PrintSupport, Svg |
| `qcustomplot` | vendored library | Widgets, PrintSupport |

## How the components communicate

```
             ┌────────────────────── edits (undoable) ─────────────────────┐
             │                                                             │
  Python ──► PlotEditor ──push──► QUndoStack ──redo/undo──► PlotModel ──signals──► PlotRenderer ──► QCustomPlot
  bindings       ▲                                             │                       ▲   (draws)
                 │                                             │ signals               │ hit-test, coords
  PropertiesPanel┤                                             ▼                       │
                 └──────────── InteractionController ◄── mouse/keyboard events on the plot widget
```

1. **PlotModel** is the single source of truth. Its mutators are primitive:
   they change state and emit fine-grained signals (`markerChanged(id)`,
   `datasetDataChanged(id)`, …). Each object has a stable `ObjectId`.
2. **PlotEditor** is the only place where *user-level edits* are made. Each
   edit becomes a `QUndoCommand` that stores just the object state it needs
   (before/after of one marker, a removed dataset and its index), never a
   snapshot of the whole figure. `QVector` is implicitly shared, so keeping a
   removed dataset in a command costs no copy.
3. **PlotRenderer** observes the model and keeps QCustomPlot in sync: one
   plottable per dataset (`QCPGraph` for sorted x, `QCPCurve` otherwise),
   `ContourPlottable` (+ optional `QCPColorMap`) per matrix, and QCP items for
   markers, annotations and the region. It never edits the model.
4. **InteractionController** is an event filter on the plot widget, so
   QCustomPlot's own interaction handling is bypassed and all behaviour is
   defined in one place. It uses the renderer for hit-testing and
   pixel↔data conversion.
   - During a drag it calls `PlotEditor::preview*()`, which changes the model
     without recording history, so every layer stays in sync live.
   - On release it commits a single command, using the state captured at
     press time as the "before".
5. **PropertiesPanel** shows the model and edits it through PlotEditor.
   Spin-box steps and wheel scrolling of contour levels use *mergeable*
   commands, so one gesture becomes one undo step (merging stops after 1.5 s
   of inactivity, and never joins edits of different panels: all panels
   share one undo stack, but object ids are only unique per panel). The
   panel writes back only the fields the user changed, so values outside a
   widget's range or precision are never clamped by accident.
6. **Python bindings** hold `(figure, panel, id)` handles and call
   PlotEditor, so edits made from Python are undoable exactly like mouse
   edits. Results are read from the model. Calls through an `Axes` name
   their target panel in a per-thread call context (`pyglue::CallContext`,
   carried into the GUI-thread task of each call), so they cannot be
   redirected by a user click and threads never wait on a shared lock.

Adding a new object type follows the existing pattern: a value type, model
storage and signals, a traits struct for the generic commands, a renderer
visual, and optionally a panel page. The 2D/contour feature was added this
way without changing the other layers' structure.

### Rendering details

- **Layers:**
  - `grid` → `images` (colour maps) → `regions` → `main` (curves, contours)
    → `objects` (markers, annotations) → `legend`.
  - `regions` and `objects` are *buffered* layers. Dragging a marker or region
    edge repaints only that layer (`QCPLayer::replot()`), so dragging stays
    smooth with millions of data points on screen.
- **Contours:**
  - `ContourGrid` stores the per-cell min/max of the grid once. When levels
    change, only cells whose range contains a level are visited, using a
    binary search over the sorted levels.
  - Measured: 24 levels on a 1024×1024 grid recompute in about 6 ms (see
    `contourRecomputeIsFast` in the tests).
  - Pan and zoom only re-project the cached segments.
- **Export:**
  - PNG uses `savePng` with a scale factor.
  - PDF uses `savePdf` with non-cosmetic pens.
  - SVG renders through `QSvgGenerator` + `QCPPainter` in vectorized mode, so
    the output contains `<path>` elements and no embedded raster images.
  - Selection highlights are switched off while exporting.

### Multi-panel figures

A figure holds one or more *panels*. Each panel has its own `PlotModel`,
`PlotEditor` and `PlotRenderer` (drawing into its own `QCPAxisRect`).

- **Shared state:**
  - All editors share the figure's `QUndoStack`, so there is one history.
  - The interaction controller knows all panels; the panel under the mouse
    becomes current.
- **First panel:** it also owns the figure-level elements: header row
  (title, top-left and top-right labels), padding and background.
- **Layout (`FigureWindow::setGrid`):**
  - Panels go into a `QCPLayoutGrid` with stretch factors.
  - Margin groups align the frames.
  - Shared axes are linked through `rangeChanged`.
  - Ratio panels use zero spacing with fixed zero inner margins, so the
    frames touch.

### Timeline

`gui/Timeline` turns a figure into stacked rows.

- **Rows:** each row is an ordinary panel, so data, styles, undo and Python
  handles all work. Row x axes are linked to row 0 through `rangeChanged`.
- **Navigator:** a separate model and renderer (not an editable panel) on
  its own fixed-height axis rect. The visible window is the navigator
  model's region. The window and the row range are synchronised in both
  directions, with a re-entrancy guard.
- **Event handling:** the timeline installs its own event filter *after*
  the interaction controller, so Qt calls it first.
  - It handles the navigator: drag, resize, wheel and click.
  - It handles time-marker drags and Ctrl+click, plus the cursor.
  - It passes everything else on, so normal zoom, pan and editing keep
    working in the rows, restricted to the time axis.
- **Cursor:** the cursor line and value boxes live on their own buffered
  "cursor" layer, so mouse moves only repaint that layer. Values are
  interpolated by binary search in the sorted time arrays.
- **Auto-fit:** it runs once per event-loop pass after window changes. It
  scans only the samples inside the window (also found by binary search).
- **Clock axis:** a `QCPAxisTickerDateTime` subclass shifts the relative
  axis by the epoch origin, so ticks fall on round clock times.
- **Time markers:** pins are undoable commands on the figure's shared undo
  stack.
- **Hidden rows:** their panel is taken out of the layout and hidden with
  all its items.
- **CSV loading:** `model/TimeSeriesLoader` detects the time column and
  its format (epoch magnitude: s, ms, µs or ns; or ISO 8601), sorts rows,
  and turns empty cells into NaN gaps.

### Display themes and hover explanations

- `DisplayTheme` (render layer, no widgets) holds the current theme. Its
  widget palette is applied by `Appearance` (Fusion style + palette, so it
  looks the same on every platform). Its plot colours are applied by each
  `PlotRenderer` *on top of* the figure's style when drawing on screen
  (`displayStyle()`); the model's style never changes, so undo, style files,
  Python and exports see the real style. `setExportMode()` switches back to
  the real colours for the duration of an export.
- Very dark data colours are lightened on dark themes (`ink()`), so `'k'`
  lines stay visible.
- `Explanations.cpp` keeps every hover text in one table keyed by
  "group/label" or "action/text"; `applyExplanations()` attaches them, and a
  test fails if a new control has none. `Appearance` swallows tooltip events
  application-wide when the user switches explanations off.
- Settings persist with `QSettings("Graphplot", "Graphplot")`, shared by the
  app and the Python module.

### Styles

`Style` (model layer) contains every visual parameter. `PlotRenderer::applyStyle`
is the only place that turns it into QCustomPlot settings: fonts, pens, tick
lengths and directions, mirroring, minor ticks, number format, legend, header,
peaks and so on. Each panel's model carries the style, and changing it is an
undoable command. New objects take their defaults from it (line width,
colour cycle, contour levels). Style files are parsed in `Style::parse`:

- Unknown keys and invalid values are reported with line numbers and do not
  abort loading.
- An `inherit:` line chains a built-in style or another file.
- `Style::toText` writes a complete, commented file.

### Peaks and Bruker data

- **Reader:** `BrukerReader` resolves an experiment folder, a `pdata/N`
  folder or a `2rr` file. It decodes the submatrix tiling (XDIM), the int32
  or float64 data type, byte order and the `NC_proc` scaling, and computes
  the ppm axes as OFFSET − i·SW_p/(SF·SI). The Python tests verify the result
  against nmrglue's reader.
- **Picking:** `PeakPicker` finds strict local maxima in each 3×3
  neighbourhood above the threshold. It drops candidates near the diagonal
  if requested, then suppresses peaks within the minimum distance (strongest
  first). Positions are refined by parabolic interpolation, and a click snaps
  to the strongest point within a search window.
- **Drawing:** peaks are drawn by one `PeakOverlay` on the buffered
  "objects" layer. Laid-out labels are cached.

## Technical difficulties and how they are handled

### Qt GUI thread vs. Python

**Difficulty:** Qt widgets must be created and used on the thread that owns
the `QApplication`. On macOS this must be the process's main thread, because
Cocoa only allows UI work there. Running Qt in a background thread therefore
does not work portably.

**Solution:** Qt lives on Python's main thread. The first figure must be
created there; `ensureApp()` checks this. The event loop runs in three places:

1. **At the interactive prompt:**
   - `PyOS_InputHook` runs a `QEventLoop` until stdin becomes readable. On
     Windows it polls with `_kbhit` instead.
   - CPython calls this hook with the GIL released, from the classic readline
     REPL. The new REPL in 3.13+ honours it too (gh-119842).
   - Verified with a pseudo-terminal test: a worker thread's call completed
     while the prompt was idle.
2. **In IPython:** `%gui plotter` registers an equivalent prompt_toolkit
   input hook.
3. **In scripts:** `show()`, `pause()` and `wait_for_region()` run a nested
   loop. They release the GIL and check for Ctrl+C every 30 ms (Python signal
   handlers only run when the GIL is held).

**Calls from other Python threads:** `inGui()` marshals them to the GUI
thread with a blocking queued call, with the GIL released while waiting. That
way the GUI thread can run Python callbacks meanwhile without a deadlock.
Such a call completes once the main thread runs the event loop. A main thread
that is busy computing, or `join()`-ing that worker, delays it. This is
documented in the package docstring.

**Callbacks:**
- Python callbacks run on the GUI thread, re-acquire the GIL, and catch all
  Python exceptions (reported as "unraisable"), so no exception unwinds
  through Qt.
- They fire once per *completed* edit, not on every intermediate mouse move.

### QCustomPlot specifics

- **Interaction:** QCustomPlot's built-in interactions conflict with object
  dragging. They are disabled and replaced by the event filter.
- **Monotonic x:** `QCPGraph` sorts by x. Non-monotonic data, such as
  parametric curves, is therefore drawn with `QCPCurve` automatically.
- **Hit-testing text:** `QCPItemText::selectTest` reports a hit *inside* the
  text box as `0.99 × selectionTolerance` (≈ 8 px). Hit-testing accounts for
  this; it was found by a failing interaction test.
- **No contour plottable:** QCustomPlot doesn't have one, so a small custom
  `QCPAbstractPlottable` draws the segments with QCPPainter.
- **Mixed coordinates:** markers and the region use mixed position types:
  data coordinates on one axis and axis-rect ratio on the other. They always
  span the plot area at any zoom level.
- **Rich text:** QCustomPlot draws plain text only. Rich text uses
  `QTextDocument`:
  - **Axis labels:** QCustomPlot reserves the space via a blank label in the
    same font plus extra padding; an overlay layerable draws the formatted
    text there.
  - **Title, header and legend:** these are custom layout elements.
  - **Annotations and marker labels:** these are a custom item.
- **Legend icons:** `drawLegendIcon` is protected, and QCustomPlot's
  friendship with its own legend item class does not extend to subclasses.
  The rich legend item therefore lets the base class draw the icon (with an
  empty name) and draws the text itself.
- **Extra panels and layers:** QCustomPlot puts only its default axis rect
  on the "background" layer. Panels created later would paint their
  background over the grid, image and region layers. The renderer moves
  every panel to "background"; this was found when the timeline navigator's
  window band did not show.
- **Legend auto-add:** QCustomPlot adds every new plottable to the *main*
  legend by default. This is switched off; legends are rebuilt per panel.
- **Licence:** QCustomPlot is GPLv3 (see README).

### Data ownership

NumPy arrays are copied into `QVector<double>` when they cross into C++.
Python can then mutate or free its arrays freely, and the GUI thread never
reads Python memory without the GIL.

## Testing

- `tests/test_graphplot.cpp` (QtTest, headless with
  `QT_QPA_PLATFORM=offscreen`):
  - model and undo/redo semantics;
  - renderer synchronisation;
  - **simulated mouse interaction:** region selection, edge drag, placing and
    dragging markers, annotation drag, wheel and rectangle zoom, pan,
    Alt+wheel contour scrolling;
  - contour geometry, checked against an analytic circle;
  - peak picking (off-grid positions, diagonal exclusion, no duplicates,
    undo), the snapping Peak tool and label dragging;
  - Bruker write/read round-trip and panning on reversed axes;
  - style files (round-trip of every key, `inherit`, error reporting), user
    default style, undoable figure styles, rich-text conversion;
  - error bars, steps and fills;
  - multi-panel layout (touching frames, shared x, active panel, one undo
    history);
  - contour performance;
  - export validity (PNG size, SVG is vector-only, PDF header);
  - file loaders.
- `tests/test_python.py`: the full Python API, including:
  - the example from the project brief;
  - callbacks, exceptions in callbacks, and cross-thread calls combined with
    `wait_for_region`;
  - contours and export;
  - the Bruker reader, compared with nmrglue;
  - the complete NMR cross-peak workflow on a synthetic COSY (pick, assign,
    CSV round-trip);
  - styles;
  - `hist`, `errorbar` and `fill_between`;
  - subplots and ratio panels;
  - log axes;
  - the `pyplot` compatibility layer.
- Run everything with `ctest --test-dir build`.
