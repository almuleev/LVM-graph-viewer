// LVM Viewer — native Win32 GUI front end.
//
// Self-contained desktop viewer (no external GUI toolkit). Features:
//   - Main menu bar (Файл / Вид / Измерения / Линии / Справка) + toolbar.
//   - Time plot and FFT (Hz) spectrum, with zoom/pan on both axes.
//   - Channel show/hide, colored legend, min/max envelope for dense data.
//   - Measure tool with a settings menu: choose which read-outs to show
//     (X, Y, Δx, Δy, 1/Δt, distance, point number) and optionally snap
//     markers to the nearest real data sample ("примагничивание").
//   - Reference guide lines: add vertical / horizontal lines on the plot.
//   - Two independent smoothing controls: a moving-average filter (changes
//     the rendered values) and a purely visual Catmull-Rom spline that
//     curves between samples without moving the underlying data points.
//   - Playback / pause that sweeps a playhead through the time signal.
//   - Export the visible segment to PNG (GDI+) or CSV.
//   - Keyboard shortcuts (see Справка → Горячие клавиши / F1).
//
// Build:
//   g++ -std=c++17 -O2 -municode -static -mwindows -o lvm_viewer_gui.exe \
//       gui_main.cpp lvm_parser.cpp fft.cpp analysis.cpp \
//       -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using std::max;
using std::min;
#include <gdiplus.h>

#include "analysis.hpp"
#include "lvm_parser.hpp"

namespace {

enum {
    IDC_OPEN = 1001,
    IDC_SAVEPNG,
    IDC_SAVECSV,
    IDC_MODE,
    IDC_PLAY,
    IDC_MEASURE,
    IDC_ZOOMIN,
    IDC_ZOOMOUT,
    IDC_RESET,
    IDC_PANLEFT,
    IDC_PANRIGHT,
    IDC_LOCKY,          // toolbar: lock the vertical scale
    IDC_PTSETTINGS,     // toolbar: open the measurement-point settings panel

    // Menu-only commands (no toolbar button).
    IDM_EXIT = 1100,
    IDM_VISMOOTH,       // visual (spline) smoothing toggle
    IDM_SNAP,           // snap measurement markers to data
    IDM_ADD_VLINE,      // arm: place a vertical guide line
    IDM_ADD_HLINE,      // arm: place a horizontal guide line
    IDM_CLEAR_LINES,
    IDM_CLEAR_POINTS,
    IDM_HOTKEYS,
    IDM_ABOUT,
    IDS_COLOR,          // settings panel: marker colour button
    IDW_START,          // welcome screen: start working

    // Measurement read-out toggles (used as control ids in the settings panel).
    IDM_PT_NUM = 1200,
    IDM_PT_X,
    IDM_PT_Y,
    IDM_PT_DX,
    IDM_PT_DY,
    IDM_PT_INVDT,
    IDM_PT_DIST,

    IDC_CHAN_BASE = 2000,
};

const int kTopBar = 80;        // two toolbar rows
const int kRightPanel = 180;
const int kBottomBar = 28;     // status-bar strip at the very bottom
const int kAxisBottom = 38;    // room under the plot for the X tick labels + title
const int kAxisLeft = 70;

const COLORREF kPalette[] = {
    RGB(31, 119, 180), RGB(255, 127, 14), RGB(44, 160, 44), RGB(214, 39, 40),
    RGB(148, 103, 189), RGB(140, 86, 75), RGB(227, 119, 194), RGB(127, 127, 127),
    RGB(188, 189, 34), RGB(23, 190, 207),
};
COLORREF channel_color(std::size_t i) { return kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))]; }

// Which read-outs to draw next to measurement markers (toggled from the
// "Измерения → Отображать у точек" menu).
struct PointDisplay {
    bool number = true;   // #1, #2, …
    bool x = true;        // x coordinate
    bool y = true;        // y coordinate
    bool dx = true;       // Δx to the previous point
    bool dy = true;       // Δy to the previous point
    bool inv_dt = true;   // 1/Δt (Hz) — time mode only
    bool dist = false;    // Euclidean distance to the previous point
};

// A reference line the user dropped on the plot. `value` is in data units of
// the axis it pins (x for vertical, y for horizontal); `freq` records whether
// it belongs to the Hz view so it only shows in the matching mode.
struct GuideLine {
    bool vertical = true;
    double value = 0.0;
    bool freq = false;
};

struct App {
    lvm::Dataset ds;
    std::vector<char> visible;
    bool freq_mode = false;

    double data_t0 = 0.0, data_t1 = 1.0;
    double win_start = 0.0, win_end = 1.0;
    double freq_start = 0.0, freq_end = 1.0;
    double approx_dt = 1.0;

    lvm::Spectrum spec;
    bool spec_valid = false;

    bool visual_smooth = false;  // Catmull-Rom spline rendering (data unchanged)

    bool lock_y = false;            // freeze the vertical scale (no auto-fit)
    double y_lock_min = -1.0, y_lock_max = 1.0;

    bool measure_mode = false;
    bool snap_to_data = true;       // snap markers to the nearest real sample
    PointDisplay pdisp;             // which read-outs to draw at markers
    COLORREF marker_color = RGB(200, 0, 0);
    std::vector<std::pair<double, double>> points;  // measurement points (data coords)

    std::vector<GuideLine> guides;  // vertical / horizontal reference lines
    int pending_line = 0;           // 0 none, 1 next click = vertical, 2 = horizontal

    bool playing = false;
    bool playhead_active = false;
    double playhead = 0.0;
    double play_anchor_data = 0.0;        // signal time when playback (re)started
    LARGE_INTEGER play_anchor_qpc = {};   // performance counter at that moment

    // Mapping cache from the last paint (data <-> pixels) for hit-testing.
    double vx0 = 0, vx1 = 1, vy0 = 0, vy1 = 1;
    RECT vrect = {0, 0, 1, 1};
    bool vvalid = false;

    std::wstring file_name;
    std::string last_error;

    HWND main = nullptr;
    HWND open = nullptr, savepng = nullptr, savecsv = nullptr, mode = nullptr;
    HWND play = nullptr, measure = nullptr;
    HWND reset = nullptr, locky = nullptr, ptsettings = nullptr;
    HWND status = nullptr;
    std::vector<HWND> checks;

    HWND settings_wnd = nullptr; // measurement-point settings panel (modeless)
    HWND welcome_wnd = nullptr;  // start screen

    HMENU menu = nullptr;        // main menu bar
    HFONT ui_font = nullptr;     // Segoe UI for controls / labels
    HFONT bold_font = nullptr;   // semibold for headings
    HFONT title_font = nullptr;  // large font for the welcome title

    bool dragging = false;
    int drag_x = 0;
    double drag_lo = 0.0, drag_hi = 0.0;
};

App g;
ULONG_PTR g_gdiplus_token = 0;

std::wstring to_w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

std::string to_acp(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_ACP, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

// Compact ASCII number for CSV; empty for NaN (matches the CLI / pandas).
std::string numfmt(double v) {
    if (std::isnan(v)) return "";
    char b[32];
    std::snprintf(b, sizeof(b), "%.15g", v);
    return b;
}

bool has_data() { return g.ds.ok && g.ds.rows() > 1 && g.ds.channel_count() > 0; }

void set_status() {
    if (!g.status) return;
    std::wstring s;
    wchar_t buf[512];
    if (!has_data()) {
        s = L"Файл не открыт. Нажмите «Открыть файл» (или клавишу O).";
    } else if (g.freq_mode) {
        swprintf(buf, 512,
                 L"Гц (FFT)  |  Каналов: %zu  |  Найквист: %.4g Гц  |  Окно: %.4g .. %.4g Гц",
                 g.ds.channel_count(), g.spec_valid ? g.spec.nyquist : 0.0,
                 g.freq_start, g.freq_end);
        s = buf;
    } else {
        swprintf(buf, 512,
                 L"Время  |  Каналов: %zu  |  Точек: %zu  |  Окно: %.5g .. %.5g c  |  Y: ",
                 g.ds.channel_count(), g.ds.rows(), g.win_start, g.win_end);
        s = buf;
        s += g.lock_y ? L"фикс." : L"авто";
        if (g.visual_smooth) s += L" (+сплайн)";
    }
    if (has_data()) {
        std::size_t nlines = 0;
        for (const auto& gl : g.guides)
            if (gl.freq == g.freq_mode) ++nlines;
        if (nlines) { swprintf(buf, 512, L"  |  Линий: %zu", nlines); s += buf; }
    }
    if (g.points.size() >= 2) {
        const auto& a = g.points[g.points.size() - 2];
        const auto& b = g.points.back();
        const double dx = b.first - a.first, dy = b.second - a.second;
        if (g.freq_mode) {
            swprintf(buf, 512, L"   |   Δf = %.5g Гц,  Δamp = %.4g", dx, dy);
        } else {
            const double inv = (dx != 0.0) ? 1.0 / dx : 0.0;
            swprintf(buf, 512, L"   |   Δt = %.6g c,  Δy = %.5g,  1/Δt = %.6g Гц", dx, dy, inv);
        }
        s += buf;
    }
    SetWindowTextW(g.status, s.c_str());
}

void compute_spectrum() {
    if (!has_data()) return;
    g.spec = lvm::compute_spectrum(g.ds, 16384);
    g.spec_valid = g.spec.ok;
}

int channel_index_by_name(const std::string& name) {
    for (std::size_t i = 0; i < g.ds.names.size(); ++i)
        if (g.ds.names[i] == name) return static_cast<int>(i);
    return -1;
}

void destroy_checks() {
    for (HWND h : g.checks) DestroyWindow(h);
    g.checks.clear();
}

void rebuild_checks() {
    destroy_checks();
    if (!has_data()) return;
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    for (std::size_t i = 0; i < g.ds.channel_count(); ++i) {
        HWND c = CreateWindowExW(
            0, L"BUTTON", to_w(g.ds.names[i]).c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 10, 10, g.main,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_BASE + i)), inst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(c, BM_SETCHECK, g.visible[i] ? BST_CHECKED : BST_UNCHECKED, 0);
        g.checks.push_back(c);
    }
}

