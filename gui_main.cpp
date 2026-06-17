// LVM Viewer — native Win32 GUI front end.
//
// Self-contained desktop viewer (no external GUI toolkit). Features:
//   - Time plot and FFT (Hz) spectrum, with zoom/pan on both axes.
//   - Channel show/hide, colored legend, min/max envelope for dense data.
//   - Measure tool: click points, read distance (Δx/Δy, 1/Δt) between them.
//   - Playback / pause that sweeps a playhead through the time signal.
//   - Export the visible segment to PNG (GDI+) or CSV.
//   - Smoothing factor (moving average) for the time plot.
//   - Keyboard shortcuts.
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
    IDC_SMOOTH,
    IDC_CHAN_BASE = 2000,
};

const int kTopBar = 80;       // two toolbar rows
const int kRightPanel = 180;
const int kBottomBar = 28;
const int kAxisLeft = 70;
const int kSmoothMax = 80;    // max moving-average window (samples)

const COLORREF kPalette[] = {
    RGB(31, 119, 180), RGB(255, 127, 14), RGB(44, 160, 44), RGB(214, 39, 40),
    RGB(148, 103, 189), RGB(140, 86, 75), RGB(227, 119, 194), RGB(127, 127, 127),
    RGB(188, 189, 34), RGB(23, 190, 207),
};
COLORREF channel_color(std::size_t i) { return kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))]; }

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

    int smooth = 0;            // moving-average window (0 = off)

    bool measure_mode = false;
    std::vector<std::pair<double, double>> points;  // measurement points (data coords)

    bool playing = false;
    bool playhead_active = false;
    double playhead = 0.0;

    // Mapping cache from the last paint (data <-> pixels) for hit-testing.
    double vx0 = 0, vx1 = 1, vy0 = 0, vy1 = 1;
    RECT vrect = {0, 0, 1, 1};
    bool vvalid = false;

    std::wstring file_name;
    std::string last_error;

    HWND main = nullptr;
    HWND open = nullptr, savepng = nullptr, savecsv = nullptr, mode = nullptr;
    HWND play = nullptr, measure = nullptr;
    HWND zin = nullptr, zout = nullptr, reset = nullptr;
    HWND smoothlbl = nullptr, smoothbar = nullptr;
    HWND status = nullptr;
    std::vector<HWND> checks;

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
                 L"Время  |  Каналов: %zu  |  Точек: %zu  |  Окно: %.5g .. %.5g c  |  Сглаж.: %d",
                 g.ds.channel_count(), g.ds.rows(), g.win_start, g.win_end, g.smooth);
        s = buf;
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
    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
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
    place(g.zin, 100, 42);
    place(g.zout, 100, 42);
    place(g.reset, 120, 42);
    MoveWindow(g.smoothlbl, x, 46, 130, 22, TRUE);
    x += 130 + 4;
    MoveWindow(g.smoothbar, x, 44, 200, 26, TRUE);

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
    p.bottom = rc.bottom - kBottomBar - 6;
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
    SetTimer(g.main, 1, 30, nullptr);
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

// ---- smoothing -----------------------------------------------------------

// Fill `out` with values for indices [lo,hi). With w>1, apply a centered
// (zero-phase) moving average over a window of w samples, skipping NaNs.
void build_series(const std::vector<double>& col, std::size_t lo, std::size_t hi, int w,
                  std::vector<float>& out) {
    const std::size_t count = hi - lo;
    out.resize(count);
    const long n = static_cast<long>(col.size());
    if (w <= 1) {
        for (std::size_t i = 0; i < count; ++i) out[i] = static_cast<float>(col[lo + i]);
        return;
    }
    const int half = w / 2;
    long a = static_cast<long>(lo) - half;
    long b = static_cast<long>(lo) + half;
    double sum = 0.0;
    long cnt = 0;
    for (long k = a; k <= b; ++k)
        if (k >= 0 && k < n) { const double v = col[k]; if (!std::isnan(v)) { sum += v; ++cnt; } }
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = cnt > 0 ? static_cast<float>(sum / cnt)
                         : std::numeric_limits<float>::quiet_NaN();
        if (a >= 0 && a < n) { const double v = col[a]; if (!std::isnan(v)) { sum -= v; --cnt; } }
        ++a; ++b;
        if (b >= 0 && b < n) { const double v = col[b]; if (!std::isnan(v)) { sum += v; ++cnt; } }
    }
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

