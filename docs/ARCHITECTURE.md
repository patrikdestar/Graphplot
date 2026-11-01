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
│   │   └── DataLoader.*           column / matrix text file readers
│   ├── commands/               5. history layer
│   │   ├── Commands.*             QUndoCommand subclasses (generic add/remove/edit templates)
│   │   └── PlotEditor.*           single editing API → pushes commands on a QUndoStack
│   ├── render/                 2. rendering layer
│   │   ├── PlotRenderer.*         model → QCustomPlot objects; hit-testing; coordinates; view
│   │   ├── ContourPlottable.*     custom QCustomPlot plottable (QPainter) for contour lines
│   │   └── Exporter.*             PNG / SVG / PDF
│   ├── interaction/            3. interaction layer
│   │   └── InteractionController.*  mouse/keyboard → view changes and editor calls
│   ├── gui/                    4. GUI layer
│   │   ├── FigureWindow.*         main window: wiring, toolbar, menus, status bar
│   │   ├── PropertiesPanel.*      object tree + property editors
│   │   └── Icons.*                toolbar icons drawn with QPainter
│   ├── app/main.cpp            standalone executable
│   └── python/                 6. Python binding layer
│       ├── bindings.cpp           pybind11 module "_plotter"
│       └── GuiThread.*            event-loop / GIL / thread marshalling glue
├── python/plotter/             pure-Python part of the package (show(), IPython hook)
├── examples/                   Python example scripts
├── tests/                      QtTest suite (C++) and unittest suite (Python)
└── docs/
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
   of inactivity).
6. **Python bindings** hold `(figure, id)` handles and call PlotEditor, so
   edits made from Python are undoable exactly like mouse edits. Results are
   read from the model.

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
  - contour performance;
  - export validity (PNG size, SVG is vector-only, PDF header);
  - file loaders.
- `tests/test_python.py`: the full Python API, including the example from
  the project brief, callbacks, exceptions in callbacks, cross-thread calls
  combined with `wait_for_region`, contours and export.
- Run everything with `ctest --test-dir build`.