void layout() {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    int x = 8;
    auto place = [&](HWND h, int w, int y) { MoveWindow(h, x, y, w, 28, TRUE); x += w + 6; };
    x = 8;
    place(g.open, 110, 8);
    place(g.savepng, 120, 8);
    place(g.savecsv, 120, 8);
    place(g.mode, 100, 8);
    place(g.play, 150, 8);
    place(g.measure, 120, 8);
    x = 8;
    place(g.reset, 120, 42);
    place(g.locky, 150, 42);
    place(g.ptsettings, 170, 42);

    const int panel_x = cw - kRightPanel + 12;
    int y = kTopBar + 28;
    for (HWND c : g.checks) { MoveWindow(c, panel_x, y, kRightPanel - 24, 22, TRUE); y += 24; }

    MoveWindow(g.status, 8, ch - kBottomBar + 4, cw - 16, 20, TRUE);
}

RECT plot_rect() {
    RECT rc;
    GetClientRect(g.main, &rc);
    RECT p;
    p.left = kAxisLeft;
    p.top = kTopBar + 6;
    p.right = rc.right - kRightPanel;
    p.bottom = rc.bottom - kBottomBar - kAxisBottom;
    if (p.right < p.left + 20) p.right = p.left + 20;
    if (p.bottom < p.top + 20) p.bottom = p.top + 20;
    return p;
}

// ---- view (pan / zoom) ---------------------------------------------------

void clamp_range(double& lo, double& hi, double minb, double maxb, double minw) {
    double w = hi - lo;
    const double full = maxb - minb;
    if (w < minw) w = minw;
    if (w > full) w = full;
    if (lo < minb) lo = minb;
    hi = lo + w;
    if (hi > maxb) { hi = maxb; lo = hi - w; if (lo < minb) lo = minb; }
}

bool active_axis(double*& lo, double*& hi, double& minb, double& maxb, double& minw) {
    if (g.freq_mode) {
        if (!g.spec_valid || g.spec.freqs.size() < 2) return false;
        lo = &g.freq_start; hi = &g.freq_end;
        minb = 0.0; maxb = g.spec.nyquist;
        minw = (g.spec.freqs[1] - g.spec.freqs[0]) * 4.0;
        return true;
    }
    if (!has_data()) return false;
    lo = &g.win_start; hi = &g.win_end;
    minb = g.data_t0; maxb = g.data_t1;
    minw = std::max(g.approx_dt * 4.0, (g.data_t1 - g.data_t0) * 1e-6);
    return true;
}

void zoom_at(double center_frac, double factor) {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    const double c = *lo + w * center_frac;
    const double nw = w * factor;
    *lo = c - nw * center_frac;
    *hi = *lo + nw;
    clamp_range(*lo, *hi, minb, maxb, minw);
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
}

void pan_by(double frac) {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    *lo += w * frac;
    *hi += w * frac;
    clamp_range(*lo, *hi, minb, maxb, minw);
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
}

void reset_view() {
    g.win_start = g.data_t0;
    g.win_end = g.data_t1;
    g.freq_start = 0.0;
    g.freq_end = g.spec_valid ? g.spec.nyquist : 1.0;
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
}

// ---- playback ------------------------------------------------------------

void stop_play() {
    g.playing = false;
    KillTimer(g.main, 1);
    if (g.play) SetWindowTextW(g.play, L"▶ Воспроизв.");
}

void start_play() {
    if (!has_data() || g.freq_mode) return;
    g.playing = true;
    g.playhead_active = true;
    if (g.playhead < g.win_start || g.playhead >= g.data_t1) g.playhead = g.win_start;
    // Anchor the playhead to wall-clock time so playback runs at 1 s of signal
    // per 1 s of real time, independent of timer jitter.
    g.play_anchor_data = g.playhead;
    QueryPerformanceCounter(&g.play_anchor_qpc);
    SetTimer(g.main, 1, 16, nullptr);   // ~60 fps for smooth scrolling
    if (g.play) SetWindowTextW(g.play, L"⏸ Пауза");
}

void toggle_play() {
    if (g.playing) stop_play();
    else start_play();
}

// ---- loading -------------------------------------------------------------

bool load_path(const std::wstring& wpath) {
    lvm::Dataset ds = lvm::read_lvm_file(to_acp(wpath.c_str()));
    if (!ds.ok) { g.last_error = ds.error; return false; }

    const std::vector<double> raw_time = ds.time;
    lvm::drop_duplicate_time_channels(ds, raw_time);
    lvm::make_monotonic(ds.time);

    g.ds = std::move(ds);
    g.visible.assign(g.ds.channel_count(), 1);
    g.data_t0 = g.ds.time.front();
    g.data_t1 = g.ds.time.back();
    if (g.data_t1 <= g.data_t0) g.data_t1 = g.data_t0 + 1.0;
    g.win_start = g.data_t0;
    g.win_end = g.data_t1;
    g.approx_dt = (g.data_t1 - g.data_t0) / static_cast<double>(g.ds.rows());
    g.points.clear();
    stop_play();
    g.playhead = g.data_t0;
    g.playhead_active = false;
    g.lock_y = false;   // a fresh file starts on auto-fit
    if (g.locky) SendMessageW(g.locky, BM_SETCHECK, BST_UNCHECKED, 0);
    if (g.menu) CheckMenuItem(g.menu, IDC_LOCKY, MF_BYCOMMAND | MF_UNCHECKED);

    compute_spectrum();
    g.freq_start = 0.0;
    g.freq_end = g.spec_valid ? g.spec.nyquist : 1.0;

    const wchar_t* base = wcsrchr(wpath.c_str(), L'\\');
    g.file_name = base ? base + 1 : wpath;
    SetWindowTextW(g.main, (L"LVM Viewer — " + g.file_name).c_str());

    rebuild_checks();
    layout();
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
    return true;
}

void open_file() {
    wchar_t file[2048] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = L"LVM / текстовые файлы\0*.lvm;*.txt\0Все файлы\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 2048;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    if (!load_path(file))
        MessageBoxW(g.main, to_w(g.last_error).c_str(), L"Ошибка чтения", MB_ICONERROR | MB_OK);
}

// ---- series ---------------------------------------------------------------

// Copy values for indices [lo,hi) into `out` as floats. (Data is rendered as-is;
// the only smoothing offered is the purely visual spline in draw_time.)
void build_series(const std::vector<double>& col, std::size_t lo, std::size_t hi,
                  std::vector<float>& out) {
    const std::size_t count = hi - lo;
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) out[i] = static_cast<float>(col[lo + i]);
}

