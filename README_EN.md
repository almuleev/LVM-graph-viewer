# LVM Graph Viewer

<p align="center">
  <img src="docs/assets/github-banner.png" alt="LVM Graph Viewer banner">
</p>

Native Win32 viewer and CLI toolkit for LabVIEW `.lvm` / `.txt` signal files.

## Why This Project Exists

LabVIEW measurement logs are often easy to produce but inconvenient to inspect quickly outside a LabVIEW environment. `LVM Graph Viewer` focuses on a practical middle ground:

- no Qt
- no external GUI runtime
- no heavyweight dependencies
- fast opening of real measurement files
- interactive viewing plus script-friendly CLI access

## Core Features

### Desktop viewer

- Time-domain and FFT modes in one window
- Zoom, pan, playback and auto-zoom
- Dark and light themes
- Russian and English interface
- Inline channel rename in the channel list
- PNG, CSV and TXT export
- Drag and drop file opening

### Measurements and analysis

- Snapped measurement points
- Independent point groups with separate colours and visibility
- `X`, `Y`, `Δx`, `Δy`, `1/Δt` and distance readouts
- Vertical and horizontal guide lines
- Named markers
- Undo and redo for points, lines and markers

### Command-line mode

- File structure and parser information
- Per-channel statistics
- CSV export
- FFT peak inspection
- Windowed processing for selected time ranges

## Interface Screenshots

| Main workspace | Measurement groups in action |
|---|---|
| ![Main workspace](docs/assets/ui-overview-dark.png) | ![Measurement groups in action](docs/assets/ui-measurement-groups-dark.png) |

## Data Previews

| Time-domain | FFT |
|---|---|
| ![Time-domain preview](docs/assets/preview-time.png) | ![FFT preview](docs/assets/preview-fft.png) |

| Point groups |
|---|
| ![Point groups preview](docs/assets/preview-point-groups.png) |

The screenshots above show the real application interface.
The graph previews below are based on the bundled sample input files from [`lvm_files_for_tests`](lvm_files_for_tests).

## Quick Start

### Download a ready-to-run build

Use the [latest GitHub release](https://github.com/almuleev/LVM-graph-viewer/releases/latest) and download the Windows executable.

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

## Build Notes

- Language: `C++17`
- GUI stack: `Win32 API + GDI/GDI+`
- Recommended Windows toolchain: `MSYS2 / MinGW g++`
- GUI output name is fixed to `LVM-graph-viewer-win-x64.exe` through `build_gui.ps1`

## Repository Layout

| Path | Purpose |
|---|---|
| `gui_main.cpp` | Native GUI implementation |
| `main.cpp` | CLI implementation |
| `lvm_parser.cpp/.hpp` | `.lvm` / `.txt` parser |
| `analysis.cpp/.hpp` | Analysis helpers |
| `fft.cpp/.hpp` | FFT engine |
| `tests/run_tests.cpp` | Regression tests |
| `docs/assets/` | Repository visuals |
| `docs/PROJECT_CONTEXT.md` | Extended implementation context |

## Related Files

- [Russian README](README_RU.md)
- [Main README](README.md)
- [Changelog](CHANGELOG.md)
- [MIT License](LICENSE)
