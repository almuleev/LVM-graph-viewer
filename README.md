# LVM Reader (C++)

A dependency-free C++ port of the Python **LVM Signal Viewer**. Two front ends
share one parser/FFT library:

- **`lvm_viewer_gui.exe`** — a native Win32 desktop viewer (window, buttons,
  interactive plot). See [GUI viewer](#gui-viewer).
- **`lvm_reader.exe`** — a command-line tool (structure, statistics, FFT peaks,
  CSV export). See [CLI usage](#usage).

The parser is a faithful C++ port of `read_lvm_file` / `prepare_loaded_data`
from the Python viewer's `lvm_viewer.py`, and the FFT matches numpy's `rfft`
(to ~1e-13) used by the viewer's Hz mode. Results match the Python reference on
the bundled sample files (verified on a 1 GB / 6.8 M-sample file too).

## GUI viewer

A self-contained desktop app drawn with the Win32 API + GDI (no Qt, no DLLs):

- **Открыть файл** — open a `.lvm`/`.txt` via the file dialog (or pass a path on
  the command line / drag a file onto the exe).
- Multi-channel plot with a colored legend; for dense views it draws a min/max
  envelope so even millions of samples render instantly.
- **Каналы** panel — checkboxes to show/hide each channel.
- **Время / Гц** — switch between the time plot and the FFT spectrum.
- **Сохранить PNG / Сохранить CSV** — export the current view: PNG image (GDI+),
  or CSV of the **visible segment** (the time window in Time mode, or the visible
  spectrum band in Hz mode). Default names reflect the mode
  (`…_plot.png` / `…_spectrum.png`, `…_segment.csv` / `…_spectrum.csv`).
- **▶ Воспроизв. / ⏸ Пауза** — play back the time signal: a red playhead sweeps
  across the plot and the window auto-scrolls to follow it.
- **Измерение** — measurement tool. Click points on the plot to drop markers;
  the status bar shows the distance between the last two (Δt/Δy and 1/Δt in Hz,
  or Δf/Δamp in Hz mode). Right-click clears the points.
- **Сглаживание** slider — smoothing factor (centered moving average, 0–80
  samples) for the time plot, to tame a noisy/“sharp” signal.
- **Увеличить / Уменьшить / Сбросить вид** plus mouse-wheel zoom and
  left-drag panning. Zoom/pan work on **both** axes — time in Time mode and the
  **frequency axis in Hz mode** — so you can drill into a narrow band. A status
  bar shows the current window and sample count.

### Keyboard shortcuts

| Key | Action |
|-----|--------|
| `O` / `Ctrl+O` | Open file |
| `S` / `Ctrl+S` | Save PNG |
| `E` / `Ctrl+E` | Save CSV |
| `M` | Toggle Time / Hz |
| `Space` | Play / Pause |
| `V` | Measure tool on/off |
| `+` / `↑` | Zoom in |
| `−` / `↓` | Zoom out |
| `←` / `→` | Pan left / right |
| `Home` | Reset view |

Build it (Windows):

```powershell
powershell -ExecutionPolicy Bypass -File build_gui.ps1
```

or with the Makefile target `make gui`, or directly:

```bash
g++ -std=c++17 -O2 -municode -static -mwindows -o lvm_viewer_gui.exe \
    gui_main.cpp lvm_parser.cpp fft.cpp analysis.cpp \
    -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32
```

Then double-click `lvm_viewer_gui.exe`. (Close the app before rebuilding — a
running exe can't be overwritten by the linker.)

## Build (CLI)

Requires a C++17 compiler. Tested with MSYS2/MinGW `g++ 14.2`.

Using the Makefile:

```bash
make          # build lvm_reader(.exe)
make test     # build and run the unit tests
```

Or directly with g++ (static linking keeps the binary self-contained — no
`libstdc++`/`libgcc` DLLs needed at runtime):

```bash
g++ -std=c++17 -O2 -static -o lvm_reader.exe \
    main.cpp lvm_parser.cpp fft.cpp analysis.cpp
```

> Note: on this MSYS2 toolchain the dynamic link step fails (`ld returned 116`);
> `-static` both works around it and produces a portable binary.

## Usage

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

## Quick run on Windows (double-click)

`run.bat` is a convenience launcher: double-click it and type/paste a file path,
or drag a `.lvm`/`.txt` file onto it. It runs `--info --stats --fft` and keeps
the window open until you press a key. (The batch calls the exe by its full
path, so it also works on machines where running programs from the current
directory is disabled.)

## Examples

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

## Parsing notes

Behaviour mirrors the Python viewer:

- Lines starting with `***` and known metadata keys (e.g. `Separator`,
  `Multi_Headings`, `X0`) are skipped.
- Decimal commas are normalised to dots; columns are split on tabs.
- A row needs at least two numeric values to count as data; missing/invalid
  cells become `NaN` so channel alignment is preserved.
- The first column is treated as time/X; remaining columns become
  `Channel_1`, `Channel_2`, … Channels with no real values are dropped.
- Rows with a non-numeric time value are dropped.
- Columns that merely duplicate the time axis are removed (disable with
  `--keep-dup-time`).
- `--monotonic` flattens `Multi_Headings` sections that reset local time.

## Spectrum notes

- The sample rate is derived from the median positive time step, the signal is
  mean-removed, and amplitude is `2/N * |rfft|` — matching the Python Hz mode.
- By default the full selection is transformed (Bluestein FFT handles any N).
  For very large files, `--fft-samples N` uniformly decimates to keep the run
  fast; decimation keeps the frequency axis correct (the Nyquist limit drops).

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
```
