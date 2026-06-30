# AI Project Context

This document is a compact, provider-agnostic reference for generative models
and coding agents working on this repository.

## Project Summary

- `LVM Graph Viewer` is a C++17 application for LabVIEW `.lvm` / `.txt` signal
  files.
- It has two front ends that share the same parsing and analysis core:
  - `lvm_reader.exe`: CLI tool for inspection, stats, FFT, selection, and CSV
    export.
  - `LVM-graph-viewer-win-x64.exe`: native Win32 GUI viewer.
- `Start GUI.bat`: obvious double-click launcher for the GUI in a release
  folder.
- `run.bat`: CLI helper that launches `lvm_reader.exe`.

## Core Pipeline

1. `lvm_parser.cpp/.hpp` reads LabVIEW measurement files and tab-separated
   numeric files into a `lvm::Dataset`.
2. The parser treats the first numeric column as time and the rest as channels.
3. `drop_duplicate_time_channels()` removes channels that duplicate the time
   axis.
4. `make_monotonic()` repairs sectioned files where time resets.
5. `analysis.cpp/.hpp` computes FFT spectra and peak lists from the prepared
   dataset.
6. `gui_main.cpp` renders the current dataset in Time or Hz mode and provides
   zoom, pan, markers, measurements, exports, and channel visibility control.

## What `lvm_reader.exe` Does

`lvm_reader.exe` is the command-line companion to the GUI. It can:

- print file structure and parser info,
- compute per-channel statistics,
- show the first rows of selected data,
- export the selected window to CSV,
- compute FFT peaks or export the full spectrum to CSV,
- filter by time range and channel list,
- optionally rebuild a monotonic timeline for multi-section files.

## Repository Layout

- `main.cpp` - CLI entry point.
- `gui_main.cpp` - Win32 GUI.
- `lvm_parser.cpp/.hpp` - file parser and timeline fixes.
- `analysis.cpp/.hpp` - FFT spectrum and peak helpers.
- `fft.cpp/.hpp` - FFT implementation.
- `export_helpers.cpp/.hpp` - export prompt text, filenames, and range helpers.
- `formula_engine.cpp/.hpp` - formula parsing, normalization, and evaluation helpers.
- `tests/run_tests.cpp` - regression tests.
- `lvm_files_for_tests/` - sample measurement input.

## Working Rules For Models

- Inspect the existing code before changing behavior.
- Preserve the shared parser/analysis core unless the user asks otherwise.
- Do not revert unrelated user changes.
- Use `apply_patch` for edits.
- Keep changes focused and mechanically justified.
- If a build or test fails, identify the concrete blocker instead of guessing.

## Notes For Future Changes

- `run.bat` depends on `lvm_reader.exe`.
- `Start GUI.bat` should stay the most obvious entry point for first-time users.
- Build artifacts should stay out of source changes unless the user requests
  them.
- If you need a quick sanity check, the regression test target is the fastest
  way to validate parser and FFT behavior.
