# Contributing

Thanks for your interest in improving `AM Graph Viewer`.

## Before You Start

- Check existing issues before opening a new one.
- For UI changes, include screenshots or a short screen recording when possible.
- For parser, FFT or export changes, include a reproducible sample file or a minimal test case.

## Build

### GUI

```powershell
powershell -ExecutionPolicy Bypass -File .\build_gui.ps1
```

### CLI and tests

```bash
make
make test
```

## Pull Request Notes

- Keep changes focused.
- Mention behavioural changes clearly.
- Update documentation when UI, hotkeys or export behaviour changes.
- If you touch parser or FFT logic, add or update tests where practical.

## Reporting Bugs

Please include:

- release version or commit hash
- Windows version
- exact reproduction steps
- sample file if the issue depends on input data
- screenshot if the issue is visual
