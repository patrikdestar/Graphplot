#!/usr/bin/env python3
# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""Pack the built 'plotter' package into a wheel (.whl) for `pip install`.

Run it with the Python the module was built for (the wheel is tagged for
that interpreter):

    python tools/make_wheel.py --package build/python/plotter \
        --bundle deploy/ --out dist/

--bundle is a folder whose contents (Qt DLLs and plugin folders, e.g. the
output of windeployqt) are copied into the package, so the wheel works
without a separate Qt installation. plotter/__init__.py adds that folder to
the DLL search path on Windows.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import pathlib
import re
import sys
import sysconfig
import zipfile

NAME = "graphplot_plotter"   # distribution name; the import name is 'plotter'
SUMMARY = "Interactive scientific plotting (C++/Qt) controlled from Python"
AUTHOR = "Justinas Minkevicius"
AUTHOR_EMAIL = "oompagoo@gmail.com"


def project_version(root: pathlib.Path) -> str:
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\(\s*Graphplot\s+VERSION\s+([0-9.]+)", text)
    if not m:
        sys.exit("cannot read the version from CMakeLists.txt")
    return m.group(1)


def record_line(arcname: str, data: bytes) -> str:
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
    return f"{arcname},sha256={digest},{len(data)}"


def main() -> None:
    root = pathlib.Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--package", required=True, type=pathlib.Path, help="built plotter package folder")
    ap.add_argument("--bundle", type=pathlib.Path, help="folder copied into the package (Qt DLLs, plugins)")
    ap.add_argument("--out", default="dist", type=pathlib.Path, help="output folder")
    ap.add_argument("--version", help="wheel version (default: from CMakeLists.txt)")
    args = ap.parse_args()

    pkg = args.package
    if not any(pkg.glob("_plotter*.pyd")) and not any(pkg.glob("_plotter*.so")):
        sys.exit(f"no compiled _plotter module in {pkg}")
    version = args.version or project_version(root)
    py = f"cp{sys.version_info.major}{sys.version_info.minor}"
    plat = sysconfig.get_platform().replace("-", "_").replace(".", "_")
    tag = f"{py}-{py}-{plat}"
    dist_info = f"{NAME}-{version}.dist-info"

    files: list[tuple[str, bytes]] = []
    for f in sorted(pkg.rglob("*")):
        if f.is_file() and "__pycache__" not in f.parts:
            files.append((f"plotter/{f.relative_to(pkg).as_posix()}", f.read_bytes()))
    if args.bundle:
        for f in sorted(args.bundle.rglob("*")):
            rel = f.relative_to(args.bundle)
            # The application itself and translations are not needed by the module.
            if f.is_file() and f.suffix.lower() != ".exe" and rel.parts[0] != "translations":
                files.append((f"plotter/{rel.as_posix()}", f.read_bytes()))
    for lic in ("LICENSE", "AUTHORS"):
        files.append((f"{dist_info}/licenses/{lic}", (root / lic).read_bytes()))
    files.append((f"{dist_info}/licenses/qcustomplot-GPL.txt",
                  (root / "third_party/qcustomplot/GPL.txt").read_bytes()))

    metadata = "\n".join([
        "Metadata-Version: 2.1",
        f"Name: {NAME}",
        f"Version: {version}",
        f"Summary: {SUMMARY}",
        f"Author: {AUTHOR}",
        f"Author-email: {AUTHOR} <{AUTHOR_EMAIL}>",
        "License: Proprietary (see LICENSE)",
        f"Requires-Python: =={sys.version_info.major}.{sys.version_info.minor}.*",
        "Requires-Dist: numpy",
        "", "Python module of Graphplot: `import plotter` or `import plotter.pyplot as plt`.", ""])
    wheel = "\n".join(["Wheel-Version: 1.0", "Generator: graphplot make_wheel.py",
                       "Root-Is-Purelib: false", f"Tag: {tag}", ""])
    files.append((f"{dist_info}/METADATA", metadata.encode()))
    files.append((f"{dist_info}/WHEEL", wheel.encode()))
    files.append((f"{dist_info}/top_level.txt", b"plotter\n"))

    record = [record_line(n, d) for n, d in files] + [f"{dist_info}/RECORD,,"]
    files.append((f"{dist_info}/RECORD", ("\n".join(record) + "\n").encode()))

    args.out.mkdir(parents=True, exist_ok=True)
    out = args.out / f"{NAME}-{version}-{tag}.whl"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in files:
            z.writestr(name, data)
    print(out)


if __name__ == "__main__":
    main()
