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
    IDC_AUTOY,          // toolbar: auto-fit vertical scale (was lock_y)
    IDC_PTSETTINGS,     // toolbar: open the measurement-point settings panel

    // Menu-only commands (no toolbar button).
    IDM_EXIT = 1100,
    IDM_VISMOOTH,       // visual (spline) smoothing toggle
    IDM_SNAP,           // snap measurement markers to data
    IDM_ADD_VLINE,      // arm: place a vertical guide line
    IDM_ADD_HLINE,      // arm: place a horizontal guide line
    IDM_ADD_MARKER,     // arm: place a marker
    IDM_CLEAR_LINES,
    IDM_CLEAR_MARKERS,
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

    // Playback speed menu items.
    IDM_SPEED_00001 = 1300,
    IDM_SPEED_0001,
    IDM_SPEED_001,
    IDM_SPEED_01,
    IDM_SPEED_05,
    IDM_SPEED_1,
    IDM_SPEED_2,
    IDM_SPEED_5,
    IDM_SPEED_10,

    IDM_UNDO = 1400,
    IDM_REDO,
    IDM_THEME = 1500,
    IDM_LANG_RU = 1600,
    IDM_LANG_EN = 1601,

    IDC_CHAN_BASE = 2000,
};

const int kTopBar = 72;        // two-row compact toolbar
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

struct Theme {
    COLORREF bg_main, bg_toolbar, bg_panel, bg_plot, bg_status;
    COLORREF grid, minor_grid, frame, axis_text;
    COLORREF text_primary, text_secondary;
    COLORREF accent, accent_hover, separator;
    COLORREF btn_bg, btn_border, btn_hover, btn_active;
    COLORREF btn_pressed; // "pressed" look for active toggle buttons
    COLORREF playhead;
    COLORREF marker_color;
};

static const Theme kLightTheme = {
    RGB(250, 252, 255),   // bg_main
    RGB(235, 240, 246),   // bg_toolbar
    RGB(242, 245, 250),   // bg_panel
    RGB(255, 255, 255),   // bg_plot
    RGB(245, 247, 250),   // bg_status
    RGB(230, 235, 240),   // grid
    RGB(240, 243, 246),   // minor_grid
    RGB(180, 190, 200),   // frame
    RGB(80, 90, 100),     // axis_text
    RGB(30, 40, 50),      // text_primary
    RGB(100, 110, 120),   // text_secondary
    RGB(0, 120, 212),     // accent
    RGB(0, 100, 180),     // accent_hover
    RGB(210, 218, 228),   // separator
    RGB(250, 251, 253),   // btn_bg
    RGB(200, 208, 218),   // btn_border
    RGB(230, 235, 242),   // btn_hover
    RGB(0, 120, 212),     // btn_active
    RGB(170, 175, 180),   // btn_pressed
    RGB(220, 40, 40),     // playhead
    RGB(200, 0, 0),       // marker_color
};

static const Theme kDarkTheme = {
    RGB(30, 32, 38),      // bg_main
    RGB(45, 48, 55),      // bg_toolbar
    RGB(38, 40, 48),      // bg_panel
    RGB(25, 28, 35),      // bg_plot
    RGB(35, 38, 45),      // bg_status
    RGB(55, 60, 70),      // grid
    RGB(40, 44, 52),      // minor_grid
    RGB(80, 85, 95),      // frame
    RGB(180, 185, 190),   // axis_text
    RGB(220, 225, 230),   // text_primary
    RGB(140, 145, 150),   // text_secondary
    RGB(0, 150, 255),     // accent
    RGB(30, 130, 235),    // accent_hover
    RGB(60, 65, 75),      // separator
    RGB(55, 58, 65),      // btn_bg
    RGB(75, 80, 90),      // btn_border
    RGB(70, 75, 85),      // btn_hover
    RGB(0, 150, 255),     // btn_active
    RGB(20, 25, 35),      // btn_pressed
    RGB(255, 60, 60),     // playhead
    RGB(255, 80, 80),     // marker_color
};

const Theme* g_theme = &kLightTheme;
HBRUSH g_panel_brush = nullptr;
HBRUSH g_welcome_brush = nullptr;

void update_theme_brushes() {
    if (g_panel_brush) DeleteObject(g_panel_brush);
    g_panel_brush = CreateSolidBrush(g_theme->bg_panel);
    if (g_welcome_brush) DeleteObject(g_welcome_brush);
    g_welcome_brush = CreateSolidBrush(g_theme->bg_main);
}

// ---- string table --------------------------------------------------------
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
    const wchar_t* fmt_pt_x; const wchar_t* fmt_pt_dx; const wchar_t* fmt_pt_dy; const wchar_t* fmt_pt_invdt; const wchar_t* fmt_pt_dist;
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
    const wchar_t* filter_open;
    const wchar_t* filter_png;
    const wchar_t* filter_csv;
    const wchar_t* csv_time;
    const wchar_t* csv_freq;
    const wchar_t* status_vline;
    const wchar_t* status_hline;
    const wchar_t* status_marker;
};

static const Strings kRu = {
    L"LVM Viewer",
    L"Файл", L"Вид", L"Точки", L"Линии", L"Маркеры", L"Справка",
    L"Открыть файл…\tCtrl+O", L"Сохранить PNG…\tCtrl+S", L"Сохранить CSV…\tCtrl+E", L"Отменить\tCtrl+Z", L"Повторить\tCtrl+Shift+Z", L"Выход\tAlt+F4",
    L"Время / Гц\tM", L"Увеличить\t+", L"Уменьшить\t−", L"Сбросить вид\tHome", L"Auto Y", L"Сглаживание\tC", L"Play / Pause\tПробел", L"Тёмная тема\tT", L"Скорость",
    L"Точки\tV", L"Настройки…", L"Очистить\tDelete",
    L"Вертикальная\tL", L"Горизонтальная\tH", L"Очистить",
    L"Добавить\tK", L"Очистить",
    L"Горячие клавиши…\tF1", L"О программе…",
    L"Открыть", L"PNG", L"CSV", L"Время/Гц", L"▶ Play", L"⏸ Пауза", L"Точки", L"Сброс", L"Auto zoom", L"Настройки",
    L"Каналы",
    L"Время", L"Гц (FFT)", L"Каналов", L"Точек", L"Окно", L"Y: авто", L"Y: фикс.", L"Линий", L"Маркеров", L"Скорость",
    L"Время, c", L"Частота, Гц",
    L"Показывать номер точки", L"Показывать координату X", L"Показывать координату Y", L"Расстояние между точками по X (Δx)", L"Расстояние между точками по Y (Δy)", L"Частота 1/Δt", L"Расстояние d (по прямой)",
    L"X=%.5g", L"Δx=%.5g", L"Δy=%.5g", L"1/Δt=%.5g Гц", L"d=%.5g",
    L"Примагничивать маркеры к графику",
    L"Настройки точек измерения",
    L"Горячие клавиши — LVM Viewer", L"О программе — LVM Viewer",
    L"Нет данных", L"Сначала откройте файл.", L"Не удалось сохранить PNG.", L"Не удалось сохранить CSV.", L"Ошибка чтения",
    L"LVM Viewer", L"Просмотрщик сигналов LabVIEW (.lvm / .txt)",
    L"Как работать с приложением:\r   •  «Открыть файл» (O) — загрузите .lvm или .txt.\r   •  «Время / Гц» (M) — график сигнала или его спектр (БПФ).\r   •  «Измерение» (V) — кликайте точки на графике. Что показывать\r       у точек и примагничивание — в окне «Настройки точек».\r   •  Колесо мыши — масштаб, тяга ЛКМ — прокрутка по времени.\r   •  «Фикс. Y» — зафиксировать масштаб по высоте.\r   •  Пробел — воспроизведение в реальном времени (1 с = 1 с).\r   •  F1 — полный список горячих клавиш.",
    L"Открыть файл", L"Настройки точек…", L"Горячие клавиши", L"Начать работу",
    L"Файлы\n  O / Ctrl+O\t— Открыть\n  S / Ctrl+S\t— PNG\n  E / Ctrl+E\t— CSV\n  Ctrl+Z\t— Отменить\n  Ctrl+Shift+Z\t— Повторить\n\nВид\n  M\t— Время/Гц\n  C\t— Сглаживание\n  + / ↑\t— Увеличить\n  − / ↓\t— Уменьшить\n  ← / →\t— Сдвиг влево/вправо\n  Home\t— Сброс\n  Пробел\t— Play / Pause\n\nЛинии и маркеры\n  L\t— Вертикальная линия\n  H\t— Горизонтальная линия\n  K\t— Маркер\n  Esc\t— Отменить добавление\n\nТочки\n  V\t— Режим точек вкл/выкл\n  Delete\t— Очистить точки\n\nМышь\n  Колесо\t— Масштаб под курсором\n  Shift+колесо\t— Прокрутка влево/вправо\n  Ctrl+колесо\t— Масштаб по высоте (Y)\n  Alt+колесо\t— Точный масштаб (X)\n  ЛКМ + тяга\t— Панорамирование\n  ЛКМ\t— Поставить точку / линию / маркер (в режиме)\n  ПКМ\t— Очистить точки\n\n  F1\t— Эта справка",
    L"LVM Viewer — просмотрщик сигналов LabVIEW (.lvm / .txt)\n\nНативное приложение Win32 + GDI/GDI+, без внешних\nзависимостей и без Qt. Время и спектр (БПФ), измерения\nс примагничиванием, направляющие линии, визуальное\nсглаживание, экспорт PNG/CSV.\n\nСборка: build_gui.ps1 (MinGW g++) или make gui.",
    L"Открыть файл…", L"Сохранить PNG", L"Сохранить CSV", L"Переключить Время / Гц", L"Воспроизведение", L"Пауза", L"Режим измерения точек", L"Сбросить вид", L"Авто масштаб по Y", L"Настройки точек",
    L"Русский", L"English", L"Язык",
    L"Загрузка файла...\nПожалуйста, подождите",
    L"Откройте файл .lvm или .txt (кнопка «Открыть файл» / клавиша O)",
    L"   |   Δf = %.5g Гц,  Δamp = %.4g",
    L"   |   Δt = %.6g c,  Δy = %.5g,  1/Δt = %.6g Гц",
    L" (+сплайн)",
    L"%.5g Гц",
    L"%.5g c",
    L"y=%.5g",
    L"Гц",
    L"c",
    L"Светлая тема",
    L"Тёмная тема",
    L"Цвет маркеров…",
    L"Ошибка",
    L"Сохранено (PNG): ",
    L"Сохранено (CSV): ",
    L"LVM / текстовые файлы\0*.lvm;*.txt\0Все файлы\0*.*\0",
    L"PNG изображение\0*.png\0Все файлы\0*.*\0",
    L"CSV файл\0*.csv\0Все файлы\0*.*\0",
    L"Time",
    L"Frequency",
    L"Кликните на графике, чтобы поставить вертикальную линию (Esc — отмена). Можно добавить несколько линий подряд.",
    L"Кликните на графике, чтобы поставить горизонтальную линию (Esc — отмена). Можно добавить несколько линий подряд.",
    L"Кликните на графике, чтобы поставить маркер (Esc — отмена)."
};

