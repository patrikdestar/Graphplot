# Graphplot on Ubuntu 22.04 and 24.04

This guide covers the ready-made `.deb` packages: which one to download, how
to install them, how to start the application, and how to use the `plotter`
Python module.

The packages use Ubuntu's own Qt and Python:

- the download is small;
- security updates come through normal system updates;
- the app looks and behaves like other Qt programs on your desktop.

Each package is built for one Ubuntu release, so take the matching one.

| Ubuntu | Package | Qt | Python module works with |
|---|---|---|---|
| 22.04 LTS (Jammy) | `graphplot_0.1.0_ubuntu22.04_amd64.deb` | 6.2 | the system Python 3.10 (`/usr/bin/python3`) |
| 24.04 LTS (Noble) | `graphplot_0.1.0_ubuntu24.04_amd64.deb` | 6.4 | the system Python 3.12 (`/usr/bin/python3`) |

Check your release with `lsb_release -rs`. The packages also work on
derivatives of the same base, such as Linux Mint 21 (22.04) and Mint 22
(24.04). They are for 64-bit PCs only (amd64).

For another distribution or Python version, build from source; see
[Building](../README.md#building).

## 1. Install

Download the `.deb` for your release, open a terminal in the download folder
and run (for 24.04):

```bash
sudo apt install ./graphplot_0.1.0_ubuntu24.04_amd64.deb
```

On 22.04:

```bash
sudo apt install ./graphplot_0.1.0_ubuntu22.04_amd64.deb
```

Keep the `./`. Without it, apt looks for a package named like the file in its
online repositories. apt installs the Qt libraries, NumPy and anything else
that is missing. Double-clicking the `.deb` and using *App Center* or
*Software Install* also works.

What gets installed:

| Path | Content |
|---|---|
| `/usr/bin/graphplot` | the application |
| `/usr/lib/python3/dist-packages/plotter/` | the Python module |
| `/usr/share/applications/graphplot.desktop` | the menu entry |
| `/usr/share/icons/hicolor/scalable/apps/graphplot.svg` | the icon |
| `/usr/share/doc/graphplot/` | license files |

## 2. The application

Start it from the application menu (search for *Graphplot*), or from a
terminal:

```bash
graphplot
```

```bash
graphplot data.csv
```

```bash
graphplot --timeline imu.csv gps.csv wheels.csv
```

```bash
graphplot ~/nmr/cosy/1
```

```bash
graphplot --demo2d --export figure.pdf
```

```bash
graphplot --help
```

All options are explained in
[Running the app](../README.md#running-the-app-and-the-examples).

Files can be dropped on the window. To open CSV files with Graphplot from the
file manager: right-click a `.csv` → *Open With…* → *Graphplot*.

Where things are saved:

- Settings (theme, recent files, window layout): `~/.config/Graphplot/`.
- Your default style: `~/.config/Graphplot/default.gpstyle`. The
  `GRAPHPLOT_STYLE` variable points to another file.

### Without a screen (servers, SSH, CI)

```bash
QT_QPA_PLATFORM=offscreen graphplot data.csv --export plot.png
```

Over SSH with X forwarding (`ssh -X`), the window opens on your local screen.

## 3. The Python module (`import plotter`)

The module is installed for the system Python, so it works right away:

```bash
python3 -c "import plotter; print(plotter.__file__)"
```

### First script

Save as `first.py` and run `python3 first.py`:

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

For matplotlib scripts, change only the import:

```python
import plotter.pyplot as plt
```

Interactive work:

- `python3 -i`: windows stay live while you type.
- IPython (`sudo apt install ipython3`): run `%gui plotter` first.
- Jupyter: figures show inline.

The full reference is in [PYTHON_API.md](PYTHON_API.md).

### Virtual environments

A normal virtual environment does not see system packages. Create it with
`--system-site-packages` so that `plotter`, and the matching NumPy, are
visible:

```bash
python3 -m venv --system-site-packages ~/.venvs/plots
```

```bash
. ~/.venvs/plots/bin/activate
```

```bash
python -c "import plotter"
```

The module is compiled for the system Python: 3.10 on 22.04, 3.12 on 24.04.
It does **not** load in:

- other Python versions (deadsnakes, pyenv, uv-managed Pythons);
- conda environments.

For those, build the module from source with that Python; see
[Installing](../README.md#installing-optional).

## 4. Updating and removing

Install a newer `.deb` the same way; apt replaces the old version.

To remove Graphplot:

```bash
sudo apt remove graphplot
```

To also remove your settings and default style:

```bash
rm -r ~/.config/Graphplot
```

## 5. Problems

| Symptom | Cause and fix |
|---|---|
| `Depends: python3 (< 3.11) but 3.12… is to be installed` (or similar) | The package is for the other Ubuntu release. Download the one matching `lsb_release -rs`. |
| `qt.qpa.plugin: Could not load the Qt platform plugin "xcb"` | The platform plugins are missing or damaged. Run `sudo apt install --reinstall qt6-qpa-plugins`. Without a desktop session (SSH without `-X`), use `QT_QPA_PLATFORM=offscreen`. |
| Wayland: window decorations look different or the window opens at a fixed place | Qt uses XWayland by default. For native Wayland, run `sudo apt install qt6-wayland`, then start it with `QT_QPA_PLATFORM=wayland graphplot`. |
| `ModuleNotFoundError: No module named 'plotter'` | You are not using the system Python, e.g. a venv without `--system-site-packages`, conda, or pyenv. Check `which python3`; it should be `/usr/bin/python3`. |
| `ImportError: ... undefined symbol` / `numpy.core.multiarray failed to import` | A pip-installed NumPy for another version shadows the system one. In a venv, use `--system-site-packages` and don't pip-install NumPy into it. |
| The menu entry does not appear | Log out and back in, or run `update-desktop-database ~/.local/share/applications` (harmless if nothing changes). |

More in [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

## License

Graphplot is proprietary. Any use requires the author's prior written
permission; see `/usr/share/doc/graphplot/LICENSE`. The package uses
Ubuntu's Qt 6 libraries (LGPLv3) and contains QCustomPlot (GPLv3); see
`/usr/share/doc/graphplot/qcustomplot/GPL.txt`.
