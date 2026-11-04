Graphplot for Windows (64-bit) - portable build
================================================

Start:      double-click graphplot.exe
From a terminal (cmd or PowerShell), in this folder:
            graphplot.exe data.csv
            graphplot.exe --timeline imu.csv gps.csv
            graphplot.exe --demo2d --export figure.pdf
            graphplot.exe --help

Nothing needs installing: Qt and the Microsoft C++ runtime are included.
Keep all files and folders together; graphplot.exe needs them.

For Python ("import plotter"), install the wheel that matches your Python
version (graphplot_plotter-...-cp312-...whl for Python 3.12):
            py -3.12 -m pip install graphplot_plotter-0.1.0-cp312-cp312-win_amd64.whl

Full guide: docs/INSTALL_WINDOWS.md in the source repository.

Copyright (c) 2026 Justinas Minkevicius <oompagoo@gmail.com>. All rights
reserved. Use requires the author's written permission - see licenses\LICENSE.
Third-party code: Qt 6 (LGPLv3, https://www.qt.io/licensing; the Qt DLLs
are unmodified and can be replaced) and QCustomPlot (GPLv3, licenses\).
