# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""Publication-style plot in the spirit of CERN/LHC experiment figures:
stacked/filled histograms, data points with Poisson errors, an uncertainty
band, a ratio panel and experiment labels - all appearance from the
'science' style (edit it with Style > Edit style..., or save your own).

    PYTHONPATH=build/python python examples/hep_style.py
"""
import numpy as np

import plotter

rng = np.random.default_rng(7)
edges = np.linspace(100, 180, 41)
centres = 0.5 * (edges[1:] + edges[:-1])
width = np.diff(edges)

background = 40000 * np.exp(-(centres - 100) / 25) * width / 2
signal = 900 * np.exp(-0.5 * ((centres - 125) / 2.0) ** 2) * width / 2
expected = background + signal
data = rng.poisson(expected)

plotter.style.use("science")
fig = plotter.figure(size=(800, 760))
fig.xlabel = "$m_{\\gamma\\gamma}$ (GeV)"
main, ratio = fig.ratio_panel(0.28, ylabel="Data / Bkg.")

main.hist(centres, edges, weights=background, histtype="stepfilled", label="Background")
main.fill_between(centres, background * 1.04, background * 0.96, step="mid",
                  color="#9c9ca1", alpha=0.5, label="Bkg. uncertainty")
main.hist(centres, edges, weights=expected, label="Signal + background")
main.errorbar(centres, data, yerr=np.sqrt(data), xerr=width / 2, label="Data", color="black")
main.ylabel = "Events / 2 GeV"

# Draw order = stacking order: band first, then the points on top.
ratio.fill_between(centres, np.full_like(centres, 1.04), np.full_like(centres, 0.96),
                   step="mid", color="#9c9ca1", alpha=0.5)
ratio.errorbar(centres, data / background, yerr=np.sqrt(data) / background, xerr=width / 2,
               color="black")
ratio.plot([100, 180], [1, 1], color="gray", style="--")

fig.set_style({"label.top.left": "**CMS** *Simulation Preliminary*",
               "label.top.right": "138 fb$^{-1}$ (13 TeV)",
               "label.inside": "H $\\to\\gamma\\gamma$"})
main.autoscale()
main.set_xlim(100, 180)
ratio.set_ylim(0.8, 1.35)
fig.savefig("hep_style.pdf")   # vector output
fig.savefig("hep_style.png")
fig.show()