// Auto-fit vertical range over the currently visible time window (with 5% pad).
bool current_time_yrange(double& ymin, double& ymax) {
    if (!has_data()) return false;
    const std::vector<double>& t = g.ds.time;
    const std::size_t n = t.size();
    std::size_t lo = static_cast<std::size_t>(
        std::lower_bound(t.begin(), t.end(), g.win_start) - t.begin());
    std::size_t hi = static_cast<std::size_t>(
        std::upper_bound(t.begin(), t.end(), g.win_end) - t.begin());
    if (lo > 0) --lo;
    if (hi < n) ++hi;
    ymin = 1e300; ymax = -1e300;
    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        const auto& col = g.ds.channels[c];
        for (std::size_t i = lo; i < hi; ++i) {
            const double v = col[i];
            if (std::isnan(v)) continue;
            if (v < ymin) ymin = v;
            if (v > ymax) ymax = v;
        }
    }
    if (ymin > ymax) { ymin = -1; ymax = 1; }
    if (ymax - ymin < 1e-12) { ymin -= 1; ymax += 1; }
    const double pad = (ymax - ymin) * 0.05;
    ymin -= pad; ymax += pad;
    return true;
}

// ---- drawing -------------------------------------------------------------

void draw_text(HDC dc, int x, int y, const wchar_t* s, UINT align) {
    SetTextAlign(dc, align);
    TextOutW(dc, x, y, s, lstrlenW(s));
}

void draw_axes(HDC dc, const RECT& p, double x0, double x1, double y0, double y1,
               const wchar_t* xlabel) {
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(225, 225, 225));
    HPEN frame = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    HGDIOBJ old = SelectObject(dc, grid);
    SetTextColor(dc, RGB(80, 80, 80));
    wchar_t buf[64];

    const int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        const double fx = static_cast<double>(i) / ticks;
        const int px = p.left + static_cast<int>(fx * (p.right - p.left));
        SelectObject(dc, grid);
        MoveToEx(dc, px, p.top, nullptr);
        LineTo(dc, px, p.bottom);
        swprintf(buf, 64, L"%.4g", x0 + fx * (x1 - x0));
        SetTextAlign(dc, TA_CENTER | TA_TOP);
        TextOutW(dc, px, p.bottom + 4, buf, lstrlenW(buf));

        const double fy = static_cast<double>(i) / ticks;
        const int py = p.bottom - static_cast<int>(fy * (p.bottom - p.top));
        MoveToEx(dc, p.left, py, nullptr);
        LineTo(dc, p.right, py);
        swprintf(buf, 64, L"%.4g", y0 + fy * (y1 - y0));
        SetTextAlign(dc, TA_RIGHT | TA_BASELINE);
        TextOutW(dc, p.left - 6, py + 4, buf, lstrlenW(buf));
    }

    SelectObject(dc, frame);
    Rectangle(dc, p.left, p.top, p.right, p.bottom);
    draw_text(dc, (p.left + p.right) / 2, p.bottom + 20, xlabel, TA_CENTER | TA_TOP);

    SelectObject(dc, old);
    DeleteObject(grid);
    DeleteObject(frame);
}

void draw_legend(HDC dc, const RECT& p) {
    int y = p.top + 6;
    for (std::size_t i = 0; i < g.ds.channel_count(); ++i) {
        if (!g.visible[i]) continue;
        HPEN pen = CreatePen(PS_SOLID, 2, channel_color(i));
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, p.right - 150, y + 7, nullptr);
        LineTo(dc, p.right - 126, y + 7);
        SelectObject(dc, old);
        DeleteObject(pen);
        SetTextColor(dc, RGB(40, 40, 40));
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        std::wstring nm = to_w(g.ds.names[i]);
        TextOutW(dc, p.right - 120, y, nm.c_str(), static_cast<int>(nm.size()));
        y += 16;
    }
}