static const Strings kEn = {
    L"LVM Viewer",
    L"File", L"View", L"Points", L"Lines", L"Markers", L"Help",
    L"Open file…\tCtrl+O", L"Save PNG…\tCtrl+S", L"Save CSV…\tCtrl+E", L"Undo\tCtrl+Z", L"Redo\tCtrl+Shift+Z", L"Exit\tAlt+F4",
    L"Time / Hz\tM", L"Zoom in\t+", L"Zoom out\t−", L"Reset view\tHome", L"Auto Y", L"Smoothing\tC", L"Play / Pause\tSpace", L"Dark theme\tT", L"Speed",
    L"Points\tV", L"Settings…", L"Clear\tDelete",
    L"Vertical\tL", L"Horizontal\tH", L"Clear",
    L"Add\tK", L"Clear",
    L"Keyboard shortcuts…\tF1", L"About…",
    L"Open", L"PNG", L"CSV", L"Time/Hz", L"▶ Play", L"⏸ Pause", L"Points", L"Reset", L"Auto zoom", L"Settings",
    L"Channels",
    L"Time", L"Hz (FFT)", L"Channels", L"Points", L"Window", L"Y: auto", L"Y: fixed", L"Lines", L"Markers", L"Speed",
    L"Time, s", L"Frequency, Hz",
    L"Show point number", L"Show X coordinate", L"Show Y coordinate", L"Distance along X (Δx)", L"Distance along Y (Δy)", L"Frequency 1/Δt", L"Straight-line distance d",
    L"X=%.5g", L"Δx=%.5g", L"Δy=%.5g", L"1/Δt=%.5g Hz", L"d=%.5g",
    L"Snap markers to graph",
    L"Measurement point settings",
    L"Keyboard shortcuts — LVM Viewer", L"About — LVM Viewer",
    L"No data", L"Open a file first.", L"Failed to save PNG.", L"Failed to save CSV.", L"Read error",
    L"LVM Viewer", L"LabVIEW signal viewer (.lvm / .txt)",
    L"How to use the app:\r   •  «Open file» (O) — load a .lvm or .txt.\r   •  «Time / Hz» (M) — signal plot or its FFT spectrum.\r   •  «Measure» (V) — click points on the plot. What to show\r       at points and snapping — in the «Point settings» window.\r   •  Mouse wheel — zoom, left-drag — pan.\r   •  «Lock Y» — freeze the vertical scale.\r   •  Space — real-time playback (1 s = 1 s).\r   •  F1 — full list of keyboard shortcuts.",
    L"Open file", L"Point settings…", L"Keyboard shortcuts", L"Start working",
    L"Files\n  O / Ctrl+O\t— Open\n  S / Ctrl+S\t— PNG\n  E / Ctrl+E\t— CSV\n  Ctrl+Z\t— Undo\n  Ctrl+Shift+Z\t— Redo\n\nView\n  M\t— Time / Hz\n  C\t— Smoothing\n  + / ↑\t— Zoom in\n  − / ↓\t— Zoom out\n  ← / →\t— Pan left / right\n  Home\t— Reset view\n  Space\t— Play / Pause\n\nLines and markers\n  L\t— Vertical line\n  H\t— Horizontal line\n  K\t— Marker\n  Esc\t— Cancel adding\n\nPoints\n  V\t— Measure mode on/off\n  Delete\t— Clear points\n\nMouse\n  Wheel\t— Zoom under cursor\n  Shift+wheel\t— Pan left / right\n  Ctrl+wheel\t— Zoom Y\n  Alt+wheel\t— Fine zoom X\n  Left-drag\t— Pan\n  Left-click\t— Drop point / line / marker (in mode)\n  Right-click\t— Clear points\n\n  F1\t— This help",
    L"LVM Viewer — LabVIEW signal viewer (.lvm / .txt)\n\nNative Win32 + GDI/GDI+ application, no external\ndependencies, no Qt. Time and spectrum (FFT), measurements\nwith snapping, guide lines, visual smoothing, PNG/CSV export.\n\nBuild: build_gui.ps1 (MinGW g++) or make gui.",
    L"Open file…", L"Save PNG", L"Save CSV", L"Toggle Time / Hz", L"Playback", L"Pause", L"Measurement point mode", L"Reset view", L"Auto Y scale", L"Point settings",
    L"Русский", L"English", L"Language",
    L"Loading file...\nPlease wait",
    L"Open a .lvm or .txt file (click «Open file» / press O)",
    L"   |   Δf = %.5g Hz,  Δamp = %.4g",
    L"   |   Δt = %.6g s,  Δy = %.5g,  1/Δt = %.6g Hz",
    L" (+spline)",
    L"%.5g Hz",
    L"%.5g s",
    L"y=%.5g",
    L"Hz",
    L"s",
    L"Light theme",
    L"Dark theme",
    L"Marker colour…",
    L"Error",
    L"Saved (PNG): ",
    L"Saved (CSV): ",
    L"LVM / text files\0*.lvm;*.txt\0All files\0*.*\0",
    L"PNG image\0*.png\0All files\0*.*\0",
    L"CSV file\0*.csv\0All files\0*.*\0",
    L"Time",
    L"Frequency",
    L"Click on the plot to place a vertical line (Esc to cancel). You can add multiple lines.",
    L"Click on the plot to place a horizontal line (Esc to cancel). You can add multiple lines.",
    L"Click on the plot to place a marker (Esc to cancel)."
};

const Strings* g_str = &kRu;

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

    bool auto_y = true;            // auto-fit vertical scale (true=auto, false=fixed)
    double y_lock_min = -1.0, y_lock_max = 1.0;

    bool auto_y_amp = true;        // auto-fit amplitude in Hz mode
    double y_amp_max = 1.0;        // locked amplitude max in Hz mode

    bool measure_mode = false;
    bool snap_to_data = true;       // snap markers to the nearest real sample
    PointDisplay pdisp;             // which read-outs to draw at markers
    COLORREF marker_color = g_theme->marker_color;
    std::vector<std::pair<double, double>> points;  // measurement points (data coords)

    std::vector<GuideLine> guides;  // vertical / horizontal reference lines
    int pending_line = 0;           // 0 none, 1 next click = vertical, 2 = horizontal

    bool playing = false;
    bool playhead_active = false;
    double playhead = 0.0;
    double play_anchor_data = 0.0;        // signal time when playback (re)started
    LARGE_INTEGER play_anchor_qpc = {};   // performance counter at that moment
    double play_speed = 1.0;

    // Mapping cache from the last paint (data <-> pixels) for hit-testing.
    double vx0 = 0, vx1 = 1, vy0 = 0, vy1 = 1;
    RECT vrect = {0, 0, 1, 1};
    bool vvalid = false;

    struct Marker { double x = 0.0; std::wstring label; bool freq = false; };
    std::vector<Marker> markers;
    bool pending_marker = false;

    std::wstring file_name;
    std::string last_error;

    HWND main = nullptr;
    HWND open = nullptr, savepng = nullptr, savecsv = nullptr, mode = nullptr;
    HWND play = nullptr, measure = nullptr;
    HWND reset = nullptr, autoy = nullptr, ptsettings = nullptr;
    HWND status = nullptr;
    std::vector<HWND> checks;
    std::vector<HWND> buttons;   // owner-drawn toolbar buttons
    HWND hovered_btn = nullptr;
    std::wstring status_text;
    std::wstring hover_status_text;  // shown in status bar when hovering toolbar buttons
    std::vector<int> toolbar_seps;

    HWND settings_wnd = nullptr; // measurement-point settings panel (modeless)
    HWND welcome_wnd = nullptr;  // start screen

    HMENU menu = nullptr;        // main menu bar
    HFONT ui_font = nullptr;     // Segoe UI for controls / labels
    HFONT bold_font = nullptr;   // semibold for headings
    HFONT title_font = nullptr;  // large font for the welcome title
    HFONT axis_font = nullptr;   // 11px for axis tick labels
    // icon_font removed — toolbar now uses text labels with ui_font

    bool dragging = false;
    int drag_x = 0;
    double drag_lo = 0.0, drag_hi = 0.0;
};

App g;
ULONG_PTR g_gdiplus_token = 0;

struct LegendItem { int channel; RECT rect; };
std::vector<LegendItem> g_legend_items;
RECT g_legend_box = {0,0,0,0};

// ---- undo / redo system ------------------------------------------------
struct UndoAction {
    enum Type { NONE, ADD_POINT, ADD_LINE, ADD_MARKER, CLEAR_POINTS, CLEAR_LINES, CLEAR_MARKERS } type = NONE;
    std::pair<double, double> point;
    GuideLine line;
    App::Marker marker;
    std::vector<std::pair<double, double>> saved_points;
    std::vector<GuideLine> saved_lines;
    std::vector<App::Marker> saved_markers;
};
std::vector<UndoAction> g_undo;
std::vector<UndoAction> g_redo;

