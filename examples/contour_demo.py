"""2D data: contour plot with scrollable levels (TopSpin-style).

In the window:  Alt + mouse wheel  -> scroll contour levels
                L                  -> toggle 'Levels' mode (plain wheel scrolls levels)
                *  /  /            -> halve / double the levels

    PYTHONPATH=build/python python examples/contour_demo.py
"""
import numpy as np

import plotter

n = 512
f2 = np.linspace(0, 10, n)
f1 = np.linspace(0, 10, n)
X, Y = np.meshgrid(f2, f1)  # rows = f1 (y), columns = f2 (x)


def peak(x0, y0, a, w):
    return a * np.exp(-((X - x0) ** 2 + (Y - y0) ** 2) / (2 * w ** 2))


z = (peak(2, 2, 1.0, 0.12) + peak(5.5, 5.5, 0.8, 0.12) + peak(8, 8, 0.6, 0.1)
     + peak(2, 5.5, 0.15, 0.1) + peak(5.5, 2, 0.15, 0.1)
     + peak(5.5, 8, 0.03, 0.1) + peak(8, 5.5, 0.03, 0.1)       # weak cross peaks
     + peak(3.5, 7, -0.2, 0.15)                                  # negative peak
     + 0.002 * np.random.default_rng(1).standard_normal(X.shape))

fig = plotter.figure("2D spectrum")
fig.xlabel, fig.ylabel = "F2 (ppm)", "F1 (ppm)"
c = fig.contour(z, x=f2, y=f1, label="HSQC-like", levels=12, factor=1.4)
fig.on("levels_changed", lambda h: print(f"base level = {h.base_level:.4g}"))
print("initial levels:", np.round(c.levels(), 4))

# The weak cross peaks at (5.5, 8) only appear after lowering the levels,
# e.g. three Alt+wheel notches up, or programmatically:
#   c.scale_levels(0.25)
fig.show()
