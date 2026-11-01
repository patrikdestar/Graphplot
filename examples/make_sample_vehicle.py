# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""Write synthetic vehicle logs (IMU 200 Hz, GPS 10 Hz, wheel speed 50 Hz) as CSV.

    python examples/make_sample_vehicle.py [output_dir]     (default: sample_vehicle)

Files use different time formats on purpose (epoch seconds, epoch ms, ISO
date-times) - the timeline aligns them on one clock. The drive contains a
braking event at ~42 s, a pothole at ~61 s and a left turn at ~75-82 s.
"""
import os
import sys
from datetime import datetime, timedelta, timezone

import numpy as np

T0 = 1714658600.0  # 2024-05-02 14:03:20 UTC
DURATION = 120.0


def speed_profile(t):
    v = np.clip(t * 1.2, 0, 22.0)                      # accelerate to ~80 km/h
    brake = (t > 42) & (t < 47)
    v = np.where(brake, 22.0 - (t - 42) * 3.0, v)     # hard braking
    v = np.where(t >= 47, np.clip(7.0 + (t - 47) * 0.8, 0, 18.0), v)
    return v


def main(out="sample_vehicle"):
    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(11)

    t = np.arange(0, DURATION, 1 / 200)               # IMU 200 Hz
    v = speed_profile(t)
    ax = np.gradient(v, t) + 0.05 * rng.standard_normal(t.size)
    turn = (t > 75) & (t < 82)
    yaw = np.where(turn, 0.35 * np.sin(np.pi * (t - 75) / 7), 0.0)
    ay = v * yaw + 0.05 * rng.standard_normal(t.size)
    az = 9.81 + 0.08 * rng.standard_normal(t.size)
    pothole = np.exp(-((t - 61.0) / 0.03) ** 2)
    az += 6.0 * pothole * np.sin(2 * np.pi * 18 * (t - 61))
    gz = yaw + 0.004 * rng.standard_normal(t.size)
    with open(os.path.join(out, "imu.csv"), "w") as f:
        f.write("timestamp,ax [m/s2],ay [m/s2],az [m/s2],gz [rad/s]\n")
        for row in zip(T0 + t, ax, ay, az, gz):
            f.write("%.4f,%.4f,%.4f,%.4f,%.5f\n" % row)

    tg = np.arange(0, DURATION, 0.1)                   # GPS 10 Hz, epoch ms
    vg = speed_profile(tg) + 0.1 * rng.standard_normal(tg.size)
    with open(os.path.join(out, "gps.csv"), "w") as f:
        f.write("time_ms;speed [m/s];altitude [m]\n")
        for ti, vi in zip(tg, vg):
            f.write("%d;%.3f;%.2f\n" % (int((T0 + ti) * 1000), vi, 120 + 0.02 * ti))

    tw = np.arange(0, DURATION, 0.02)                  # wheel speeds 50 Hz, ISO time
    vw = speed_profile(tw)
    slip = (tw > 42.5) & (tw < 44.0)                   # ABS activity while braking
    fl = vw * (1 - 0.15 * slip * (np.sin(2 * np.pi * 6 * tw) > 0))
    fr = vw * (1 + 0.03 * np.where((tw > 75) & (tw < 82), 1, 0))
    start = datetime.fromtimestamp(T0, tz=timezone.utc).replace(tzinfo=None)
    with open(os.path.join(out, "wheels.csv"), "w") as f:
        f.write("datetime,wheel_FL [m/s],wheel_FR [m/s]\n")
        for ti, a, b in zip(tw, fl, fr):
            stamp = (start + timedelta(seconds=float(ti))).isoformat(timespec="milliseconds")
            f.write(f"{stamp}Z,{a:.3f},{b:.3f}\n")
    print("wrote", ", ".join(sorted(os.listdir(out))), "to", out)


if __name__ == "__main__":
    main(*sys.argv[1:2])
