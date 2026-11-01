# Troubleshooting and FAQ

Problems are grouped by where they show up. Error messages are quoted
exactly as Graphplot prints them, so you can search this page for them.

## Building

**CMake cannot find Qt (`Could not find a package configuration file provided by "Qt6"`).**
Install the Qt 6 development packages (Debian/Ubuntu:
`qt6-base-dev qt6-svg-dev libgl1-mesa-dev`), or point CMake at your Qt
installation:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
```

**CMake cannot find pybind11.** Install it into the Python you will use
(`pip install pybind11 numpy`) and re-run CMake from a fresh build folder.
To build only the application, add `-DGRAPHPLOT_BUILD_PYTHON=OFF`.

**The Python module was built for another Python.** The module is compiled
for the interpreter CMake found. Choose one explicitly:

```bash
cmake -S . -B build -DPython_EXECUTABLE=$(which python3)
```

## Starting Python

**`ModuleNotFoundError: No module named 'plotter'`.** Point Python at the
build folder (the build copies the package there):

```bash
export PYTHONPATH=/path/to/Graphplot/build/python
```

**Changes to `python/plotter/*.py` have no effect.** The package is copied
into `build/python` by the build; run `cmake --build build` after editing.

**`qt.qpa.xcb: could not connect to display` (servers, SSH, CI).** There is
no screen. For scripts that only export files, use the off-screen platform:

```bash
QT_QPA_PLATFORM=offscreen python my_script.py
```

## Windows and the event loop

**My script ends immediately / the window flashes and disappears.** A
script must keep the event loop running while you look at the window:
`fig.show()` (blocks until you close it) or `plotter.show()` at the end.
`plt.show()` behaves the same way.

**The window freezes while my loop computes.** The window is only redrawn
while the event loop runs. Call `plotter.pause(0.01)` inside the loop, or
show it with `fig.show(block=False)` and use `fig.wait_for_region()` /
`plotter.pause()` when you need interaction.

**IPython: the window doesn't respond while the prompt waits.** Run
`%gui plotter` once. The plain `python` prompt and `python -i` need nothing.

**Jupyter notebook.** There is no kernel integration yet: the windows only
respond while a cell runs `plotter.show()` (blocking) or `plotter.pause(...)`.

**`RuntimeError: figures must be created on the GUI (main) thread`.** Create
figures on the main thread; worker threads may then call their methods.

**A call from a worker thread hangs.** Calls from other threads are executed
on the GUI thread and wait until the main thread runs the event loop
(`show()`, `pause()`, the interactive prompt). Don't block the main thread
with `thread.join()` without running the event loop.

**`RuntimeError: the figure is not shown: call fig.show(block=False) first`.**
`wait_for_region()` without a timeout needs a visible window to select in.

**Using a figure after closing its window.** Closing the window keeps the
figure's data: you can still read it, plot into it, `savefig` or `save` it,
and `show()` it again. Only when Python no longer refers to the figure at all
is it freed; a handle that outlives that raises
`RuntimeError: the figure has been closed`.

**`KeyError: ... no longer exists`.** The object was removed (by you, by
undo, or by `clear()`). Handles stay valid across undo/redo of the object
itself.

## Data

**My sensor log starts in 1970 / times are wrong by 1000×.** Graphplot
detects Unix epochs from the size of the numbers and treats a column as
epoch time only if the whole column lies after ~1973. Relative counters in
ms / µs / ns need the unit in the header: `time_ms`, `t [us]`,
`stamp (ns)`. See [FILE_FORMATS.md](FILE_FORMATS.md#time-series-sensor-logs).

**Numbers with a decimal comma (`1,5`) are not read.** Only `.` is a
decimal point. Save the file with `;` as separator and `.` decimals, or
convert it with pandas (`pd.read_csv(f, sep=";", decimal=",")`) and plot from
Python.

**"Line N is not numeric".** Column files allow one header line. Remove
extra lines (e.g. a units row) or turn them into comments with `#`.

**"Line N has X columns, expected Y".** A row has a different number of
fields. Missing values must still be present as empty cells (`1,,3`).

**Bruker: "proc2s not found - is this a 2D processed data set?"** Select the
processed-data folder `<expno>/pdata/<procno>` (or the experiment folder) of
a 2D experiment that has been processed (`xfb`) in TopSpin.

**Masked or missing values.** `NaN`, `NaT`, empty cells and masked array
entries are drawn as gaps; they are skipped by region statistics.

## Peaks

**"Pick peaks" finds nothing.** Picking uses the lowest visible contour
level as the threshold: scroll the levels down (Alt+wheel) until the peaks
you want have contours, or pass `threshold=` in Python. With *Skip diagonal ±*
set above 0, peaks closer to the diagonal than that distance (in ppm) are
ignored.

**Picking twice adds nothing.** Peaks within the minimum distance of an
existing peak *of the same spectrum* are skipped on purpose.

## Appearance

**I chose a dark theme but my exported PDF is white.** On purpose: exports
and the clipboard use the figure's own style so papers get light figures.
Tick *View → Appearance → Exports use the theme colours too* (or
`plotter.theme(exports=True)`) to export what you see.

**Black lines look grey-white in a dark theme.** Very dark data colours are
lightened on screen so they stay visible; the figure keeps the colour you set
and exports use it.

**The hover explanations are distracting.** Untick *View → Appearance → Show
explanations on hover* (or `plotter.tooltips(False, save=True)`).

**Where are these settings stored?** In the user configuration
(`~/.config/Graphplot/Graphplot.conf` on Linux), shared by the app and Python.

## Output

**The exported figure has a different size than I expected.** Exports use
100 figure pixels per inch in every format: a figure of 800 × 500 pixels is
8 × 5 inches as PNG, SVG and PDF. Set the size with
`plotter.figure(size=(w, h))` or `plt.figure(figsize=(w_in, h_in))`.

**The legend moves in all panels when I set `loc` for one.** The legend
position is a figure-wide style setting.

**Math in labels doesn't render.** Use `$...$` for math
(`$E_T^{miss}$`, `$\sqrt{s}$`, Greek letters like `$\alpha$`), `**bold**`
and `*italic*`; write `\$` for a literal dollar sign. Only a subset of LaTeX
is supported.

## Licensing

**Can I use Graphplot at work or in a product?** Not without the author's
written permission; see [LICENSE](../LICENSE). QCustomPlot, which Graphplot
uses, is GPLv3; distributing Graphplot under other terms also requires a
commercial QCustomPlot license.
