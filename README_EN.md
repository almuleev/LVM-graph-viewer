# LVM Graph Viewer

![Banner](banner.png)

A dependency-free C++ desktop application for viewing LabVIEW `.lvm` / `.txt` signal files.

**[🇷🇺 Русский](README_RU.md)** | **[🇬🇧 English](README_EN.md)**

## Overview

Two front ends share one parser/FFT library:

- **`lvm_viewer_gui.exe`** — a native Win32 desktop viewer (window, buttons, interactive plot). See [GUI viewer](#gui-viewer).
- **`lvm_reader.exe`** — a command-line tool (structure, statistics, FFT peaks, CSV export). See [CLI usage](#cli-usage).

The parser is a faithful C++ port of the Python LVM Signal Viewer, and the FFT matches numpy's `rfft` (to ~1e-13). Results match the Python reference on the bundled sample files (verified on a 1 GB / 6.8 M-sample file too).

---

## GUI Viewer

A self-contained desktop app drawn with the Win32 API + GDI (no Qt, no DLLs). Latest version: **v0.4.4**.

### Start Screen

On launch (with no file) a welcome window shows the app name, a short how-to, and buttons to open a file, open the point settings, or view the shortcuts.

### Main Interface

- **Main menu** (File / View / Measurements / Lines / Help) alongside the toolbar, a modern Segoe UI look, and a status bar.
- **Dark theme** — toggle with `T` or via the View menu. Full dark mode including toolbar, menu bar, panel, and settings window.
- **Language** — switch between English and Russian via the View menu.
- **Open file** — open a `.lvm`/`.txt` via the file dialog (or pass a path on the command line / drag a file onto the exe).
- Multi-channel plot with a colored **interactive legend** — click a legend item to toggle the channel, `Ctrl+Click` to solo (show only that channel).
- **Channels** panel — checkboxes to show/hide each channel.
- **Time / Hz** — switch between the time plot and the FFT spectrum.
- **Save PNG / Save CSV** — export the current view: PNG image (GDI+), or CSV of the **visible segment** (the time window in Time mode, or the visible spectrum band in Hz mode). Default names reflect the mode (`…_plot.png` / `…_spectrum.png`, `…_segment.csv` / `…_spectrum.csv`).
- **▶ Play / ⏸ Pause** — play back the time signal in **real time** (1 s of signal per 1 s of wall clock); a red playhead sweeps across the plot and, once it passes the middle, the window scrolls smoothly to follow it.
- **Measure** — measurement tool. Click points on the plot to drop markers; the read-outs are drawn right on the chart and in the status bar. A dedicated **«Measurement point settings»** panel (toolbar button or *Measurements → Settings…*) has checkboxes to choose what is shown — point number, X, Y, distance along X (Δx) and/or Y (Δy), 1/Δt, straight-line distance d — to **snap markers to the graph**, and a button to **change the marker colour**. Right-click (or `Delete`) clears the points.
- **Lines** — drop **vertical** and **horizontal** reference lines on the plot. Pick *Add vertical/horizontal line*, then click where it goes (vertical lines snap to a sample when snapping is on); *Clear lines* removes them. Lines are remembered per view (Time vs Hz).
- **Auto zoom** — by default the vertical scale auto-fits the visible data; this toggle freezes it at the current range so the height stops readjusting (handy during playback / panning).
- **Visual smoothing (spline)** — a *purely visual* Catmull-Rom curve drawn between samples. It smooths the **line** without moving the underlying data points (measurements/snapping still hit the true samples); when zoomed in the real samples are shown as dots so the distinction is clear.
- **Minor grid** — fine grid lines between the major axis ticks for easier reading.
- **Reset view** plus mouse-wheel zoom (under the cursor) and left-drag panning. Zoom/pan work on **both** axes — time in Time mode and the **frequency axis in Hz mode** — so you can drill into a narrow band. A status bar shows the current window and sample count.

### Keyboard Shortcuts

The full list also lives in-app under **Help → Keyboard shortcuts** (or press `F1`).

| Key | Action |
|-----|--------|
| `O` / `Ctrl+O` | Open file |
| `S` / `Ctrl+S` | Save PNG |
| `E` / `Ctrl+E` | Save CSV |
| `M` | Toggle Time / Hz |
| `Space` | Play / Pause |
| `V` | Measure tool on/off |
| `C` | Visual (spline) smoothing on/off |
| `+` / `↑` | Zoom in |
| `−` / `↓` | Zoom out |
| `←` / `→` | Pan left / right |
| `Home` | Reset view |
| `Delete` | Clear measurement points |
| `Esc` | Cancel adding a guide line |
| `F1` | Keyboard-shortcuts help |
| `T` | Toggle dark/light theme |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |

Mouse: wheel zooms under the cursor, left-drag pans, left-click drops a measurement point or guide line (in the matching mode), right-click clears measurement points. Click the legend to toggle channels, Ctrl+Click to solo.

### Build (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File build_gui.ps1
```

or with the Makefile target `make gui`, or directly:

```bash
g++ -std=c++17 -O2 -municode -static -mwindows -o lvm_viewer_gui.exe \
    gui_main.cpp lvm_parser.cpp fft.cpp analysis.cpp \
    -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32
```

Then double-click `lvm_viewer_gui.exe`. (Close the app before rebuilding — a running exe can't be overwritten by the linker.)

---

## CLI Usage

Requires a C++17 compiler. Tested with MSYS2/MinGW `g++ 14.2`.

Using the Makefile:

```bash
make          # build lvm_reader(.exe)
make test     # build and run the unit tests
```

Or directly with g++ (static linking keeps the binary self-contained — no `libstdc++`/`libgcc` DLLs needed at runtime):

```bash
g++ -std=c++17 -O2 -static -o lvm_reader.exe \
    main.cpp lvm_parser.cpp fft.cpp analysis.cpp
```

> Note: on this MSYS2 toolchain the dynamic link step fails (`ld returned 116`); `-static` both works around it and produces a portable binary.

```text
lvm_reader <file.lvm|file.txt> [options]

Actions:
  -i, --info          Show file structure / header info
  -s, --stats         Show per-channel statistics (min/max/mean/count)
  -H, --head N        Print the first N data rows
  -c, --csv FILE      Export the (selected) data to a CSV file
      --fft           Show the strongest spectral peaks per channel
      --peaks N       Number of peaks to show (default 5; implies --fft)
      --fft-csv FILE  Export the magnitude spectrum to a CSV file

Selection (applied to stats / head / csv / fft):
      --start T       Keep rows with time >= T
      --end T         Keep rows with time <= T
      --channels LIST Comma-separated 1-based channel positions, e.g. 1,3

Parsing / spectrum options:
  -m, --monotonic     Rebuild a monotonic timeline (for sectioned files)
      --keep-dup-time Keep channels that duplicate the time axis
      --fft-samples N Decimate to at most N samples for FFT (0 = use all)
  -v, --verbose       Verbose parser output
  -h, --help          Show this help
```

With no action flag, `--info` and `--stats` are shown.

### Quick run on Windows (double-click)

`run.bat` is a convenience launcher: double-click it and type/paste a file path, or drag a `.lvm`/`.txt` file onto it. It runs `--info --stats --fft` and keeps the window open until you press a key. (The batch calls the exe by its full path, so it also works on machines where running programs from the current directory is disabled.)

### Examples

```bash
# Structure + statistics (default)
./lvm_reader.exe lvm_files_for_tests/test.lvm

# First 5 rows
./lvm_reader.exe lvm_files_for_tests/test.lvm --head 5

# Export to CSV
./lvm_reader.exe lvm_files_for_tests/test.lvm --csv out.csv

# Spectral peaks per channel
./lvm_reader.exe lvm_files_for_tests/test.lvm --fft --peaks 3

# Export the spectrum for channel 1 only
./lvm_reader.exe lvm_files_for_tests/test.lvm --channels 1 --fft-csv spec.csv

# A time window of the first channel
./lvm_reader.exe lvm_files_for_tests/test.lvm --start 0 --end 0.5 --channels 1 --csv window.csv

# Multi-header file with a rebuilt monotonic timeline
./lvm_reader.exe lvm_files_for_tests/test1.lvm --info --monotonic
```

---

## Parsing Notes

Behaviour mirrors the Python viewer:

- Lines starting with `***` and known metadata keys (e.g. `Separator`, `Multi_Headings`, `X0`) are skipped.
- Decimal commas are normalised to dots; columns are split on tabs.
- A row needs at least two numeric values to count as data; missing/invalid cells become `NaN` so channel alignment is preserved.
- The first column is treated as time/X; remaining columns become `Channel_1`, `Channel_2`, … Channels with no real values are dropped.
- Rows with a non-numeric time value are dropped.
- Columns that merely duplicate the time axis are removed (disable with `--keep-dup-time`).
- `--monotonic` flattens `Multi_Headings` sections that reset local time.

## Spectrum Notes

- The sample rate is derived from the median positive time step, the signal is mean-removed, and amplitude is `2/N * |rfft|` — matching the Python Hz mode.
- By default the full selection is transformed (Bluestein FFT handles any N). For very large files, `--fft-samples N` uniformly decimates to keep the run fast; decimation keeps the frequency axis correct (the Nyquist limit drops).

## Files

- `lvm_parser.hpp` / `lvm_parser.cpp` — parser library
- `fft.hpp` / `fft.cpp` — radix-2 + Bluestein FFT primitives
- `analysis.hpp` / `analysis.cpp` — spectrum + peak detection
- `gui_main.cpp` — native Win32 GUI viewer
- `build_gui.ps1` — GUI build helper (Windows/PowerShell)
- `main.cpp` — command-line front end
- `run.bat` — double-click / drag-and-drop launcher for the CLI (Windows)
- `tests/run_tests.cpp` — unit tests (`make test`)
- `Makefile` — build helper

## Changelog

### v0.4.4
- Deeper zoom — zoom in far enough to see individual samples.
- Fixed measurement read-outs: values (X, Δx, Δy, 1/Δt, d) are shown next to points instead of the checkbox label text.
- Measurement points now display filled dots in the chosen marker colour.
- Visual smoothing (Catmull-Rom spline) now works in FFT (Hz) mode as well as Time mode.
