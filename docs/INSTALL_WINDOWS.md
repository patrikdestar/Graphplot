# Graphplot on Windows

This guide covers the ready-made Windows build: what to download, how to start
the application, and how to use the `plotter` Python module.

- Supported: Windows 10 and 11, 64-bit (x64).
- Python module: CPython 3.10, 3.11, 3.12, 3.13 and 3.14 (64-bit, from
  python.org, the Microsoft Store, or a virtual environment of one of these).

To build from source instead, see [Building](../README.md#building) in the
README.

## 1. Download

From the release page (or a workflow run's *Artifacts*) take:

| File | What it is |
|---|---|
| `graphplot-0.1.0-windows-x64.zip` | The application. Portable: no installer, nothing written to the system. |
| `graphplot_plotter-0.1.0-cp3XX-cp3XX-win_amd64.whl` | The Python module for Python 3.XX (`cp312` = Python 3.12). Take only the one for your Python. |

Which Python do you have? In a terminal:

```bat
py --version
```

or `python --version`. `Python 3.12.x` means you need the `cp312` wheel.

## 2. The application

1. Right-click the zip → **Extract All…** and choose a folder, for example
   `C:\Programs\Graphplot`. Don't run it from inside the zip.
2. Double-click `graphplot.exe`.

On the first start, Windows SmartScreen may say *"Windows protected your PC"*.
This happens because the build is not code-signed. Click **More info → Run
anyway**.

Keep the folder complete: `graphplot.exe` needs the DLLs and the
`platforms\`, `styles\`, `imageformats\` and other folders next to it. To get
a Start-menu entry, right-click `graphplot.exe` → **Pin to Start**. To get a
desktop shortcut, use **Send to → Desktop (create shortcut)**.

### Opening files

Use **File → Open** (Ctrl+O), or drag files and folders onto the window. You
can also drag a `.csv` file onto `graphplot.exe` in Explorer.

### Command line

Open *cmd* or PowerShell in the folder. In Explorer, type `cmd` in the
address bar and press Enter. Then:

```bat
graphplot.exe data.csv
```

```bat
graphplot.exe --timeline imu.csv gps.csv wheels.csv
```

```bat
graphplot.exe C:\nmr\cosy\1
```

```bat
graphplot.exe --demo2d --export figure.pdf
```

```bat
graphplot.exe --help
```

The options are the same as on Linux; see
[Running the app](../README.md#running-the-app-and-the-examples).

`--version`, `--help` and `--export` errors print to the terminal you started
it from. In PowerShell, write `.\graphplot.exe` instead of `graphplot.exe`.

To run it from any folder, add the Graphplot folder to `PATH`:
**Settings → System → About → Advanced system settings → Environment
Variables → Path → Edit → New**.

### Settings and styles

- Settings (theme, recent files, window layout) are kept in the registry
  under `HKEY_CURRENT_USER\Software\Graphplot`.
- Your default style (*Style → Use as my default style*) is saved as
  `%LOCALAPPDATA%\Graphplot\default.gpstyle`. Set the `GRAPHPLOT_STYLE`
  environment variable to use another file.

### Removing it

Delete the folder. To also remove the settings, delete the registry key above
and `%LOCALAPPDATA%\Graphplot`.

## 3. The Python module (`import plotter`)

The wheel contains everything it needs, including its own copy of Qt. It does
not need the application zip, and you don't need to install Qt.

Install it with the Python you will use. For Python 3.12:

```bat
py -3.12 -m pip install graphplot_plotter-0.1.0-cp312-cp312-win_amd64.whl
```

pip also installs NumPy if it is missing.

In a virtual environment (recommended for projects):

```bat
py -3.12 -m venv C:\venvs\plots
```

```bat
C:\venvs\plots\Scripts\activate
```

```bat
pip install graphplot_plotter-0.1.0-cp312-cp312-win_amd64.whl
```

Check that it works:

```bat
python -c "import plotter; print(plotter.__file__)"
```

### First script

Save this as `first.py` and run `python first.py`:

```python
import numpy as np
import plotter

x = np.linspace(0, 10, 500)
fig = plotter.figure("First plot")
fig.plot(x, np.sin(x), label="sin")
fig.plot(x, np.cos(x), label="cos", style="--")
fig.legend()
fig.savefig("first.png")      # also .svg / .pdf
plotter.show()                # window stays open until you close it
```

Existing matplotlib scripts usually only need a different import:

```python
import plotter.pyplot as plt
```

Interactive work:

- `python -i`: windows stay live while you type.
- IPython: run `%gui plotter` first.
- Jupyter: figures show inline in the notebook.

Everything else is in [PYTHON_API.md](PYTHON_API.md).

### Headless use (no screen)

On build servers or in scheduled tasks, render without opening windows:

```bat
set QT_QPA_PLATFORM=offscreen
```

```bat
python make_figures.py
```

PowerShell: `$env:QT_QPA_PLATFORM = "offscreen"`. The same works for
`graphplot.exe --export`.

### Upgrading and removing

To upgrade, install the newer wheel with `pip install --upgrade <wheel file>`.

To remove the module:

```bat
pip uninstall graphplot_plotter
```

## 4. Problems

| Symptom | Cause and fix |
|---|---|
| *"…is not a supported wheel on this platform"* | The wheel does not match your Python. Use the `cpXY` wheel for your version, and a 64-bit Python (`python -c "import struct; print(struct.calcsize('P')*8)"` must print 64). |
| `ImportError: DLL load failed while importing _plotter` | Another Qt, or a PyQt/PySide installation, comes first in the DLL search. Try a clean virtual environment. Check that `plotter\Qt6Core.dll` exists in `site-packages`. |
| *"This application failed to start because no Qt platform plugin could be initialized"* | The zip was extracted incompletely, or `graphplot.exe` was copied away from its folder. Extract the whole zip again. If `QT_PLUGIN_PATH` is set system-wide, remove it. |
| *"VCRUNTIME140.dll was not found"* | Files are missing from the folder. Extract the zip again. Installing the *Microsoft Visual C++ Redistributable (x64)* also fixes it. |
| Blurry on high-DPI screens | Right-click `graphplot.exe` → Properties → Compatibility → *Change high DPI settings* → untick any override. |
| Nothing is printed in the terminal | Start it from *cmd* or PowerShell. Output does not appear in Git Bash/MSYS terminals; there, use `cmd //c graphplot.exe --version`. |

More in [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

## License

Graphplot is proprietary. Any use requires the author's prior written
permission; see `licenses\LICENSE` in the zip. The build contains:

- Qt 6 (LGPLv3), as unmodified, replaceable DLLs;
- QCustomPlot (GPLv3); see `licenses\qcustomplot-GPL.txt`.
