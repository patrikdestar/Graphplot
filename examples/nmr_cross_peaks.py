# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""2D NMR workflow: open a Bruker spectrum, find off-diagonal cross peaks,
assign them and export the peak list.

    python examples/make_sample_nmr.py              # synthetic COSY -> sample_nmr/
    PYTHONPATH=build/python python examples/nmr_cross_peaks.py [experiment folder]

In the window you can continue by hand:
  * Alt + mouse wheel (or 'Levels', key L): scroll the contour levels
  * Peak tool (K): click near a peak to add it (snaps to the maximum)
  * Peaks tab: edit labels / comments, Pick peaks, Export CSV
  * drag a peak label to move it out of the way (a leader line is drawn)
"""
import sys

import plotter

path = sys.argv[1] if len(sys.argv) > 1 else "sample_nmr/cosy/1"

plotter.style.use("nmr")
fig = plotter.figure("COSY")
spectrum = fig.open_bruker(path)          # ppm axes, high -> low, diagonal shown
print(spectrum, "lowest contour:", spectrum.base_level)

# Off-diagonal cross peaks only (skip everything within 0.1 ppm of the diagonal).
cross = fig.pick_peaks(exclude_diagonal=0.1, region="all")
print(f"{len(cross)} cross peaks")

# Assign: the label is shown on the plot, the comment is kept off the plot.
shifts = {"H1": 1.15, "H2": 2.32, "H3": 4.12, "H4": 1.26, "H5": 3.55, "H6": 0.92}


def nearest(ppm):
    return min(shifts, key=lambda k: abs(shifts[k] - ppm))


for p in cross:
    a, b = nearest(p.x), nearest(p.y)
    p.label = f"{a}/{b}"
    p.comment = f"3J coupling {a}-{b}; intensity {p.intensity:.3g}"

fig.export_peaks("cross_peaks.csv")
fig.show_peak_table()
fig.show()
