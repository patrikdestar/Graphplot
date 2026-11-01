"""An ordinary matplotlib script, ported by changing one import line.

    PYTHONPATH=build/python python examples/matplotlib_port.py
"""
import numpy as np

import plotter.pyplot as plt   # was: import matplotlib.pyplot as plt

x = np.linspace(0, 4 * np.pi, 400)
plt.figure(figsize=(8, 5))
plt.plot(x, np.sin(x), "b-", label=r"$\sin(x)$")
plt.plot(x, np.cos(x), "r--", label=r"$\cos(x)$")
plt.errorbar(x[::25], np.sin(x[::25]), yerr=0.1, fmt="ko", label="measured")
plt.fill_between(x, np.sin(x) - 0.2, np.sin(x) + 0.2, alpha=0.2, color="b")
plt.axhline(0, color="gray", ls=":")
plt.xlabel(r"$\omega t$ (rad)")
plt.ylabel("amplitude")
plt.title("Matplotlib-style script")
plt.legend(loc="lower left")
plt.savefig("matplotlib_port.svg")
plt.show()
