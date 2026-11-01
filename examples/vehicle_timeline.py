# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""Vehicle sensor logs on one timeline: IMU, GPS and wheel speeds.

    python examples/make_sample_vehicle.py                 # synthetic logs -> sample_vehicle/
    PYTHONPATH=build/python python examples/vehicle_timeline.py [log files...]

In the window:
  * move the mouse over the rows: a vertical line through all rows shows the
    value of every signal at that moment
  * navigator (bottom strip): drag the shaded window, drag its edges to
    resize it, wheel to zoom, click to jump, double-click for everything
  * Ctrl+click (or M) drops a time marker through all rows; drag it, right-
    click to rename/delete; the Timeline tab lists them (double-click: jump)
  * Timeline tab: drag rows to reorder, drag a signal onto another row,
    tick to hide, edit heights; choose the navigator signal; clock time
"""
import glob
import sys

import numpy as np

import plotter

files = sys.argv[1:] or sorted(glob.glob("sample_vehicle/*.csv"))
if not files:
    sys.exit("no logs: run examples/make_sample_vehicle.py first")

tl = plotter.timeline("Test drive")
tl.load_csv(*files)                      # one row per file; time formats are aligned
tl.clock_time = tl.time_origin is not None

# --- analysis in Python, results shown in the GUI --------------------------------
rows = {r.name: r for r in tl.rows}
if "imu" in rows:
    imu = rows["imu"]
    ax_line = next(l for l in imu.lines() if l.label.startswith("ax"))
    t, ax = ax_line.x, ax_line.y

    # Hard-braking events: longitudinal acceleration below -2 m/s^2 for > 0.5 s.
    braking = ax < -2.0
    edges = np.flatnonzero(np.diff(braking.astype(int)))
    starts, ends = edges[::2] + 1, edges[1::2] + 1
    for s, e in zip(starts, ends):
        if t[e] - t[s] > 0.5:
            tl.pin(t[s], f"brake {(-ax[s:e]).max():.1f} m/s²")

    # Vertical shocks (potholes): |az - g| > 3 m/s^2.
    az_line = next((l for l in imu.lines() if l.label.startswith("az")), None)
    if az_line is not None:
        shock = np.abs(az_line.y - 9.81) > 3.0
        if shock.any():
            tl.pin(az_line.x[np.argmax(shock)], "shock", color="orange")
        # Give az its own row (very different scale from ax / ay).
        tl.move_signal(az_line, tl.add_row("vertical acc."))

    # A derived signal: longitudinal jerk (smoothed), in a new row.
    k = 25
    ax_s = np.convolve(ax, np.ones(k) / k, mode="same")
    jerk = np.gradient(ax_s, t)
    tl.add_row("jerk (m/s³)").plot(t, jerk, label="jerk")

if "gps" in rows:
    tl.navigator = "speed [m/s]"

print(tl)
for p in tl.pins():
    print("  ", p)
tl.show()
