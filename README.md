# LVM Graph Viewer

![Banner](banner.png)

**[Русский](README_RU.md)** | **[English](README_EN.md)**

Native C++ toolkit for working with LabVIEW signal files `.lvm` / `.txt`.

- `lvm_viewer_gui.exe` — dependency-free Win32 desktop viewer with plots, FFT, measurements, markers, guide lines, playback, PNG export, and CSV export.
- `lvm_reader.exe` — command-line companion for structure inspection, statistics, FFT peaks, and CSV export.

The parser is a faithful C++ port of the Python LVM Signal Viewer, and the FFT matches `numpy.rfft` with reference-level precision.

## At A Glance

| Item | Value |
|------|-------|
| Current version | `v0.8.1` |
| Language | `C++17` |
| GUI | Win32 API + GDI / GDI+ |
| CLI | Standalone executable |
| FFT | Radix-2 + Bluestein |
| Main formats | `.lvm`, tab-separated `.txt`, CSV export, PNG export |
| Build target | Windows (`MSYS2/MinGW g++`) |

## What The Project Gives You

| Area | Highlights |
|------|------------|
| GUI | Time-domain plots, FFT mode, zoom/pan, interactive legend, dark theme, RU/EN UI, drag & drop |
| Measurements | Snap-to-data points, `X`, `Y`, `Δx`, `Δy`, `1/Δt`, distance, custom marker colour |
| Analysis | Spectrum computation, peak search, duplicate time-axis channel removal, monotonic time rebuild |
| Export | PNG image export and CSV export for visible data |
| CLI | File info, per-channel stats, head output, channel/time selection, FFT peaks, FFT CSV |

## Quick Start

### GUI

```powershell
powershell -ExecutionPolicy Bypass -File build_gui.ps1
```

### CLI

```bash
make
./lvm_reader.exe lvm_files_for_tests/test.lvm --info --stats --fft
```

## Documentation

- [English documentation](README_EN.md)
- [Русская документация](README_RU.md)

## Latest Update

`v0.8.1` polishes the viewer after the larger `v0.8.0` update:

- fixed remaining mojibake in Russian menu and settings text;
- refined active button visuals and unified `Auto zoom` / `Авто масштабирование` labels;
- clamped edge axis labels so values no longer overlap the toolbar or right panel;
- simplified playback speed selection to direct custom input;
- tightened the welcome screen footer layout and removed decorative clutter.

<details>
<summary>Repository structure</summary>

| File | Purpose |
|------|---------|
| `gui_main.cpp` | Native Win32 GUI viewer |
| `main.cpp` | CLI front-end |
| `lvm_parser.cpp` / `lvm_parser.hpp` | `.lvm` / `.txt` parser |
| `analysis.cpp` / `analysis.hpp` | Spectrum computation and peak search |
| `fft.cpp` / `fft.hpp` | FFT implementation |
| `tests/run_tests.cpp` | Unit tests |
| `build_gui.ps1` | GUI build helper |
| `Makefile` | CLI, tests, and GUI build targets |

</details>