// Vertical / horizontal reference lines, using the cached mapping.
void draw_guides(HDC dc) {
    if (g.guides.empty() || !g.vvalid) return;
    const RECT& p = g.vrect;
    if (g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return;
    auto mx = [&](double dx) {
        return p.left + static_cast<int>((dx - g.vx0) / (g.vx1 - g.vx0) * (p.right - p.left));
    };
    auto my = [&](double dy) {
        return p.bottom - static_cast<int>((dy - g.vy0) / (g.vy1 - g.vy0) * (p.bottom - p.top));
    };

    HRGN clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
    SelectClipRgn(dc, clip);
    HPEN pen = CreatePen(PS_DASH, 1, RGB(0, 150, 60));
    HGDIOBJ old = SelectObject(dc, pen);
    SetTextColor(dc, RGB(0, 110, 45));
    wchar_t b[48];
    for (const auto& gl : g.guides) {
        if (gl.freq != g.freq_mode) continue;
        if (gl.vertical) {
            const int X = mx(gl.value);
            if (X < p.left || X > p.right) continue;
            MoveToEx(dc, X, p.top, nullptr); LineTo(dc, X, p.bottom);
            swprintf(b, 48, g.freq_mode ? L"%.5g Гц" : L"%.5g c", gl.value);
            SetTextAlign(dc, TA_LEFT | TA_TOP);
            TextOutW(dc, X + 3, p.top + 2, b, lstrlenW(b));
        } else {
            const int Y = my(gl.value);
            if (Y < p.top || Y > p.bottom) continue;
            MoveToEx(dc, p.left, Y, nullptr); LineTo(dc, p.right, Y);
            swprintf(b, 48, L"y=%.5g", gl.value);
            SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
            TextOutW(dc, p.left + 4, Y - 2, b, lstrlenW(b));
        }
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

// Draw measurement points / segments using the cached mapping. Which read-outs
// appear is controlled by g.pdisp (the "Отображать у точек" settings menu).
void draw_measure(HDC dc) {
    if (g.points.empty() || !g.vvalid) return;
    const RECT& p = g.vrect;
    if (g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return;
    auto mx = [&](double dx) {
        return p.left + static_cast<int>((dx - g.vx0) / (g.vx1 - g.vx0) * (p.right - p.left));
    };
    auto my = [&](double dy) {
        return p.bottom - static_cast<int>((dy - g.vy0) / (g.vy1 - g.vy0) * (p.bottom - p.top));
    };
    const wchar_t* xunit = g.freq_mode ? L"Гц" : L"c";

    HRGN clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
    SelectClipRgn(dc, clip);

    HPEN seg = CreatePen(PS_DASH, 1, g.marker_color);
    HGDIOBJ old = SelectObject(dc, seg);
    for (std::size_t i = 1; i < g.points.size(); ++i) {
        MoveToEx(dc, mx(g.points[i - 1].first), my(g.points[i - 1].second), nullptr);
        LineTo(dc, mx(g.points[i].first), my(g.points[i].second));
    }
    SelectObject(dc, old);
    DeleteObject(seg);

    HPEN pp = CreatePen(PS_SOLID, 2, g.marker_color);
    old = SelectObject(dc, pp);
    wchar_t b[96];
    for (std::size_t i = 0; i < g.points.size(); ++i) {
        const int X = mx(g.points[i].first), Y = my(g.points[i].second);
        MoveToEx(dc, X - 6, Y, nullptr); LineTo(dc, X + 7, Y);
        MoveToEx(dc, X, Y - 6, nullptr); LineTo(dc, X, Y + 7);

        std::wstring lab;
        if (g.pdisp.number) { swprintf(b, 96, L"#%zu ", i + 1); lab += b; }
        if (g.pdisp.x) { swprintf(b, 96, L"x=%.5g", g.points[i].first); lab += b; lab += xunit; lab += L" "; }
        if (g.pdisp.y) { swprintf(b, 96, L"y=%.5g", g.points[i].second); lab += b; }
        if (!lab.empty()) {
            SetTextColor(dc, g.marker_color);
            SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
            TextOutW(dc, X + 8, Y - 2, lab.c_str(), static_cast<int>(lab.size()));
        }

        if (i >= 1) {  // segment read-out at the midpoint
            const double dx = g.points[i].first - g.points[i - 1].first;
            const double dy = g.points[i].second - g.points[i - 1].second;
            std::wstring dl;
            if (g.pdisp.dx) { swprintf(b, 96, L"Δx=%.5g", dx); dl += b; dl += xunit; dl += L" "; }
            if (g.pdisp.dy) { swprintf(b, 96, L"Δy=%.5g ", dy); dl += b; }
            if (g.pdisp.inv_dt && !g.freq_mode) {
                const double inv = (dx != 0.0) ? 1.0 / dx : 0.0;
                swprintf(b, 96, L"1/Δt=%.5g Гц ", inv); dl += b;
            }
            if (g.pdisp.dist) {
                swprintf(b, 96, L"d=%.5g", std::sqrt(dx * dx + dy * dy)); dl += b;
            }
            if (!dl.empty()) {
                const int mxp = (mx(g.points[i].first) + mx(g.points[i - 1].first)) / 2;
                const int myp = (my(g.points[i].second) + my(g.points[i - 1].second)) / 2;
                SetTextColor(dc, g.marker_color);
                SetTextAlign(dc, TA_CENTER | TA_BOTTOM);
                TextOutW(dc, mxp, myp - 2, dl.c_str(), static_cast<int>(dl.size()));
            }
        }
    }
    SelectObject(dc, old);
    DeleteObject(pp);

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

// Render a polyline as a smooth Catmull-Rom spline through the given pixel
// points. Purely visual: it curves *between* samples but never moves them.
void draw_catmull_rom(HDC dc, const std::vector<POINT>& pts) {
    if (pts.size() < 2) return;
    if (pts.size() == 2) {
        MoveToEx(dc, pts[0].x, pts[0].y, nullptr);
        LineTo(dc, pts[1].x, pts[1].y);
        return;
    }
    const int steps = 12;
    MoveToEx(dc, pts[0].x, pts[0].y, nullptr);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const POINT& p0 = pts[i == 0 ? 0 : i - 1];
        const POINT& p1 = pts[i];
        const POINT& p2 = pts[i + 1];
        const POINT& p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
        for (int s = 1; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            const double t2 = t * t, t3 = t2 * t;
            const double x = 0.5 * (2.0 * p1.x + (-p0.x + p2.x) * t +
                                    (2.0 * p0.x - 5.0 * p1.x + 4.0 * p2.x - p3.x) * t2 +
                                    (-p0.x + 3.0 * p1.x - 3.0 * p2.x + p3.x) * t3);
            const double y = 0.5 * (2.0 * p1.y + (-p0.y + p2.y) * t +
                                    (2.0 * p0.y - 5.0 * p1.y + 4.0 * p2.y - p3.y) * t2 +
                                    (-p0.y + 3.0 * p1.y - 3.0 * p2.y + p3.y) * t3);
            LineTo(dc, static_cast<int>(x + 0.5), static_cast<int>(y + 0.5));
        }
    }
}

void draw_time(HDC dc, const RECT& p) {
    const std::vector<double>& t = g.ds.time;
    const std::size_t n = t.size();
    std::size_t lo = static_cast<std::size_t>(
        std::lower_bound(t.begin(), t.end(), g.win_start) - t.begin());
    std::size_t hi = static_cast<std::size_t>(
        std::upper_bound(t.begin(), t.end(), g.win_end) - t.begin());
    if (lo > 0) --lo;
    if (hi < n) ++hi;
    if (hi <= lo) return;

    // Vertical range: auto-fit to the visible window, or frozen when locked.
    double ymin, ymax;
    current_time_yrange(ymin, ymax);
    if (g.lock_y) { ymin = g.y_lock_min; ymax = g.y_lock_max; }

    draw_axes(dc, p, g.win_start, g.win_end, ymin, ymax, L"Время, c");

    const int pw = p.right - p.left, ph = p.bottom - p.top;
    const double xspan = g.win_end - g.win_start;
    auto mapx = [&](double tt) { return p.left + static_cast<int>((tt - g.win_start) / xspan * pw); };
    auto mapy = [&](double yy) { return p.bottom - static_cast<int>((yy - ymin) / (ymax - ymin) * ph); };

    HRGN clip = CreateRectRgn(p.left + 1, p.top + 1, p.right, p.bottom);
    SelectClipRgn(dc, clip);

    static std::vector<float> series;
    const bool sparse = (hi - lo) <= static_cast<std::size_t>(pw) * 2;

    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        build_series(g.ds.channels[c], lo, hi, series);
        HPEN pen = CreatePen(PS_SOLID, 1, channel_color(c));
        HGDIOBJ old = SelectObject(dc, pen);

        if (sparse) {
            // Collect contiguous (non-NaN) runs, then draw each as straight
            // segments or a visual spline. NaN gaps break the line.
            std::vector<POINT> run;
            auto flush = [&]() {
                if (g.visual_smooth) {
                    draw_catmull_rom(dc, run);
                } else {
                    for (std::size_t k = 0; k < run.size(); ++k) {
                        if (k == 0) MoveToEx(dc, run[k].x, run[k].y, nullptr);
                        else LineTo(dc, run[k].x, run[k].y);
                    }
                }
                run.clear();
            };
            for (std::size_t i = lo; i < hi; ++i) {
                const float v = series[i - lo];
                if (std::isnan(v)) { flush(); continue; }
                run.push_back(POINT{mapx(t[i]), mapy(v)});
            }
            flush();

            // With visual smoothing on and few points in view, mark the real
            // samples so it's clear the curve only interpolates between them.
            if (g.visual_smooth && (hi - lo) <= 400) {
                HBRUSH dot = CreateSolidBrush(channel_color(c));
                HGDIOBJ ob = SelectObject(dc, dot);
                HGDIOBJ opn = SelectObject(dc, GetStockObject(NULL_PEN));
                for (std::size_t i = lo; i < hi; ++i) {
                    const float v = series[i - lo];
                    if (std::isnan(v)) continue;
                    const int px = mapx(t[i]), py = mapy(v);
                    Ellipse(dc, px - 2, py - 2, px + 3, py + 3);
                }
                SelectObject(dc, opn);
                SelectObject(dc, ob);
                DeleteObject(dot);
            }
        } else {
            std::vector<float> cmin(pw, 1e30f), cmax(pw, -1e30f);
            for (std::size_t i = lo; i < hi; ++i) {
                const float v = series[i - lo];
                if (std::isnan(v)) continue;
                int cxp = mapx(t[i]) - p.left;
                if (cxp < 0 || cxp >= pw) continue;
                if (v < cmin[cxp]) cmin[cxp] = v;
                if (v > cmax[cxp]) cmax[cxp] = v;
            }
            int prev_x = -1, prev_y = 0;
            for (int cxp = 0; cxp < pw; ++cxp) {
                if (cmax[cxp] < -1e29f) continue;
                const int x = p.left + cxp;
                const int yhi = mapy(cmax[cxp]);
                const int ylo = mapy(cmin[cxp]);
                MoveToEx(dc, x, ylo, nullptr);
                LineTo(dc, x, yhi - 1);
                if (prev_x >= 0) { MoveToEx(dc, prev_x, prev_y, nullptr); LineTo(dc, x, (yhi + ylo) / 2); }
                prev_x = x; prev_y = (yhi + ylo) / 2;
            }
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    // Playhead.
    if (g.playhead_active && g.playhead >= g.win_start && g.playhead <= g.win_end) {
        HPEN ph_pen = CreatePen(PS_SOLID, 1, RGB(220, 30, 30));
        HGDIOBJ old = SelectObject(dc, ph_pen);
        const int X = mapx(g.playhead);
        MoveToEx(dc, X, p.top, nullptr);
        LineTo(dc, X, p.bottom);
        SelectObject(dc, old);
        DeleteObject(ph_pen);
    }

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
    draw_legend(dc, p);

    g.vx0 = g.win_start; g.vx1 = g.win_end; g.vy0 = ymin; g.vy1 = ymax;
    g.vrect = p; g.vvalid = true;
}

void draw_freq(HDC dc, const RECT& p) {
    if (!g.spec_valid) compute_spectrum();
    if (!g.spec_valid || g.spec.freqs.size() < 2) {
        draw_axes(dc, p, 0, 1, 0, 1, L"Частота, Гц");
        return;
    }
    const auto& f = g.spec.freqs;
    double f0 = g.freq_start, f1 = g.freq_end;
    if (f1 <= f0) { f0 = 0.0; f1 = g.spec.nyquist; }

    std::size_t klo = static_cast<std::size_t>(std::lower_bound(f.begin(), f.end(), f0) - f.begin());
    std::size_t khi = static_cast<std::size_t>(std::upper_bound(f.begin(), f.end(), f1) - f.begin());
    if (klo > 0) --klo;
    if (khi < f.size()) ++khi;

    double ymax = 0.0;
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        int ci = channel_index_by_name(g.spec.names[j]);
        if (ci < 0 || !g.visible[ci]) continue;
        for (std::size_t k = klo; k < khi; ++k)
            if (g.spec.amp[j][k] > ymax) ymax = g.spec.amp[j][k];
    }
    if (ymax <= 0) ymax = 1;
    const double ytop = ymax * 1.08;

    draw_axes(dc, p, f0, f1, 0, ytop, L"Частота, Гц");

    const int pw = p.right - p.left, ph = p.bottom - p.top;
    const double fspan = f1 - f0;
    auto mapx = [&](double ff) { return p.left + static_cast<int>((ff - f0) / fspan * pw); };
    auto mapy = [&](double yy) { return p.bottom - static_cast<int>(yy / ytop * ph); };

    HRGN clip = CreateRectRgn(p.left + 1, p.top + 1, p.right, p.bottom);
    SelectClipRgn(dc, clip);

    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        int ci = channel_index_by_name(g.spec.names[j]);
        if (ci < 0 || !g.visible[ci]) continue;
        HPEN pen = CreatePen(PS_SOLID, 1, channel_color(ci));
        HGDIOBJ old = SelectObject(dc, pen);
        const auto& a = g.spec.amp[j];
        bool started = false;
        int last_px = -1;
        for (std::size_t k = klo; k < khi; ++k) {
            const int px = mapx(f[k]);
            const int py = mapy(a[k]);
            if (px == last_px && started) continue;
            if (!started) { MoveToEx(dc, px, py, nullptr); started = true; }
            else LineTo(dc, px, py);
            last_px = px;
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
    draw_legend(dc, p);

    g.vx0 = f0; g.vx1 = f1; g.vy0 = 0; g.vy1 = ytop;
    g.vrect = p; g.vvalid = true;
}

void draw_chart(HDC dc, const RECT& p) {
    if (!has_data()) {
        SetTextAlign(dc, TA_CENTER | TA_BASELINE);
        SetTextColor(dc, RGB(120, 120, 120));
        const wchar_t* msg = L"Откройте файл .lvm или .txt (кнопка «Открыть файл» / клавиша O)";
        TextOutW(dc, (p.left + p.right) / 2, (p.top + p.bottom) / 2, msg, lstrlenW(msg));
        g.vvalid = false;
        return;
    }
    if (g.freq_mode) draw_freq(dc, p);
    else draw_time(dc, p);
    draw_guides(dc);
    draw_measure(dc);
}

void on_paint(HDC hdc) {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ obmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(250, 251, 253));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    // Toolbar band across the top.
    RECT topbar = {0, 0, cw, kTopBar};
    HBRUSH tbb = CreateSolidBrush(RGB(237, 241, 247));
    FillRect(mem, &topbar, tbb);
    DeleteObject(tbb);

    // Right-side channels panel.
    RECT panel = {cw - kRightPanel, kTopBar, cw, ch};
    HBRUSH pbg = CreateSolidBrush(RGB(242, 244, 248));
    FillRect(mem, &panel, pbg);
    DeleteObject(pbg);

    // Hairline separators (under the toolbar, left of the panel).
    HPEN sep = CreatePen(PS_SOLID, 1, RGB(206, 213, 224));
    HGDIOBJ oldpen = SelectObject(mem, sep);
    MoveToEx(mem, 0, kTopBar - 1, nullptr); LineTo(mem, cw, kTopBar - 1);
    MoveToEx(mem, cw - kRightPanel, kTopBar, nullptr); LineTo(mem, cw - kRightPanel, ch);
    SelectObject(mem, oldpen);
    DeleteObject(sep);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(40, 50, 65));
    SetTextAlign(mem, TA_LEFT | TA_TOP);
    SelectObject(mem, g.bold_font ? g.bold_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    TextOutW(mem, cw - kRightPanel + 12, kTopBar + 6, L"Каналы", 6);
    SelectObject(mem, g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));

    draw_chart(mem, plot_rect());

    BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
    SelectObject(mem, obmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// ---- PNG export (GDI+) ---------------------------------------------------

int png_encoder_clsid(CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(malloc(size));
    if (!info) return -1;
    Gdiplus::GetImageEncoders(num, size, info);
    int found = -1;
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(info[i].MimeType, L"image/png") == 0) { *clsid = info[i].Clsid; found = static_cast<int>(i); break; }
    free(info);
    return found;
}

bool save_png(const std::wstring& path) {
    RECT pr = plot_rect();
    int W = (pr.right - pr.left) + 90;
    int H = (pr.bottom - pr.top) + 60;
    if (W < 400) W = 1000;
    if (H < 240) H = 600;

    HDC screen = GetDC(g.main);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, W, H);
    HGDIOBJ obmp = SelectObject(mem, bmp);

    RECT all = {0, 0, W, H};
    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem, &all, bg);
    DeleteObject(bg);
    SelectObject(mem, reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    SetBkMode(mem, TRANSPARENT);
    RECT inner = {70, 14, W - 20, H - 46};
    draw_chart(mem, inner);

    SelectObject(mem, obmp);

    bool ok = false;
    {
        Gdiplus::Bitmap gb(bmp, nullptr);
        CLSID clsid;
        if (png_encoder_clsid(&clsid) >= 0)
            ok = (gb.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok);
    }

    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(g.main, screen);
    // draw_chart updated the mapping cache for the off-screen rect; restore it.
    InvalidateRect(g.main, nullptr, FALSE);
    return ok;
}

// Export the visible segment: time-domain rows (Time mode) or spectrum (Hz).
bool save_csv(const std::wstring& path) {
    std::ofstream out(to_acp(path.c_str()), std::ios::binary);
    if (!out) return false;
    if (g.freq_mode) {
        if (!g.spec_valid) return false;
        std::vector<std::size_t> cols;
        out << "Frequency";
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            int ci = channel_index_by_name(g.spec.names[j]);
            if (ci >= 0 && g.visible[ci]) { out << "," << g.spec.names[j]; cols.push_back(j); }
        }
        out << "\n";
        for (std::size_t k = 0; k < g.spec.freqs.size(); ++k) {
            const double fr = g.spec.freqs[k];
            if (fr < g.freq_start || fr > g.freq_end) continue;
            out << numfmt(fr);
            for (std::size_t j : cols) out << "," << numfmt(g.spec.amp[j][k]);
            out << "\n";
        }
    } else {
        std::vector<std::size_t> cols;
        out << "Time";
        for (std::size_t c = 0; c < g.ds.channel_count(); ++c)
            if (g.visible[c]) { out << "," << g.ds.names[c]; cols.push_back(c); }
        out << "\n";
        for (std::size_t r = 0; r < g.ds.rows(); ++r) {
            const double tt = g.ds.time[r];
            if (tt < g.win_start || tt > g.win_end) continue;
            out << numfmt(tt);
            for (std::size_t c : cols) out << "," << numfmt(g.ds.channels[c][r]);
            out << "\n";
        }
    }
    return true;
}

bool save_dialog(std::wstring& out_path, const wchar_t* filter, const wchar_t* defext,
                 const std::wstring& defname) {
    wchar_t file[2048] = L"";
    lstrcpynW(file, defname.c_str(), 2048);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 2048;
    ofn.lpstrDefExt = defext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;
    out_path = file;
    return true;
}

std::wstring file_stem() {
    std::wstring stem = g.file_name;
    std::size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos) stem = stem.substr(0, dot);
    return stem;
}

void status_msg(const std::wstring& m) { if (g.status) SetWindowTextW(g.status, m.c_str()); }

void save_png_dialog() {
    if (!has_data()) { MessageBoxW(g.main, L"Сначала откройте файл.", L"Нет данных", MB_ICONINFORMATION); return; }
    std::wstring def = file_stem() + (g.freq_mode ? L"_spectrum.png" : L"_plot.png");
    std::wstring path;
    if (!save_dialog(path, L"PNG изображение\0*.png\0Все файлы\0*.*\0", L"png", def)) return;
    if (save_png(path)) {
        const wchar_t* b = wcsrchr(path.c_str(), L'\\');
        status_msg(std::wstring(L"Сохранено (PNG): ") + (b ? b + 1 : path.c_str()));
    } else {
        MessageBoxW(g.main, L"Не удалось сохранить PNG.", L"Ошибка", MB_ICONERROR);
    }
}

void save_csv_dialog() {
    if (!has_data()) { MessageBoxW(g.main, L"Сначала откройте файл.", L"Нет данных", MB_ICONINFORMATION); return; }
    std::wstring def = file_stem() + (g.freq_mode ? L"_spectrum.csv" : L"_segment.csv");
    std::wstring path;
    if (!save_dialog(path, L"CSV файл\0*.csv\0Все файлы\0*.*\0", L"csv", def)) return;
    if (save_csv(path)) {
        const wchar_t* b = wcsrchr(path.c_str(), L'\\');
        status_msg(std::wstring(L"Сохранено (CSV): ") + (b ? b + 1 : path.c_str()));
    } else {
        MessageBoxW(g.main, L"Не удалось сохранить CSV.", L"Ошибка", MB_ICONERROR);
    }
}

// ---- hit testing ---------------------------------------------------------

bool px_to_data(int px, int py, double& dx, double& dy) {
    if (!g.vvalid) return false;
    const RECT& p = g.vrect;
    if (p.right <= p.left || p.bottom <= p.top) return false;
    dx = g.vx0 + static_cast<double>(px - p.left) / (p.right - p.left) * (g.vx1 - g.vx0);
    dy = g.vy0 + static_cast<double>(p.bottom - py) / (p.bottom - p.top) * (g.vy1 - g.vy0);
    return true;
}

// Snap a clicked coordinate to the nearest real sample (Time mode) or spectrum
// bin (Hz mode) of the closest visible channel, so a marker lands exactly on a
// data point. The stored data is never modified — only the marker is adjusted.
void snap_to_nearest(double& dx, double& dy) {
    if (g.freq_mode) {
        if (!g.spec_valid || g.spec.freqs.size() < 2) return;
        const auto& f = g.spec.freqs;
        std::size_t k = static_cast<std::size_t>(std::lower_bound(f.begin(), f.end(), dx) - f.begin());
        if (k >= f.size()) k = f.size() - 1;
        if (k > 0 && (dx - f[k - 1]) < (f[k] - dx)) --k;
        double best = std::numeric_limits<double>::max(), by = dy;
        bool any = false;
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            const int ci = channel_index_by_name(g.spec.names[j]);
            if (ci < 0 || !g.visible[ci]) continue;
            const double v = g.spec.amp[j][k];
            const double d = std::fabs(v - dy);
            if (d < best) { best = d; by = v; any = true; }
        }
        dx = f[k];
        if (any) dy = by;
        return;
    }
    if (!has_data()) return;
    const auto& t = g.ds.time;
    std::size_t i = static_cast<std::size_t>(std::lower_bound(t.begin(), t.end(), dx) - t.begin());
    if (i >= t.size()) i = t.size() - 1;
    if (i > 0 && (dx - t[i - 1]) < (t[i] - dx)) --i;
    double best = std::numeric_limits<double>::max(), by = dy;
    bool any = false;
    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        const double v = g.ds.channels[c][i];
        if (std::isnan(v)) continue;
        const double d = std::fabs(v - dy);
        if (d < best) { best = d; by = v; any = true; }
    }
    dx = t[i];
    if (any) dy = by;
}

