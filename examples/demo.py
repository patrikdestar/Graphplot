# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""The workflow from the project brief: plot, mark, select, process, re-plot.

    PYTHONPATH=build/python python examples/demo.py
"""
import numpy as np

import plotter

x = np.linspace(0, 100, 2000)
y1 = np.sin(x)
y2 = np.cos(x)

fig = plotter.figure("Demo")
fig.xlabel = "x"
fig.ylabel = "signal"

fig.plot(x, y1, label="Signal 1", color="blue")
fig.plot(x, y2, label="Signal 2", color="red")

fig.marker(25.0, color="green", label="Marker 1")
fig.marker(50.0, color="orange", label="Marker 2")

fig.show(block=False)

print("Drag a region with the 'Region' tool (R), then release the mouse ...")
region = fig.wait_for_region()  # window stays interactive while waiting
if region is not None:
    xmin, xmax = region
    print(f"selected {xmin:.3f} .. {xmax:.3f}")
    mask = (x >= xmin) & (x <= xmax)
    y_corrected = y1 - np.mean(y1[mask])
    fig.plot(x, y_corrected, label="Corrected signal", color="black")
    print("markers:", fig.get_marker_positions())

fig.show()  # block until the window is closed
