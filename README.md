# LVM Graph Viewer

![Banner](banner.png)

**[🇷🇺 Русский](README_RU.md)** | **[🇬🇧 English](README_EN.md)**

A dependency-free C++ desktop application for viewing LabVIEW `.lvm` / `.txt` signal files.

Two front ends share one parser/FFT library:

- **`lvm_viewer_gui.exe`** — a native Win32 desktop viewer (window, buttons, interactive plot). Latest version: **v0.4.0**.
- **`lvm_reader.exe`** — a command-line tool (structure, statistics, FFT peaks, CSV export).

The parser is a faithful C++ port of the Python LVM Signal Viewer, and the FFT matches numpy's `rfft` (to ~1e-13). Results match the Python reference on the bundled sample files (verified on a 1 GB / 6.8 M-sample file too).

## GUI Highlights (v0.4.0)

- **i18n** — full English / Russian language switch via the View menu.
- **Dark theme** — full dark mode including toolbar, menu bar, panel, and settings window (toggle with `T` or View menu).
- **Interactive legend** — click a legend item to toggle the channel, `Ctrl+Click` to solo (show only that channel).
- **Minor grid** — fine grid lines between major axis ticks for easier reading.
- **Active button accent** — active toggle buttons (Measure, Auto zoom) show bright blue text for clear visual feedback.
- **Auto zoom** — renamed from "Auto Y", freezes the vertical scale at the current range.
- **Welcome screen** — buttons no longer overlap the instructional text.
- **Performance** — fewer GDI object allocations in rendering loops.

Full documentation: [README_EN.md](README_EN.md) | [README_RU.md](README_RU.md)

## Quick Build

```bash
g++ -std=c++17 -O2 -municode -static -mwindows -o lvm_viewer_gui.exe \
    gui_main.cpp lvm_parser.cpp fft.cpp analysis.cpp \
    -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32
```

## CLI Quick Start

```bash
make
./lvm_reader.exe lvm_files_for_tests/test.lvm --info --stats --fft
```

See [README_EN.md](README_EN.md) or [README_RU.md](README_RU.md) for full details.
