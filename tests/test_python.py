# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""Tests for the Python API. Run headless:

    QT_QPA_PLATFORM=offscreen PYTHONPATH=build/python python -m unittest -v tests/test_python.py
"""
import os
import tempfile
import threading
import time
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import numpy as np  # noqa: E402

import plotter  # noqa: E402


class PlotterTests(unittest.TestCase):
    def setUp(self):
        self.fig = plotter.figure("test")
        self.fig.show(block=False)
        self.x = np.linspace(0, 100, 2000)

    def tearDown(self):
        self.fig.close()
        plotter.pause(0.01)

    def test_example_from_spec(self):
        fig, x = self.fig, self.x
        y1, y2 = np.sin(x), np.cos(x)
        l1 = fig.plot(x, y1, label="Signal 1", color="blue")
        fig.plot(x, y2, label="Signal 2", color="red")
        fig.marker(25.0, color="green", label="Marker 1")
        fig.marker(50.0, color="orange", label="Marker 2")
        self.assertEqual(len(fig.lines()), 2)
        np.testing.assert_allclose(l1.y, y1)
        self.assertEqual(fig.get_marker_positions(),
                         [("Marker 1", "v", 25.0), ("Marker 2", "v", 50.0)])
        corrected = fig.plot(x, y1 - np.mean(y1), label="Corrected signal", color="black")
        self.assertAlmostEqual(float(np.mean(corrected.y)), 0.0, places=12)
        self.assertEqual(corrected.color, "#0b0b0b")

    def test_plot_options_and_handles(self):
        line = self.fig.plot(self.x, self.x ** 2, label="q", style="--", width=2.5, marker="o")
        self.assertEqual(line.style, "dash")
        self.assertEqual(line.marker, "circle")
        self.assertEqual(line.width, 2.5)
        line.color = (1.0, 0.0, 0.0)
        self.assertEqual(line.color, "#ff0000")
        line.visible = False
        self.assertFalse(line.visible)
        self.fig.undo()  # visibility change is undoable
        self.assertTrue(line.visible)
        with self.assertRaises(ValueError):
            self.fig.plot([1, 2, 3], [1, 2])
        with self.assertRaises(ValueError):
            self.fig.plot(self.x, self.x, color="no-such-colour")

    def test_y_only_and_int_input(self):
        line = self.fig.plot([3, 1, 2])
        np.testing.assert_array_equal(line.x, [0, 1, 2])
        np.testing.assert_array_equal(line.y, [3, 1, 2])

    def test_replace_updates_in_place(self):
        a = self.fig.plot(self.x, np.sin(self.x), label="s")
        b = self.fig.plot(self.x[:10], np.zeros(10), label="s", replace=True)
        self.assertEqual(len(self.fig.lines()), 1)
        self.assertEqual(len(a.x), 10)
        self.assertEqual(repr(a), repr(b))

    def test_set_data_not_undoable_by_default(self):
        line = self.fig.plot(self.x, self.x)
        line.set_data([0, 1], [2, 3])
        np.testing.assert_array_equal(line.y, [2, 3])
        line.set_data([0], [5], undoable=True)
        self.fig.undo()
        np.testing.assert_array_equal(line.y, [2, 3])

    def test_remove_and_undo_keep_handle(self):
        line = self.fig.plot(self.x, self.x, label="r")
        line.remove()
        self.assertFalse(line.exists)
        with self.assertRaises(KeyError):
            _ = line.y
        self.fig.undo()
        self.assertTrue(line.exists)
        self.assertEqual(line.label, "r")

    def test_markers_and_annotations(self):
        m = self.fig.marker(10.0, "h", label="baseline", color="#336699", style=":")
        self.assertEqual(m.orientation, "h")
        m.position = 12.5
        self.assertEqual(m.position, 12.5)
        self.fig.undo()
        self.assertEqual(m.position, 10.0)
        a = self.fig.annotate("peak", 30, 0.9, color="red", fontsize=14)
        a.text = "big peak"
        a.position = (31, 0.8)
        self.assertEqual(a.position, (31.0, 0.8))
        self.assertEqual(self.fig.annotations()[0].text, "big peak")
        self.fig.remove(a)
        self.assertEqual(self.fig.annotations(), [])

    def test_region(self):
        self.assertIsNone(self.fig.get_selected_region())
        self.fig.set_selected_region(40, 20)
        self.assertEqual(self.fig.get_selected_region(), (20.0, 40.0))
        mask = self.fig.region_mask(self.x)
        self.assertTrue(np.all(self.x[mask] >= 20))
        self.fig.clear_selection()
        self.assertIsNone(self.fig.get_selected_region())
        self.fig.undo()
        self.assertEqual(self.fig.get_selected_region(), (20.0, 40.0))

    def test_axes_and_view(self):
        self.fig.plot(self.x, np.sin(self.x))
        self.fig.title = "T"
        self.fig.xlabel = "time (s)"
        self.assertEqual(self.fig.xlabel, "time (s)")
        self.fig.set_xlim(10, 20)
        self.assertEqual(self.fig.xlim(), (10.0, 20.0))
        self.fig.reset_view()
        self.assertEqual(self.fig.xlim(), (0.0, 100.0))

    def test_callbacks(self):
        events = []
        self.fig.on("region_changed", lambda a, b: events.append(("region", a, b)))
        self.fig.on("marker_changed", lambda m: events.append(("marker", m.position)))
        self.fig.set_selected_region(1, 2)
        m = self.fig.marker(5.0)
        m.position = 6.0
        plotter.pause(0.05)
        self.assertIn(("region", 1.0, 2.0), events)
        self.assertIn(("marker", 6.0), events)

    def test_callback_exception_does_not_crash(self):
        def bad(*_):
            raise RuntimeError("boom")
        self.fig.on("region_changed", bad)
        self.fig.set_selected_region(1, 2)
        plotter.pause(0.05)  # exception is reported as "unraisable", loop survives
        self.assertEqual(self.fig.get_selected_region(), (1.0, 2.0))

    def test_calls_from_worker_thread_and_wait_for_region(self):
        # A worker thread edits the figure while the main thread waits in the
        # event loop - exercises the GUI-thread marshalling.
        self.fig.plot(self.x, np.sin(self.x))
        result = {}

        def worker():
            time.sleep(0.1)
            result["line"] = self.fig.plot(self.x, np.cos(self.x), label="from thread")
            self.fig.set_selected_region(10, 30)

        t = threading.Thread(target=worker)
        t.start()
        region = self.fig.wait_for_region(timeout=5)
        t.join()
        self.assertEqual(region, (10.0, 30.0))
        self.assertEqual(result["line"].label, "from thread")

    def test_wait_for_region_timeout(self):
        self.assertIsNone(self.fig.wait_for_region(timeout=0.1))

    def test_contour(self):
        yy, xx = np.mgrid[-1:1:200j, -1:1:300j]
        z = np.exp(-(xx ** 2 + yy ** 2) / 0.1) - 0.5 * np.exp(-((xx - 0.6) ** 2 + yy ** 2) / 0.02)
        c = self.fig.contour(z, x=(-1, 1), y=np.linspace(-1, 1, 200), label="2D", levels=8)
        self.assertEqual(c.extent, (-1.0, 1.0, -1.0, 1.0))
        self.assertEqual(c.z.shape, (200, 300))
        base = c.base_level
        self.assertGreater(base, 0)
        self.assertEqual(len(c.levels()), 16)  # positive + negative
        self.assertLess(max(c.levels()), np.abs(z).max())
        c.scale_levels(0.5)
        self.assertAlmostEqual(c.base_level, base / 2)
        self.fig.undo()
        self.assertAlmostEqual(c.base_level, base)
        c.base_level = 0.01
        c.negative = False
        self.assertEqual(len(c.levels()), 8)
        c.set_data(z * 2)
        np.testing.assert_allclose(c.z, z * 2)
        with self.assertRaises(ValueError):
            self.fig.contour(z, x=np.r_[0, 1, 5, np.arange(3, 300)])  # not uniform
        with self.assertRaises(ValueError):
            self.fig.contour(np.zeros(5))

    def test_levels_changed_callback(self):
        got = []
        z = np.random.default_rng(0).normal(size=(50, 50))
        c = self.fig.contour(z)
        self.fig.on("levels_changed", lambda h: got.append(h.base_level))
        self.fig.scale_contour_levels(2.0)
        plotter.pause(0.05)
        self.assertEqual(len(got), 1)
        self.assertAlmostEqual(got[0], c.base_level)

    def test_open_bruker_matches_nmrglue_layout(self):
        """Write a tiled int32 2rr exactly as nmrglue orders it, read it back."""
        try:
            from nmrglue.fileio import bruker as ngb
        except ImportError:
            self.skipTest("nmrglue not installed")
        si1, si2, xd1, xd2 = 64, 128, 16, 32
        rng = np.random.default_rng(3)
        data = rng.integers(-2**20, 2**20, size=(si1, si2)).astype(np.int32)
        nc = -3
        with tempfile.TemporaryDirectory() as d:
            pdata = os.path.join(d, "exp", "1", "pdata", "1")
            os.makedirs(pdata)
            tiles = ngb.reorder_submatrix(data, (si1, si2), (xd1, xd2), reverse=True)
            np.asarray(tiles, dtype="<i4").tofile(os.path.join(pdata, "2rr"))
            for name, si, xd, sw, sf, off, nuc in (("procs", si2, xd2, 6000.0, 600.13, 10.5, "1H"),
                                                   ("proc2s", si1, xd1, 24000.0, 150.9, 160.0, "13C")):
                with open(os.path.join(pdata, name), "w") as f:
                    f.write(f"##$AXNUC= <{nuc}>\n##$BYTORDP= 0\n##$DTYPP= 0\n##$NC_proc= {nc}\n"
                            f"##$OFFSET= {off}\n##$SF= {sf}\n##$SI= {si}\n##$SW_p= {sw}\n"
                            f"##$XDIM= {xd}\n##END=\n")
            # Reference: nmrglue's reader on the same files.
            dic, ref = ngb.read_pdata(pdata, read_acqus=False)
            c = self.fig.open_bruker(os.path.join(d, "exp", "1"))  # experiment folder
            np.testing.assert_allclose(c.z, ref)
            np.testing.assert_allclose(c.z, data * 2.0 ** nc)
            x0, x1, y0, y1 = c.extent
            self.assertAlmostEqual(x0, 10.5)
            self.assertAlmostEqual(x1, 10.5 - (si2 - 1) * 6000.0 / (600.13 * si2))
            self.assertAlmostEqual(y0, 160.0)
            self.assertAlmostEqual(y1, 160.0 - (si1 - 1) * 24000.0 / (150.9 * si1))
        self.assertTrue(self.fig.x_reversed and self.fig.y_reversed)
        self.assertFalse(self.fig.diagonal)  # heteronuclear
        self.assertEqual(self.fig.xlabel, "F2 1H (ppm)")
        with self.assertRaises(RuntimeError):
            self.fig.open_bruker("/nonexistent")

    def test_nmr_cross_peak_workflow(self):
        """Open a COSY, pick off-diagonal cross peaks, assign and export them."""
        import importlib.util
        here = os.path.dirname(os.path.abspath(__file__))
        spec = importlib.util.spec_from_file_location(
            "make_sample_nmr", os.path.join(here, "..", "examples", "make_sample_nmr.py"))
        gen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gen)
        with tempfile.TemporaryDirectory() as d:
            import contextlib
            import io
            with contextlib.redirect_stdout(io.StringIO()):
                gen.main(d)
            c = self.fig.open_bruker(os.path.join(d, "cosy", "1"))
            self.assertTrue(self.fig.diagonal)  # homonuclear -> diagonal shown
            cross = self.fig.pick_peaks(exclude_diagonal=0.1, region="all")
            expected = {(round(gen.SHIFTS[a], 2), round(gen.SHIFTS[b], 2))
                        for pair in gen.COUPLED for a, b in (pair, pair[::-1])}
            found = {(round(p.x, 2), round(p.y, 2)) for p in cross}
            self.assertEqual(found, expected)
            self.assertEqual(self.fig.pick_peaks(exclude_diagonal=0.1, region="all"), [])
            # Assign: label shown on the plot, comment only in the table/CSV.
            for p in cross:
                a = min(gen.SHIFTS, key=lambda k: abs(gen.SHIFTS[k] - p.x))
                b = min(gen.SHIFTS, key=lambda k: abs(gen.SHIFTS[k] - p.y))
                p.label = f"{a.split()[0]}/{b.split()[0]}"
                p.comment = f"{a} couples to {b}, 3J"
            self.assertEqual(len(self.fig.peak_table()), 8)
            csv = os.path.join(d, "peaks.csv")
            self.fig.export_peaks(csv)
            labels = sorted(p.label for p in self.fig.peaks())
            self.fig.clear_peaks()
            self.assertEqual(self.fig.peaks(), [])
            self.fig.import_peaks(csv)
            self.assertEqual(sorted(p.label for p in self.fig.peaks()), labels)
            self.assertIn("3J", self.fig.peaks()[0].comment)
            manual = self.fig.add_peak(1.17, 2.30, label="snap")
            self.assertAlmostEqual(manual.x, 1.15, places=2)  # snapped to the maximum
            self.assertGreater(manual.intensity, c.base_level)

    def test_styles(self):
        self.assertIn("science", plotter.style.available())
        keys = plotter.style.keys()
        self.assertIn("xtick.direction", keys)
        errors = self.fig.use_style("science")
        self.assertEqual(errors, [])
        self.assertEqual(self.fig.get_style_param("xtick.minor.visible"), "true")
        self.fig.set_style({"font.size": 14, "axes.prop_cycle": ["red", (0, 0.5, 1.0)]},
                           xtick_direction="out")
        self.assertEqual(self.fig.get_style_param("xtick.direction"), "out")
        self.assertEqual(self.fig.get_style_param("axes.prop_cycle"), "#e34948, #0080ff")
        self.fig.undo()
        self.assertEqual(self.fig.get_style_param("xtick.direction"), "in")
        with self.assertRaises(ValueError):
            self.fig.set_style({"axes.linewidth": "thick"})
        with tempfile.TemporaryDirectory() as d:
            fn = os.path.join(d, "s.gpstyle")
            self.fig.save_style(fn)
            with open(fn) as f:
                self.assertIn("xtick.direction: in", f.read())
            # global default affects new figures only
            plotter.style.use(fn)
            try:
                g = plotter.figure()
                self.assertEqual(g.get_style_param("xtick.direction"), "in")
                self.assertEqual(self.fig.get_style_param("xtick.direction"), "in")
                plotter.style.set({"font.size": 21})
                self.assertEqual(plotter.style.get("font.size"), "21")
                g.close()
            finally:
                plotter.style.reload()
        self.assertEqual(plotter.style.get("font.size"), "10")

    def test_errorbar_hist_fill(self):
        rng = np.random.default_rng(1)
        data = rng.normal(0, 1, 5000)
        counts, edges, step = self.fig.hist(data, bins=20, range=(-4, 4), label="MC")
        np.testing.assert_array_equal(counts, np.histogram(data, 20, (-4, 4))[0])
        self.assertEqual(step.step, "post")
        self.assertEqual(len(step.x), len(edges) + 1)  # closed outline
        c2, e2, pts = self.fig.hist(data, bins=20, range=(-4, 4), histtype="errorbar", label="Data")
        lo, hi = pts.yerr
        np.testing.assert_allclose(lo, np.sqrt(c2))
        np.testing.assert_allclose(pts.x, 0.5 * (e2[1:] + e2[:-1]))
        cd, _, _ = self.fig.hist(data, bins=20, range=(-4, 4), density=True, histtype="stepfilled")
        self.assertAlmostEqual(float((cd * np.diff(e2)).sum()), 1.0)
        e = self.fig.errorbar([1, 2, 3], [1, 4, 9], yerr=[[0.1, 0.2, 0.3], [1, 2, 3]], xerr=0.5)
        lo, hi = e.yerr
        np.testing.assert_allclose(hi, [1, 2, 3])
        with self.assertRaises(ValueError):
            self.fig.errorbar([1, 2], [1, 2], yerr=[1, 2, 3])
        n_before = len(self.fig.lines())
        band = self.fig.fill_between([0, 1, 2], [2, 3, 4], [1, 1, 1], label="band", alpha=0.25)
        self.assertEqual(len(self.fig.lines()), n_before + 2)
        self.assertTrue(band.fill.startswith("#40"))  # alpha 0.25 -> 0x40
        self.fig.undo()  # the band is one undo step
        self.assertEqual(len(self.fig.lines()), n_before)
        # Unlabelled lines are not legend entries; labelling adds them.
        line = self.fig.plot([0, 1], [0, 1])
        line.label = "now labelled"
        line.fill = True
        self.assertIsNotNone(line.fill)
        line.fill = None
        self.assertIsNone(line.fill)

    def test_subplots_and_ratio_panel(self):
        x = np.linspace(0, 10, 50)
        self.fig.plot(x, np.sin(x), label="main")
        main, lower = self.fig.ratio_panel(0.3)
        self.assertEqual(len(self.fig.axes), 2)
        self.assertEqual(main, self.fig.axes[0])
        r = lower.plot(x, np.ones_like(x), label="ratio")
        self.assertEqual([l.label for l in main.lines()], ["main"])  # content stays in panel 0
        self.assertEqual([l.label for l in lower.lines()], ["ratio"])
        # Handles stay bound to their panel, whatever is current.
        self.fig.sca(main)
        self.assertEqual(self.fig.gca(), main)
        main.xlabel = "x"
        self.assertEqual(r.label, "ratio")
        r.color = "red"
        self.assertEqual(lower.lines()[0].color, "#e34948")
        lower.set_xlim(2, 4)
        self.assertEqual(main.xlim(), (2.0, 4.0))  # shared x
        self.assertEqual(lower.ylabel, "Ratio")
        events = []
        self.fig.on("marker_changed", lambda m: events.append(m.position))
        m = lower.marker(3.0)
        m.position = 3.5
        plotter.pause(0.05)
        self.assertIn(3.5, events)
        grid = self.fig.subplots(2, 2)
        self.assertEqual(len(grid), 2)
        self.assertEqual(len(grid[0]), 2)
        grid[1][1].plot([0, 1], [1, 0], label="corner")
        self.assertEqual(self.fig.gca(), grid[1][1])
        with tempfile.TemporaryDirectory() as d:
            self.fig.savefig(os.path.join(d, "grid.svg"))

    def test_log_axes(self):
        x = np.linspace(1, 100, 100)
        self.fig.plot(x, x ** 3, label="cubic")
        self.fig.y_log = True
        self.fig.autoscale()
        lo, hi = self.fig.ylim()
        self.assertGreater(lo, 0)
        self.assertGreaterEqual(hi, 1e6)
        self.fig.undo()
        self.assertFalse(self.fig.y_log)

    def test_pyplot_compat(self):
        import plotter.pyplot as plt
        x = np.linspace(0, 10, 100)
        plt.figure(figsize=(6, 4))
        lines = plt.plot(x, np.sin(x), "r--", x, np.cos(x), "bo", label="trig")
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[0].style, "dash")
        self.assertEqual(lines[1].marker, "circle")
        plt.errorbar(x[:5], x[:5], yerr=0.1, fmt="ks", label="pts")
        plt.hist(np.random.default_rng(0).normal(5, 1, 500), bins=20, histtype="stepfilled", alpha=0.5)
        plt.axvline(5, color="green", ls=":")
        plt.xlabel(r"$\theta$ (rad)")
        plt.ylabel("value")
        plt.title("test")
        plt.xlim(0, 8)
        self.assertEqual(plt.xlim(), (0.0, 8.0))
        plt.yscale("log")
        plt.legend(loc="lower left", frameon=False)
        self.assertEqual(plt.gcf().native.get_style_param("legend.loc"), "lower left")
        fig, axs = plt.subplots(2, 2, sharex=True, figsize=(5, 5))
        self.assertEqual(len(axs), 2)
        axs[1][0].plot([1, 2], [3, 4])
        axs[1][0].set_xlabel("x")
        self.assertEqual(axs[1][0].get_xlabel(), "x")
        fig, (a, b) = plt.subplots(2, 1, gridspec_kw={"height_ratios": [3, 1], "hspace": 0})
        b.stairs([1, 3, 2], [0, 1, 2, 3], fill=True)
        a.scatter([1, 2, 3], [1, 2, 3], s=36, c="purple")
        yy, xx = np.mgrid[-1:1:50j, -1:1:60j]
        c = a.contour(xx, yy, np.exp(-(xx ** 2 + yy ** 2) * 4), levels=6)
        self.assertEqual(c.n_levels, 6)
        with tempfile.TemporaryDirectory() as d:
            plt.savefig(os.path.join(d, "f.png"), dpi=50)
            self.assertTrue(os.path.getsize(os.path.join(d, "f.png")) > 1000)
        with self.assertRaises(TypeError):
            plt.plot([1, 2], [1, 2], bogus=3)
        plt.close("all")

    def test_timeline(self):
        t = np.arange(0, 100, 0.01)
        ax = np.where((t > 40) & (t < 45), -3.0, 0.0)
        speed = 20 - np.clip(t - 40, 0, 10) * 0.8
        tl = plotter.timeline("drive")
        tl._show()
        imu = tl.add_signals("IMU", t, {"ax": ax, "ay": 0 * t})
        gps = tl.add_row("GPS")
        sp = gps.plot(t, speed, label="speed")
        self.assertEqual([r.name for r in tl.rows], ["IMU", "GPS"])
        self.assertEqual(tl.full_range, (0.0, t[-1]))
        events = []
        tl.on("window_changed", lambda a, b: events.append(("w", a, b)))
        tl.on("pins_changed", lambda: events.append(("p",)))
        tl.window = (30, 50)
        self.assertEqual(tl.window, (30.0, 50.0))
        plotter.pause(0.05)
        self.assertIn(("w", 30.0, 50.0), events)
        v = tl.values_at(42.5)
        self.assertEqual(v[("IMU", "ax")], -3.0)
        self.assertAlmostEqual(v[("GPS", "speed")], 18.0)
        tl.cursor_time = 42.5
        self.assertEqual(tl.cursor_time, 42.5)
        p = tl.pin(41.0, "brake")
        self.assertEqual([x.label for x in tl.pins()], ["brake"])
        p.t = 40.5
        self.assertEqual(tl.pins()[0].t, 40.5)
        tl.undo()
        self.assertEqual(tl.pins()[0].t, 41.0)
        p.jump()
        self.assertAlmostEqual(sum(tl.window) / 2, 41.0)
        self.assertIn(("p",), events)
        tl.navigator = "speed"
        self.assertEqual(tl.navigator, "GPS / speed")
        moved = tl.move_signal(sp, imu)
        self.assertEqual(len(imu.lines()), 3)
        self.assertEqual(moved.label, "speed")
        self.assertTrue(tl.remove_row(gps))
        self.assertEqual(len(tl.rows), 1)
        r2 = tl.add_row("extra")
        tl.set_row_order([r2, imu])
        self.assertEqual(tl.rows[0], r2)
        r2.height = 0.5
        r2.visible = False
        self.assertFalse(r2.visible)
        tl.auto_fit_y = False
        self.assertFalse(tl.auto_fit_y)
        with self.assertRaises(ValueError):
            tl.clock_time = True  # no absolute timestamps
        tl.time_origin = 1714658600.0
        tl.clock_time = True
        self.assertTrue(tl.format_time(1.25).endswith(".250"))
        with tempfile.TemporaryDirectory() as d:
            tl.savefig(os.path.join(d, "tl.png"))
        tl.close()

    def test_timeline_csv(self):
        import importlib.util
        here = os.path.dirname(os.path.abspath(__file__))
        spec = importlib.util.spec_from_file_location(
            "make_sample_vehicle", os.path.join(here, "..", "examples", "make_sample_vehicle.py"))
        gen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gen)
        with tempfile.TemporaryDirectory() as d:
            import contextlib
            import io
            with contextlib.redirect_stdout(io.StringIO()):
                gen.main(d)
            tl = plotter.timeline()
            tl.load_csv(*(os.path.join(d, f) for f in ("imu.csv", "gps.csv", "wheels.csv")))
            self.assertEqual([r.name for r in tl.rows], ["imu", "gps", "wheels"])
            self.assertAlmostEqual(tl.time_origin, gen.T0)
            lo, hi = tl.full_range
            self.assertAlmostEqual(lo, 0.0, places=3)
            self.assertGreater(hi, 119.9)
            # Epoch s, epoch ms and ISO times line up: speed from GPS and wheels agree.
            v = tl.values_at(30.0)
            self.assertAlmostEqual(v[("gps", "speed [m/s]")], v[("wheels", "wheel_FR [m/s]")], delta=0.5)
            with self.assertRaises(RuntimeError):
                tl.load_csv(os.path.join(d, "missing.csv"))
            tl.close()

    def test_audit_handles_and_lifetime(self):
        # Handles act on their own panel / figure, whatever is current.
        l0 = self.fig.plot([0, 1], [0, 1], label="panel0")
        a0, a1 = self.fig.subplots(2, 1)
        l1 = a1.plot([0, 1], [1, 0], label="panel1")
        self.fig.sca(a0)
        self.fig.remove(l1)
        self.assertEqual([l.label for l in a0.lines()], ["panel0"])
        self.assertEqual(a1.lines(), [])
        with self.assertRaises(KeyError):
            l1.remove()  # already removed
        other = plotter.figure()
        with self.assertRaises(ValueError):
            other.remove(l0)  # belongs to another figure
        with self.assertRaises(ValueError):
            self.fig.sca(a1)
            self.fig.plot([0, 1], [0, 1], fill=True, fill_to=l0)  # other panel
        # Axes properties are read from that panel.
        a1.x_log = True
        self.assertTrue(a1.x_log)
        self.assertFalse(a0.x_log)
        # Closed figures are not re-opened by show() and are forgotten.
        other.close()
        plotter.show(block=False)
        self.assertFalse(other.is_open)
        self.assertNotIn(other, plotter._figures)
        # Colours: separate alpha scale, numpy scalar errors, datetime64 input.
        c = self.fig.plot([0, 1], [0, 1], color=(255, 0, 0, 0.5))
        self.assertEqual(c.color, "#80ff0000")
        e = self.fig.errorbar([0, 1], [0, 1], yerr=np.float32(0.1))
        self.assertAlmostEqual(float(e.yerr[0][0]), 0.1, places=6)
        t = np.array(["2024-05-02T14:03:20", "2024-05-02T14:03:21"], dtype="datetime64[ms]")
        d = self.fig.plot(t, [1, 2])
        self.assertAlmostEqual(d.x[0], 1714658600.0)
        with self.assertRaises(ValueError):
            self.fig.plot([0, 1], [0, 1], color=(300, 0, 0))

    def test_audit_timeline_api(self):
        tl = plotter.timeline()
        t = np.array(["2024-05-02T14:03:20.000", "2024-05-02T14:03:20.500", "2024-05-02T14:03:21.000"],
                     dtype="datetime64[ms]")
        row = tl.add_row("imu")
        row.plot(t, [0.0, 1.0, 0.0], label=None)  # label None is fine
        row.plot(t, [5.0, 6.0, 7.0], label="b")
        self.assertAlmostEqual(tl.time_origin, 1714658600.0)
        self.assertEqual(list(row.lines()[1].x), [0.0, 0.5, 1.0])
        tl.xlabel = "Time"  # forwarded to the figure
        self.assertEqual(tl.figure.xlabel, "Time")
        self.assertIn("imu / b", tl.signals())
        self.assertEqual(tl.signal("b").label, "b")
        p = tl.pin(0.75, "event")
        tl.window = (0.0, 0.2)
        self.assertEqual(tl.next_pin().id, p.id)
        self.assertIsNone(tl.next_pin())
        tl.remove_pin(p)
        tl.window = (0.25, 1.0)
        data = tl.window_data()
        self.assertEqual(list(data["imu / b"][0]), [0.5, 1.0])
        p = tl.pin(0.5, "x")
        tl.remove_pin(p)
        tl.pin(0.2)
        tl.pin(0.3)
        tl.clear_pins()
        self.assertEqual(tl.pins(), [])
        with tempfile.TemporaryDirectory() as d:
            long_csv = os.path.join(d, "long.csv")
            tl.export_csv(long_csv, window=False)
            with open(long_csv) as f:
                rows = f.read().splitlines()
            self.assertEqual(rows[0], "signal,time_s,time_utc,value")
            self.assertEqual(len(rows), 1 + 6)
            wide = os.path.join(d, "wide.csv")
            tl.export_csv(wide, window=(0, 1), resample=0.25)
            with open(wide) as f:
                lines = f.read().splitlines()
            self.assertTrue(lines[0].startswith("time_s,time_utc,imu / "))
            self.assertEqual(len(lines), 1 + 5)  # 0, .25, .5, .75, 1
        cb = lambda *a: None  # noqa: E731
        tl.on("window_changed", cb)
        self.assertTrue(tl.off("window_changed", cb))
        self.assertFalse(tl.off("window_changed", cb))
        tl.close()

    def test_region_stats(self):
        fig = self.fig
        fig.plot([0, 1, 2, 3, 4], [0, 1, 2, 3, 4], label="a")
        fig.plot([0, 1, 2], [5, 5, 5], label="a")  # same name -> distinct key
        with self.assertRaises(ValueError):
            fig.region_stats()  # nothing selected
        with self.assertRaises(ValueError):
            fig.region_stats(1)  # only one bound
        st = fig.region_stats(4, 0)
        self.assertEqual(len(st), 2)
        self.assertEqual(st["a"]["n"], 5)
        self.assertAlmostEqual(st["a"]["mean"], 2.0)
        self.assertAlmostEqual(st["a"]["integral"], 8.0)
        self.assertEqual(st["a"]["x_at_max"], 4.0)

    def test_audit_round2(self):
        f = self.fig
        self.assertTrue(np.isnan(f.plot(np.ma.array([1, 2, 3.], mask=[0, 1, 0])).y[1]))
        self.assertTrue(np.isnan(f.plot(np.array(["2024-01-01", "NaT"], "datetime64[s]"), [1, 2]).x[1]))
        f.plot([1, 2, 3], [1, 2, 3], label="a", yerr=1)
        line = f.plot([1, 2, 3, 4], [4, 3, 2, 1], label="a", replace=True, yerr=0.5)
        self.assertEqual(list(line.yerr[0]), [0.5] * 4)
        line.set_errors(yerr=0.2)
        f.undo()  # set_errors is undoable
        self.assertEqual(list(line.yerr[0]), [0.5] * 4)
        self.assertEqual(f.plot([1, 2], [1, 2], label=3).label, "3")
        with self.assertRaises(ValueError):
            f.plot([1, 2], [1, 2], yerr=[0.1, -0.1])
        f.contour(np.ones((3, 500)), x=np.linspace(0, 1, 500, dtype=np.float32))
        # A figure that was never shown survives show(block=False).
        g = plotter.figure()
        plotter.show(block=False)
        self.assertIn(g, plotter.figures())
        g.close()
        self.assertNotIn(g, plotter.figures())

    def test_paths_and_timeline_window(self):
        import pathlib
        tl = plotter.timeline()
        t = np.linspace(0, 120, 121)
        tl.add_row("a").plot(t, np.sin(t), label="s")
        tl.window = (40, 50)
        tl.add_row("b").plot(t, np.cos(t), label="c")  # must not move the window
        self.assertEqual(tl.window, (40.0, 50.0))
        tl.figure.close()
        self.fig.plot([1, 2], [1, 2])
        with tempfile.TemporaryDirectory() as d:
            self.fig.savefig(pathlib.Path(d) / "p.png")  # os.PathLike accepted
            self.assertTrue((pathlib.Path(d) / "p.png").exists())
            home = os.environ.get("HOME")
            os.environ["HOME"] = d
            try:
                self.fig.savefig("~/q.svg")  # "~" is the home folder
            finally:
                if home is not None:
                    os.environ["HOME"] = home
            self.assertTrue((pathlib.Path(d) / "q.svg").exists())

    def test_audit_round3(self):
        import datetime as dt
        f = self.fig
        f.plot([0, 1], [0, 1])
        with tempfile.TemporaryDirectory() as d:
            f.savefig(filename=os.path.join(d, "a.png"))  # keyword still works
        # Style colours: alpha scaled on its own; NumPy values; all-or-nothing set().
        f.set_style({"axes.facecolor": (255, 0, 0, 0.5)})
        self.assertEqual(f.get_style_param("axes.facecolor"), "#80ff0000")
        size = plotter.style.get("font.size")
        with self.assertRaises(ValueError):
            plotter.style.set({"font.size": 13, "axes.formatter": "bogus"})
        self.assertEqual(plotter.style.get("font.size"), size)
        # off() removes a bound method.

        class C:
            n = 0

            def cb(self, *a):
                C.n += 1
        c = C()
        f.on("region_changed", c.cb)
        self.assertTrue(f.off("region_changed", c.cb))
        # Colour setters reject None; widths must be >= 0.
        m = f.marker(1.0, color="red")
        with self.assertRaises(ValueError):
            m.color = None
        with self.assertRaises(ValueError):
            m.width = -2
        # Timeline: duplicate names kept apart, NaT origin, naive datetimes UTC,
        # clear_pins is one step, resampled export has no NaN at the edges.
        tl = plotter.timeline()
        base = np.datetime64("2024-05-01T12:00:00")
        t = base + np.arange(10) * np.timedelta64(1, "s")
        t[0] = np.datetime64("NaT")
        tl.add_row("imu").plot(t, np.arange(10.0), label="ax")
        self.assertAlmostEqual(tl.time_origin, 1714564801.0)
        tl.add_row("imu").plot([dt.datetime(2024, 5, 1, 12, 0, i) for i in range(10)],
                               np.arange(10.0), label="ax")
        self.assertEqual(len(tl.signals()), 2)
        self.assertAlmostEqual(tl.signals()["imu / ax #2"].x[1], 0.0)  # UTC, aligned
        for k in range(3):
            tl.pin(1.0 + k)
        tl.clear_pins()
        tl.undo()
        self.assertEqual(len(tl.pins()), 3)
        self.assertIn(tl.pins()[0], tl.pins())
        tl.window = (0.5, 7.5)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "r.csv")
            tl.export_csv(p, resample=1.0)
            rows = open(p).read().splitlines()
        self.assertNotIn("nan", rows[1].split(",")[1])
        tl.figure.close()

    def test_theme_and_tooltips(self):
        self.assertIn("dark", plotter.themes())
        before = plotter.theme()
        try:
            self.assertEqual(plotter.theme("midnight"), "midnight")
            with self.assertRaises(ValueError):
                plotter.theme("nope")
            self.fig.plot([0, 1], [0, 1])
            with tempfile.TemporaryDirectory() as d:
                self.fig.savefig(os.path.join(d, "light.svg"))  # exports stay light by default
                svg = open(os.path.join(d, "light.svg")).read()
            self.assertIn("#ffffff", svg.lower())
            self.assertNotIn("#141d31", svg.lower())  # Midnight's plot background
            self.assertFalse(plotter.tooltips(False))
            self.assertTrue(plotter.tooltips(True))
        finally:
            plotter.theme(before)

    def test_save_load_and_export_data(self):
        import pathlib
        f = self.fig
        f.plot([0, 1, 2], [1, 2, 3], label="a", yerr=0.1)
        pk_fig = plotter.figure()
        y, x = np.mgrid[0:21, 0:21]
        pk_fig.contour(np.exp(-((x - 7) ** 2 + (y - 9) ** 2) / 4.0))
        p = pk_fig.add_peak(7, 9, label="H1", comment="note, with comma")
        f.set_selected_region(0.5, 2)
        with tempfile.TemporaryDirectory() as d:
            f.save(pathlib.Path(d) / "a.gpfig")
            pk_fig.save(os.path.join(d, "p.gpfig"))
            g = plotter.load(os.path.join(d, "a.gpfig"))
            self.assertEqual(list(g.line("a").y), [1.0, 2.0, 3.0])
            self.assertEqual(g.get_selected_region(), (0.5, 2.0))
            q = plotter.load(os.path.join(d, "p.gpfig"))
            self.assertEqual(q.peaks()[0].comment, "note, with comma")
            g.export_data(os.path.join(d, "a.csv"))
            self.assertEqual(open(os.path.join(d, "a.csv")).read().splitlines()[1], "0,1,0.1,0.1")
            g.export_data(os.path.join(d, "r.csv"), region=True)
            self.assertEqual(len(open(os.path.join(d, "r.csv")).read().splitlines()), 3)
            with self.assertRaises(RuntimeError):
                plotter.load(os.path.join(d, "a.csv"))  # not a figure file
            g.close()
            q.close()
        pk_fig.close()

    def test_audit_round4(self):
        import threading
        import plotter.pyplot as plt
        f = self.fig
        # Labels: None and non-strings everywhere.
        m = f.marker(1.0, label=None)
        m.label = 5
        self.assertEqual(m.label, "5")
        line = f.plot([0, 1], [0, 1])
        line.label = None
        with self.assertRaises(ValueError):
            f.marker(1.0, width=-1)
        with self.assertRaises(ValueError):
            f.set_xlim(1, 1)
        f.set_xlim(8, 2)  # high, low: reversed axis
        self.assertTrue(f.x_reversed)
        self.assertEqual(f.xlim(), (2.0, 8.0))
        # Annotation colour reports what is drawn; fontsize None = style default.
        a = f.annotate("t", 0.5, 0.5)
        self.assertNotEqual(a.color, "#000000")
        a.fontsize = None
        with self.assertRaises(ValueError):
            a.fontsize = -5
        # hist ignores NaN; automatic names are not repeated.
        counts, _, _ = f.hist([1.0, 2.0, np.nan, 3.0], bins=3)
        self.assertEqual(counts.sum(), 3)
        names = {ln.label for ln in f.lines()}
        self.assertEqual(len(names), len(f.lines()))
        # show(block=False) from a worker thread works.
        errors = []
        t = threading.Thread(target=lambda: (f.show(block=False) if True else None))
        try:
            t.start()
            while t.is_alive():
                plotter.pause(0.01)
        except Exception as e:  # pragma: no cover
            errors.append(e)
        self.assertEqual(errors, [])
        # pyplot: matplotlib-style limits, hidden "_" labels, closed figure replaced.
        plt.figure()
        plt.plot([0, 1], [0, 1], label="_nolegend_")
        self.assertNotEqual(plt.gca().panel.lines()[0].label, "_nolegend_")  # not shown as a legend entry
        plt.xlim(8, 2)
        self.assertEqual(plt.xlim(), (8.0, 2.0))
        old = plt.gcf()
        old.native.close()
        self.assertIsNot(plt.gcf(), old)
        plt.close("all")
        self.assertIn("plots", plotter.theme_options())

    def test_append_xerr_and_inline_png(self):
        f = self.fig
        line = f.plot([0, 1], [0, 1], xerr=0.2)
        self.assertEqual(list(line.xerr[0]), [0.2, 0.2])
        line.append([2, 3], [4, 5])
        self.assertEqual(list(line.x), [0, 1, 2, 3])
        line.append(np.arange(4.0, 10.0), np.arange(4.0, 10.0), max_points=5)
        self.assertEqual(list(line.x), [5, 6, 7, 8, 9])
        with self.assertRaises(ValueError):
            line.append([1], [1, 2])
        self.assertEqual(f._repr_png_()[:4], b"\x89PNG")

    def test_images_and_colorbar(self):
        import plotter.pyplot as plt
        f = self.fig
        y, x = np.mgrid[0:20, 0:30]
        z = np.sin(x / 5.0) * np.cos(y / 4.0)
        img = f.image(z, (0, 3), (0, 2), cmap="magma", vmin=-1, vmax=1)
        self.assertFalse(img.contours)
        self.assertTrue(img.image and img.colorbar)
        self.assertEqual(img.cmap, "magma")
        self.assertEqual(img.clim, (-1.0, 1.0))
        img.cmap = "viridis_r"
        img.clim = None
        with self.assertRaises(ValueError):
            img.cmap = "rainbowz"
        with self.assertRaises(ValueError):
            f.image(z, vmin=1, vmax=0)
        with tempfile.TemporaryDirectory() as d:
            f.save(os.path.join(d, "i.gpfig"))
            g = plotter.load(os.path.join(d, "i.gpfig"))
            c = g.contours()[0]
            self.assertEqual((c.cmap, c.colorbar, c.contours), ("viridis_r", True, False))
            g.close()
            # pyplot
            fig, ax = plt.subplots()
            im = ax.imshow(z, cmap="gray", extent=(0, 3, 0, 2), origin="lower")
            plt.colorbar(im, label="I")
            self.assertTrue(im.colorbar)
            self.assertEqual(im.label, "I")
            ax2 = plt.figure().gca()
            ax2.pcolormesh(np.linspace(0, 3, 31), np.linspace(0, 2, 21), z)  # edges
            plt.colorbar()
            plt.savefig(os.path.join(d, "img.png"))
            plt.close("all")

    def test_custom_ticks(self):
        import plotter.pyplot as plt
        f = self.fig
        f.plot([1, 2, 3], [1, 2, 3])
        f.set_xticks([1, 2, 3], ["a", "b", "c"])
        self.assertEqual(f.get_xticks()[1], ["a", "b", "c"])
        f.set_yticks([])
        self.assertEqual(len(f.get_yticks()[0]), 0)
        with self.assertRaises(ValueError):
            f.set_xticks([1, 2], ["only one"])
        f.set_xticks(None)
        self.assertIsNone(f.get_xticks())
        plt.figure()
        plt.plot([0, 1], [0, 1])
        plt.xticks([0, 0.5, 1], ["lo", "mid", "hi"])
        self.assertEqual(plt.xticks()[1], ["lo", "mid", "hi"])
        plt.close("all")

    def test_audit_round5(self):
        import json
        f = self.fig
        line = f.plot([0, 1, 2], [10, 11, 12], label="a", yerr=[1, 2, 3])
        f.set_selected_region(-np.inf, 5)
        m = f.marker(float("nan"))
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "a.gpfig")
            f.save(p)
            g = plotter.load(p)
            self.assertEqual(g.get_selected_region(), (-np.inf, 5.0))  # non-finite survives
            self.assertTrue(np.isnan(g.markers()[0].position))
            g.close()
            # A damaged file: half an error-bar pair -> refused, window untouched.
            doc = json.load(open(p))
            del doc["panels"][0]["model"]["datasets"][0]["yErrHigh"]
            json.dump(doc, open(os.path.join(d, "bad.gpfig"), "w"))
            with self.assertRaises(RuntimeError):
                plotter.load(os.path.join(d, "bad.gpfig"))
            empty = plotter.figure()
            with self.assertRaises(RuntimeError):
                empty._load(os.path.join(d, "bad.gpfig"))
            self.assertEqual(empty.lines(), [])  # nothing half-loaded
            empty._load(p)  # still usable
            # Duplicate ids and absurd level counts are refused.
            doc = json.load(open(p))
            ds = doc["panels"][0]["model"]["datasets"]
            ds.append(dict(ds[0]))
            json.dump(doc, open(os.path.join(d, "dup.gpfig"), "w"))
            with self.assertRaises(RuntimeError):
                plotter.load(os.path.join(d, "dup.gpfig"))
            empty.close()
            # Wide CSV is not used when there are x errors.
            line.set_errors(xerr=[0.1, 0.2, 0.3])
            f.export_data(os.path.join(d, "x.csv"))
            self.assertTrue(open(os.path.join(d, "x.csv")).readline().startswith("dataset,"))
        # append() drops error bars even when the length stays the same.
        line.append([3], [13], max_points=3)
        self.assertIsNone(line.yerr)
        # Text arguments: bytes decode, lists are rejected.
        line.label = b"volts"
        self.assertEqual(line.label, "volts")
        with self.assertRaises(TypeError):
            f.marker(1.0, label=["a"])
        with self.assertRaises(ValueError):
            f.contour(np.ones((3, 3)), levels=10**6)

    def test_timeline_y_ranges_round_trip(self):
        tl = plotter.timeline()
        t = np.linspace(0, 10, 50)
        tl.add_row("A").plot(t, 300 * np.sin(t), label="a")
        tl.auto_fit_y = False
        tl.rows[0].axes.set_ylim(-5, 5)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.gpfig")
            tl.save(p)
            t2 = plotter.load(p)
            self.assertEqual(t2.rows[0].axes.ylim(), (-5.0, 5.0))
            t2.close()
        tl.figure.close()

    def test_workflow_fixes(self):
        import plotter.pyplot as plt
        f = self.fig
        f.plot([0, 1, 2], [0, 1, 2], label="a")
        st = f.region_stats(20, 30)["a"]
        self.assertEqual(st["n"], 0)
        self.assertTrue(np.isnan(st["mean"]))  # not a misleading 0
        # fill_between bands are inside the autoscaled view; points get x margins.
        g = plotter.figure()
        x = np.linspace(0, 1, 20)
        g.fill_between(x, np.sin(x) - 0.5, np.sin(x) + 0.5)
        g.autoscale()
        lo, hi = g.ylim()
        self.assertLessEqual(lo, -0.5)
        self.assertGreaterEqual(hi, np.sin(1) + 0.5)
        g.plot([0, 1, 2], [0, 1, 0], marker="o", style="none")
        g.autoscale()
        self.assertLess(g.xlim()[0], 0)
        g.close()
        # pyplot: no legend unless asked; show() returns without a screen.
        fig, axs = plt.subplots(1, 2)
        axs[0].plot([0, 1], [0, 1], label="x")
        plt.show()  # offscreen: must not block
        plt.close("all")

    def test_panel_titles_and_suptitle(self):
        f = self.fig
        a, b = f.subplots(1, 2)
        a.title, b.title = "left", "right"
        f.suptitle = "Both"
        self.assertEqual((a.title, b.title, f.suptitle), ("left", "right", "Both"))
        with tempfile.TemporaryDirectory() as d:
            f.save(os.path.join(d, "t.gpfig"))
            g = plotter.load(os.path.join(d, "t.gpfig"))
            self.assertEqual(g.suptitle, "Both")
            self.assertEqual(g.axes[1].title, "right")
            g.close()

    def test_pyplot_everyday_idioms(self):
        import plotter.pyplot as plt
        plt.figure()
        plt.subplot(211)
        plt.bar([1, 2, 3], [3, 5, 2], label="b")
        plt.subplot(2, 1, 2)
        plt.step([0, 1, 2], [1, 3, 2], where="post")
        plt.semilogy([1, 2, 3], [1, 10, 100])
        ax = plt.gca()
        self.assertTrue(ax.panel.y_log)
        self.assertEqual(len(ax.get_lines()), 2)
        plt.errorbar([1, 2], [1, 2], yerr=0.1, ecolor="r", mfc="w")  # cosmetic keywords accepted
        plt.axis([0, 4, 0.5, 200])
        self.assertEqual(plt.axis()[:2], (0.0, 4.0))
        plt.suptitle("S")
        self.assertEqual(plt.gcf().native.suptitle, "S")
        plt.gcf().set_size_inches(5, 4)
        plt.ion(), plt.ioff(), plt.draw()
        plt.cla()
        self.assertEqual(plt.gca().get_lines(), [])
        plt.close("all")

    def test_no_hangs(self):
        import plotter.pyplot as plt
        f = self.fig
        nan = float("nan")
        f.plot([0, 0, 1, 1, 0, nan, 2, 2, 3], [0, 1, 1, 0, 0, nan, 0, 2, 2], fill=True)
        z = np.full((3, 3), 0.3)
        z[0, 0] = 0.1 + 0.2  # a range of one rounding step
        f.image(z)
        with tempfile.TemporaryDirectory() as d:
            f.savefig(os.path.join(d, "a.png"))
            plt.figure()
            plt.bar(["a", "b"], [1, 2], yerr=[0.1, 0.2], color=["C0", "C1"])
            plt.barh([1, 2], [2, 1])
            self.assertEqual(plt.gca().panel.get_xticks()[1], ["a", "b"])
            plt.savefig(os.path.join(d, "b.png"))
            plt.close("all")

    def test_round6_correctness(self):
        import plotter.pyplot as plt
        f = self.fig
        # Views follow data; shared axes fit the union.
        f.plot([0, 1], [0, 1])
        f.plot([0, 1], [0, 5])
        self.assertGreaterEqual(f.ylim()[1], 5)
        g = plotter.figure()
        a, b = g.subplots(1, 2, sharey=True)
        a.plot([0, 1], [0, 1])
        b.plot([0, 1], [5, 50])
        a.autoscale()
        self.assertLessEqual(a.ylim()[0], 0)
        self.assertGreaterEqual(a.ylim()[1], 50)
        g.close()
        # Exact contour levels; one-sided colour limits; blocky images with full cells.
        y, x = np.mgrid[-2:2:30j, -2:2:30j]
        c = f.contour(np.exp(-(x**2 + y**2)), levels=[0.1, 0.5, 0.9])
        self.assertEqual(sorted(c.levels()), [0.1, 0.5, 0.9])
        img = f.image(np.arange(9.0).reshape(3, 3), vmin=2)
        self.assertEqual(img.clim, (2.0, None))
        # Colour bar: removed with its image; style changes keep it.
        img.remove()
        # pyplot: imshow extents, clf, subplot shape mixing.
        plt.figure()
        plt.imshow(np.zeros((3, 3)))
        self.assertEqual(plt.xlim(), (-0.5, 2.5))
        plt.clf()
        plt.subplot(221)
        plt.plot([0, 1])
        with self.assertRaises(ValueError):
            plt.subplot(212)
        plt.close("all")
        # A single panel keeps its title when a suptitle is set; figure names become suptitles.
        h = plotter.figure("Run 42")
        h.subplots(1, 2)
        self.assertEqual(h.suptitle, "Run 42")
        h.close()

    def test_pyplot_compat_round2(self):
        import io
        import plotter.pyplot as plt
        plt.figure()
        x = np.linspace(0, 5, 20)
        plt.plot(x, x, "C1--")
        plt.plot(x, 2 * x, color="tab:green")
        counts, _, lines = plt.hist([np.arange(50.0), np.arange(50.0) + 20], bins=5, label=["a", "b"])
        self.assertEqual(counts.shape, (2, 5))
        self.assertEqual([ln.label for ln in lines], ["a", "b"])
        plt.legend(["one", "two"], loc=2)
        buf = io.BytesIO()
        plt.savefig(buf, format="png")
        self.assertEqual(buf.getvalue()[:4], b"\x89PNG")
        fig = plt.figure()
        ax = fig.add_subplot(111)
        ax.set(xlabel="X", xlim=(0, 2))
        self.assertEqual(ax.get_xlim(), (0.0, 2.0))
        fig.add_subplot(2, 1, 2)
        self.assertEqual(len(fig.axes), 2)
        plt.figure(num=7)
        plt.close(7)
        self.assertTrue(plt.figure(num=7) is not None)
        plt.close("all")
        with self.assertRaises(RuntimeError):
            plotter.figure().wait_for_region()  # never shown: would wait forever

    def test_audit_pyplot(self):
        import plotter.pyplot as plt
        fig, axs = plt.subplots(2, 2)
        self.assertEqual(axs.shape, (2, 2))
        axs[0, 1].plot([1, 2], [3, 4])
        self.assertEqual(len(list(axs.flat)), 4)
        plt.figure(num="same")
        same = plt.gcf()
        plt.figure(num="same")
        self.assertIs(plt.gcf(), same)
        lines = plt.plot([0, 1, 2], np.array([[0, 1], [1, 2], [2, 3]]), drawstyle="steps-post")
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[0].step, "post")
        plt.errorbar([0, 1], [0, 1], yerr=0.1, fmt="none")
        plt.plot([0, 1], [0, 1], color="0.5", label=3, alpha=0.4)
        plt.fill_between([0, 1, 2], [1, 2, 3], where=[True, False, True])
        plt.axvline(1, ymin=0.2, ymax=0.8)
        plt.xlim(left=-1)
        self.assertEqual(plt.xlim()[0], -1.0)
        ax = plt.gca()
        ax.text(0.5, 0.5, "centre", transform=ax.transAxes)
        yy, xx = np.mgrid[-1:1:30j, -1:1:30j]
        c = ax.contour(xx, yy, np.exp(-(xx ** 2 + yy ** 2)), levels=np.int64(5))
        self.assertEqual(c.n_levels, 5)
        c2 = ax.contour(xx, yy, np.exp(-(xx ** 2 + yy ** 2)), levels=[0.1, 0.2, 0.4, 0.8])
        self.assertEqual(sorted(c2.levels()), [0.1, 0.2, 0.4, 0.8])  # exactly the levels asked for
        with self.assertWarns(UserWarning):
            plt.scatter([1, 2, 3, 4, 5], [1, 2, 3, 4, 5], c=[1, 2, 3, 4, 5])  # per-point values
        plt.close("all")

    def test_savefig(self):
        self.fig.plot(self.x, np.sin(self.x), label="s")
        self.fig.marker(50, label="M")
        with tempfile.TemporaryDirectory() as d:
            for ext in ("png", "svg", "pdf"):
                fn = os.path.join(d, "out." + ext)
                self.fig.savefig(fn, width=640, height=400)
                self.assertGreater(os.path.getsize(fn), 1000)
            with self.assertRaises(RuntimeError):
                self.fig.savefig(os.path.join(d, "out.xyz"))

    def test_closed_figure_keeps_its_data(self):
        fig = plotter.figure()
        fig.show(block=False)
        fig.close()
        self.assertFalse(fig.is_open)
        # Data stays accessible after closing the window.
        fig.plot([1, 2], [3, 4])
        self.assertEqual(len(fig.lines()), 1)


if __name__ == "__main__":
    unittest.main()