void show_hotkeys() {
    MessageBoxW(g.main,
        L"Файлы\n"
        L"  O / Ctrl+O\t— Открыть файл\n"
        L"  S / Ctrl+S\t— Сохранить PNG\n"
        L"  E / Ctrl+E\t— Сохранить CSV\n\n"
        L"Вид\n"
        L"  M\t— Переключить Время / Гц\n"
        L"  C\t— Визуальное сглаживание (сплайн)\n"
        L"  + / ↑\t— Увеличить\n"
        L"  − / ↓\t— Уменьшить\n"
        L"  ← / →\t— Сдвиг влево / вправо\n"
        L"  Home\t— Сбросить вид\n"
        L"  Пробел\t— Воспроизведение / Пауза\n\n"
        L"Измерения и линии\n"
        L"  V\t— Режим измерения вкл/выкл\n"
        L"  Delete\t— Очистить точки измерения\n"
        L"  Esc\t— Отменить добавление линии\n\n"
        L"Мышь\n"
        L"  Колесо\t— Масштаб под курсором\n"
        L"  ЛКМ + тяга\t— Панорамирование\n"
        L"  ЛКМ\t— Поставить точку / линию (в режиме)\n"
        L"  ПКМ\t— Очистить точки измерения\n\n"
        L"  F1\t— Эта справка",
        L"Горячие клавиши — LVM Viewer", MB_OK | MB_ICONINFORMATION);
}