void push_undo(const UndoAction& a) {
    g_undo.push_back(a);
    g_redo.clear(); // new action clears redo stack
}
void pop_undo() {
    if (g_undo.empty()) return;
    UndoAction a = g_undo.back();
    g_undo.pop_back();
    switch (a.type) {
        case UndoAction::ADD_POINT:
            if (!g.points.empty()) {
                g_redo.push_back(a);
                g.points.pop_back();
            }
            break;
        case UndoAction::ADD_LINE: {
            auto it = std::find_if(g.guides.begin(), g.guides.end(), [&](const GuideLine& gl) {
                return gl.vertical == a.line.vertical && gl.value == a.line.value && gl.freq == a.line.freq;
            });
            if (it != g.guides.end()) {
                g_redo.push_back(a);
                g.guides.erase(it);
            }
            break;
        }
        case UndoAction::ADD_MARKER: {
            auto it = std::find_if(g.markers.begin(), g.markers.end(), [&](const App::Marker& m) {
                return m.x == a.marker.x && m.freq == a.marker.freq && m.label == a.marker.label;
            });
            if (it != g.markers.end()) {
                g_redo.push_back(a);
                g.markers.erase(it);
            }
            break;
        }
        case UndoAction::CLEAR_POINTS:
            g_redo.push_back(a);
            g.points = a.saved_points;
            break;
        case UndoAction::CLEAR_LINES:
            g_redo.push_back(a);
            g.guides = a.saved_lines;
            break;
        case UndoAction::CLEAR_MARKERS:
            g_redo.push_back(a);
            g.markers = a.saved_markers;
            break;
        default: break;
    }
}
void pop_redo() {
    if (g_redo.empty()) return;
    UndoAction a = g_redo.back();
    g_redo.pop_back();
    switch (a.type) {
        case UndoAction::ADD_POINT:
            g.points.push_back(a.point);
            g_undo.push_back(a);
            break;
        case UndoAction::ADD_LINE:
            g.guides.push_back(a.line);
            g_undo.push_back(a);
            break;
        case UndoAction::ADD_MARKER:
            g.markers.push_back(a.marker);
            g_undo.push_back(a);
            break;
        case UndoAction::CLEAR_POINTS:
            g_undo.push_back(a);
            g.points.clear();
            break;
        case UndoAction::CLEAR_LINES:
            g_undo.push_back(a);
            g.guides.clear();
            break;
        case UndoAction::CLEAR_MARKERS:
            g_undo.push_back(a);
            g.markers.clear();
            break;
        default: break;
    }
}

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
    std::wstring s;
    wchar_t buf[512];
    if (!has_data()) {
        s = g_str->msg_nodata;
    } else if (g.freq_mode) {
        swprintf(buf, 512,
                 g_str->st_hz,
                 g.ds.channel_count(), g.spec_valid ? g.spec.nyquist : 0.0,
                 g.freq_start, g.freq_end);
        s = buf;
    } else {
        swprintf(buf, 512,
                 g_str->st_time,
                 g.ds.channel_count(), g.ds.rows(), g.win_start, g.win_end);
        s = buf;
        s += g.auto_y ? g_str->st_yauto : g_str->st_yfix;
        if (g.visual_smooth) s += g_str->st_spline;
    }
    if (has_data()) {
        std::size_t nlines = 0;
        for (const auto& gl : g.guides)
            if (gl.freq == g.freq_mode) ++nlines;
        if (nlines) { swprintf(buf, 512, g_str->st_lines, nlines); s += buf; }
        std::size_t nmark = 0;
        for (const auto& m : g.markers)
            if (m.freq == g.freq_mode) ++nmark;
        if (nmark) { swprintf(buf, 512, g_str->st_markers, nmark); s += buf; }
    }
    if (g.playing) {
        swprintf(buf, 512, g_str->st_speed, g.play_speed);
        s += buf;
    }
    if (g.points.size() >= 2) {
        const auto& a = g.points[g.points.size() - 2];
        const auto& b = g.points.back();
        const double dx = b.first - a.first, dy = b.second - a.second;
        if (g.freq_mode) {
            swprintf(buf, 512, g_str->msg_delta_f, dx, dy);
        } else {
            const double inv = (dx != 0.0) ? 1.0 / dx : 0.0;
            swprintf(buf, 512, g_str->msg_delta_t, dx, dy, inv);
        }
        s += buf;
    }
    g.status_text = s;
    if (g.status) SetWindowTextW(g.status, s.c_str());
    if (g.main) {
        RECT rc;
        GetClientRect(g.main, &rc);
        RECT sr = {0, rc.bottom - kBottomBar, rc.right, rc.bottom};
        InvalidateRect(g.main, &sr, FALSE);
    }
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

void hide_ui_controls() {
    for (HWND b : g.buttons) ShowWindow(b, SW_HIDE);
    for (HWND c : g.checks) ShowWindow(c, SW_HIDE);
    if (g.status) ShowWindow(g.status, SW_HIDE);
}

void show_ui_controls() {
    for (HWND b : g.buttons) ShowWindow(b, SW_SHOW);
    for (HWND c : g.checks) ShowWindow(c, SW_SHOW);
    if (g.status) ShowWindow(g.status, SW_SHOW);
}

void layout() {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    g.toolbar_seps.clear();
    int x = 8;
    auto place = [&](HWND h, int w, int row_y) { MoveWindow(h, x, row_y, w, 28, TRUE); x += w + 4; };
    auto sep = [&]() { g.toolbar_seps.push_back(x + 2); x += 8; };

    // Row 1
    x = 8;
    place(g.open, 100, 8);
    place(g.savepng, 60, 8);
    place(g.savecsv, 60, 8);
    sep();
    place(g.mode, 100, 8);
    place(g.play, 80, 8);

    // Row 2
    x = 8;
    place(g.measure, 80, 40);
    place(g.reset, 80, 40);
    place(g.autoy, 90, 40);
    sep();
    place(g.ptsettings, 110, 40);

    const int panel_x = cw - kRightPanel + 12;
    int y = kTopBar + 28;
    for (HWND c : g.checks) { MoveWindow(c, panel_x + 18, y, kRightPanel - 24 - 18, 24, TRUE); y += 26; }

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
        minw = (g.spec.freqs[1] - g.spec.freqs[0]) * 0.5;
        return true;
    }
    if (!has_data()) return false;
    lo = &g.win_start; hi = &g.win_end;
    minb = g.data_t0; maxb = g.data_t1;
    minw = std::max(g.approx_dt * 0.5, (g.data_t1 - g.data_t0) * 1e-6);
    return true;
}

bool current_time_yrange(double& ymin, double& ymax);
void sync_menu();

void invalidate_plot() {
    RECT pr = plot_rect();
    RECT rc; GetClientRect(g.main, &rc);
    pr.bottom = rc.bottom; // include status bar
    InvalidateRect(g.main, &pr, FALSE);
}

void zoom_at(double center_frac, double factor) {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    const double c = *lo + w * center_frac;
    double nw = w * factor;
    if (nw < minw) nw = minw;
    const double full = maxb - minb;
    if (nw > full) nw = full;
    double nlo = c - nw * center_frac;
    double nhi = nlo + nw;
    if (nlo < minb) { nlo = minb; nhi = nlo + nw; }
    if (nhi > maxb) { nhi = maxb; nlo = nhi - nw; if (nlo < minb) nlo = minb; }
    *lo = nlo;
    *hi = nhi;
    set_status();
    invalidate_plot();
}

void zoom_y_at(double center_frac, double factor) {
    if (!has_data()) return;
    double ymin, ymax;
    current_time_yrange(ymin, ymax);
    if (!g.auto_y) { ymin = g.y_lock_min; ymax = g.y_lock_max; }
    const double w = ymax - ymin;
    if (w <= 0) return;
    const double c = ymin + w * center_frac;
    double nw = w * factor;
    const double minw = std::max(1e-12, w * 1e-6);
    if (nw < minw) nw = minw;
    double nlo = c - nw * center_frac;
    double nhi = nlo + nw;
    g.y_lock_min = nlo;
    g.y_lock_max = nhi;
    g.auto_y = false;
    if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_UNCHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
    sync_menu();
    set_status();
    invalidate_plot();
}

void zoom_y_amp_at(double center_frac, double factor) {
    if (!g.spec_valid || g.spec.amp.empty()) return;
    double ymax = 0.0;
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        int ci = channel_index_by_name(g.spec.names[j]);
        if (ci < 0 || !g.visible[ci]) continue;
        for (auto v : g.spec.amp[j]) if (v > ymax) ymax = v;
    }
    if (ymax <= 0) ymax = 1.0;
    double ytop = ymax * 1.08;
    if (!g.auto_y_amp) ytop = g.y_amp_max;
    const double w = ytop;
    if (w <= 0) return;
    const double c = w * center_frac;
    double nw = w * factor;
    const double minw = std::max(1e-12, w * 1e-6);
    if (nw < minw) nw = minw;
    g.y_amp_max = nw;
    g.auto_y_amp = false;
    set_status();
    invalidate_plot();
}

void pan_by(double frac) {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    *lo += w * frac;
    *hi += w * frac;
    clamp_range(*lo, *hi, minb, maxb, minw);
    set_status();
    invalidate_plot();
}

void reset_view() {
    g.win_start = g.data_t0;
    g.win_end = g.data_t1;
    g.freq_start = 0.0;
    g.freq_end = g.spec_valid ? g.spec.nyquist : 1.0;
    set_status();
    invalidate_plot();
}

// ---- playback ------------------------------------------------------------

void stop_play() {
    g.playing = false;
    KillTimer(g.main, 1);
    if (g.play) SetWindowTextW(g.play, g_str->btn_play);
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
    if (g.play) SetWindowTextW(g.play, g_str->btn_pause);
}

void toggle_play() {
    if (g.playing) stop_play();
    else start_play();
}

// ---- loading -------------------------------------------------------------

static HWND g_loading_wnd = nullptr;

void show_loading(const std::wstring& msg) {
    if (g_loading_wnd) { DestroyWindow(g_loading_wnd); g_loading_wnd = nullptr; }
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    g_loading_wnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"Static", msg.c_str(),
        WS_POPUP | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | WS_BORDER,
        0, 0, 300, 90, g.main, nullptr, inst, nullptr);
    if (!g_loading_wnd) return;
    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (font) SendMessageW(g_loading_wnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    RECT mr, wr;
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_loading_wnd, &wr);
    SetWindowPos(g_loading_wnd, HWND_TOPMOST,
                 mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
                 mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    UpdateWindow(g_loading_wnd);
}

