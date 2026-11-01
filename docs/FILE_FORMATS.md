# File formats

Every file Graphplot reads or writes, with the exact rules the loaders use.
All text files are read as UTF-8. Lines starting with `#` or `%` are
comments, and empty lines are skipped.

| Kind | Open with | Python |
|---|---|---|
| [Column data](#column-data-1d) | *File → Open data…* (Ctrl+O), drag & drop | `numpy.loadtxt` + `fig.plot` |
| [2D matrix](#2d-matrix) | *File → Open 2D matrix…* | `fig.contour(z, x, y)` |
| [Time series (sensor logs)](#time-series-sensor-logs) | *File → Open time series (CSV)…* (Ctrl+T), drag & drop onto a timeline | `tl.load_csv(...)` |
| [Bruker TopSpin 2D](#bruker-topspin-processed-2d-spectra) | *File → Open Bruker 2D spectrum…*, drag & drop the folder | `fig.open_bruker(path)` |
| [Peak list](#peak-list-csv) | Peaks tab → *Import CSV…* / *Export CSV…* | `fig.import_peaks` / `fig.export_peaks` |
| [Style file](#style-files-gpstyle) | *Style → Load style file…*, drag & drop | `fig.use_style(path)` |
| [Exported figures](#exported-figures) | *File → Export…*, Ctrl+Shift+C (clipboard) | `fig.savefig(path)` |

## Separators and cells (all text tables)

- **Separator:** detected from the first non-comment line, in this order: tab,
  `;`, `,`; otherwise runs of spaces. Use `;` when your numbers have a decimal
  comma: only `.` is accepted as the decimal point.
- **Quotes:** with tab, `;` or `,`, a field may be quoted (`"Temp, K"`); a
  doubled quote inside quotes is a literal quote (`"5"" pipe"`).
- **Missing values:** an empty cell or `nan` (any case) is a missing value,
  drawn as a gap.
- **Header:** the first line that is not all numbers is the header (column
  names). In space-separated files a name may contain single spaces when the
  columns are separated by two or more spaces or a tab
  (`time (s)   value`).

## Column data (1D)

```
# time (s), signal A, signal B
time,a,b
0,1,2
1,2,4
2,3,6
```

- The first column is x; every further column becomes one line (dataset),
  named after its header. With a single column, the values are plotted
  against their index.
- Every data row must have the same number of columns; the error message
  names the first line that doesn't.
- A trailing separator (`1,2,`) does not create an extra empty dataset.

## 2D matrix

A grid of z values: one text row per y, one column per x.

```
# extent: 0 10 -5 5
0.1, 0.2, 0.3
0.4, 0.5, 0.6
```

- `# extent: x0 x1 y0 y1` (optional) gives the coordinates of the first and
  last column and row; without it they are 0 … n−1.
- At least 2 × 2 values. All rows need the same number of values; empty cells
  are missing values (they do not shift the columns).

## Time series (sensor logs)

One time column plus any number of signal columns; one file per sensor is
typical (`imu.csv`, `gps.csv`, `can.csv`).

```
timestamp,ax,ay,az
1714658600.000,0.01,-0.02,9.81
1714658600.010,0.02,-0.01,9.80
```

- **Time column:** the first column whose header is `time`, `t`,
  `timestamp`, `stamp`, `datetime`, `date_time`, `date time`, `sec`, `secs`,
  `seconds`, `ts`
  (any case; units in brackets and `_s`, `_ms`, `_us`, `_ns` suffixes are
  ignored for this test), otherwise the first column.
- **Time values**, recognised automatically:

  | Form | Example | Meaning |
  |---|---|---|
  | ISO 8601 date-time | `2024-05-02T14:03:27.250`, `2024-05-02 14:03:27` | absolute (local time unless a zone is given) |
  | Unix epoch | `1714658600.25`, `1714658600250` (ms), µs, ns | absolute, when the **whole** column lies after ~1973 |
  | Relative seconds | `0.0`, `0.01`, … | relative |
  | Unit in the header | `time_ms`, `t [us]`, `stamp (ns)`, `t_min` | scaled to seconds |

- In space-separated files a date and a clock time in two fields
  (`2024-05-02 14:03:27 1.5`) are joined into one timestamp.
- Rows without a valid time are dropped; rows are sorted by time. Columns
  without any number (text labels) are skipped.
- **Several files** are aligned on one clock when their times are absolute;
  t = 0 is the earliest start. Relative files start at their own zero.
- Large logs: memory use is about the size of the numbers (8 bytes per
  value), not of the text.

## Bruker TopSpin processed 2D spectra

Select any of:

- the processed-data folder `…/<expno>/pdata/<procno>`,
- the `2rr` file inside it,
- the experiment folder `…/<expno>` (then `pdata/1` is used).

Files read from the pdata folder:

| File | Used for |
|---|---|
| `2rr` | real part of the spectrum (int32 or float64, little / big endian, tiled by XDIM) |
| `procs` | F2 (direct, x axis): `SI`, `SW_p`, `SF`, `OFFSET`, `XDIM`, `BYTORDP`, `DTYPP`, `NC_proc`, `AXNUC` |
| `proc2s` | F1 (indirect, y axis): the same keys |
| `title` (optional) | dataset name |

The axes are in ppm, reversed (high ppm on the left/bottom, as in TopSpin),
labelled with the nuclei (e.g. `¹H (ppm)`), and the diagonal is shown.

## Peak list (CSV)

```
x,y,intensity,label,comment
4.13,1.30,1.2e7,H3/H5,"weak, check NOESY"
```

- Written by *Export CSV…* / `fig.export_peaks(path)`; the first two header names
  are the axis labels.
- Read by *Import CSV…* / `fig.import_peaks(path)`: the first two columns are x
  and y; `intensity`, `label` and `comment` are optional. Standard CSV quoting
  (fields may contain commas, quotes and line breaks).

## Style files (`.gpstyle`)

Plain `key: value` lines; see [STYLE_REFERENCE.md](STYLE_REFERENCE.md) for all
keys. Example: [example_science.gpstyle](example_science.gpstyle).

```
# thesis.gpstyle
inherit: science          # start from a built-in or another file
font.size: 11
lines.linewidth: 1.5
```

Unknown keys and invalid values are reported and ignored; the rest of the
file still applies. Your personal default style is
`~/.config/Graphplot/default.gpstyle` (or the file named by the
`GRAPHPLOT_STYLE` environment variable).

## Exported figures

| Format | Contents | Physical size |
|---|---|---|
| PNG | bitmap at `scale` × the figure size (default 2×) | 100 figure pixels per inch (stored in the file) |
| SVG | vectors, text as text | 100 figure pixels per inch |
| PDF | vectors, embedded fonts | 100 figure pixels per inch |

So an 800 × 500 figure is 8 × 5 inches in every format. Selection highlights
are never exported. *File → Copy figure to clipboard* (Ctrl+Shift+C) puts the same image as a
PNG export on the clipboard.