void show_about() {
    MessageBoxW(g.main,
        L"LVM Viewer — просмотрщик сигналов LabVIEW (.lvm / .txt)\n\n"
        L"Нативное приложение Win32 + GDI/GDI+, без внешних\n"
        L"зависимостей и без Qt. Время и спектр (БПФ), измерения\n"
        L"с примагничиванием, направляющие линии, визуальное\n"
        L"сглаживание, экспорт PNG/CSV.\n\n"
        L"Сборка: build_gui.ps1 (MinGW g++) или make gui.",
        L"О программе — LVM Viewer", MB_OK | MB_ICONINFORMATION);
}

// Refresh every checkable menu item from the current app state. Cheap, so we
// just call it whenever a toggle changes (menu, toolbar, or accelerator).
void sync_menu() {
    if (!g.menu) return;
    auto chk = [&](UINT id, bool on) {
        CheckMenuItem(g.menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };
    chk(IDM_VISMOOTH, g.visual_smooth);
    chk(IDC_MEASURE, g.measure_mode);
    chk(IDC_LOCKY, g.lock_y);
}

HMENU make_menu() {
    HMENU bar = CreateMenu();

    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDC_OPEN, L"Открыть файл…\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDC_SAVEPNG, L"Сохранить PNG…\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDC_SAVECSV, L"Сохранить CSV…\tCtrl+E");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"Выход\tAlt+F4");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Файл");

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, IDC_MODE, L"Время / Гц\tM");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDC_ZOOMIN, L"Увеличить\t+");
    AppendMenuW(view, MF_STRING, IDC_ZOOMOUT, L"Уменьшить\t−");
    AppendMenuW(view, MF_STRING, IDC_RESET, L"Сбросить вид\tHome");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDC_LOCKY, L"Зафиксировать масштаб Y");
    AppendMenuW(view, MF_STRING, IDM_VISMOOTH, L"Визуальное сглаживание (сплайн)\tC");
    AppendMenuW(view, MF_STRING, IDC_PLAY, L"Воспроизведение / Пауза\tПробел");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"Вид");

    HMENU meas = CreatePopupMenu();
    AppendMenuW(meas, MF_STRING, IDC_MEASURE, L"Режим измерения\tV");
    AppendMenuW(meas, MF_STRING, IDC_PTSETTINGS, L"Настройки точек…");
    AppendMenuW(meas, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(meas, MF_STRING, IDM_CLEAR_POINTS, L"Очистить точки\tDelete");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(meas), L"Измерения");

    HMENU lines = CreatePopupMenu();
    AppendMenuW(lines, MF_STRING, IDM_ADD_VLINE, L"Добавить вертикальную линию");
    AppendMenuW(lines, MF_STRING, IDM_ADD_HLINE, L"Добавить горизонтальную линию");
    AppendMenuW(lines, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(lines, MF_STRING, IDM_CLEAR_LINES, L"Очистить линии");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(lines), L"Линии");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_HOTKEYS, L"Горячие клавиши…\tF1");
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"О программе…");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Справка");

    return bar;
}

// ---- measurement-point settings panel (modeless) -------------------------