void hide_loading() {
    if (g_loading_wnd) { DestroyWindow(g_loading_wnd); g_loading_wnd = nullptr; }
}

bool load_path(const std::wstring& wpath) {
    show_loading(g_str->msg_loading);
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    lvm::Dataset ds = lvm::read_lvm_file(to_acp(wpath.c_str()));
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    hide_loading();
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
    g.guides.clear();
    g.markers.clear();
    g_undo.clear();
    g_redo.clear();
    stop_play();
    g.playhead = g.data_t0;
    g.playhead_active = false;
    g.auto_y = true;   // a fresh file starts on auto-fit
    if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_CHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
    if (g.menu) CheckMenuItem(g.menu, IDC_AUTOY, MF_BYCOMMAND | MF_CHECKED);

    compute_spectrum();
    g.freq_start = 0.0;
    g.freq_end = g.spec_valid ? g.spec.nyquist : 1.0;

    const wchar_t* base = wcsrchr(wpath.c_str(), L'\\');
    g.file_name = base ? base + 1 : wpath;
    SetWindowTextW(g.main, (std::wstring(g_str->app_title) + L" — " + g.file_name).c_str());
    if (g.welcome_wnd) { ShowWindow(g.welcome_wnd, SW_HIDE); show_ui_controls(); }

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
    ofn.lpstrFilter = g_str->filter_open;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 2048;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    if (!load_path(file))
        MessageBoxW(g.main, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
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
    HBRUSH plot_bg = CreateSolidBrush(g_theme->bg_plot);
    RECT fill_p = {p.left, p.top, p.right, p.bottom};
    FillRect(dc, &fill_p, plot_bg);
    DeleteObject(plot_bg);
    HPEN grid = CreatePen(PS_SOLID, 1, g_theme->grid);
    HPEN frame = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ old = SelectObject(dc, grid);
    SetTextColor(dc, g_theme->axis_text);
    HFONT font = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    wchar_t buf[64];

    const int ticks = 6;
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
        TextOutW(dc, p.left - 8, py, buf, lstrlenW(buf));
    }

    HPEN minor = CreatePen(PS_SOLID, 1, g_theme->minor_grid);
    HGDIOBJ old_minor = SelectObject(dc, minor);
    for (int i = 0; i < ticks; ++i) {
        const double f0 = static_cast<double>(i) / ticks;
        const double f1 = static_cast<double>(i + 1) / ticks;
        for (int j = 1; j <= 4; ++j) {
            const double f = f0 + (f1 - f0) * j / 5.0;
            const int px = p.left + static_cast<int>(f * (p.right - p.left));
            const int py = p.bottom - static_cast<int>(f * (p.bottom - p.top));
            MoveToEx(dc, px, p.top, nullptr); LineTo(dc, px, p.bottom);
            MoveToEx(dc, p.left, py, nullptr); LineTo(dc, p.right, py);
        }
    }
    SelectObject(dc, old_minor);
    DeleteObject(minor);

    SelectObject(dc, frame);
    Rectangle(dc, p.left, p.top, p.right, p.bottom);
    draw_text(dc, (p.left + p.right) / 2, p.bottom + 20, xlabel, TA_CENTER | TA_TOP);

    SelectObject(dc, old_font);
    SelectObject(dc, old);
    DeleteObject(grid);
    DeleteObject(frame);
}

void draw_legend(HDC dc, const RECT& p) {
    int item_h = 16;
    int pad = 6;
    int max_width = 0;
    int ch_count = static_cast<int>(g.ds.channel_count());
    if (ch_count == 0) return;

    HFONT leg_font = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ old_font = SelectObject(dc, leg_font);
    SetBkMode(dc, TRANSPARENT);

    for (int i = 0; i < ch_count; ++i) {
        std::wstring nm = to_w(g.ds.names[i]);
        SIZE sz;
        GetTextExtentPoint32W(dc, nm.c_str(), static_cast<int>(nm.size()), &sz);
        if (sz.cx > max_width) max_width = sz.cx;
    }

    int box_w = 14 + 4 + max_width + pad * 2;
    int box_h = ch_count * item_h + pad * 2;
    int box_x = p.right - box_w - 14;
    int box_y = p.bottom - box_h - 14;

    g_legend_box = {box_x, box_y, box_x + box_w, box_y + box_h};
    g_legend_items.clear();

    // Draw themed rounded background with border
    HBRUSH bg = CreateSolidBrush(g_theme->bg_plot);
    HPEN border = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ old_brush = SelectObject(dc, bg);
    HGDIOBJ old_pen = SelectObject(dc, border);
    RoundRect(dc, box_x, box_y, box_x + box_w, box_y + box_h, 4, 4);
    SelectObject(dc, old_pen);
    DeleteObject(border);
    SelectObject(dc, old_brush);
    DeleteObject(bg);

    int y = box_y + pad;
    for (int i = 0; i < ch_count; ++i) {
        bool vis = g.visible[i];
        COLORREF col = channel_color(i);
        if (!vis) {
            // Dim the color for hidden channels (average toward gray)
            col = RGB((GetRValue(col) + 180) / 2,
                      (GetGValue(col) + 180) / 2,
                      (GetBValue(col) + 180) / 2);
        }
        HBRUSH cb = CreateSolidBrush(col);
        HGDIOBJ old_b = SelectObject(dc, cb);
        HGDIOBJ old_p = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, box_x + pad, y + 6, box_x + pad + 14, y + 6 + 3, 2, 2);
        SelectObject(dc, old_p);
        SelectObject(dc, old_b);
        DeleteObject(cb);

        // If hidden, draw a diagonal line through the swatch
        if (!vis) {
            HPEN line = CreatePen(PS_SOLID, 1, g_theme->text_secondary);
            HGDIOBJ old_lp = SelectObject(dc, line);
            MoveToEx(dc, box_x + pad, y + 6, nullptr);
            LineTo(dc, box_x + pad + 14, y + 6 + 3);
            SelectObject(dc, old_lp);
            DeleteObject(line);
        }

        SetTextColor(dc, vis ? g_theme->text_primary : g_theme->text_secondary);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        std::wstring nm = to_w(g.ds.names[i]);
        TextOutW(dc, box_x + pad + 18, y, nm.c_str(), static_cast<int>(nm.size()));

        // Store hit-test rect for this item (full row, for easier clicking)
        g_legend_items.push_back({i, {box_x, y, box_x + box_w, y + item_h}});

        y += item_h;
    }
    SelectObject(dc, old_font);
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
    HPEN vpen = CreatePen(PS_DASH, 2, RGB(0, 150, 60));
    HPEN hpen = CreatePen(PS_DASH, 1, RGB(0, 150, 60));
    HGDIOBJ old = SelectObject(dc, vpen);
    SetTextColor(dc, RGB(0, 110, 45));
    HFONT lf = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ oldf = SelectObject(dc, lf);
    SetBkMode(dc, TRANSPARENT);
    wchar_t b[48];
    HBRUSH wb = CreateSolidBrush(g_theme->bg_plot);
    for (const auto& gl : g.guides) {
        if (gl.freq != g.freq_mode) continue;
        if (gl.vertical) {
            SelectObject(dc, vpen);
            const int X = mx(gl.value);
            if (X < p.left || X > p.right) continue;
            MoveToEx(dc, X, p.top, nullptr); LineTo(dc, X, p.bottom);
            swprintf(b, 48, g.freq_mode ? g_str->fmt_hz : g_str->fmt_sec, gl.value);
            SetTextAlign(dc, TA_LEFT | TA_TOP);
            SIZE ts;
            GetTextExtentPoint32W(dc, b, lstrlenW(b), &ts);
            RECT br = {X + 3, p.top + 2, X + 3 + ts.cx + 2, p.top + 2 + ts.cy};
            FillRect(dc, &br, wb);
            TextOutW(dc, X + 3, p.top + 2, b, lstrlenW(b));
        } else {
            SelectObject(dc, hpen);
            const int Y = my(gl.value);
            if (Y < p.top || Y > p.bottom) continue;
            MoveToEx(dc, p.left, Y, nullptr); LineTo(dc, p.right, Y);
            swprintf(b, 48, g_str->fmt_y, gl.value);
            SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
            SIZE ts;
            GetTextExtentPoint32W(dc, b, lstrlenW(b), &ts);
            RECT br = {p.left + 4, Y - 2 - ts.cy, p.left + 4 + ts.cx + 2, Y - 2};
            FillRect(dc, &br, wb);
            TextOutW(dc, p.left + 4, Y - 2, b, lstrlenW(b));
        }
    }
    DeleteObject(wb);
    SelectObject(dc, oldf);
    SelectObject(dc, old);
    DeleteObject(vpen);
    DeleteObject(hpen);
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

