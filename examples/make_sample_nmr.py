"""Write a synthetic 1H-1H COSY spectrum in Bruker TopSpin processed format.

    python examples/make_sample_nmr.py [output_dir]      (default: sample_nmr)

Creates <output_dir>/cosy/1/pdata/1/{2rr,procs,proc2s,title}. Open it with

    ./build/graphplot sample_nmr/cosy/1
or
    fig.open_bruker("sample_nmr/cosy/1")

The "molecule" is ethyl propanoate-like: coupled proton pairs produce
off-diagonal cross peaks symmetric about the diagonal. Only numpy is needed.
"""
import os
import sys

import numpy as np

# Proton shifts (ppm) and which ones are scalar-coupled (-> cross peaks).
SHIFTS = {"H1 (CH3)": 1.15, "H2 (CH2)": 2.32, "H3 (OCH2)": 4.12, "H4 (CH3)": 1.26,
          "H5 (CH)": 3.55, "H6 (CH3)": 0.92}
COUPLED = [("H1 (CH3)", "H2 (CH2)"), ("H3 (OCH2)", "H4 (CH3)"), ("H5 (CH)", "H6 (CH3)"),
           ("H2 (CH2)", "H5 (CH)")]

SI = 1024          # points per dimension
XDIM = 128         # submatrix (tile) size, as TopSpin typically writes
SF = 600.13        # MHz
OFFSET = 6.0       # ppm of the first (left-most) point
SW_P = 6.0 * SF    # spectral width in Hz (6 ppm)


def main(out="sample_nmr"):
    ppm = OFFSET - np.arange(SI) * SW_P / (SF * SI)
    F2, F1 = np.meshgrid(ppm, ppm)  # rows = F1, columns = F2
    rng = np.random.default_rng(7)
    z = np.zeros((SI, SI))

    def peak(x, y, a, w=0.012):
        return a * np.exp(-((F2 - x) ** 2 + (F1 - y) ** 2) / (2 * w * w))

    for name, d in SHIFTS.items():               # diagonal peaks (strong)
        z += peak(d, d, 1.0)
    for a, b in COUPLED:                         # cross peaks, symmetric
        amp = 0.12 * rng.uniform(0.6, 1.0)
        z += peak(SHIFTS[a], SHIFTS[b], amp) + peak(SHIFTS[b], SHIFTS[a], amp)
    z += peak(2.9, 1.7, -0.05)                   # a negative artefact
    z += 0.0015 * rng.standard_normal(z.shape)   # noise

    pdata = os.path.join(out, "cosy", "1", "pdata", "1")
    os.makedirs(pdata, exist_ok=True)
    nc = int(np.floor(np.log2(np.abs(z).max()))) - 28
    ints = np.round(z * 2.0 ** -nc).astype("<i4")
    # Reorder into XDIM x XDIM tiles, tiles row-major (TopSpin layout).
    tiles = ints.reshape(SI // XDIM, XDIM, SI // XDIM, XDIM).swapaxes(1, 2)
    tiles.tofile(os.path.join(pdata, "2rr"))
    for name in ("procs", "proc2s"):
        with open(os.path.join(pdata, name), "w") as f:
            f.write("##TITLE= synthetic\n##$AXNUC= <1H>\n##$BYTORDP= 0\n##$DTYPP= 0\n"
                    f"##$NC_proc= {nc}\n##$OFFSET= {OFFSET}\n##$SF= {SF}\n##$SI= {SI}\n"
                    f"##$SW_p= {SW_P}\n##$XDIM= {XDIM}\n##END=\n")
    with open(os.path.join(pdata, "title"), "w") as f:
        f.write("Synthetic 1H-1H COSY\n")
    print("wrote", os.path.dirname(os.path.dirname(pdata)))
    print("expected cross peaks (F2, F1):")
    for a, b in COUPLED:
        print(f"  {a:10s} / {b:10s}: ({SHIFTS[a]:.2f}, {SHIFTS[b]:.2f}) and ({SHIFTS[b]:.2f}, {SHIFTS[a]:.2f})")


if __name__ == "__main__":
    main(*sys.argv[1:2])