COLORREF g_custom_colors[16] = {0};

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            struct Item { const wchar_t* text; int id; bool on; };
            const Item items[] = {
                {L"Показывать номер точки",        IDM_PT_NUM,   g.pdisp.number},
                {L"Показывать координату X",       IDM_PT_X,     g.pdisp.x},
                {L"Показывать координату Y",       IDM_PT_Y,     g.pdisp.y},
                {L"Расстояние между точками по X (Δx)", IDM_PT_DX, g.pdisp.dx},
                {L"Расстояние между точками по Y (Δy)", IDM_PT_DY, g.pdisp.dy},
                {L"Частота 1/Δt",                  IDM_PT_INVDT, g.pdisp.inv_dt},
                {L"Расстояние d (по прямой)",      IDM_PT_DIST,  g.pdisp.dist},
                {L"Примагничивать маркеры к графику", IDM_SNAP,  g.snap_to_data},
            };
            int y = 14;
            for (const auto& it : items) {
                HWND c = CreateWindowExW(0, L"BUTTON", it.text,
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, y, 300, 22, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(it.id)), inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(c, BM_SETCHECK, it.on ? BST_CHECKED : BST_UNCHECKED, 0);
                y += 26;
            }
            y += 8;
            HWND col = CreateWindowExW(0, L"BUTTON", L"Цвет маркеров…",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 16, y, 160, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_COLOR)), inst, nullptr);
            SendMessageW(col, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            auto checked = [&]() { return SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED; };
            switch (id) {
                case IDM_PT_NUM:   g.pdisp.number = checked(); break;
                case IDM_PT_X:     g.pdisp.x = checked(); break;
                case IDM_PT_Y:     g.pdisp.y = checked(); break;
                case IDM_PT_DX:    g.pdisp.dx = checked(); break;
                case IDM_PT_DY:    g.pdisp.dy = checked(); break;
                case IDM_PT_INVDT: g.pdisp.inv_dt = checked(); break;
                case IDM_PT_DIST:  g.pdisp.dist = checked(); break;
                case IDM_SNAP:     g.snap_to_data = checked(); break;
                case IDS_COLOR: {
                    CHOOSECOLORW cc = {};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.lpCustColors = g_custom_colors;
                    cc.rgbResult = g.marker_color;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) g.marker_color = cc.rgbResult;
                    break;
                }
                default: return 0;
            }
            InvalidateRect(g.main, nullptr, FALSE);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);   // keep state; reopen instantly
            return 0;
        case WM_DESTROY:
            g.settings_wnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void open_settings() {
    if (!g.settings_wnd) {
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        g.settings_wnd = CreateWindowExW(
            WS_EX_TOOLWINDOW, L"LvmPtSettings", L"Настройки точек измерения",
            WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 350, 340,
            g.main, nullptr, inst, nullptr);
        if (!g.settings_wnd) return;
        RECT mr, sr;
        GetWindowRect(g.main, &mr);
        GetWindowRect(g.settings_wnd, &sr);
        const int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
        SetWindowPos(g.settings_wnd, nullptr,
                     mr.left + ((mr.right - mr.left) - sw) / 2,
                     mr.top + ((mr.bottom - mr.top) - sh) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(g.settings_wnd, SW_SHOW);
    SetForegroundWindow(g.settings_wnd);
}

// ---- welcome / start screen ----------------------------------------------

LRESULT CALLBACK WelcomeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HWND title = CreateWindowExW(0, L"STATIC", L"LVM Viewer",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 18, 520, 40, hwnd, nullptr, inst, nullptr);
            SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(g.title_font ? g.title_font : font), TRUE);
            HWND sub = CreateWindowExW(0, L"STATIC",
                L"Просмотрщик сигналов LabVIEW (.lvm / .txt)",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 58, 520, 22, hwnd, nullptr, inst, nullptr);
            SendMessageW(sub, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            HWND body = CreateWindowExW(0, L"STATIC",
                L"Как работать с приложением:\r\n"
                L"   •  «Открыть файл» (O) — загрузите .lvm или .txt.\r\n"
                L"   •  «Время / Гц» (M) — график сигнала или его спектр (БПФ).\r\n"
                L"   •  «Измерение» (V) — кликайте точки на графике. Что показывать\r\n"
                L"       у точек и примагничивание — в окне «Настройки точек».\r\n"
                L"   •  Колесо мыши — масштаб, тяга ЛКМ — прокрутка по времени.\r\n"
                L"   •  «Фикс. Y» — зафиксировать масштаб по высоте.\r\n"
                L"   •  Пробел — воспроизведение в реальном времени (1 с = 1 с).\r\n"
                L"   •  F1 — полный список горячих клавиш.",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 24, 88, 540, 200, hwnd, nullptr, inst, nullptr);
            SendMessageW(body, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            auto mkbtn = [&](const wchar_t* t, int id, int x, int w) {
                HWND b = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         x, 300, w, 32, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            };
            mkbtn(L"Открыть файл", IDC_OPEN, 24, 120);
            mkbtn(L"Настройки точек…", IDC_PTSETTINGS, 150, 160);
            mkbtn(L"Горячие клавиши", IDM_HOTKEYS, 318, 150);
            mkbtn(L"Начать работу", IDW_START, 476, 90);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_OPEN: DestroyWindow(hwnd); SendMessageW(g.main, WM_COMMAND, IDC_OPEN, 0); return 0;
                case IDC_PTSETTINGS: open_settings(); return 0;
                case IDM_HOTKEYS: show_hotkeys(); return 0;
                case IDW_START: DestroyWindow(hwnd); return 0;
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(30, 40, 55));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        case WM_DESTROY:
            g.welcome_wnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void show_welcome(HINSTANCE inst) {
    g.welcome_wnd = CreateWindowExW(0, L"LvmWelcome", L"LVM Viewer — добро пожаловать",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 590, 392,
        g.main, nullptr, inst, nullptr);
    if (!g.welcome_wnd) return;
    RECT sr;
    GetWindowRect(g.welcome_wnd, &sr);
    const int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
    SetWindowPos(g.welcome_wnd, HWND_TOP,
                 (GetSystemMetrics(SM_CXSCREEN) - sw) / 2,
                 (GetSystemMetrics(SM_CYSCREEN) - sh) / 2, 0, 0, SWP_NOSIZE);
    ShowWindow(g.welcome_wnd, SW_SHOW);
    UpdateWindow(g.welcome_wnd);
}

// ---- window procedure ----------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;

            // Modern UI font (falls back to the stock font if unavailable).
            g.ui_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g.bold_font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g.title_font = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            if (!g.ui_font) g.ui_font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT font = g.ui_font;

            g.menu = make_menu();
            SetMenu(hwnd, g.menu);

            auto mk = [&](const wchar_t* text, int id, DWORD extra) {
                HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | extra,
                                         0, 0, 10, 10, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return b;
            };
            g.open = mk(L"Открыть файл", IDC_OPEN, BS_PUSHBUTTON);
            g.savepng = mk(L"Сохранить PNG", IDC_SAVEPNG, BS_PUSHBUTTON);
            g.savecsv = mk(L"Сохранить CSV", IDC_SAVECSV, BS_PUSHBUTTON);
            g.mode = mk(L"Время / Гц", IDC_MODE, BS_PUSHBUTTON);
            g.play = mk(L"▶ Воспроизв.", IDC_PLAY, BS_PUSHBUTTON);
            // Owner-toggled (not BS_AUTO) so menu / accelerator and the button
            // stay in sync through a single code path.
            // Owner-toggled (not BS_AUTO) checkboxes so menu / accelerator and
            // the button stay in sync through a single code path.
            g.measure = mk(L"Измерение", IDC_MEASURE, BS_CHECKBOX | BS_PUSHLIKE);
            g.reset = mk(L"Сбросить вид", IDC_RESET, BS_PUSHBUTTON);
            g.locky = mk(L"Фикс. масштаб Y", IDC_LOCKY, BS_CHECKBOX | BS_PUSHLIKE);
            g.ptsettings = mk(L"Настройки точек…", IDC_PTSETTINGS, BS_PUSHBUTTON);

            g.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                       0, 0, 10, 10, hwnd, nullptr, inst, nullptr);
            SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            sync_menu();
            set_status();
            return 0;
        }
        case WM_SIZE:
            layout();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* m = reinterpret_cast<MINMAXINFO*>(lp);
            m->ptMinTrackSize.x = 820;
            m->ptMinTrackSize.y = 520;
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            on_paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (g.playing && !g.freq_mode && has_data()) {
                // Real-time playhead: 1 s of signal per 1 s of wall-clock time.
                LARGE_INTEGER now, freq;
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&freq);
                const double elapsed =
                    static_cast<double>(now.QuadPart - g.play_anchor_qpc.QuadPart) /
                    static_cast<double>(freq.QuadPart);
                g.playhead = g.play_anchor_data + elapsed;
                if (g.playhead >= g.data_t1) {
                    g.playhead = g.data_t1;
                    stop_play();
                } else {
                    // Once the playhead passes the middle, keep it centred by
                    // scrolling a little each frame — smooth, no big jumps.
                    const double span = g.win_end - g.win_start;
                    if (g.playhead > g.win_start + span * 0.5) {
                        g.win_start = g.playhead - span * 0.5;
                        g.win_end = g.win_start + span;
                        if (g.win_end > g.data_t1) { g.win_end = g.data_t1; g.win_start = g.win_end - span; }
                        if (g.win_start < g.data_t0) { g.win_start = g.data_t0; g.win_end = g.win_start + span; }
                    }
                }
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            switch (id) {
                case IDC_OPEN: open_file(); return 0;
                case IDC_SAVEPNG: save_png_dialog(); return 0;
                case IDC_SAVECSV: save_csv_dialog(); return 0;
                case IDM_EXIT: DestroyWindow(hwnd); return 0;
                case IDC_MODE:
                    g.freq_mode = !g.freq_mode;
                    if (g.freq_mode) stop_play();
                    if (g.freq_mode && !g.spec_valid) compute_spectrum();
                    set_status();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDC_PLAY: toggle_play(); return 0;
                case IDC_MEASURE:
                    g.measure_mode = !g.measure_mode;
                    if (g.measure_mode) g.pending_line = 0;
                    SendMessageW(g.measure, BM_SETCHECK,
                                 g.measure_mode ? BST_CHECKED : BST_UNCHECKED, 0);
                    sync_menu();
                    set_status();
                    return 0;
                case IDC_PTSETTINGS: open_settings(); return 0;
                case IDC_LOCKY:
                    g.lock_y = !g.lock_y;
                    if (g.lock_y) current_time_yrange(g.y_lock_min, g.y_lock_max);
                    SendMessageW(g.locky, BM_SETCHECK,
                                 g.lock_y ? BST_CHECKED : BST_UNCHECKED, 0);
                    sync_menu();
                    set_status();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDM_VISMOOTH:
                    g.visual_smooth = !g.visual_smooth;
                    sync_menu();
                    set_status();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDM_ADD_VLINE:
                    if (!has_data()) { MessageBoxW(hwnd, L"Сначала откройте файл.", L"Нет данных", MB_ICONINFORMATION); return 0; }
                    g.pending_line = 1;
                    status_msg(L"Кликните на графике, чтобы поставить вертикальную линию (Esc — отмена).");
                    return 0;
                case IDM_ADD_HLINE:
                    if (!has_data()) { MessageBoxW(hwnd, L"Сначала откройте файл.", L"Нет данных", MB_ICONINFORMATION); return 0; }
                    g.pending_line = 2;
                    status_msg(L"Кликните на графике, чтобы поставить горизонтальную линию (Esc — отмена).");
                    return 0;
                case IDM_CLEAR_LINES:
                    if (!g.guides.empty()) { g.guides.clear(); InvalidateRect(hwnd, nullptr, FALSE); }
                    set_status();
                    return 0;
                case IDM_CLEAR_POINTS:
                    if (!g.points.empty()) { g.points.clear(); InvalidateRect(hwnd, nullptr, FALSE); }
                    set_status();
                    return 0;
                case IDC_ZOOMIN: zoom_at(0.5, 0.7); return 0;
                case IDC_ZOOMOUT: zoom_at(0.5, 1.0 / 0.7); return 0;
                case IDC_RESET: reset_view(); return 0;
                case IDC_PANLEFT: pan_by(-0.2); return 0;
                case IDC_PANRIGHT: pan_by(0.2); return 0;
                case IDM_HOTKEYS: show_hotkeys(); return 0;
                case IDM_ABOUT: show_about(); return 0;
                default: break;
            }
            if (id >= IDC_CHAN_BASE && id < IDC_CHAN_BASE + static_cast<int>(g.visible.size())) {
                const int ci = id - IDC_CHAN_BASE;
                g.visible[ci] = (SendMessageW(g.checks[ci], BM_GETCHECK, 0, 0) == BST_CHECKED);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!has_data()) return 0;
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            const RECT p = plot_rect();
            double frac = 0.5;
            if (pt.x >= p.left && pt.x <= p.right)
                frac = static_cast<double>(pt.x - p.left) / (p.right - p.left);
            zoom_at(frac, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.8 : 1.25);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!has_data()) return 0;
            const RECT p = plot_rect();
            const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            if (mx < p.left || mx > p.right || my < p.top || my > p.bottom) return 0;
            if (g.pending_line) {
                double dx, dy;
                if (px_to_data(mx, my, dx, dy)) {
                    GuideLine gl;
                    gl.vertical = (g.pending_line == 1);
                    gl.freq = g.freq_mode;
                    if (gl.vertical && g.snap_to_data) { double sx = dx, sy = dy; snap_to_nearest(sx, sy); dx = sx; }
                    gl.value = gl.vertical ? dx : dy;
                    g.guides.push_back(gl);
                }
                g.pending_line = 0;
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g.measure_mode) {
                double dx, dy;
                if (px_to_data(mx, my, dx, dy)) {
                    if (g.snap_to_data) snap_to_nearest(dx, dy);
                    g.points.push_back({dx, dy});
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            double *lo, *hi, minb, maxb, minw;
            if (!active_axis(lo, hi, minb, maxb, minw)) return 0;
            g.dragging = true;
            g.drag_x = mx;
            g.drag_lo = *lo;
            g.drag_hi = *hi;
            SetCapture(hwnd);
            return 0;
        }
        case WM_RBUTTONDOWN:
            if (!g.points.empty()) { g.points.clear(); set_status(); InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        case WM_MOUSEMOVE: {
            if (!g.dragging) return 0;
            double *lo, *hi, minb, maxb, minw;
            if (!active_axis(lo, hi, minb, maxb, minw)) return 0;
            const RECT p = plot_rect();
            const int pw = p.right - p.left;
            const double span = g.drag_hi - g.drag_lo;
            const double d = static_cast<double>(GET_X_LPARAM(lp) - g.drag_x) / pw * span;
            *lo = g.drag_lo - d;
            *hi = g.drag_hi - d;
            clamp_range(*lo, *hi, minb, maxb, minw);
            set_status();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
            if (g.dragging) { g.dragging = false; ReleaseCapture(); }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE && g.pending_line) {
                g.pending_line = 0;
                set_status();
                return 0;
            }
            break;
        case WM_DESTROY:
            stop_play();
            if (g.ui_font && g.ui_font != reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
                DeleteObject(g.ui_font);
            if (g.bold_font) DeleteObject(g.bold_font);
            if (g.title_font) DeleteObject(g.title_font);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HACCEL make_accelerators() {
    ACCEL acc[] = {
        {FVIRTKEY, 'O', IDC_OPEN},
        {FVIRTKEY | FCONTROL, 'O', IDC_OPEN},
        {FVIRTKEY, 'S', IDC_SAVEPNG},
        {FVIRTKEY | FCONTROL, 'S', IDC_SAVEPNG},
        {FVIRTKEY, 'E', IDC_SAVECSV},
        {FVIRTKEY | FCONTROL, 'E', IDC_SAVECSV},
        {FVIRTKEY, 'M', IDC_MODE},
        {FVIRTKEY, 'V', IDC_MEASURE},
        {FVIRTKEY, VK_SPACE, IDC_PLAY},
        {FVIRTKEY, VK_OEM_PLUS, IDC_ZOOMIN},
        {FVIRTKEY, VK_ADD, IDC_ZOOMIN},
        {FVIRTKEY, VK_UP, IDC_ZOOMIN},
        {FVIRTKEY, VK_OEM_MINUS, IDC_ZOOMOUT},
        {FVIRTKEY, VK_SUBTRACT, IDC_ZOOMOUT},
        {FVIRTKEY, VK_DOWN, IDC_ZOOMOUT},
        {FVIRTKEY, VK_LEFT, IDC_PANLEFT},
        {FVIRTKEY, VK_RIGHT, IDC_PANRIGHT},
        {FVIRTKEY, VK_HOME, IDC_RESET},
        {FVIRTKEY, 'C', IDM_VISMOOTH},
        {FVIRTKEY, VK_DELETE, IDM_CLEAR_POINTS},
        {FVIRTKEY, VK_F1, IDM_HOTKEYS},
    };
    return CreateAcceleratorTableW(acc, static_cast<int>(sizeof(acc) / sizeof(acc[0])));
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show) {
    Gdiplus::GdiplusStartupInput gdi_in;
    Gdiplus::GdiplusStartup(&g_gdiplus_token, &gdi_in, nullptr);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"LvmViewerWnd";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    // Settings panel + welcome screen window classes.
    WNDCLASSEXW sc = {};
    sc.cbSize = sizeof(sc);
    sc.lpfnWndProc = SettingsProc;
    sc.hInstance = inst;
    sc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    sc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    sc.lpszClassName = L"LvmPtSettings";
    RegisterClassExW(&sc);

    WNDCLASSEXW wcw = {};
    wcw.cbSize = sizeof(wcw);
    wcw.lpfnWndProc = WelcomeProc;
    wcw.hInstance = inst;
    wcw.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcw.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wcw.lpszClassName = L"LvmWelcome";
    wcw.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wcw);

    g.main = CreateWindowExW(0, wc.lpszClassName, L"LVM Viewer",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720,
                             nullptr, nullptr, inst, nullptr);
    if (!g.main) return 1;

    ShowWindow(g.main, show);
    UpdateWindow(g.main);

    if (cmd && *cmd) {
        std::wstring path = cmd;
        if (!path.empty() && path.front() == L'"') path = path.substr(1, path.find_last_of(L'"') - 1);
        if (!load_path(path))
            MessageBoxW(g.main, to_w(g.last_error).c_str(), L"Ошибка чтения", MB_ICONERROR | MB_OK);
    } else {
        show_welcome(inst);   // start screen when launched without a file
    }

    HACCEL accel = make_accelerators();
    MSG m;
    while (GetMessage(&m, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(g.main, accel, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    if (accel) DestroyAcceleratorTable(accel);
    Gdiplus::GdiplusShutdown(g_gdiplus_token);
    return 0;
}