// Markers (bookmarks) with labels.
void draw_markers(HDC dc) {
    if (g.markers.empty() || !g.vvalid) return;
    const RECT& p = g.vrect;
    if (g.vx1 <= g.vx0) return;
    auto mx = [&](double dx) {
        return p.left + static_cast<int>((dx - g.vx0) / (g.vx1 - g.vx0) * (p.right - p.left));
    };
    HRGN clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
    SelectClipRgn(dc, clip);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(180, 0, 180));
    HGDIOBJ old = SelectObject(dc, pen);
    SetTextColor(dc, RGB(140, 0, 140));
    HFONT mf = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ oldf = SelectObject(dc, mf);
    SetBkMode(dc, TRANSPARENT);
    HBRUSH wb = CreateSolidBrush(g_theme->bg_plot);
    HPEN bp = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ prev_pen = SelectObject(dc, bp);
    HGDIOBJ prev_brush = SelectObject(dc, wb);
    for (const auto& m : g.markers) {
        if (m.freq != g.freq_mode) continue;
        const int X = mx(m.x);
        if (X < p.left || X > p.right) continue;
        MoveToEx(dc, X, p.top, nullptr); LineTo(dc, X, p.bottom);
        const wchar_t* txt = nullptr;
        int tlen = 0;
        wchar_t b[48];
        if (!m.label.empty()) {
            txt = m.label.c_str();
            tlen = static_cast<int>(m.label.size());
        } else {
            swprintf(b, 48, g.freq_mode ? g_str->fmt_hz : g_str->fmt_sec, m.x);
            txt = b;
            tlen = lstrlenW(b);
        }
        SIZE ts;
        GetTextExtentPoint32W(dc, txt, tlen, &ts);
        int tx = X + 3;
        int ty = p.top + 4;
        RoundRect(dc, tx - 2, ty - 1, tx + ts.cx + 4, ty + ts.cy + 2, 3, 3);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        TextOutW(dc, tx, ty, txt, tlen);
    }
    SelectObject(dc, prev_pen);
    DeleteObject(bp);
    SelectObject(dc, prev_brush);
    DeleteObject(wb);
    SelectObject(dc, oldf);
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
    const wchar_t* xunit = g.freq_mode ? g_str->unit_hz : g_str->unit_sec;

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
    HFONT mf = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ oldf = SelectObject(dc, mf);
    SetBkMode(dc, TRANSPARENT);
    wchar_t b[96];
    HBRUSH wb = CreateSolidBrush(g_theme->bg_plot);
    HPEN bp = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ prev_pen = SelectObject(dc, bp);
    HGDIOBJ prev_brush = SelectObject(dc, wb);
    for (std::size_t i = 0; i < g.points.size(); ++i) {
        const int X = mx(g.points[i].first), Y = my(g.points[i].second);
        MoveToEx(dc, X - 8, Y, nullptr); LineTo(dc, X + 9, Y);
        MoveToEx(dc, X, Y - 8, nullptr); LineTo(dc, X, Y + 9);

        // Filled dot at the centre using the chosen marker colour.
        HBRUSH dot_brush = CreateSolidBrush(g.marker_color);
        HGDIOBJ old_br = SelectObject(dc, dot_brush);
        HGDIOBJ old_pn = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, X - 3, Y - 3, X + 4, Y + 4);
        SelectObject(dc, old_pn);
        SelectObject(dc, old_br);
        DeleteObject(dot_brush);

        std::wstring lab;
        if (g.pdisp.number) { swprintf(b, 96, L"#%zu ", i + 1); lab += b; }
        if (g.pdisp.x) { swprintf(b, 96, g_str->fmt_pt_x, g.points[i].first); lab += b; lab += xunit; lab += L" "; }
        if (g.pdisp.y) { swprintf(b, 96, g_str->fmt_y, g.points[i].second); lab += b; }
        if (!lab.empty()) {
            SetTextColor(dc, g.marker_color);
            SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
            SIZE ts;
            GetTextExtentPoint32W(dc, lab.c_str(), static_cast<int>(lab.size()), &ts);
            RoundRect(dc, X + 6, Y - 4 - ts.cy, X + 12 + ts.cx, Y + 2, 3, 3);
            TextOutW(dc, X + 8, Y - 2, lab.c_str(), static_cast<int>(lab.size()));
        }

        if (i >= 1) {  // segment read-out at the midpoint
            const double dx = g.points[i].first - g.points[i - 1].first;
            const double dy = g.points[i].second - g.points[i - 1].second;
            std::wstring dl;
            if (g.pdisp.dx) { swprintf(b, 96, g_str->fmt_pt_dx, dx); dl += b; dl += xunit; dl += L" "; }
            if (g.pdisp.dy) { swprintf(b, 96, g_str->fmt_pt_dy, dy); dl += b; }
            if (g.pdisp.inv_dt && !g.freq_mode) {
                const double inv = (dx != 0.0) ? 1.0 / dx : 0.0;
                swprintf(b, 96, g_str->fmt_pt_invdt, inv); dl += b;
            }
            if (g.pdisp.dist) {
                swprintf(b, 96, g_str->fmt_pt_dist, std::sqrt(dx * dx + dy * dy)); dl += b;
            }
            if (!dl.empty()) {
                const int mxp = (mx(g.points[i].first) + mx(g.points[i - 1].first)) / 2;
                const int myp = (my(g.points[i].second) + my(g.points[i - 1].second)) / 2;
                SetTextColor(dc, g.marker_color);
                SetTextAlign(dc, TA_CENTER | TA_BOTTOM);
                SIZE ts;
                GetTextExtentPoint32W(dc, dl.c_str(), static_cast<int>(dl.size()), &ts);
                RoundRect(dc, mxp - ts.cx/2 - 4, myp - 4 - ts.cy, mxp + ts.cx/2 + 6, myp + 2, 3, 3);
                TextOutW(dc, mxp, myp - 2, dl.c_str(), static_cast<int>(dl.size()));
            }
        }
    }
    SelectObject(dc, prev_pen);
    DeleteObject(bp);
    SelectObject(dc, prev_brush);
    DeleteObject(wb);
    SelectObject(dc, oldf);
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
    if (!g.auto_y) { ymin = g.y_lock_min; ymax = g.y_lock_max; }

    draw_axes(dc, p, g.win_start, g.win_end, ymin, ymax, g_str->plot_xlabel_time);

    const int pw = p.right - p.left, ph = p.bottom - p.top;
    const double xspan = g.win_end - g.win_start;
    auto mapx = [&](double tt) { return p.left + static_cast<int>((tt - g.win_start) / xspan * pw); };
    auto mapy = [&](double yy) { return p.bottom - static_cast<int>((yy - ymin) / (ymax - ymin) * ph); };

    HRGN clip = CreateRectRgn(p.left + 1, p.top + 1, p.right, p.bottom);
    SelectClipRgn(dc, clip);

    static std::vector<float> series;
    static std::vector<float> cmin, cmax;
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
            cmin.resize(pw, 1e30f);
            cmax.resize(pw, -1e30f);
            std::fill(cmin.begin(), cmin.end(), 1e30f);
            std::fill(cmax.begin(), cmax.end(), -1e30f);
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
        HPEN ph_pen = CreatePen(PS_SOLID, 2, g_theme->playhead);
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
        draw_axes(dc, p, 0, 1, 0, 1, g_str->plot_xlabel_freq);
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
    double ytop = ymax * 1.08;
    if (!g.auto_y_amp) ytop = g.y_amp_max;

    draw_axes(dc, p, f0, f1, 0, ytop, g_str->plot_xlabel_freq);

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
        SetTextColor(dc, g_theme->text_secondary);
        const wchar_t* msg = g_str->msg_openprompt;
        TextOutW(dc, (p.left + p.right) / 2, (p.top + p.bottom) / 2, msg, lstrlenW(msg));
        g.vvalid = false;
        return;
    }
    if (g.freq_mode) draw_freq(dc, p);
    else draw_time(dc, p);
    draw_guides(dc);
    draw_markers(dc);
    draw_measure(dc);
}

void on_paint(HDC hdc) {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ obmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(g_theme->bg_main);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    // Toolbar band across the top.
    RECT topbar = {0, 0, cw, kTopBar};
    HBRUSH tbb = CreateSolidBrush(g_theme->bg_toolbar);
    FillRect(mem, &topbar, tbb);
    DeleteObject(tbb);

    // Right-side channels panel.
    RECT panel = {cw - kRightPanel, kTopBar, cw, ch - kBottomBar};
    HBRUSH pbg = CreateSolidBrush(g_theme->bg_panel);
    FillRect(mem, &panel, pbg);
    DeleteObject(pbg);

    // Hairline separators (under the toolbar, left of the panel, above status bar).
    HPEN sep = CreatePen(PS_SOLID, 1, g_theme->separator);
    HGDIOBJ oldpen = SelectObject(mem, sep);
    MoveToEx(mem, 0, kTopBar - 1, nullptr); LineTo(mem, cw, kTopBar - 1);
    MoveToEx(mem, cw - kRightPanel, kTopBar, nullptr); LineTo(mem, cw - kRightPanel, ch - kBottomBar);
    MoveToEx(mem, 0, ch - kBottomBar, nullptr); LineTo(mem, cw, ch - kBottomBar);
    SelectObject(mem, oldpen);
    DeleteObject(sep);

    // Toolbar vertical separators (two compact rows)
    HPEN vsep = CreatePen(PS_SOLID, 1, g_theme->separator);
    oldpen = SelectObject(mem, vsep);
    for (int sx : g.toolbar_seps) {
        MoveToEx(mem, sx, 8, nullptr); LineTo(mem, sx, kTopBar - 4);
    }
    SelectObject(mem, oldpen);
    DeleteObject(vsep);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, g_theme->text_primary);
    SetTextAlign(mem, TA_LEFT | TA_TOP);
    SelectObject(mem, g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    TextOutW(mem, cw - kRightPanel + 12, kTopBar + 10, g_str->panel_channels, lstrlenW(g_str->panel_channels));

    // Draw colored channel indicators next to checkboxes
    for (std::size_t i = 0; i < g.checks.size(); ++i) {
        HWND c = g.checks[i];
        if (!IsWindowVisible(c)) continue;
        RECT cr;
        GetWindowRect(c, &cr);
        MapWindowPoints(nullptr, g.main, (LPPOINT)&cr, 2);
        int sq_y = cr.top + (cr.bottom - cr.top - 10) / 2;
        int sq_x = cr.left - 16;
        HBRUSH sq = CreateSolidBrush(channel_color(i));
        RECT sr = {sq_x, sq_y, sq_x + 10, sq_y + 10};
        FillRect(mem, &sr, sq);
        DeleteObject(sq);
    }

    SelectObject(mem, g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));

    draw_chart(mem, plot_rect());

    // Status bar (owner-drawn)
    RECT sb = {0, ch - kBottomBar, cw, ch};
    HBRUSH sbb = CreateSolidBrush(g_theme->bg_status);
    FillRect(mem, &sb, sbb);
    DeleteObject(sbb);
    HPEN sb_top = CreatePen(PS_SOLID, 1, g_theme->separator);
    oldpen = SelectObject(mem, sb_top);
    MoveToEx(mem, 0, ch - kBottomBar, nullptr); LineTo(mem, cw, ch - kBottomBar);
    SelectObject(mem, oldpen);
    DeleteObject(sb_top);
    SetTextColor(mem, g_theme->text_secondary);
    SetTextAlign(mem, TA_LEFT | TA_TOP);
    SelectObject(mem, g.ui_font);
    if (!g.hover_status_text.empty()) {
        SetTextColor(mem, g_theme->accent);
        TextOutW(mem, 12, ch - kBottomBar + 5, g.hover_status_text.c_str(), static_cast<int>(g.hover_status_text.size()));
    } else if (!g.status_text.empty()) {
        TextOutW(mem, 12, ch - kBottomBar + 5, g.status_text.c_str(), static_cast<int>(g.status_text.size()));
    }

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
    HBRUSH bg = CreateSolidBrush(g_theme->bg_plot);
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
        out << g_str->csv_freq;
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
        out << g_str->csv_time;
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
    if (!has_data()) { MessageBoxW(g.main, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return; }
    std::wstring def = file_stem() + (g.freq_mode ? L"_spectrum.png" : L"_plot.png");
    std::wstring path;
    if (!save_dialog(path, g_str->filter_png, L"png", def)) return;
    if (save_png(path)) {
        const wchar_t* b = wcsrchr(path.c_str(), L'\\');
        status_msg(std::wstring(g_str->msg_saved_png) + (b ? b + 1 : path.c_str()));
    } else {
        MessageBoxW(g.main, g_str->msg_savepng_err, g_str->msg_error_title, MB_ICONERROR);
    }
}

