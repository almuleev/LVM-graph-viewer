# LVM Graph Viewer — Project Context for AI Assistants

> **Version:** v0.8.0 (latest)  
> **Language:** C++17 (Win32 API)  
> **Build system:** MSYS2/MinGW g++ (UCRT64)  
> **Repository:** `https://github.com/almuleev/LVM-graph-viewer.git`  
> **Working directory:** `X:\Alex\My Projects\programs\LVM-graph-viewer`

---

## 1. Project Overview

A dependency-free native Win32 desktop application for viewing LabVIEW `.lvm` / `.txt` signal files. It has two front-ends sharing one parser/FFT library:

- **`lvm_viewer_gui.exe`** — native Win32 GUI viewer (single ~3000-line `gui_main.cpp`).
- **`lvm_reader.exe`** — CLI tool (`main.cpp`).

The GUI supports:
- Time-domain plotting and FFT spectrum (Hz mode) with zoom/pan on both axes.
- Channel show/hide with interactive legend (click toggle, Ctrl+click solo).
- Measurement points with configurable read-outs (X, Y, Δx, Δy, 1/Δt, distance, point number) and snap-to-data.
- Reference guide lines (vertical/horizontal).
- Visual Catmull-Rom spline smoothing (doesn't change data).
- Real-time playback with playhead and adjustable speed.
- PNG export (GDI+) and CSV export.
- Full i18n (English / Russian) with live language switch.
- Light / dark theme.
- Undo / redo system for points, lines, and markers.
- Crosshair cursor lines (toggleable).
- Welcome screen when launched without a file.

---

## 2. File Structure

```
LVM-graph-viewer/
├── gui_main.cpp          ~3004 lines — entire GUI (WndProc, rendering, i18n, theme)
├── main.cpp              ~600 lines — CLI front-end
├── lvm_parser.cpp / .hpp  LabVIEW .lvm / .txt parser
├── fft.cpp / .hpp         FFT (radix-2 + Bluestein for arbitrary length)
├── analysis.cpp / .hpp    Spectrum computation, peak finding
├── Makefile               Build CLI + tests + GUI
├── build_gui.ps1          PowerShell script to build GUI (adds UCRT64 to PATH)
├── README.md / README_EN.md / README_RU.md
├── banner.png             Banner image for README
├── lvm_files_for_tests/   Sample .lvm files
├── tests/                 Unit tests
├── .gitignore
```

---

## 3. Build System

### GUI build command (MinGW UCRT64):
```bash
g++ -std=c++17 -O2 -municode -static -mwindows -o lvm_viewer_gui.exe \
    gui_main.cpp lvm_parser.cpp fft.cpp analysis.cpp \
    -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32
```

- `-municode` is required for `wWinMain` and `wchar_t` APIs.
- `-static` makes the binary self-contained (no libstdc++/libgcc DLLs).
- `-mwindows` suppresses the console window.

### Build environment quirks:
- **Compiler path:** `C:\msys64\ucrt64\bin\g++.exe`
- **From Git Bash:** `g++` may fail silently when invoked directly because of `TMP`/`TEMP` path conversion issues. Use **Python subprocess** with explicit `TMP`/`TEMP` set to Windows paths, or use `cmd //c` (note double slashes) with `MSYS2_ARG_CONV_EXCL=*`.
- **Working directory:** `X:\Alex\My Projects\programs\LVM-graph-viewer` (contains spaces; bash handles it but sometimes `cmd //c` needs care).

### Makefile targets:
- `make` — builds `lvm_reader.exe` (CLI)
- `make gui` — builds `lvm_viewer_gui.exe`
- `make test` — builds and runs tests
- `make run` — runs CLI on sample file

---

## 4. Architecture of `gui_main.cpp`

### 4.1 Global State (`App g`)

```cpp
struct App {
    lvm::Dataset ds;                     // parsed data
    std::vector<char> visible;           // per-channel visibility
    bool freq_mode = false;            // false = Time, true = Hz (FFT)
    
    double data_t0, data_t1;           // full data range
    double win_start, win_end;         // visible time window
    double freq_start, freq_end;       // visible frequency window
    double approx_dt;                  // approximate sample interval
    
    lvm::Spectrum spec;                // cached FFT spectrum
    bool spec_valid = false;
    
    bool visual_smooth = false;        // Catmull-Rom spline rendering
    bool auto_y = true;                // auto-fit vertical scale
    double y_lock_min, y_lock_max;     // locked Y range when auto_y=false
    bool auto_y_amp = true;            // auto-fit amplitude in Hz mode
    double y_amp_max;                  // locked amplitude in Hz mode
    
    bool measure_mode = false;         // V key toggle
    bool snap_to_data = true;          // snap markers to nearest sample
    PointDisplay pdisp;                // which read-outs to show
    COLORREF marker_color;
    std::vector<std::pair<double,double>> points;  // measurement points
    
    std::vector<GuideLine> guides;     // vertical/horizontal reference lines
    int pending_line = 0;              // 0=none, 1=vertical, 2=horizontal
    
    bool playing = false;              // playback active
    bool playhead_active = false;
    double playhead;
    double play_anchor_data;           // signal time at playback start
    LARGE_INTEGER play_anchor_qpc;     // QPC at playback start
    double play_speed = 1.0;
    
    // Mapping cache (last paint) for hit-testing
    double vx0, vx1, vy0, vy1;
    RECT vrect;
    bool vvalid = false;
    
    struct Marker { double x; std::wstring label; bool freq; };
    std::vector<Marker> markers;
    bool pending_marker = false;
    
    std::wstring file_name;
    std::string last_error;
    
    // HWND handles
    HWND main, open, savepng, savecsv, mode, play, measure, reset, autoy, ptsettings, status;
    std::vector<HWND> checks;          // channel visibility checkboxes
    std::vector<HWND> buttons;       // toolbar buttons (owner-drawn)
    HWND hovered_btn = nullptr;
    std::wstring status_text;
    std::wstring hover_status_text;
    std::vector<int> toolbar_seps;
    
    HWND settings_wnd;                 // modeless settings panel
    HWND welcome_wnd;                  // start screen overlay
    
    HMENU menu;
    HFONT ui_font, bold_font, title_font, axis_font;
    
    bool dragging = false;
    int drag_x = 0;
    double drag_lo, drag_hi;
    
    bool cursor_lines = false;         // toggle crosshair
    int cursor_x = -1, cursor_y = -1;
};
```

### 4.2 Key Functions

| Function | Purpose |
|----------|---------|
| `wWinMain()` | Entry point. Registers window classes, creates main window, shows welcome screen if no file argument. |
| `WndProc()` | Main message loop. Handles `WM_CREATE`, `WM_SIZE`, `WM_PAINT`, `WM_DRAWITEM`, `WM_COMMAND`, `WM_MOUSEWHEEL`, `WM_LBUTTONDOWN`, `WM_MOUSEMOVE`, `WM_KEYDOWN`, `WM_SETCURSOR`, `WM_TIMER`, `WM_GETMINMAXINFO`, `WM_ERASEBKGND`, `WM_CTLCOLORBTN`. |
| `on_paint(HDC)` | Double-buffered painting. Fills toolbar, panel, separators, draws chart, status bar. |
| `draw_chart(HDC, RECT)` | Routes to `draw_time()` or `draw_freq()`, then `draw_guides()`, `draw_markers()`, `draw_measure()`, `draw_cursor_lines()`. |
| `draw_time(HDC, RECT)` | Draws axes, grid, minor grid, data lines (polyline or spline), playhead, legend. |
| `draw_freq(HDC, RECT)` | Draws FFT spectrum with axes, grid, channel lines. |
| `draw_axes(HDC, RECT, x0, x1, y0, y1, xlabel)` | Fills plot background, draws grid lines, ticks, labels, frame. |
| `draw_legend(HDC, RECT)` | Draws colored legend boxes with channel names; updates `g_legend_items` and `g_legend_box` for hit-testing. |
| `draw_guides(HDC)` | Draws vertical/horizontal reference lines with value labels. |
| `draw_markers(HDC)` | Draws marker triangles with labels. |
| `draw_measure(HDC)` | Draws measurement points, lines between them, and read-out labels. |
| `draw_cursor_lines(HDC)` | Crosshair: dotted lines from cursor to axes + value labels. |
| `draw_tooltip()` | Shows hover tooltip near mouse for measurement points. |
| `make_menu()` | Creates full menu bar using `g_str` for i18n. |
| `rebuild_ui()` | Recreates menu, updates button texts, refreshes welcome/settings window titles. Called on language switch. |
| `sync_menu()` | Syncs menu checkmarks with application state (theme, language, smoothing, cursor lines, measure mode, auto_y, visible channels, playback speed). |
| `set_status()` | Builds status text string and invalidates status bar. |
| `update_theme_brushes()` | Recreates `g_panel_brush` and `g_menu_brush` for new theme. |
| `layout()` | Repositions toolbar buttons and channel checkboxes on resize. |
| `open_file()` | `OPENFILENAMEW` dialog, calls `load_path()`. |
| `load_path(wstring)` | Calls `lvm::read_lvm_file()`, sets up channels, resets view, hides welcome screen. |
| `compute_spectrum()` | Calls `lvm::compute_spectrum()` with `max_samples = 524288`. |
| `save_png_dialog()` / `save_csv_dialog()` | Export dialogs. |
| `show_welcome()`, `WelcomeProc()` | Overlay window with start buttons. |
| `open_settings()`, `SettingsProc()` | Modeless settings panel with checkboxes for point display options and color picker. |
| `show_hotkeys()`, `show_about()` | Modal dialogs. |
| `zoom_at()`, `zoom_y_at()`, `zoom_y_amp_at()`, `pan_by()` | View manipulation. |
| `active_axis()` | Returns `Axis` struct with min/max/span for current mode. Minimum window width = `approx_dt * 1.5`. |
| `map_to_client()`, `px_to_data()`, `snap_to_nearest()` | Coordinate conversion and snapping. |
| `channel_color(i)`, `channel_index_by_name()` | Color mapping. |
| `push_undo()`, `pop_undo()`, `pop_redo()` | Undo/redo stack. |
| `make_accelerators()` | Accelerator table for keyboard shortcuts. |

### 4.3 UI Constants

```cpp
constexpr int kTopBar = 56;       // toolbar height
constexpr int kBottomBar = 28;    // status bar height
constexpr int kRightPanel = 220;  // channels panel width
constexpr int kMargin = 12;       // plot margin inside client area
constexpr int kPlotMargin = 46;   // left margin for Y-axis labels
constexpr int kPlotBotMargin = 34; // bottom margin for X-axis labels
constexpr int kLegendItemHeight = 20;
constexpr int kLegendPadX = 10, kLegendPadY = 6;
```

### 4.4 Control IDs (important enum values)

```cpp
// Toolbar buttons (1001+)
IDC_OPEN = 1001, IDC_SAVEPNG, IDC_SAVECSV, IDC_MODE, IDC_PLAY,
IDC_MEASURE, IDC_ZOOMIN, IDC_ZOOMOUT, IDC_RESET, IDC_PANLEFT, IDC_PANRIGHT,
IDC_AUTOY, IDC_PTSETTINGS

// Menu commands (1100+)
IDM_EXIT = 1100, IDM_VISMOOTH, IDM_SNAP, IDM_ADD_VLINE, IDM_ADD_HLINE,
IDM_ADD_MARKER, IDM_CLEAR_LINES, IDM_CLEAR_MARKERS, IDM_CLEAR_POINTS,
IDM_HOTKEYS, IDM_ABOUT, IDS_COLOR, IDW_START

// Point display toggles (1200+)
IDM_PT_NUM = 1200, IDM_PT_X, IDM_PT_Y, IDM_PT_DX, IDM_PT_DY, IDM_PT_INVDT, IDM_PT_DIST

// Playback speed (1300+)
IDM_SPEED_00001 = 1300, ... IDM_SPEED_10

// Undo/redo
IDM_UNDO = 1400, IDM_REDO

// Language
IDM_LANG_RU = 1500, IDM_LANG_EN

// Cursor lines
IDM_CURSOR_LINES = 1600

// Channel checkboxes (base + index)
IDC_CHAN_BASE = 2000
```

---

## 5. i18n System (`Strings`)

### 5.1 `Strings` Struct (field order is CRITICAL)

```cpp
struct Strings {
    const wchar_t* app_title;
    const wchar_t* menu_file; const wchar_t* menu_view; const wchar_t* menu_meas; const wchar_t* menu_lines; const wchar_t* menu_markers; const wchar_t* menu_help;
    const wchar_t* m_open; const wchar_t* m_savepng; const wchar_t* m_savecsv; const wchar_t* m_undo; const wchar_t* m_redo; const wchar_t* m_exit;
    const wchar_t* m_timehz; const wchar_t* m_zoomin; const wchar_t* m_zoomout; const wchar_t* m_reset; const wchar_t* m_autoy; const wchar_t* m_smooth; const wchar_t* m_play; const wchar_t* m_theme; const wchar_t* m_speed;
    const wchar_t* m_points; const wchar_t* m_ptsettings; const wchar_t* m_clearpts;
    const wchar_t* m_vline; const wchar_t* m_hline; const wchar_t* m_clearlines;
    const wchar_t* m_addmarker; const wchar_t* m_clearmarkers;
    const wchar_t* m_hotkeys; const wchar_t* m_about;
    const wchar_t* btn_open; const wchar_t* btn_png; const wchar_t* btn_csv; const wchar_t* btn_timehz; const wchar_t* btn_play; const wchar_t* btn_pause;
    const wchar_t* btn_measure; const wchar_t* btn_reset; const wchar_t* btn_autoy; const wchar_t* btn_settings;
    const wchar_t* panel_channels;
    const wchar_t* st_time; const wchar_t* st_hz; const wchar_t* st_channels; const wchar_t* st_points; const wchar_t* st_window; const wchar_t* st_yauto; const wchar_t* st_yfix; const wchar_t* st_lines; const wchar_t* st_markers; const wchar_t* st_speed;
    const wchar_t* plot_xlabel_time; const wchar_t* plot_xlabel_freq;
    const wchar_t* pt_num; const wchar_t* pt_x; const wchar_t* pt_y; const wchar_t* pt_dx; const wchar_t* pt_dy; const wchar_t* pt_invdt; const wchar_t* pt_dist;
    const wchar_t* pt_snap;
    const wchar_t* dlg_ptsettings_title;
    const wchar_t* dlg_hotkeys_title; const wchar_t* dlg_about_title;
    const wchar_t* msg_nodata; const wchar_t* msg_openfirst; const wchar_t* msg_savepng_err; const wchar_t* msg_savecsv_err; const wchar_t* msg_read_err;
    const wchar_t* welcome_title; const wchar_t* welcome_subtitle; const wchar_t* welcome_body;
    const wchar_t* welcome_btn_open; const wchar_t* welcome_btn_settings; const wchar_t* welcome_btn_hotkeys; const wchar_t* welcome_btn_start;
    const wchar_t* hk_title;
    const wchar_t* about_body;
    const wchar_t* hover_open; const wchar_t* hover_png; const wchar_t* hover_csv; const wchar_t* hover_timehz; const wchar_t* hover_play; const wchar_t* hover_pause; const wchar_t* hover_measure; const wchar_t* hover_reset; const wchar_t* hover_autoy; const wchar_t* hover_settings;
    const wchar_t* lang_ru; const wchar_t* lang_en;
    const wchar_t* m_lang;
    // NEW v0.4.2 fields — MUST be placed here, after m_lang, before msg_loading
    const wchar_t* m_cursor;
    const wchar_t* fmt_xval;
    const wchar_t* fmt_yval;
    const wchar_t* fmt_cursor;
    const wchar_t* msg_loading;
    const wchar_t* msg_openprompt;
    const wchar_t* msg_delta_f;
    const wchar_t* msg_delta_t;
    const wchar_t* st_spline;
    const wchar_t* fmt_hz;
    const wchar_t* fmt_sec;
    const wchar_t* fmt_y;
    const wchar_t* unit_hz;
    const wchar_t* unit_sec;
    const wchar_t* theme_light;
    const wchar_t* theme_dark;
    const wchar_t* dlg_color;
    const wchar_t* msg_error_title;
    const wchar_t* msg_saved_png;
    const wchar_t* msg_saved_csv;
    const wchar_t* filter_open;   // double-null string for OPENFILENAMEW
    const wchar_t* filter_png;      // double-null string
    const wchar_t* filter_csv;    // double-null string
    const wchar_t* csv_time;
    const wchar_t* csv_freq;
    const wchar_t* status_vline;
    const wchar_t* status_hline;
    const wchar_t* status_marker;
};
```

### 5.2 Global Pointer

```cpp
const Strings* g_str = &kRu;   // or &kEn
```

### 5.3 Critical Rule for Adding Strings

When adding a new field to `Strings`:
1. **Add it to the struct** at the correct position.
2. **Add the corresponding string** to BOTH `kRu` and `kEn` initializers at the **exact same position** in the brace-enclosed list.
3. **Do NOT append new strings to the end** unless the struct field is also at the end. Any mismatch shifts all subsequent strings, causing bizarre UI bugs (wrong text in dialogs, wrong file filters, etc.).

This was the root cause of the v0.4.2→v0.4.3 bug: `m_cursor`, `fmt_xval`, `fmt_yval`, `fmt_cursor` were added to the struct after `m_lang` but appended to the end of `kRu`/`kEn`, shifting everything by 4 positions.

---

## 6. Theme System

```cpp
struct Theme {
    COLORREF bg_main, bg_toolbar, bg_panel, bg_plot, bg_status;
    COLORREF grid, minor_grid, frame, axis_text;
    COLORREF text_primary, text_secondary;
    COLORREF accent, accent_hover, separator;
    COLORREF btn_bg, btn_border, btn_hover, btn_active;
    COLORREF btn_pressed, playhead, marker_color;
};

static const Theme kLightTheme = { ... };
static const Theme kDarkTheme = { ... };
const Theme* g_theme = &kLightTheme;
```

- Menu background is set via `SetMenuInfo` with `MIM_BACKGROUND | MIM_APPLYTOSUBMENUS` using `CreateSolidBrush(g_theme->bg_toolbar)`.
- Panel background uses `g_panel_brush` (created in `update_theme_brushes()`).
- Button colors are handled in `WM_DRAWITEM` with owner-drawn `BS_OWNERDRAW` buttons.

---

## 7. Keyboard Shortcuts (Accelerators)

| Key | Action |
|-----|--------|
| `O` / `Ctrl+O` | Open file |
| `S` / `Ctrl+S` | Save PNG |
| `E` / `Ctrl+E` | Save CSV |
| `M` | Toggle Time / Hz |
| `C` | Toggle visual smoothing |
| `+` / `↑` / `Num+` | Zoom in |
| `-` / `↓` / `Num-` | Zoom out |
| `←` / `→` | Pan left / right |
| `Home` | Reset view |
| `Space` | Play / Pause |
| `V` | Toggle measure mode |
| `Delete` | Clear measurement points |
| `L` | Add vertical line (armed) |
| `H` | Add horizontal line (armed) |
| `K` | Add marker (armed) |
| `Esc` | Cancel pending line/marker |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `T` | Toggle theme |
| `F1` | Hotkeys help |

Mouse:
- Wheel — zoom X under cursor
- `Shift`+wheel — pan left/right
- `Ctrl`+wheel — zoom Y
- `Alt`+wheel — fine zoom X
- Left drag — pan
- Left click (in plot) — place point / line / marker depending on mode
- Right click — clear measurement points
- Legend click — toggle channel, `Ctrl+click` — solo channel

---

## 8. Release Workflow

1. Update version in `README.md` and `README_*`.
2. Build `lvm_viewer_gui.exe`.
3. Git commit + tag (`vX.Y.Z`).
4. Push: `git push origin main && git push origin vX.Y.Z`.
5. Create GitHub release via API and upload `.exe` as asset named `LVM-graph-viewer-vX.Y.Z-win-x64.exe`.

### GitHub Release API (used in this project):
- **Token:** `<REDACTED>`
- **Create release:** `POST https://api.github.com/repos/almuleev/LVM-graph-viewer/releases`
- **Upload asset:** `POST https://uploads.github.com/repos/almuleev/LVM-graph-viewer/releases/{id}/assets?name=LVM-graph-viewer-vX.Y.Z-win-x64.exe`
- **Headers:** `Authorization: token <TOKEN>`, `Content-Type: application/octet-stream`, `Content-Length: <bytes>`
- **curl with `-k` (insecure)** is used for both API and upload endpoints.

### Git push notes:
- Git path: `C:\Users\user\AppData\Local\GitHubDesktop\app-3.5.12\resources\app\git\cmd\git.exe`
- Use `GIT_SSL_NO_VERIFY=true` before `git push` (the bundled git doesn't support `-c http.sslVerify=false`).

---

## 9. Known Issues & Pitfalls

1. **String table alignment:** Adding fields to `Strings` without matching the initializer order causes shifted text everywhere. See §5.3.
2. **Build from Git Bash:** Direct `g++` invocation may fail silently. Use Python subprocess or `cmd //c` with explicit environment.
3. **Welcome screen text hardcoded:** `WelcomeProc` creates static text controls with hardcoded Russian strings in `WM_CREATE`. These should ideally use `g_str`, but currently they are literals. Changing them requires updating the `CreateWindowExW` calls in `WelcomeProc`.
4. **Menu darkening:** `SetMenuInfo` with `MIM_BACKGROUND` works on modern Windows but may not apply to the menu bar itself on some systems (only submenus). The menu bar remains the default OS color in some cases.
5. **Cursor lines:** `draw_cursor_lines` uses `PS_DOT` pen. The `SetBkMode(dc, TRANSPARENT)` is important so the dots are visible over the grid.
6. **Zoom limit:** `active_axis()` enforces `minw = approx_dt * 1.5` (was 4.0). Too small and single-pixel aliasing becomes visible.
7. **Legend hit-testing:** `g_legend_items` and `g_legend_box` are updated during `draw_legend`. Click handling is in `WM_LBUTTONDOWN` in `WndProc`.

---

## 10. How to Add a New Feature

### General rules:
- Keep everything in `gui_main.cpp` unless it's a new parser/FFT/analysis feature (those go in their respective files).
- Add any new user-visible strings to the `Strings` struct AND both `kRu` and `kEn` initializers at the **same position**.
- If the feature needs a menu item, add it to `make_menu()` and a toolbar button to `WM_CREATE` (if needed), plus update `sync_menu()` and `rebuild_ui()`.
- If the feature needs a new control ID, add it to the `enum` near the top of `gui_main.cpp`.
- If the feature changes state, update `set_status()` if relevant.
- Update `README.md` and `README_EN.md` / `README_RU.md` for user-facing changes.

---

## 11. Current State (as of v0.8.0)

- **Stable UI foundation:** redesigned welcome screen, live RU/EN switch, unified settings window, refreshed button states.
- **Measurement workflow expanded:** inline channel renaming, marker snapping, configurable read-outs, marker colour settings, undo/redo.
- **Signal preprocessing available:** global and per-channel multiplier / offset are applied in charts, snapping, and exported visible time-domain data.
- **Build fix included:** GUI sources and Windows build flags now preserve UTF-8 Russian strings correctly.