// Draw measurement points / segments using the cached mapping.
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

    HRGN clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
    SelectClipRgn(dc, clip);

    HPEN seg = CreatePen(PS_DASH, 1, RGB(190, 0, 0));
    HGDIOBJ old = SelectObject(dc, seg);
    for (std::size_t i = 1; i < g.points.size(); ++i) {
        MoveToEx(dc, mx(g.points[i - 1].first), my(g.points[i - 1].second), nullptr);
        LineTo(dc, mx(g.points[i].first), my(g.points[i].second));
    }
    SelectObject(dc, old);
    DeleteObject(seg);

    HPEN pp = CreatePen(PS_SOLID, 2, RGB(200, 0, 0));
    old = SelectObject(dc, pp);
    for (std::size_t i = 0; i < g.points.size(); ++i) {
        const int X = mx(g.points[i].first), Y = my(g.points[i].second);
        MoveToEx(dc, X - 6, Y, nullptr); LineTo(dc, X + 7, Y);
        MoveToEx(dc, X, Y - 6, nullptr); LineTo(dc, X, Y + 7);
        SetTextColor(dc, RGB(160, 0, 0));
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        wchar_t b[16];
        swprintf(b, 16, L"%zu", i + 1);
        TextOutW(dc, X + 6, Y + 4, b, lstrlenW(b));
        if (i >= 1) {  // segment delta label at midpoint
            const double dx = g.points[i].first - g.points[i - 1].first;
            const int mxp = (mx(g.points[i].first) + mx(g.points[i - 1].first)) / 2;
            const int myp = (my(g.points[i].second) + my(g.points[i - 1].second)) / 2;
            wchar_t lab[48];
            swprintf(lab, 48, g.freq_mode ? L"Δ=%.4g Гц" : L"Δ=%.4g c", dx);
            SetTextAlign(dc, TA_CENTER | TA_BOTTOM);
            TextOutW(dc, mxp, myp - 2, lab, lstrlenW(lab));
        }
    }
    SelectObject(dc, old);
    DeleteObject(pp);

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
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

    double ymin = 1e300, ymax = -1e300;
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
        build_series(g.ds.channels[c], lo, hi, g.smooth, series);
        HPEN pen = CreatePen(PS_SOLID, 1, channel_color(c));
        HGDIOBJ old = SelectObject(dc, pen);

        if (sparse) {
            bool started = false;
            for (std::size_t i = lo; i < hi; ++i) {
                const float v = series[i - lo];
                if (std::isnan(v)) { started = false; continue; }
                const int px = mapx(t[i]), py = mapy(v);
                if (!started) { MoveToEx(dc, px, py, nullptr); started = true; }
                else LineTo(dc, px, py);
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
    draw_measure(dc);
}

void on_paint(HDC hdc) {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ obmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    RECT panel = {cw - kRightPanel, kTopBar, cw, ch};
    HBRUSH pbg = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(mem, &panel, pbg);
    DeleteObject(pbg);

    SelectObject(mem, reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(60, 60, 60));
    SetTextAlign(mem, TA_LEFT | TA_TOP);
    TextOutW(mem, cw - kRightPanel + 12, kTopBar + 6, L"Каналы:", 7);

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

// ---- window procedure ----------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
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
            g.measure = mk(L"Измерение", IDC_MEASURE, BS_AUTOCHECKBOX | BS_PUSHLIKE);
            g.zin = mk(L"Увеличить +", IDC_ZOOMIN, BS_PUSHBUTTON);
            g.zout = mk(L"Уменьшить −", IDC_ZOOMOUT, BS_PUSHBUTTON);
            g.reset = mk(L"Сбросить вид", IDC_RESET, BS_PUSHBUTTON);

            g.smoothlbl = CreateWindowExW(0, L"STATIC", L"Сглаживание: 0", WS_CHILD | WS_VISIBLE,
                                          0, 0, 10, 10, hwnd, nullptr, inst, nullptr);
            SendMessageW(g.smoothlbl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            g.smoothbar = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                          WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                          0, 0, 10, 10, hwnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SMOOTH)), inst, nullptr);
            SendMessageW(g.smoothbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, kSmoothMax));
            SendMessageW(g.smoothbar, TBM_SETPOS, TRUE, 0);

            g.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                       0, 0, 10, 10, hwnd, nullptr, inst, nullptr);
            SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lp) == g.smoothbar) {
                g.smooth = static_cast<int>(SendMessageW(g.smoothbar, TBM_GETPOS, 0, 0));
                wchar_t b[48];
                swprintf(b, 48, L"Сглаживание: %d", g.smooth);
                SetWindowTextW(g.smoothlbl, b);
                set_status();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_TIMER:
            if (g.playing && !g.freq_mode && has_data()) {
                const double span = g.win_end - g.win_start;
                g.playhead += span * 0.012;
                if (g.playhead >= g.data_t1) { g.playhead = g.data_t1; stop_play(); }
                else if (g.playhead > g.win_end - span * 0.12) {
                    const double shift = span * 0.5;
                    g.win_start += shift; g.win_end += shift;
                    const double w = g.win_end - g.win_start;
                    if (g.win_end > g.data_t1) { g.win_end = g.data_t1; g.win_start = g.win_end - w; if (g.win_start < g.data_t0) g.win_start = g.data_t0; }
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
                case IDC_MODE:
                    g.freq_mode = !g.freq_mode;
                    if (g.freq_mode) stop_play();
                    if (g.freq_mode && !g.spec_valid) compute_spectrum();
                    set_status();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDC_PLAY: toggle_play(); return 0;
                case IDC_MEASURE:
                    g.measure_mode = (SendMessageW(g.measure, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    return 0;
                case IDC_ZOOMIN: zoom_at(0.5, 0.7); return 0;
                case IDC_ZOOMOUT: zoom_at(0.5, 1.0 / 0.7); return 0;
                case IDC_RESET: reset_view(); return 0;
                case IDC_PANLEFT: pan_by(-0.2); return 0;
                case IDC_PANRIGHT: pan_by(0.2); return 0;
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
            if (g.measure_mode) {
                double dx, dy;
                if (px_to_data(mx, my, dx, dy)) { g.points.push_back({dx, dy}); set_status(); InvalidateRect(hwnd, nullptr, FALSE); }
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
        case WM_DESTROY:
            stop_play();
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