void save_csv_dialog() {
    if (!has_data()) { MessageBoxW(g.main, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return; }
    std::wstring def = file_stem() + (g.freq_mode ? L"_spectrum.csv" : L"_segment.csv");
    std::wstring path;
    if (!save_dialog(path, g_str->filter_csv, L"csv", def)) return;
    if (save_csv(path)) {
        const wchar_t* b = wcsrchr(path.c_str(), L'\\');
        status_msg(std::wstring(g_str->msg_saved_csv) + (b ? b + 1 : path.c_str()));
    } else {
        MessageBoxW(g.main, g_str->msg_savecsv_err, g_str->msg_error_title, MB_ICONERROR);
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
    MessageBoxW(g.main, g_str->hk_title, g_str->dlg_hotkeys_title, MB_OK | MB_ICONINFORMATION);
}

void show_about() {
    MessageBoxW(g.main, g_str->about_body, g_str->dlg_about_title, MB_OK | MB_ICONINFORMATION);
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
    chk(IDC_AUTOY, g.auto_y);
    chk(IDM_THEME, g_theme == &kDarkTheme);
    ModifyMenuW(g.menu, IDM_THEME, MF_BYCOMMAND | MF_STRING, IDM_THEME,
                g_theme == &kDarkTheme ? g_str->theme_light : g_str->theme_dark);
}

HMENU make_menu() {
    HMENU bar = CreateMenu();

    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDC_OPEN, L"Открыть файл…\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDC_SAVEPNG, L"Сохранить PNG…\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDC_SAVECSV, L"Сохранить CSV…\tCtrl+E");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_UNDO, L"Отменить\tCtrl+Z");
    AppendMenuW(file, MF_STRING, IDM_REDO, L"Повторить\tCtrl+Shift+Z");
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
    AppendMenuW(view, MF_STRING, IDC_AUTOY, L"Auto Y");
    AppendMenuW(view, MF_STRING, IDM_VISMOOTH, L"Сглаживание\tC");
    AppendMenuW(view, MF_STRING, IDC_PLAY, L"Play / Pause\tПробел");
    AppendMenuW(view, MF_STRING, IDM_THEME, L"Тёмная тема\tT");
    HMENU speed = CreatePopupMenu();
    AppendMenuW(speed, MF_STRING, IDM_SPEED_00001, L"0.0001×");
    AppendMenuW(speed, MF_STRING, IDM_SPEED_0001, L"0.001×");
    AppendMenuW(speed, MF_STRING, IDM_SPEED_001, L"0.01×");
    AppendMenuW(speed, MF_STRING, IDM_SPEED_01, L"0.1×");
    AppendMenuW(speed, MF_STRING, IDM_SPEED_05, L"0.5×");
    AppendMenuW(speed, MF_STRING, IDM_SPEED_1, L"1×");
    AppendMenuW(speed, MF_STRING, IDM_SPEED_2, L"2×");
    AppendMenuW(speed, MF_STRING, IDM_SPEED_5, L"5×");
    AppendMenuW(speed, MF_STRING, IDM_SPEED_10, L"10×");
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(speed), L"Скорость");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"Вид");

    HMENU meas = CreatePopupMenu();
    AppendMenuW(meas, MF_STRING, IDC_MEASURE, L"Точки\tV");
    AppendMenuW(meas, MF_STRING, IDC_PTSETTINGS, L"Настройки…");
    AppendMenuW(meas, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(meas, MF_STRING, IDM_CLEAR_POINTS, L"Очистить\tDelete");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(meas), L"Точки");

    HMENU lines = CreatePopupMenu();
    AppendMenuW(lines, MF_STRING, IDM_ADD_VLINE, L"Вертикальная\tL");
    AppendMenuW(lines, MF_STRING, IDM_ADD_HLINE, L"Горизонтальная\tH");
    AppendMenuW(lines, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(lines, MF_STRING, IDM_CLEAR_LINES, L"Очистить");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(lines), L"Линии");

    HMENU markers = CreatePopupMenu();
    AppendMenuW(markers, MF_STRING, IDM_ADD_MARKER, L"Добавить\tK");
    AppendMenuW(markers, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(markers, MF_STRING, IDM_CLEAR_MARKERS, L"Очистить");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(markers), L"Маркеры");

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
                {g_str->pt_num,        IDM_PT_NUM,   g.pdisp.number},
                {g_str->pt_x,          IDM_PT_X,     g.pdisp.x},
                {g_str->pt_y,          IDM_PT_Y,     g.pdisp.y},
                {g_str->pt_dx,         IDM_PT_DX,    g.pdisp.dx},
                {g_str->pt_dy,         IDM_PT_DY,    g.pdisp.dy},
                {g_str->pt_invdt,      IDM_PT_INVDT, g.pdisp.inv_dt},
                {g_str->pt_dist,       IDM_PT_DIST,  g.pdisp.dist},
                {g_str->pt_snap,       IDM_SNAP,     g.snap_to_data},
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
            HWND col = CreateWindowExW(0, L"BUTTON", g_str->dlg_color,
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
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(g_theme->bg_panel);
            FillRect(dc, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
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
            WS_EX_TOOLWINDOW, L"LvmPtSettings", g_str->dlg_ptsettings_title,
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
                WS_CHILD | WS_VISIBLE | SS_LEFT, 32, 24, 520, 40, hwnd, nullptr, inst, nullptr);
            SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(g.title_font ? g.title_font : font), TRUE);
            HWND sub = CreateWindowExW(0, L"STATIC",
                L"Просмотрщик сигналов LabVIEW (.lvm / .txt)",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 32, 64, 520, 22, hwnd, nullptr, inst, nullptr);
            SendMessageW(sub, WM_SETFONT, reinterpret_cast<WPARAM>(g.bold_font ? g.bold_font : font), TRUE);
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
                WS_CHILD | WS_VISIBLE | SS_LEFT, 32, 94, 540, 200, hwnd, nullptr, inst, nullptr);
            SendMessageW(body, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            auto mkbtn = [&](const wchar_t* t, int id, int x, int w) {
                HWND b = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                         x, 340, w, 32, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            };
            mkbtn(L"Открыть файл", IDC_OPEN, 32, 120);
            mkbtn(L"Настройки точек…", IDC_PTSETTINGS, 158, 160);
            mkbtn(L"Горячие клавиши", IDM_HOTKEYS, 326, 150);
            mkbtn(L"Начать работу", IDW_START, 482, 110);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(g_theme->bg_main);
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);
            HPEN border = CreatePen(PS_SOLID, 1, g_theme->separator);
            HGDIOBJ oldp = SelectObject(hdc, border);
            HGDIOBJ oldb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, 12, 12, rc.right - 12, rc.bottom - 12, 8, 8);
            SelectObject(hdc, oldb);
            SelectObject(hdc, oldp);
            DeleteObject(border);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_OPEN: ShowWindow(hwnd, SW_HIDE); show_ui_controls(); SendMessageW(g.main, WM_COMMAND, IDC_OPEN, 0); return 0;
                case IDC_PTSETTINGS: open_settings(); return 0;
                case IDM_HOTKEYS: show_hotkeys(); return 0;
                case IDW_START: ShowWindow(hwnd, SW_HIDE); show_ui_controls(); return 0;
            }
            return 0;
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis->hwndItem) break;
            HDC dc = dis->hDC;
            RECT r = dis->rcItem;
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            bool is_toggle = (dis->hwndItem == g.measure || dis->hwndItem == g.autoy);
            bool active = is_toggle && (SendMessageW(dis->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED);
            COLORREF bg_col, border_col, text_col;
            if (active) {
                bg_col = g_theme->btn_pressed; border_col = g_theme->separator; text_col = g_theme->accent;
            } else if (pressed) {
                bg_col = g_theme->accent_hover; border_col = g_theme->accent_hover; text_col = RGB(255,255,255);
            } else {
                bg_col = g_theme->btn_bg; border_col = g_theme->btn_border; text_col = g_theme->text_primary;
            }
            HBRUSH bg = CreateSolidBrush(bg_col);
            HPEN border = CreatePen(PS_SOLID, 1, border_col);
            HGDIOBJ old_brush = SelectObject(dc, bg);
            HGDIOBJ old_pen = SelectObject(dc, border);
            RoundRect(dc, r.left, r.top, r.right, r.bottom, 4, 4);
            SelectObject(dc, old_pen);
            DeleteObject(border);
            SelectObject(dc, old_brush);
            DeleteObject(bg);
            // "Pressed" inset effect for active toggle buttons
            if (active) {
                HPEN inset = CreatePen(PS_SOLID, 2, RGB(0,0,0));
                HGDIOBJ old_ip = SelectObject(dc, inset);
                HGDIOBJ old_ib = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, r.left + 2, r.top + 2, r.right - 2, r.bottom - 2);
                SelectObject(dc, old_ib);
                SelectObject(dc, old_ip);
                DeleteObject(inset);
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, text_col);
            HFONT f = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            SelectObject(dc, f);
            wchar_t txt[128];
            GetWindowTextW(dis->hwndItem, txt, 128);
            SetTextAlign(dc, TA_CENTER | TA_TOP);
            SIZE sz;
            GetTextExtentPoint32W(dc, txt, lstrlenW(txt), &sz);
            int tx = (r.left + r.right) / 2;
            int ty = r.top + (r.bottom - r.top - sz.cy) / 2;
            if (active || pressed) { tx += 1; ty += 1; }
            TextOutW(dc, tx, ty, txt, lstrlenW(txt));
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_welcome_brush);
        }
        case WM_DESTROY:
            g.welcome_wnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void show_welcome(HINSTANCE inst) {
    if (g.welcome_wnd) {
        hide_ui_controls();
        ShowWindow(g.welcome_wnd, SW_SHOW);
        SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        UpdateWindow(g.welcome_wnd);
        return;
    }
    RECT rc;
    GetClientRect(g.main, &rc);
    g.welcome_wnd = CreateWindowExW(
        0,
        L"LvmWelcome", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, rc.right, rc.bottom,
        g.main, nullptr, inst, nullptr);
    if (!g.welcome_wnd) return;
    hide_ui_controls();
    ShowWindow(g.welcome_wnd, SW_SHOW);
    BringWindowToTop(g.welcome_wnd);
    SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    UpdateWindow(g.welcome_wnd);
}

