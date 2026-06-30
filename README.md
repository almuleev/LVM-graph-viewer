# LVM Graph Viewer

<p align="center">
  <img src="docs/assets/github-banner.png" alt="LVM Graph Viewer banner">
</p>

<p align="center">
  Native Win32 viewer and CLI toolkit for LabVIEW <code>.lvm</code> / <code>.txt</code> signal files.
</p>

<p align="center">
  <a href="README_RU.md"><strong>Русский</strong></a>
  &middot;
  <a href="README_EN.md"><strong>English</strong></a>
  &middot;
  <a href="https://github.com/almuleev/LVM-graph-viewer/releases/latest"><strong>Download latest release</strong></a>
</p>

<p align="center">
  <img src="https://img.shields.io/github/v/release/almuleev/LVM-graph-viewer?display_name=tag&sort=semver" alt="Latest release">
  <img src="https://img.shields.io/github/license/almuleev/LVM-graph-viewer" alt="License">
  <img src="https://img.shields.io/badge/platform-Windows-0078d4" alt="Platform">
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C" alt="C++17">
  <img src="https://img.shields.io/badge/UI-Win32%20%2B%20GDI-1f2937" alt="Win32 GDI">
</p>

## Overview

`LVM Graph Viewer` is a lightweight C++ application for engineers and developers who need to inspect LabVIEW measurement logs quickly without pulling in heavy external frameworks.

The project includes two front ends built on one shared parsing and analysis core:

- `LVM-graph-viewer-win-x64.exe` - native desktop viewer for interactive work
- `Start GUI.bat` - obvious double-click launcher for the GUI in a release folder
- `lvm_reader.exe` - CLI utility for inspection, FFT and export scenarios

## What It Does Well

- Opens LabVIEW `.lvm` and tab-separated `.txt` signal files
- Switches between time-domain view and FFT view
- Supports zoom, pan, playback, dark theme and live RU/EN switching
- Lets you rename channels directly in the channel list
- Provides markers, guide lines, snapped measurement points and undo/redo
- Supports independent point groups with separate colours and visibility
- Exports PNG, CSV and TXT from the viewer
- Includes sample files and regression tests in the repository

## Interface Screenshots

| Main workspace | Measurement groups in action |
|---|---|
| ![Main workspace](docs/assets/ui-overview-dark.png) | ![Measurement groups in action](docs/assets/ui-measurement-groups-dark.png) |

## Data Previews

| Time-domain view | FFT view |
|---|---|
| ![Time-domain preview](docs/assets/preview-time.png) | ![FFT preview](docs/assets/preview-fft.png) |

| Measurement point groups |
|---|
| ![Measurement groups preview](docs/assets/preview-point-groups.png) |

The interface screenshots above show the real application UI.
The graph previews below were generated from the bundled sample dataset in [`lvm_files_for_tests`](lvm_files_for_tests).

## Quick Start

### For end users

1. Open the [latest release](https://github.com/almuleev/LVM-graph-viewer/releases/latest).
2. Download the release archive.
3. Double-click `Start GUI.bat` or `LVM-graph-viewer-win-x64.exe`.
4. Use `run.bat` only if you want the command-line analyzer.

The GUI is the main entry point for first-time users. The CLI helper is useful
for scripted inspection, FFT and CSV export.

### Build the GUI

```powershell
powershell -ExecutionPolicy Bypass -File .\build_gui.ps1
```

### Build the CLI

```bash
make
```

### Run tests

```bash
make test
```

## Repository Layout

| Path | Purpose |
|---|---|
| `gui_main.cpp` | Win32 desktop viewer |
| `export_helpers.cpp/.hpp` | Export prompt and tabular export helpers |
| `formula_engine.cpp/.hpp` | Formula parsing and evaluation helpers |
| `main.cpp` | CLI entry point |
| `lvm_parser.cpp/.hpp` | LabVIEW file parser |
| `analysis.cpp/.hpp` | Spectrum helpers and analysis |
| `fft.cpp/.hpp` | FFT implementation |
| `Start GUI.bat` | Main double-click launcher for the GUI |
| `run.bat` | CLI helper launcher |
| `package_release.ps1` | Builds a release folder and optional zip archive |
| `tests/run_tests.cpp` | Regression tests |
| `lvm_files_for_tests/` | Bundled sample input files |
| `docs/assets/` | GitHub visuals and preview images |
| `docs/PROJECT_CONTEXT.md` | Extended development context |

## Documentation

- [English documentation](README_EN.md)
- [Русская документация](README_RU.md)
- [Changelog](CHANGELOG.md)
- [License](LICENSE)

## License

This project is distributed under the [MIT License](LICENSE).
