# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""Streaming updates from a processing loop (not recorded in undo history).

    PYTHONPATH=build/python python examples/live_update.py
"""
import numpy as np

import plotter

x = np.linspace(0, 20, 1000)
fig = plotter.figure("Live")
line = fig.plot(x, np.sin(x), label="wave")
fig.set_ylim(-1.5, 1.5)
fig.show(block=False)

for k in range(300):
    if not fig.is_open:
        break
    line.set_data(x, np.sin(x - 0.05 * k) * np.exp(-0.002 * k))
    plotter.pause(0.02)  # keep the window responsive

fig.show()