// ---- UI rebuild (language switch) --------------------------------------
void rebuild_ui() {
    if (!g.main) return;
    // Update menu
    SetMenu(g.main, nullptr);
    if (g.menu) DestroyMenu(g.menu);
    g.menu = make_menu();
    SetMenu(g.main, g.menu);
    MENUINFO mi = { sizeof(mi) };
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
    SetMenuInfo(g.menu, &mi);
    
    // Update buttons
    SetWindowTextW(g.open, g_str->btn_open);
    SetWindowTextW(g.savepng, g_str->btn_png);
    SetWindowTextW(g.savecsv, g_str->btn_csv);
    SetWindowTextW(g.mode, g_str->btn_timehz);
    SetWindowTextW(g.play, g.playing ? g_str->btn_pause : g_str->btn_play);
    SetWindowTextW(g.measure, g_str->btn_measure);
    SetWindowTextW(g.reset, g_str->btn_reset);
    SetWindowTextW(g.autoy, g_str->btn_autoy);
    SetWindowTextW(g.ptsettings, g_str->btn_settings);
    
    // Update welcome window if visible
    if (g.welcome_wnd && IsWindowVisible(g.welcome_wnd)) {
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        DestroyWindow(g.welcome_wnd);
        g.welcome_wnd = nullptr;
        show_welcome(inst);
    }
    
    // Update settings window title
    if (g.settings_wnd) SetWindowTextW(g.settings_wnd, g_str->dlg_ptsettings_title);
    
    // Update main window title
    if (!g.file_name.empty()) {
        SetWindowTextW(g.main, (std::wstring(g_str->app_title) + L" — " + g.file_name).c_str());
    } else {
        SetWindowTextW(g.main, g_str->app_title);
    }
    
    InvalidateRect(g.main, nullptr, TRUE);
    set_status();
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
            MENUINFO mi = { sizeof(mi) };
            mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
            mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
            SetMenuInfo(g.menu, &mi);

            auto mk = [&](const wchar_t* text, int id, DWORD extra) {
                HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | extra,
                                         0, 0, 10, 10, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                g.buttons.push_back(b);
                return b;
            };
            g.open = mk(g_str->btn_open, IDC_OPEN, 0);
            g.savepng = mk(g_str->btn_png, IDC_SAVEPNG, 0);
            g.savecsv = mk(g_str->btn_csv, IDC_SAVECSV, 0);
            g.mode = mk(g_str->btn_timehz, IDC_MODE, 0);
            g.play = mk(g_str->btn_play, IDC_PLAY, 0);
            g.measure = mk(g_str->btn_measure, IDC_MEASURE, 0);
            g.reset = mk(g_str->btn_reset, IDC_RESET, 0);
            g.autoy = mk(g_str->btn_autoy, IDC_AUTOY, 0);
            g.ptsettings = mk(g_str->btn_settings, IDC_PTSETTINGS, 0);

            g.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                       0, 0, 10, 10, hwnd, nullptr, inst, nullptr);
            SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            ShowWindow(g.status, SW_HIDE);   // owner-drawn in on_paint

            g.axis_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            SetTimer(hwnd, 2, 50, nullptr);   // hover tracking timer
            update_theme_brushes();
            sync_menu();
            set_status();
            return 0;
        }
        case WM_SIZE:
            layout();
            if (g.welcome_wnd && IsWindowVisible(g.welcome_wnd)) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                MoveWindow(g.welcome_wnd, 0, 0, rc.right, rc.bottom, TRUE);
                SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
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
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            SelectObject(dc, g.ui_font);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            on_paint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            HWND btn = dis->hwndItem;
            if (!btn) break;
            HDC dc = dis->hDC;
            RECT r = dis->rcItem;
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            bool is_toggle = (btn == g.measure || btn == g.autoy);
            bool active = is_toggle && (SendMessageW(btn, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool hover = (btn == g.hovered_btn);

            COLORREF bg_col, border_col, text_col;
            if (active) {
                bg_col = g_theme->btn_pressed;
                border_col = g_theme->separator;
                text_col = g_theme->accent;
            } else if (pressed) {
                bg_col = g_theme->accent_hover;
                border_col = g_theme->accent_hover;
                text_col = RGB(255, 255, 255);
            } else if (hover) {
                bg_col = g_theme->btn_hover;
                border_col = g_theme->btn_border;
                text_col = g_theme->text_primary;
            } else {
                bg_col = g_theme->btn_bg;
                border_col = g_theme->btn_border;
                text_col = g_theme->text_primary;
            }

            HBRUSH bg = CreateSolidBrush(bg_col);
            HPEN border = CreatePen(PS_SOLID, 1, border_col);
            HGDIOBJ old_brush = SelectObject(dc, bg);
            HGDIOBJ old_pen = SelectObject(dc, border);
            RoundRect(dc, r.left, r.top, r.right, r.bottom, 4, 4);
            SelectObject(dc, old_pen);
            DeleteObject(border);
            SelectObject(dc, old_brush);
            DeleteObject(bg);

            // "Pressed" inset effect for active toggle buttons
            if (active) {
                HPEN inset = CreatePen(PS_SOLID, 2, RGB(0,0,0));
                HGDIOBJ old_ip = SelectObject(dc, inset);
                HGDIOBJ old_ib = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, r.left + 2, r.top + 2, r.right - 2, r.bottom - 2);
                SelectObject(dc, old_ib);
                SelectObject(dc, old_ip);
                DeleteObject(inset);
            }

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, text_col);
            HFONT f = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            SelectObject(dc, f);
            wchar_t txt[128];
            GetWindowTextW(btn, txt, 128);
            SetTextAlign(dc, TA_CENTER | TA_TOP);
            SIZE sz;
            GetTextExtentPoint32W(dc, txt, lstrlenW(txt), &sz);
            int tx = (r.left + r.right) / 2;
            int ty = r.top + (r.bottom - r.top - sz.cy) / 2;
            if (active || pressed) { tx += 1; ty += 1; }
            TextOutW(dc, tx, ty, txt, lstrlenW(txt));
            return TRUE;
        }
        case WM_TIMER:
            if (LOWORD(wp) == 2) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                HWND new_hover = nullptr;
                for (HWND h : g.buttons) {
                    RECT r;
                    GetWindowRect(h, &r);
                    MapWindowPoints(nullptr, hwnd, (LPPOINT)&r, 2);
                    if (PtInRect(&r, pt)) { new_hover = h; break; }
                }
                if (new_hover != g.hovered_btn) {
                    if (g.hovered_btn) InvalidateRect(g.hovered_btn, nullptr, FALSE);
                    if (new_hover) InvalidateRect(new_hover, nullptr, FALSE);
                    g.hovered_btn = new_hover;
                    // Update hover status description and invalidate status bar
                    g.hover_status_text.clear();
                    if (new_hover == g.open) g.hover_status_text = g_str->hover_open;
                    else if (new_hover == g.savepng) g.hover_status_text = g_str->hover_png;
                    else if (new_hover == g.savecsv) g.hover_status_text = g_str->hover_csv;
                    else if (new_hover == g.mode) g.hover_status_text = g_str->hover_timehz;
                    else if (new_hover == g.play) g.hover_status_text = g.playing ? g_str->hover_pause : g_str->hover_play;
                    else if (new_hover == g.measure) g.hover_status_text = g_str->hover_measure;
                    else if (new_hover == g.reset) g.hover_status_text = g_str->hover_reset;
                    else if (new_hover == g.autoy) g.hover_status_text = g_str->hover_autoy;
                    else if (new_hover == g.ptsettings) g.hover_status_text = g_str->hover_settings;
                    RECT rc; GetClientRect(hwnd, &rc);
                    RECT sr = {0, rc.bottom - kBottomBar, rc.right, rc.bottom};
                    InvalidateRect(hwnd, &sr, FALSE);
                }
                return 0;
            }
            if (g.playing && !g.freq_mode && has_data()) {
                // Real-time playhead: 1 s of signal per 1 s of wall-clock time.
                LARGE_INTEGER now, freq;
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&freq);
                const double elapsed =
                    static_cast<double>(now.QuadPart - g.play_anchor_qpc.QuadPart) /
                    static_cast<double>(freq.QuadPart);
                g.playhead = g.play_anchor_data + elapsed * g.play_speed;
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
                RECT pr = plot_rect();
                RECT rc; GetClientRect(hwnd, &rc);
                pr.bottom = rc.bottom; // include status bar
                InvalidateRect(hwnd, &pr, FALSE);
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
                    InvalidateRect(g.measure, nullptr, FALSE);
                    sync_menu();
                    set_status();
                    return 0;
                case IDC_PTSETTINGS: open_settings(); return 0;
                case IDC_AUTOY:
                    g.auto_y = !g.auto_y;
                    if (!g.auto_y) current_time_yrange(g.y_lock_min, g.y_lock_max);
                    SendMessageW(g.autoy, BM_SETCHECK,
                                 g.auto_y ? BST_CHECKED : BST_UNCHECKED, 0);
                    InvalidateRect(g.autoy, nullptr, FALSE);
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
                case IDM_THEME:
                    g_theme = (g_theme == &kLightTheme) ? &kDarkTheme : &kLightTheme;
                    update_theme_brushes();
                    {
                        MENUINFO mi = { sizeof(mi) };
                        mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
                        mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
                        SetMenuInfo(g.menu, &mi);
                    }
                    sync_menu();
                    if (g.welcome_wnd) InvalidateRect(g.welcome_wnd, nullptr, TRUE);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDM_ADD_VLINE:
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    g.pending_line = 1;
                    g.pending_marker = false;
                    status_msg(g_str->status_vline);
                    return 0;
                case IDM_ADD_HLINE:
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    g.pending_line = 2;
                    g.pending_marker = false;
                    status_msg(g_str->status_hline);
                    return 0;
                case IDM_CLEAR_LINES:
                    if (!g.guides.empty()) {
                        UndoAction ua; ua.type = UndoAction::CLEAR_LINES; ua.saved_lines = g.guides;
                        push_undo(ua);
                        g.guides.clear(); InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    set_status();
                    return 0;
                case IDM_CLEAR_POINTS:
                    if (!g.points.empty()) {
                        UndoAction ua; ua.type = UndoAction::CLEAR_POINTS; ua.saved_points = g.points;
                        push_undo(ua);
                        g.points.clear(); InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    set_status();
                    return 0;
                case IDM_ADD_MARKER:
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    g.pending_marker = true;
                    g.pending_line = 0;
                    status_msg(g_str->status_marker);
                    return 0;
                case IDM_CLEAR_MARKERS:
                    if (!g.markers.empty()) {
                        UndoAction ua; ua.type = UndoAction::CLEAR_MARKERS; ua.saved_markers = g.markers;
                        push_undo(ua);
                        g.markers.clear(); InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    set_status();
                    return 0;
                case IDM_SPEED_00001: g.play_speed = 0.0001; set_status(); return 0;
                case IDM_SPEED_0001: g.play_speed = 0.001; set_status(); return 0;
                case IDM_SPEED_001: g.play_speed = 0.01; set_status(); return 0;
                case IDM_SPEED_01: g.play_speed = 0.1; set_status(); return 0;
                case IDM_SPEED_05: g.play_speed = 0.5; set_status(); return 0;
                case IDM_SPEED_1: g.play_speed = 1.0; set_status(); return 0;
                case IDM_SPEED_2: g.play_speed = 2.0; set_status(); return 0;
                case IDM_SPEED_5: g.play_speed = 5.0; set_status(); return 0;
                case IDM_SPEED_10: g.play_speed = 10.0; set_status(); return 0;
                case IDM_UNDO:
                    pop_undo();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    set_status();
                    return 0;
                case IDM_REDO:
                    pop_redo();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    set_status();
                    return 0;
                case IDC_ZOOMIN: zoom_at(0.5, 0.7); return 0;
                case IDC_ZOOMOUT: zoom_at(0.5, 1.0 / 0.7); return 0;
                case IDC_RESET: reset_view(); return 0;
                case IDC_PANLEFT: pan_by(-0.2); return 0;
                case IDC_PANRIGHT: pan_by(0.2); return 0;
                case IDM_HOTKEYS: show_hotkeys(); return 0;
                case IDM_ABOUT: show_about(); return 0;
                case IDM_LANG_RU: g_str = &kRu; rebuild_ui(); return 0;
                case IDM_LANG_EN: g_str = &kEn; rebuild_ui(); return 0;
                default: break;
            }
            if (id >= IDC_CHAN_BASE && id < IDC_CHAN_BASE + static_cast<int>(g.visible.size())) {
                const int ci = id - IDC_CHAN_BASE;
                g.visible[ci] = (SendMessageW(g.checks[ci], BM_GETCHECK, 0, 0) == BST_CHECKED);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        case WM_SETCURSOR: {
            if (reinterpret_cast<HWND>(wp) == hwnd) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                RECT p = plot_rect();
                bool in_plot = (pt.x >= p.left && pt.x <= p.right && pt.y >= p.top && pt.y <= p.bottom);
                if (g.dragging || g.measure_mode || g.pending_line || g.pending_marker) {
                    SetCursor(LoadCursor(nullptr, IDC_CROSS));
                    return TRUE;
                }
                if (in_plot) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            if (!has_data()) return 0;
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            const RECT p = plot_rect();
            bool in_plot = (pt.x >= p.left && pt.x <= p.right && pt.y >= p.top && pt.y <= p.bottom);
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            bool up = GET_WHEEL_DELTA_WPARAM(wp) > 0;

            if (shift) {
                pan_by(up ? -0.1 : 0.1);
                return 0;
            }
            if (ctrl) {
                if (g.freq_mode) {
                    if (in_plot) {
                        double frac = static_cast<double>(p.bottom - pt.y) / (p.bottom - p.top);
                        zoom_y_amp_at(frac, up ? 0.85 : 1.0 / 0.85);
                    } else {
                        zoom_y_amp_at(0.5, up ? 0.85 : 1.0 / 0.85);
                    }
                } else {
                    if (in_plot) {
                        double frac = static_cast<double>(p.bottom - pt.y) / (p.bottom - p.top);
                        zoom_y_at(frac, up ? 0.85 : 1.0 / 0.85);
                    } else {
                        zoom_y_at(0.5, up ? 0.85 : 1.0 / 0.85);
                    }
                }
                return 0;
            }
            if (alt) {
                double frac = 0.5;
                if (pt.x >= p.left && pt.x <= p.right)
                    frac = static_cast<double>(pt.x - p.left) / (p.right - p.left);
                zoom_at(frac, up ? 0.95 : 1.0 / 0.95);
                return 0;
            }
            double frac = 0.5;
            if (pt.x >= p.left && pt.x <= p.right)
                frac = static_cast<double>(pt.x - p.left) / (p.right - p.left);
            zoom_at(frac, up ? 0.8 : 1.25);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!has_data()) return 0;
            const RECT p = plot_rect();
            const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);

            // --- Legend click handling (toggle / solo) ---
            if (mx >= g_legend_box.left && mx < g_legend_box.right &&
                my >= g_legend_box.top && my < g_legend_box.bottom) {
                for (const auto& li : g_legend_items) {
                    if (mx >= li.rect.left && mx < li.rect.right &&
                        my >= li.rect.top && my < li.rect.bottom) {
                        const int ci = li.channel;
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                            // Solo: show only this channel
                            for (std::size_t j = 0; j < g.visible.size(); ++j) {
                                g.visible[j] = (static_cast<int>(j) == ci);
                                if (j < g.checks.size())
                                    SendMessageW(g.checks[j], BM_SETCHECK,
                                        g.visible[j] ? BST_CHECKED : BST_UNCHECKED, 0);
                            }
                        } else {
                            // Toggle
                            g.visible[ci] = !g.visible[ci];
                            if (ci < static_cast<int>(g.checks.size()))
                                SendMessageW(g.checks[ci], BM_SETCHECK,
                                    g.visible[ci] ? BST_CHECKED : BST_UNCHECKED, 0);
                        }
                        InvalidateRect(hwnd, nullptr, TRUE);
                        return 0;
                    }
                }
            }

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
                    UndoAction ua; ua.type = UndoAction::ADD_LINE; ua.line = gl;
                    push_undo(ua);
                }
                // Режим множественного добавления: остаётся активным до Esc
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g.pending_marker) {
                double dx, dy;
                if (px_to_data(mx, my, dx, dy)) {
                    App::Marker mk;
                    mk.x = dx;
                    mk.freq = g.freq_mode;
                    wchar_t buf[16];
                    swprintf(buf, 16, L"M%zu", g.markers.size() + 1);
                    mk.label = buf;
                    g.markers.push_back(mk);
                    UndoAction ua; ua.type = UndoAction::ADD_MARKER; ua.marker = mk;
                    push_undo(ua);
                }
                g.pending_marker = false;
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g.measure_mode) {
                double dx, dy;
                if (px_to_data(mx, my, dx, dy)) {
                    if (g.snap_to_data) snap_to_nearest(dx, dy);
                    g.points.push_back({dx, dy});
                    UndoAction ua; ua.type = UndoAction::ADD_POINT; ua.point = {dx, dy};
                    push_undo(ua);
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
            if (!g.points.empty()) {
                UndoAction ua; ua.type = UndoAction::CLEAR_POINTS; ua.saved_points = g.points;
                push_undo(ua);
                g.points.clear(); set_status(); InvalidateRect(hwnd, nullptr, FALSE);
            }
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
            if (wp == VK_ESCAPE && (g.pending_line || g.pending_marker)) {
                g.pending_line = 0;
                g.pending_marker = false;
                set_status();
                return 0;
            }
            break;
        case WM_DESTROY:
            stop_play();
            KillTimer(hwnd, 2);
            if (g.ui_font && g.ui_font != reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
                DeleteObject(g.ui_font);
            if (g.bold_font) DeleteObject(g.bold_font);
            if (g.title_font) DeleteObject(g.title_font);
            if (g.axis_font) DeleteObject(g.axis_font);
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
        {FVIRTKEY, 'T', IDM_THEME},
        {FVIRTKEY, 'L', IDM_ADD_VLINE},
        {FVIRTKEY, 'H', IDM_ADD_HLINE},
        {FVIRTKEY, 'K', IDM_ADD_MARKER},
        {FVIRTKEY, VK_DELETE, IDM_CLEAR_POINTS},
        {FVIRTKEY | FCONTROL, 'Z', IDM_UNDO},
        {FVIRTKEY | FCONTROL | FSHIFT, 'Z', IDM_REDO},
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
    wcw.hbrBackground = nullptr;
    wcw.lpszClassName = L"LvmWelcome";
    wcw.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wcw);

    g.main = CreateWindowExW(0, wc.lpszClassName, g_str->app_title,
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720,
                             nullptr, nullptr, inst, nullptr);
    if (!g.main) return 1;

    ShowWindow(g.main, show);
    UpdateWindow(g.main);

    if (cmd && *cmd) {
        std::wstring path = cmd;
        if (!path.empty() && path.front() == L'"') path = path.substr(1, path.find_last_of(L'"') - 1);
        if (!load_path(path))
            MessageBoxW(g.main, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
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
