// LVM Viewer вЂ” native Win32 GUI front end.
//
// Self-contained desktop viewer (no external GUI toolkit). Features:
//   - Main menu bar (Р¤Р°Р№Р» / Р’РёРґ / РР·РјРµСЂРµРЅРёСЏ / Р›РёРЅРёРё / РЎРїСЂР°РІРєР°) + toolbar.
//   - Time plot and FFT (Hz) spectrum, with zoom/pan on both axes.
//   - Channel show/hide, colored legend, min/max envelope for dense data.
//   - Measure tool with a settings menu: choose which read-outs to show
//     (X, Y, О”x, О”y, 1/О”t, distance, point number) and optionally snap
//     markers to the nearest real data sample ("примагничивание").
//   - Reference guide lines: add vertical / horizontal lines on the plot.
//   - Two independent smoothing controls: a moving-average filter (changes
//     the rendered values) and a purely visual Catmull-Rom spline that
//     curves between samples without moving the underlying data points.
//   - Playback / pause that sweeps a playhead through the time signal.
//   - Export the visible segment to PNG (GDI+) or CSV.
//   - Keyboard shortcuts (see РЎРїСЂР°РІРєР° в†’ Р“РѕСЂСЏС‡РёРµ РєР»Р°РІРёС€Рё / F1).
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
#include <shellapi.h>

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
    IDC_SAVETXT,
    IDC_MODE,
    IDC_PLAY,
    IDC_MEASURE,
    IDC_ZOOMIN,
    IDC_ZOOMOUT,
    IDC_RESET,
    IDC_PANLEFT,
    IDC_PANRIGHT,
    IDC_GOTO_START,
    IDC_GOTO_END,
    IDC_AUTOY,          // toolbar: auto-fit vertical scale (was lock_y)
    IDC_PTSETTINGS,     // toolbar: open the measurement-point settings panel
    IDC_SHOW_ALL,
    IDC_HIDE_ALL,

    // Menu-only commands (no toolbar button).
    IDM_EXIT = 1100,
    IDM_VISMOOTH,       // visual (spline) smoothing toggle
    IDM_VPAN,           // vertical pan toggle
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
    IDM_SPEED_CUSTOM,

    IDM_UNDO = 1400,
    IDM_REDO,
    IDM_THEME = 1500,
    IDM_LANG_RU = 1600,
    IDM_LANG_EN = 1601,
    IDM_MODE_TIME = 1700,
    IDM_MODE_FREQ,

    IDC_CHAN_BASE = 2000,
    IDC_CHAN_LABEL_BASE = 3000,
    IDC_CHAN_EDIT = 4000,

    IDC_SET_LANG_RU = 5000,
    IDC_SET_LANG_EN,
    IDC_SET_HOTKEY_LIST,
    IDC_SET_HOTKEY_CTRL,
    IDC_SET_HOTKEY_SHIFT,
    IDC_SET_HOTKEY_ALT,
    IDC_SET_HOTKEY_KEY,
    IDC_SET_HOTKEY_APPLY,
    IDC_SET_HOTKEY_RESET,
    IDC_SET_HOTKEY_CLEAR,

    IDC_SET_TRANSFORM_LIST = 5100,
    IDC_SET_GLOBAL_MUL,
    IDC_SET_GLOBAL_ADD,
    IDC_SET_CHANNEL_MUL,
    IDC_SET_CHANNEL_ADD,
    IDC_SET_TRANSFORM_APPLY,
    IDC_SET_TRANSFORM_RESET_CHANNEL,
    IDC_SET_TRANSFORM_RESET_ALL,

    IDW_TITLE = 5200,
    IDW_SUBTITLE,
    IDW_INTRO,
    IDW_FEATURES,
    IDW_ACTIONS_TITLE,
    IDW_ACTIONS_HINT,
    IDW_LANG_LABEL,
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

struct SpeedPromptState {
    HWND wnd = nullptr;
    HWND edit = nullptr;
    bool done = false;
    bool accepted = false;
    double value = 1.0;
};

SpeedPromptState g_speed_prompt;

void update_theme_brushes() {
    if (g_panel_brush) DeleteObject(g_panel_brush);
    g_panel_brush = CreateSolidBrush(g_theme->bg_panel);
    if (g_welcome_brush) DeleteObject(g_welcome_brush);
    g_welcome_brush = CreateSolidBrush(g_theme->bg_main);
}

const int IDC_SPEED_PROMPT_EDIT = 6200;
const int IDC_SPEED_PROMPT_OK = 6201;
const int IDC_SPEED_PROMPT_CANCEL = 6202;

// ---- string table --------------------------------------------------------
struct Strings {
    const wchar_t* app_title;
    const wchar_t* menu_file; const wchar_t* menu_view; const wchar_t* menu_meas; const wchar_t* menu_lines; const wchar_t* menu_markers; const wchar_t* menu_help;
    const wchar_t* m_open; const wchar_t* m_savepng; const wchar_t* m_savecsv; const wchar_t* m_undo; const wchar_t* m_redo; const wchar_t* m_exit;
    const wchar_t* m_timehz; const wchar_t* m_zoomin; const wchar_t* m_zoomout; const wchar_t* m_reset; const wchar_t* m_autoy; const wchar_t* m_smooth; const wchar_t* m_vpan; const wchar_t* m_play; const wchar_t* m_theme; const wchar_t* m_speed;
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
    L"Время / Гц\tM", L"Увеличить\t+", L"Уменьшить\t−", L"Сбросить вид\tHome", L"Авто масштабирование", L"Сглаживание\tC", L"Вертикальное панорамирование\tP", L"Play / Pause\tПробел", L"Тёмная тема\tT", L"Скорость",
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
    L"РљР°Рє СЂР°Р±РѕС‚Р°С‚СЊ СЃ РїСЂРёР»РѕР¶РµРЅРёРµРј:\r   вЂў  В«РћС‚РєСЂС‹С‚СЊ С„Р°Р№Р»В» (O) вЂ” Р·Р°РіСЂСѓР·РёС‚Рµ .lvm РёР»Рё .txt.\r   вЂў  В«Р’СЂРµРјСЏ / Р“С†В» (M) вЂ” РіСЂР°С„РёРє СЃРёРіРЅР°Р»Р° РёР»Рё РµРіРѕ СЃРїРµРєС‚СЂ (Р‘РџР¤).\r   вЂў  В«РР·РјРµСЂРµРЅРёРµВ» (V) вЂ” РєР»РёРєР°Р№С‚Рµ С‚РѕС‡РєРё РЅР° РіСЂР°С„РёРєРµ. Р§С‚Рѕ РїРѕРєР°Р·С‹РІР°С‚СЊ\r       Сѓ С‚РѕС‡РµРє Рё РїСЂРёРјР°РіРЅРёС‡РёРІР°РЅРёРµ вЂ” РІ РѕРєРЅРµ В«РќР°СЃС‚СЂРѕР№РєРё С‚РѕС‡РµРєВ».\r   вЂў  РљРѕР»РµСЃРѕ РјС‹С€Рё вЂ” РјР°СЃС€С‚Р°Р±, С‚СЏРіР° Р›РљРњ вЂ” РїСЂРѕРєСЂСѓС‚РєР° РїРѕ РІСЂРµРјРµРЅРё.\r   вЂў  В«Р¤РёРєСЃ. YВ» вЂ” Р·Р°С„РёРєСЃРёСЂРѕРІР°С‚СЊ РјР°СЃС€С‚Р°Р± РїРѕ РІС‹СЃРѕС‚Рµ.\r   вЂў  РџСЂРѕР±РµР» вЂ” РІРѕСЃРїСЂРѕРёР·РІРµРґРµРЅРёРµ РІ СЂРµР°Р»СЊРЅРѕРј РІСЂРµРјРµРЅРё (1 СЃ = 1 СЃ).\r   вЂў  F1 вЂ” РїРѕР»РЅС‹Р№ СЃРїРёСЃРѕРє РіРѕСЂСЏС‡РёС… РєР»Р°РІРёС€.",
    L"Открыть файл", L"Настройки точек…", L"Горячие клавиши", L"Начать работу",
    L"Файлы\n  O / Ctrl+O\t— Открыть\n  S / Ctrl+S\t— PNG\n  E / Ctrl+E\t— CSV\n  Ctrl+Z\t— Отменить\n  Ctrl+Shift+Z\t— Повторить\n\nВид\n  M\t— Время/Гц\n  C\t— Сглаживание\n  + / ↑\t— Увеличить\n  − / ↓\t— Уменьшить\n  ← / →\t— Сдвиг влево/вправо\n  Home\t— Сброс\n  Ctrl+Home\t— В начало\n  Ctrl+End\t— В конец\n  Пробел\t— Play / Pause\n\nЛинии и маркеры\n  L\t— Вертикальная линия\n  H\t— Горизонтальная линия\n  K\t— Маркер\n  Esc\t— Отменить добавление\n\nТочки\n  V\t— Режим точек вкл/выкл\n  Delete\t— Очистить точки\n\nМышь\n  Колесо\t— Масштаб под курсором\n  Shift+колесо\t— Прокрутка влево/вправо\n  Ctrl+колесо\t— Масштаб по высоте (Y)\n  Alt+колесо\t— Сдвиг вверх/вниз (Y)\n  ЛКМ + тяга\t— Панорамирование (вкл/выкл вертикальное через Вид)\n  ЛКМ\t— Поставить точку / линию / маркер (в режиме)\n  ПКМ\t— Очистить точки\n\n  F1\t— Эта справка",
    L"LVM Viewer — просмотрщик сигналов LabVIEW (.lvm / .txt)\n\nНативное приложение Win32 + GDI/GDI+, без внешних\nзависимостей и без Qt. Время и спектр (БПФ), измерения\nс примагничиванием, направляющие линии, визуальное\nсглаживание, экспорт PNG/CSV.\n\nСборка: build_gui.ps1 (MinGW g++) или make gui.",
    L"Открыть файл…", L"Сохранить PNG", L"Сохранить CSV", L"Переключить Время / Гц", L"Воспроизведение", L"Пауза", L"Режим измерения точек", L"Сбросить вид", L"Авто масштабирование", L"Настройки точек",
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
    L"Time / Hz\tM", L"Zoom in\t+", L"Zoom out\t−", L"Reset view\tHome", L"Auto zoom", L"Smoothing\tC", L"Vertical pan\tP", L"Play / Pause\tSpace", L"Dark theme\tT", L"Speed",
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
    L"Files\n  O / Ctrl+O\t— Open\n  S / Ctrl+S\t— PNG\n  E / Ctrl+E\t— CSV\n  Ctrl+Z\t— Undo\n  Ctrl+Shift+Z\t— Redo\n\nView\n  M\t— Time / Hz\n  C\t— Smoothing\n  + / ↑\t— Zoom in\n  − / ↓\t— Zoom out\n  ← / →\t— Pan left / right\n  Home\t— Reset view\n  Ctrl+Home\t— Go to start\n  Ctrl+End\t— Go to end\n  Space\t— Play / Pause\n\nLines and markers\n  L\t— Vertical line\n  H\t— Horizontal line\n  K\t— Marker\n  Esc\t— Cancel adding\n\nPoints\n  V\t— Measure mode on/off\n  Delete\t— Clear points\n\nMouse\n  Wheel\t— Zoom under cursor\n  Shift+wheel\t— Pan left / right\n  Ctrl+wheel\t— Zoom Y\n  Alt+wheel\t— Pan up/down (Y)\n  Left-drag\t— Pan (toggle vertical via View)\n  Left-click\t— Drop point / line / marker (in mode)\n  Right-click\t— Clear points\n\n  F1\t— This help",
    L"LVM Viewer — LabVIEW signal viewer (.lvm / .txt)\n\nNative Win32 + GDI/GDI+ application, no external\ndependencies, no Qt. Time and spectrum (FFT), measurements\nwith snapping, guide lines, visual smoothing, PNG/CSV export.\n\nBuild: build_gui.ps1 (MinGW g++) or make gui.",
    L"Open file…", L"Save PNG", L"Save CSV", L"Toggle Time / Hz", L"Playback", L"Pause", L"Measurement point mode", L"Reset view", L"Auto zoom", L"Point settings",
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
// "РР·РјРµСЂРµРЅРёСЏ в†’ РћС‚РѕР±СЂР°Р¶Р°С‚СЊ Сѓ С‚РѕС‡РµРє" menu).
struct PointDisplay {
    bool number = true;   // #1, #2, вЂ¦
    bool x = true;        // x coordinate
    bool y = true;        // y coordinate
    bool dx = true;       // О”x to the previous point
    bool dy = true;       // О”y to the previous point
    bool inv_dt = true;   // 1/О”t (Hz) вЂ” time mode only
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

struct HotkeyBinding {
    int command = 0;
    BYTE fvirt = FVIRTKEY;
    WORD key = 0;
};

struct App {
    lvm::Dataset ds;
    std::vector<char> visible;
    std::vector<std::wstring> channel_labels;  // user-editable display names
    double global_value_mul = 1.0;
    double global_value_add = 0.0;
    std::vector<double> channel_value_mul;
    std::vector<double> channel_value_add;
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
    std::vector<HotkeyBinding> hotkeys;

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

    struct Marker {
        double x = 0.0;
        double y = 0.0;
        std::wstring label;
        bool freq = false;
        bool snapped = false;
        int channel = -1;
    };
    std::vector<Marker> markers;
    bool pending_marker = false;
    int active_marker = -1;

    std::wstring file_name;
    std::string last_error;

    HWND main = nullptr;
    HWND open = nullptr, savepng = nullptr, savecsv = nullptr;
    HWND mode_time = nullptr, mode_freq = nullptr;
    HWND play = nullptr, measure = nullptr, marker_btn = nullptr;
    HWND vline_btn = nullptr, hline_btn = nullptr;
    HWND reset = nullptr, autoy = nullptr, ptsettings = nullptr;
    HWND show_all_btn = nullptr, hide_all_btn = nullptr;
    HWND status = nullptr;
    std::vector<HWND> checks;
    std::vector<HWND> check_labels;
    HWND channel_edit = nullptr;
    int editing_channel = -1;
    std::vector<HWND> buttons;   // owner-drawn toolbar buttons
    HWND hovered_btn = nullptr;
    std::wstring status_text;
    std::wstring status_detail_text;
    COLORREF status_detail_color = RGB(0, 0, 0);
    std::wstring hover_status_text;  // shown in status bar when hovering toolbar buttons
    std::vector<int> toolbar_seps;

    HWND settings_wnd = nullptr; // measurement-point settings panel (modeless)
    HWND welcome_wnd = nullptr;  // start screen

    HMENU menu = nullptr;        // main menu bar
    HACCEL accel = nullptr;      // current accelerator table (rebuilt from hotkeys)
    HFONT ui_font = nullptr;     // Segoe UI for controls / labels
    HFONT bold_font = nullptr;   // semibold for headings
    HFONT title_font = nullptr;  // large font for the welcome title
    HFONT axis_font = nullptr;   // 11px for axis tick labels
    // icon_font removed вЂ” toolbar now uses text labels with ui_font

    bool dragging = false;
    int drag_x = 0, drag_y = 0;
    double drag_lo = 0.0, drag_hi = 0.0;
    double drag_y_lo = 0.0, drag_y_hi = 0.0;

    bool fft_window_active = false;
    double fft_window_start = 0.0, fft_window_end = 0.0;
    bool fft_selecting = false;
    int fft_select_anchor_x = 0, fft_select_current_x = 0;
    double fft_select_anchor_t = 0.0, fft_select_current_t = 0.0;

    double spec_source_start = 0.0, spec_source_end = 0.0;
    bool spec_source_from_selection = false;
    bool spec_source_valid = false;

    bool vertical_pan = true;  // enable vertical panning with left-drag
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
WNDPROC g_channel_edit_proc = nullptr;

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

bool marker_status_detail(std::wstring& text, COLORREF& color) {
    if (g.active_marker < 0 || g.active_marker >= static_cast<int>(g.markers.size())) return false;
    const App::Marker& m = g.markers[static_cast<std::size_t>(g.active_marker)];
    if (m.freq != g.freq_mode || !m.snapped || m.channel < 0) return false;
    color = channel_color(static_cast<std::size_t>(m.channel));
    wchar_t buf[160];
    if (g.freq_mode) {
        if (g_str == &kEn) swprintf(buf, 160, L"   |   %ls: f=%.6g Hz, amp=%.6g", m.label.c_str(), m.x, m.y);
        else swprintf(buf, 160, L"   |   %ls: f=%.6g Гц, amp=%.6g", m.label.c_str(), m.x, m.y);
    } else {
        if (g_str == &kEn) swprintf(buf, 160, L"   |   %ls: t=%.6g s, y=%.6g", m.label.c_str(), m.x, m.y);
        else swprintf(buf, 160, L"   |   %ls: t=%.6g c, y=%.6g", m.label.c_str(), m.x, m.y);
    }
    text = buf;
    return true;
}

bool has_fft_window() {
    return g.fft_window_active && g.fft_window_end > g.fft_window_start;
}

void clear_fft_window() {
    g.fft_window_active = false;
    g.fft_window_start = 0.0;
    g.fft_window_end = 0.0;
}

void clamp_time_window(double& start, double& end) {
    if (start > end) std::swap(start, end);
    start = std::max(start, g.data_t0);
    end = std::min(end, g.data_t1);
}

void set_fft_window(double start, double end) {
    clamp_time_window(start, end);
    if (end <= start) {
        clear_fft_window();
        return;
    }
    g.fft_window_active = true;
    g.fft_window_start = start;
    g.fft_window_end = end;
}

bool current_fft_source_window(double& start, double& end, bool& from_selection) {
    if (!has_data()) return false;
    if (has_fft_window()) {
        start = g.fft_window_start;
        end = g.fft_window_end;
        from_selection = true;
        return true;
    }
    start = g.win_start;
    end = g.win_end;
    clamp_time_window(start, end);
    from_selection = false;
    return end > start;
}

bool last_fft_source_window(double& start, double& end, bool& from_selection) {
    if (!g.spec_source_valid || g.spec_source_end <= g.spec_source_start) return false;
    start = g.spec_source_start;
    end = g.spec_source_end;
    from_selection = g.spec_source_from_selection;
    return true;
}

std::wstring fft_window_status(double start, double end, bool from_selection) {
    wchar_t buf[160];
    if (g_str == &kEn) {
        swprintf(buf, 160, from_selection
            ? L"   |   FFT window: selected %.6g..%.6g s"
            : L"   |   FFT window: visible %.6g..%.6g s",
            start, end);
    } else {
        swprintf(buf, 160, from_selection
            ? L"   |   FFT окно: выбранный участок %.6g..%.6g c"
            : L"   |   FFT окно: видимый участок %.6g..%.6g c",
            start, end);
    }
    return buf;
}

void set_status() {
    std::wstring s;
    wchar_t buf[512];
    g.status_detail_text.clear();
    g.status_detail_color = g_theme->accent;
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
        double fft_start, fft_end;
        bool from_selection = false;
        if (g.freq_mode) {
            if (last_fft_source_window(fft_start, fft_end, from_selection)) {
                s += fft_window_status(fft_start, fft_end, from_selection);
            }
        } else if (has_fft_window()) {
            s += fft_window_status(g.fft_window_start, g.fft_window_end, true);
        }
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
    marker_status_detail(g.status_detail_text, g.status_detail_color);
    g.status_text = s;
    if (g.status) SetWindowTextW(g.status, s.c_str());
    if (g.main) {
        RECT rc;
        GetClientRect(g.main, &rc);
        RECT sr = {0, rc.bottom - kBottomBar, rc.right, rc.bottom};
        InvalidateRect(g.main, &sr, FALSE);
    }
}

void ensure_channel_transform_vectors();
double transform_channel_value(std::size_t ci, double raw);

bool build_time_window_dataset(const lvm::Dataset& in, double start, double end, lvm::Dataset& out) {
    out = lvm::Dataset{};
    out.stats = in.stats;
    out.ok = true;
    out.names = in.names;
    out.channels.resize(in.channels.size());
    ensure_channel_transform_vectors();
    for (std::size_t r = 0; r < in.time.size(); ++r) {
        const double t = in.time[r];
        if (t < start || t > end) continue;
        out.time.push_back(t);
        for (std::size_t c = 0; c < in.channels.size(); ++c) {
            out.channels[c].push_back(transform_channel_value(c, in.channels[c][r]));
        }
    }
    return true;
}

void compute_spectrum_for_window(double start, double end, bool from_selection) {
    if (!has_data()) return;
    clamp_time_window(start, end);
    g.spec_source_start = start;
    g.spec_source_end = end;
    g.spec_source_from_selection = from_selection;
    g.spec_source_valid = end > start;
    lvm::Dataset view;
    build_time_window_dataset(g.ds, start, end, view);
    g.spec = lvm::compute_spectrum(view, 16384);
    g.spec_valid = g.spec.ok;
}

void compute_spectrum_from_current_source() {
    if (!has_data()) return;
    double start = 0.0, end = 0.0;
    bool from_selection = false;
    if (current_fft_source_window(start, end, from_selection)) {
        compute_spectrum_for_window(start, end, from_selection);
    }
}

void compute_spectrum() {
    if (!has_data()) return;
    double start = 0.0, end = 0.0;
    bool from_selection = false;
    if (g.freq_mode && last_fft_source_window(start, end, from_selection)) {
        compute_spectrum_for_window(start, end, from_selection);
        return;
    }
    compute_spectrum_from_current_source();
}

int channel_index_by_name(const std::string& name) {
    for (std::size_t i = 0; i < g.ds.names.size(); ++i)
        if (g.ds.names[i] == name) return static_cast<int>(i);
    return -1;
}

void finish_channel_rename(bool apply) {
    if (g.editing_channel < 0 || g.editing_channel >= static_cast<int>(g.channel_labels.size())) return;
    const int ci = g.editing_channel;
    if (apply && g.channel_edit) {
        wchar_t buf[256];
        GetWindowTextW(g.channel_edit, buf, 256);
        g.channel_labels[ci] = buf;
        if (ci < static_cast<int>(g.check_labels.size()) && g.check_labels[ci]) {
            SetWindowTextW(g.check_labels[ci], g.channel_labels[ci].c_str());
        }
        InvalidateRect(g.main, nullptr, TRUE);
    }
    if (g.channel_edit) {
        DestroyWindow(g.channel_edit);
        g.channel_edit = nullptr;
    }
    g_channel_edit_proc = nullptr;
    g.editing_channel = -1;
}

LRESULT CALLBACK ChannelEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_GETDLGCODE:
            return CallWindowProcW(g_channel_edit_proc, hwnd, msg, wp, lp) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) {
                finish_channel_rename(true);
                SetFocus(g.main);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                finish_channel_rename(false);
                SetFocus(g.main);
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            finish_channel_rename(true);
            return 0;
    }
    return CallWindowProcW(g_channel_edit_proc, hwnd, msg, wp, lp);
}

void start_channel_rename(int ci) {
    if (ci < 0 || ci >= static_cast<int>(g.channel_labels.size())) return;
    finish_channel_rename(true);
    if (ci >= static_cast<int>(g.check_labels.size()) || !g.check_labels[ci]) return;

    RECT r;
    GetWindowRect(g.check_labels[ci], &r);
    MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&r), 2);
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    g.channel_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", g.channel_labels[ci].c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        r.left - 2, r.top - 1, (r.right - r.left) + 4, (r.bottom - r.top) + 2,
        g.main, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_EDIT)), inst, nullptr);
    if (!g.channel_edit) return;
    SendMessageW(g.channel_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(g.channel_edit, EM_SETSEL, 0, -1);
    g_channel_edit_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g.channel_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChannelEditProc)));
    g.editing_channel = ci;
    SetFocus(g.channel_edit);
}

void destroy_checks() {
    finish_channel_rename(true);
    for (HWND h : g.checks) DestroyWindow(h);
    g.checks.clear();
    for (HWND h : g.check_labels) DestroyWindow(h);
    g.check_labels.clear();
}

void rebuild_checks() {
    destroy_checks();
    if (!has_data()) return;
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    for (std::size_t i = 0; i < g.ds.channel_count(); ++i) {
        HWND c = CreateWindowExW(
            0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 10, 10, g.main,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_BASE + i)), inst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(c, BM_SETCHECK, g.visible[i] ? BST_CHECKED : BST_UNCHECKED, 0);
        g.checks.push_back(c);

        HWND lbl = CreateWindowExW(
            0, L"STATIC", g.channel_labels[i].c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY | SS_CENTERIMAGE,
            0, 0, 10, 10, g.main,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_LABEL_BASE + i)), inst, nullptr);
        SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g.check_labels.push_back(lbl);
    }
}

void hide_ui_controls() {
    for (HWND b : g.buttons) ShowWindow(b, SW_HIDE);
    for (HWND c : g.checks) ShowWindow(c, SW_HIDE);
    for (HWND c : g.check_labels) ShowWindow(c, SW_HIDE);
    if (g.channel_edit) ShowWindow(g.channel_edit, SW_HIDE);
    if (g.status) ShowWindow(g.status, SW_HIDE);
}

void show_ui_controls() {
    for (HWND b : g.buttons) ShowWindow(b, SW_SHOW);
    for (HWND c : g.checks) ShowWindow(c, SW_SHOW);
    for (HWND c : g.check_labels) ShowWindow(c, SW_SHOW);
    if (g.channel_edit) ShowWindow(g.channel_edit, SW_SHOW);
    if (g.status) ShowWindow(g.status, SW_SHOW);
}

bool welcome_visible() {
    return g.welcome_wnd && IsWindow(g.welcome_wnd) && IsWindowVisible(g.welcome_wnd);
}

void redraw_button(HWND btn);
void redraw_toolbar_buttons();

void layout() {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    g.toolbar_seps.clear();
    int x = 8;
    auto place = [&](HWND h, int w, int row_y) { MoveWindow(h, x, row_y, w, 28, TRUE); x += w + 4; };
    auto sep = [&]() { g.toolbar_seps.push_back(x + 2); x += 8; };

    // Row 1: frequent global actions
    x = 8;
    place(g.open, 100, 8);
    sep();
    place(g.mode_time, 78, 8);
    place(g.mode_freq, 88, 8);
    place(g.play, 92, 8);
    sep();
    place(g.reset, 80, 8);
    place(g.autoy, 90, 8);

    // Row 2: graph tools and settings
    x = 8;
    place(g.measure, 80, 40);
    place(g.marker_btn, 80, 40);
    place(g.vline_btn, 80, 40);
    place(g.hline_btn, 92, 40);
    sep();
    place(g.ptsettings, 110, 40);

    const int panel_x = cw - kRightPanel + 12;
    MoveWindow(g.show_all_btn, panel_x, kTopBar + 28, 66, 24, TRUE);
    MoveWindow(g.hide_all_btn, panel_x + 72, kTopBar + 28, 82, 24, TRUE);

    int y = kTopBar + 58;
    for (std::size_t i = 0; i < g.checks.size(); ++i) {
        MoveWindow(g.checks[i], panel_x, y + 2, 18, 20, TRUE);
        if (i < g.check_labels.size()) {
            MoveWindow(g.check_labels[i], panel_x + 22, y, kRightPanel - 24 - 22, 24, TRUE);
        }
        y += 26;
    }
    if (g.channel_edit && g.editing_channel >= 0 && g.editing_channel < static_cast<int>(g.check_labels.size())) {
        RECT r;
        GetWindowRect(g.check_labels[g.editing_channel], &r);
        MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&r), 2);
        MoveWindow(g.channel_edit, r.left - 2, r.top - 1, (r.right - r.left) + 4, (r.bottom - r.top) + 2, TRUE);
    }

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
void set_mode(bool freq_mode);
void rebuild_ui();
void rebuild_accelerators();
void refresh_settings_controls();
void compute_spectrum_from_current_source();
void ensure_channel_transform_vectors();
double transform_channel_value(std::size_t ci, double raw);
HMENU make_menu();
std::wstring hotkey_text_for_command(int command);
std::wstring format_edit_number(double value);
bool parse_wide_double_text(const wchar_t* text, double& out);

const wchar_t* speed_menu_text() {
    return (g_str == &kEn) ? L"Speed" : L"Скорость";
}

const wchar_t* speed_prompt_title_text() {
    return (g_str == &kEn) ? L"Playback speed" : L"Скорость воспроизведения";
}

const wchar_t* speed_prompt_label_text() {
    return (g_str == &kEn)
        ? L"Enter a playback speed multiplier:"
        : L"Введите множитель скорости воспроизведения:";
}

const wchar_t* speed_prompt_apply_text() {
    return (g_str == &kEn) ? L"Apply" : L"Применить";
}

const wchar_t* speed_prompt_cancel_text() {
    return (g_str == &kEn) ? L"Cancel" : L"Отмена";
}

const wchar_t* speed_prompt_invalid_text() {
    return (g_str == &kEn)
        ? L"Enter a positive number, for example 0.5, 1, or 2.75."
        : L"Введите положительное число, например 0.5, 1 или 2.75.";
}

void set_play_speed(double speed) {
    if (!(speed > 0.0) || !std::isfinite(speed)) return;
    if (g.playing) {
        LARGE_INTEGER now{}, freq{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        const double elapsed = static_cast<double>(now.QuadPart - g.play_anchor_qpc.QuadPart) /
                               static_cast<double>(freq.QuadPart);
        g.playhead = g.play_anchor_data + elapsed * g.play_speed;
        if (has_data()) {
            if (g.playhead < g.data_t0) g.playhead = g.data_t0;
            if (g.playhead > g.data_t1) g.playhead = g.data_t1;
        }
        g.play_anchor_data = g.playhead;
        g.play_anchor_qpc = now;
    }
    g.play_speed = speed;
    set_status();
}

LRESULT CALLBACK SpeedPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            CreateWindowExW(0, L"STATIC", speed_prompt_label_text(),
                            WS_CHILD | WS_VISIBLE | SS_LEFT,
                            16, 16, 320, 20, hwnd, nullptr,
                            reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            g_speed_prompt.edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", format_edit_number(g_speed_prompt.value).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                16, 44, 320, 24, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_EDIT)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND ok = CreateWindowExW(
                0, L"BUTTON", speed_prompt_apply_text(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                86, 82, 116, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_OK)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND cancel = CreateWindowExW(
                0, L"BUTTON", speed_prompt_cancel_text(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                214, 82, 122, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_CANCEL)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            if (g_speed_prompt.edit) SendMessageW(g_speed_prompt.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (ok) SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (cancel) SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            HWND label = GetWindow(hwnd, GW_CHILD);
            if (label) SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_SPEED_PROMPT_OK: {
                    double value = 0.0;
                    wchar_t buf[128]{};
                    if (g_speed_prompt.edit) GetWindowTextW(g_speed_prompt.edit, buf, 128);
                    if (!parse_wide_double_text(buf, value) || !(value > 0.0) || !std::isfinite(value)) {
                        MessageBoxW(hwnd, speed_prompt_invalid_text(), speed_prompt_title_text(), MB_OK | MB_ICONWARNING);
                        if (g_speed_prompt.edit) SetFocus(g_speed_prompt.edit);
                        return 0;
                    }
                    g_speed_prompt.value = value;
                    g_speed_prompt.accepted = true;
                    DestroyWindow(hwnd);
                    return 0;
                }
                case IDC_SPEED_PROMPT_CANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_speed_prompt.done = true;
            g_speed_prompt.wnd = nullptr;
            g_speed_prompt.edit = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool prompt_custom_play_speed(double& out_speed) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = SpeedPromptProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"LvmSpeedPrompt";
        atom = RegisterClassExW(&wc);
    }

    g_speed_prompt.done = false;
    g_speed_prompt.accepted = false;
    g_speed_prompt.value = g.play_speed;
    g_speed_prompt.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmSpeedPrompt",
        speed_prompt_title_text(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 368, 150,
        g.main, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_speed_prompt.wnd) return false;

    RECT mr{}, wr{};
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_speed_prompt.wnd, &wr);
    SetWindowPos(
        g_speed_prompt.wnd, HWND_TOP,
        mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
        mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(g.main, FALSE);
    if (g_speed_prompt.edit) {
        SetFocus(g_speed_prompt.edit);
        SendMessageW(g_speed_prompt.edit, EM_SETSEL, 0, -1);
    }

    MSG msg;
    while (!g_speed_prompt.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_speed_prompt.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(g.main, TRUE);
    SetForegroundWindow(g.main);
    if (!g_speed_prompt.accepted) return false;
    out_speed = g_speed_prompt.value;
    return true;
}

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

void pan_y_by(double frac) {
    if (g.freq_mode) {
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
        const double shift = w * frac;
        g.y_amp_max = ytop + shift;
        if (g.y_amp_max < 1e-12) g.y_amp_max = 1e-12;
        g.auto_y_amp = false;
        set_status();
        invalidate_plot();
        return;
    }
    if (!has_data()) return;
    double ymin, ymax;
    current_time_yrange(ymin, ymax);
    if (g.auto_y) {
        g.y_lock_min = ymin;
        g.y_lock_max = ymax;
        g.auto_y = false;
        if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_UNCHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
        sync_menu();
    }
    const double w = g.y_lock_max - g.y_lock_min;
    const double shift = w * frac;
    g.y_lock_min += shift;
    g.y_lock_max += shift;
    set_status();
    invalidate_plot();
}

void goto_start() {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    *lo = minb;
    *hi = minb + w;
    if (*hi > maxb) { *hi = maxb; *lo = maxb - w; if (*lo < minb) *lo = minb; }
    set_status();
    invalidate_plot();
}

void goto_end() {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return;
    const double w = *hi - *lo;
    *hi = maxb;
    *lo = maxb - w;
    if (*lo < minb) { *lo = minb; *hi = minb + w; if (*hi > maxb) *hi = maxb; }
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
    redraw_button(g.play);
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
    redraw_button(g.play);
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
    g.channel_labels.clear();
    for (const auto& n : g.ds.names) g.channel_labels.push_back(to_w(n));
    g.channel_value_mul.assign(g.ds.channel_count(), 1.0);
    g.channel_value_add.assign(g.ds.channel_count(), 0.0);
    g.data_t0 = g.ds.time.front();
    g.data_t1 = g.ds.time.back();
    if (g.data_t1 <= g.data_t0) g.data_t1 = g.data_t0 + 1.0;
    g.win_start = g.data_t0;
    g.win_end = g.data_t1;
    g.approx_dt = (g.data_t1 - g.data_t0) / static_cast<double>(g.ds.rows());
    g.points.clear();
    g.guides.clear();
    g.markers.clear();
    g.active_marker = -1;
    clear_fft_window();
    g.fft_selecting = false;
    g.spec_source_valid = false;
    g.spec_source_start = 0.0;
    g.spec_source_end = 0.0;
    g.spec_source_from_selection = false;
    g_undo.clear();
    g_redo.clear();
    stop_play();
    g.playhead = g.data_t0;
    g.playhead_active = false;
    g.auto_y = true;   // a fresh file starts on auto-fit
    if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_CHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
    if (g.menu) CheckMenuItem(g.menu, IDC_AUTOY, MF_BYCOMMAND | MF_CHECKED);

    compute_spectrum_from_current_source();
    g.freq_start = 0.0;
    g.freq_end = g.spec_valid ? g.spec.nyquist : 1.0;

    const wchar_t* base = wcsrchr(wpath.c_str(), L'\\');
    g.file_name = base ? base + 1 : wpath;
    SetWindowTextW(g.main, (std::wstring(g_str->app_title) + L" — " + g.file_name).c_str());
    if (g.welcome_wnd) { ShowWindow(g.welcome_wnd, SW_HIDE); show_ui_controls(); }

    rebuild_checks();
    refresh_settings_controls();
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

// Copy transformed values for indices [lo,hi) into `out` as floats.
void build_series(std::size_t channel_index, std::size_t lo, std::size_t hi,
                  std::vector<float>& out) {
    const std::vector<double>& col = g.ds.channels[channel_index];
    const std::size_t count = hi - lo;
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i)
        out[i] = static_cast<float>(transform_channel_value(channel_index, col[lo + i]));
}

// Auto-fit vertical range over the currently visible time window (with 5% pad).
bool current_time_yrange(double& ymin, double& ymax) {
    if (!has_data()) return false;
    ensure_channel_transform_vectors();
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
            const double v = transform_channel_value(c, col[i]);
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
        SIZE xsz{};
        GetTextExtentPoint32W(dc, buf, lstrlenW(buf), &xsz);
        int tx = px - xsz.cx / 2;
        if (tx < p.left) tx = p.left;
        const int max_tx = p.right - xsz.cx;
        if (tx > max_tx) tx = max_tx;
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        TextOutW(dc, tx, p.bottom + 4, buf, lstrlenW(buf));

        const double fy = static_cast<double>(i) / ticks;
        const int py = p.bottom - static_cast<int>(fy * (p.bottom - p.top));
        MoveToEx(dc, p.left, py, nullptr);
        LineTo(dc, p.right, py);
        swprintf(buf, 64, L"%.4g", y0 + fy * (y1 - y0));
        SIZE ysz{};
        GetTextExtentPoint32W(dc, buf, lstrlenW(buf), &ysz);
        int ty = py - ysz.cy / 2;
        if (ty < p.top + 2) ty = p.top + 2;
        const int max_ty = p.bottom - ysz.cy - 2;
        if (ty > max_ty) ty = max_ty;
        SetTextAlign(dc, TA_RIGHT | TA_TOP);
        TextOutW(dc, p.left - 8, ty, buf, lstrlenW(buf));
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
        std::wstring nm = g.channel_labels[i];
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
        std::wstring nm = g.channel_labels[i];
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
    auto my = [&](double dy) {
        return p.bottom - static_cast<int>((dy - g.vy0) / (g.vy1 - g.vy0) * (p.bottom - p.top));
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

        if (m.snapped && m.channel >= 0) {
            const int Y = my(m.y);
            if (Y >= p.top && Y <= p.bottom) {
                HBRUSH dot = CreateSolidBrush(channel_color(static_cast<std::size_t>(m.channel)));
                HGDIOBJ old_dot_br = SelectObject(dc, dot);
                HGDIOBJ old_dot_pen = SelectObject(dc, GetStockObject(NULL_PEN));
                Ellipse(dc, X - 4, Y - 4, X + 5, Y + 5);
                SelectObject(dc, old_dot_pen);
                SelectObject(dc, old_dot_br);
                DeleteObject(dot);
            }
        }
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

    double fft0 = 0.0, fft1 = 0.0;
    bool show_fft_window = false;
    int fft_left_px = 0, fft_right_px = 0;
    if (g.fft_selecting) {
        fft0 = g.fft_select_anchor_t;
        fft1 = g.fft_select_current_t;
        show_fft_window = true;
    } else if (has_fft_window()) {
        fft0 = g.fft_window_start;
        fft1 = g.fft_window_end;
        show_fft_window = true;
    }
    if (show_fft_window) {
        clamp_time_window(fft0, fft1);
        const double vis0 = std::max(fft0, g.win_start);
        const double vis1 = std::min(fft1, g.win_end);
        if (vis1 > vis0) {
            fft_left_px = mapx(vis0);
            fft_right_px = mapx(vis1);
            if (fft_right_px <= fft_left_px) fft_right_px = fft_left_px + 1;
        }
    }

    static std::vector<float> series;
    static std::vector<float> cmin, cmax;
    const bool sparse = (hi - lo) <= static_cast<std::size_t>(pw) * 2;

    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        build_series(c, lo, hi, series);
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

    if (show_fft_window && fft_right_px > fft_left_px) {
        HPEN sel_pen = CreatePen(PS_DOT, 1, g_theme->accent);
        HGDIOBJ old_pen = SelectObject(dc, sel_pen);
        MoveToEx(dc, fft_left_px, p.top, nullptr); LineTo(dc, fft_left_px, p.bottom);
        MoveToEx(dc, fft_right_px, p.top, nullptr); LineTo(dc, fft_right_px, p.bottom);
        SelectObject(dc, old_pen);
        DeleteObject(sel_pen);

        HPEN tick_pen = CreatePen(PS_SOLID, 2, g_theme->accent);
        HGDIOBJ old_tick_pen = SelectObject(dc, tick_pen);
        const int tick = 8;
        const int top_y = p.top + 2;
        const int bottom_y = p.bottom - 2;

        MoveToEx(dc, fft_left_px, top_y, nullptr); LineTo(dc, fft_left_px + tick, top_y);
        MoveToEx(dc, fft_right_px - tick, top_y, nullptr); LineTo(dc, fft_right_px, top_y);
        MoveToEx(dc, fft_left_px, bottom_y, nullptr); LineTo(dc, fft_left_px + tick, bottom_y);
        MoveToEx(dc, fft_right_px - tick, bottom_y, nullptr); LineTo(dc, fft_right_px, bottom_y);

        SelectObject(dc, old_tick_pen);
        DeleteObject(tick_pen);
    }
    draw_legend(dc, p);

    g.vx0 = g.win_start; g.vx1 = g.win_end; g.vy0 = ymin; g.vy1 = ymax;
    g.vrect = p; g.vvalid = true;
}

void draw_freq(HDC dc, const RECT& p) {
    if (!g.spec_valid && !g.spec_source_valid) compute_spectrum();
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
        std::vector<POINT> pts;
        for (std::size_t k = klo; k < khi; ++k)
            pts.push_back(POINT{mapx(f[k]), mapy(a[k])});
        if (g.visual_smooth && pts.size() >= 2) {
            draw_catmull_rom(dc, pts);
            // Mark real samples when few are visible.
            if (pts.size() <= 400) {
                HBRUSH dot = CreateSolidBrush(channel_color(ci));
                HGDIOBJ ob = SelectObject(dc, dot);
                HGDIOBJ opn = SelectObject(dc, GetStockObject(NULL_PEN));
                for (const auto& pt : pts)
                    Ellipse(dc, pt.x - 2, pt.y - 2, pt.x + 3, pt.y + 3);
                SelectObject(dc, opn);
                SelectObject(dc, ob);
                DeleteObject(dot);
            }
        } else {
            for (std::size_t k = 0; k < pts.size(); ++k) {
                if (k == 0) MoveToEx(dc, pts[k].x, pts[k].y, nullptr);
                else LineTo(dc, pts[k].x, pts[k].y);
            }
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
        std::wstring msg = (g_str == &kEn)
            ? L"Open a .lvm or .txt file (" + hotkey_text_for_command(IDC_OPEN) + L")"
            : L"Откройте файл .lvm или .txt (" + hotkey_text_for_command(IDC_OPEN) + L")";
        TextOutW(dc, (p.left + p.right) / 2, (p.top + p.bottom) / 2, msg.c_str(), static_cast<int>(msg.size()));
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

    if (welcome_visible()) {
        BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
        SelectObject(mem, obmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        return;
    }

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
        if (!g.status_detail_text.empty()) {
            SIZE sz = {};
            GetTextExtentPoint32W(mem, g.status_text.c_str(), static_cast<int>(g.status_text.size()), &sz);
            SetTextColor(mem, g.status_detail_color);
            TextOutW(mem, 12 + sz.cx, ch - kBottomBar + 5, g.status_detail_text.c_str(), static_cast<int>(g.status_detail_text.size()));
        }
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

std::string current_channel_label(std::size_t ci) {
    if (ci < g.channel_labels.size()) return to_acp(g.channel_labels[ci].c_str());
    if (ci < g.ds.names.size()) return g.ds.names[ci];
    return "Channel_" + std::to_string(ci + 1);
}

void ensure_channel_transform_vectors() {
    const std::size_t n = g.ds.channel_count();
    if (g.channel_value_mul.size() != n) g.channel_value_mul.assign(n, 1.0);
    if (g.channel_value_add.size() != n) g.channel_value_add.assign(n, 0.0);
}

double transform_channel_value(std::size_t ci, double raw) {
    if (std::isnan(raw)) return raw;
    const double global_mul = g.global_value_mul;
    const double global_add = g.global_value_add;
    const double local_mul = (ci < g.channel_value_mul.size()) ? g.channel_value_mul[ci] : 1.0;
    const double local_add = (ci < g.channel_value_add.size()) ? g.channel_value_add[ci] : 0.0;
    return raw * global_mul * local_mul + global_add + local_add;
}

std::wstring format_edit_number(double value) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%.12g", value);
    return buf;
}

bool parse_wide_double_text(const wchar_t* text, double& out) {
    if (!text) return false;
    std::wstring s = text;
    for (wchar_t& ch : s) if (ch == L',') ch = L'.';
    const wchar_t* begin = s.c_str();
    while (*begin == L' ' || *begin == L'\t' || *begin == L'\r' || *begin == L'\n') ++begin;
    if (*begin == 0) return false;
    wchar_t* end = nullptr;
    double value = wcstod(begin, &end);
    if (begin == end) return false;
    while (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n') ++end;
    if (*end != 0) return false;
    out = value;
    return true;
}

bool read_edit_double(HWND parent, int id, double& out) {
    wchar_t buf[128];
    HWND edit = GetDlgItem(parent, id);
    if (!edit) return false;
    GetWindowTextW(edit, buf, 128);
    return parse_wide_double_text(buf, out);
}

void reset_channel_transform(std::size_t ci) {
    ensure_channel_transform_vectors();
    if (ci < g.channel_value_mul.size()) g.channel_value_mul[ci] = 1.0;
    if (ci < g.channel_value_add.size()) g.channel_value_add[ci] = 0.0;
}

void clear_transform_sensitive_overlays() {
    g.points.clear();
    g.markers.clear();
    g.active_marker = -1;
    g.guides.erase(
        std::remove_if(g.guides.begin(), g.guides.end(), [](const GuideLine& gl) { return !gl.vertical; }),
        g.guides.end());
    g.pending_line = 0;
    g.pending_marker = false;
    g.measure_mode = false;
    g_undo.clear();
    g_redo.clear();
    if (g.measure) SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
}

void on_signal_transform_changed() {
    if (!has_data()) return;
    clear_transform_sensitive_overlays();
    g.auto_y = true;
    g.auto_y_amp = true;
    if (g.autoy) SendMessageW(g.autoy, BM_SETCHECK, BST_CHECKED, 0);
    g.spec_valid = false;
    g.spec = lvm::Spectrum{};
    g.spec_source_valid = false;
    compute_spectrum_from_current_source();
    sync_menu();
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
}

// Export the visible segment: time-domain rows (Time mode) or spectrum (Hz).
bool save_csv(const std::wstring& path) {
    std::ofstream out(to_acp(path.c_str()), std::ios::binary);
    if (!out) return false;
    ensure_channel_transform_vectors();
    if (g.freq_mode) {
        if (!g.spec_valid) return false;
        std::vector<std::size_t> cols;
        out << g_str->csv_freq;
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            int ci = channel_index_by_name(g.spec.names[j]);
            if (ci >= 0 && g.visible[ci]) { out << "," << current_channel_label(static_cast<std::size_t>(ci)); cols.push_back(j); }
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
            if (g.visible[c]) { out << "," << current_channel_label(c); cols.push_back(c); }
        out << "\n";
        for (std::size_t r = 0; r < g.ds.rows(); ++r) {
            const double tt = g.ds.time[r];
            if (tt < g.win_start || tt > g.win_end) continue;
            out << numfmt(tt);
            for (std::size_t c : cols) out << "," << numfmt(transform_channel_value(c, g.ds.channels[c][r]));
            out << "\n";
        }
    }
    return true;
}

bool save_txt_view(const std::wstring& path) {
    std::ofstream out(to_acp(path.c_str()), std::ios::binary);
    if (!out) return false;
    ensure_channel_transform_vectors();
    if (g.freq_mode) {
        if (!g.spec_valid) return false;
        std::vector<std::size_t> cols;
        out << "Frequency";
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            int ci = channel_index_by_name(g.spec.names[j]);
            if (ci >= 0 && g.visible[ci]) { out << "\t" << current_channel_label(static_cast<std::size_t>(ci)); cols.push_back(j); }
        }
        out << "\r\n";
        for (std::size_t k = 0; k < g.spec.freqs.size(); ++k) {
            const double fr = g.spec.freqs[k];
            if (fr < g.freq_start || fr > g.freq_end) continue;
            out << numfmt(fr);
            for (std::size_t j : cols) out << "\t" << numfmt(g.spec.amp[j][k]);
            out << "\r\n";
        }
    } else {
        std::vector<std::size_t> cols;
        out << "Time";
        for (std::size_t c = 0; c < g.ds.channel_count(); ++c)
            if (g.visible[c]) { out << "\t" << current_channel_label(c); cols.push_back(c); }
        out << "\r\n";
        for (std::size_t r = 0; r < g.ds.rows(); ++r) {
            const double tt = g.ds.time[r];
            if (tt < g.win_start || tt > g.win_end) continue;
            out << numfmt(tt);
            for (std::size_t c : cols) out << "\t" << numfmt(transform_channel_value(c, g.ds.channels[c][r]));
            out << "\r\n";
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

const wchar_t* txt_filter() {
    return (g_str == &kEn)
        ? L"TXT file\0*.txt\0All files\0*.*\0"
        : L"TXT файл\0*.txt\0Все файлы\0*.*\0";
}

const wchar_t* txt_saved_prefix() {
    return (g_str == &kEn) ? L"Exported (TXT): " : L"Выгружено (TXT): ";
}

const wchar_t* txt_save_error() {
    return (g_str == &kEn) ? L"Failed to export TXT." : L"Не удалось выгрузить TXT.";
}

void save_txt_dialog() {
    if (!has_data()) { MessageBoxW(g.main, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return; }
    std::wstring def = file_stem() + (g.freq_mode ? L"_spectrum.txt" : L"_segment.txt");
    std::wstring path;
    if (!save_dialog(path, txt_filter(), L"txt", def)) return;
    if (save_txt_view(path)) {
        const wchar_t* b = wcsrchr(path.c_str(), L'\\');
        status_msg(std::wstring(txt_saved_prefix()) + (b ? b + 1 : path.c_str()));
    } else {
        MessageBoxW(g.main, txt_save_error(), g_str->msg_error_title, MB_ICONERROR);
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
// data point. The stored data is never modified вЂ” only the marker is adjusted.
bool snap_to_nearest_target(double& dx, double& dy, int* out_channel = nullptr) {
    if (g.freq_mode) {
        if (!g.spec_valid || g.spec.freqs.size() < 2) return false;
        const auto& f = g.spec.freqs;
        std::size_t k = static_cast<std::size_t>(std::lower_bound(f.begin(), f.end(), dx) - f.begin());
        if (k >= f.size()) k = f.size() - 1;
        if (k > 0 && (dx - f[k - 1]) < (f[k] - dx)) --k;
        double best = std::numeric_limits<double>::max(), by = dy;
        int best_ci = -1;
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            const int ci = channel_index_by_name(g.spec.names[j]);
            if (ci < 0 || !g.visible[ci]) continue;
            const double v = g.spec.amp[j][k];
            const double d = std::fabs(v - dy);
            if (d < best) { best = d; by = v; best_ci = ci; }
        }
        dx = f[k];
        if (best_ci >= 0) {
            dy = by;
            if (out_channel) *out_channel = best_ci;
            return true;
        }
        return false;
    }
    if (!has_data()) return false;
    ensure_channel_transform_vectors();
    const auto& t = g.ds.time;
    std::size_t i = static_cast<std::size_t>(std::lower_bound(t.begin(), t.end(), dx) - t.begin());
    if (i >= t.size()) i = t.size() - 1;
    if (i > 0 && (dx - t[i - 1]) < (t[i] - dx)) --i;
    double best = std::numeric_limits<double>::max(), by = dy;
    int best_ci = -1;
    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        const double v = transform_channel_value(c, g.ds.channels[c][i]);
        if (std::isnan(v)) continue;
        const double d = std::fabs(v - dy);
        if (d < best) { best = d; by = v; best_ci = static_cast<int>(c); }
    }
    dx = t[i];
    if (best_ci >= 0) {
        dy = by;
        if (out_channel) *out_channel = best_ci;
        return true;
    }
    return false;
}

void snap_to_nearest(double& dx, double& dy) {
    snap_to_nearest_target(dx, dy, nullptr);
}

int hit_test_marker(int px, int py) {
    if (!g.vvalid || g.markers.empty()) return -1;
    const RECT& p = g.vrect;
    if (px < p.left || px > p.right || py < p.top || py > p.bottom) return -1;
    if (g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return -1;
    auto mx = [&](double dx) {
        return p.left + static_cast<int>((dx - g.vx0) / (g.vx1 - g.vx0) * (p.right - p.left));
    };
    auto my = [&](double dy) {
        return p.bottom - static_cast<int>((dy - g.vy0) / (g.vy1 - g.vy0) * (p.bottom - p.top));
    };
    int best = -1;
    int best_score = 999999;
    for (std::size_t i = 0; i < g.markers.size(); ++i) {
        const App::Marker& m = g.markers[i];
        if (m.freq != g.freq_mode) continue;
        const int dxp = std::abs(px - mx(m.x));
        if (dxp > 6) continue;
        int score = dxp * 10;
        if (m.snapped) {
            const int dyp = std::abs(py - my(m.y));
            if (dyp <= 8) score = dxp + dyp;
        }
        if (score < best_score) {
            best_score = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

const HotkeyBinding* find_hotkey_binding(int command) {
    for (const auto& hk : g.hotkeys)
        if (hk.command == command) return &hk;
    return nullptr;
}

std::vector<HotkeyBinding> default_hotkeys() {
    return {
        {IDC_OPEN, FVIRTKEY | FCONTROL, 'O'},
        {IDC_SAVEPNG, FVIRTKEY | FCONTROL, 'S'},
        {IDC_SAVECSV, FVIRTKEY | FCONTROL, 'E'},
        {IDM_UNDO, FVIRTKEY | FCONTROL, 'Z'},
        {IDM_REDO, FVIRTKEY | FCONTROL | FSHIFT, 'Z'},
        {IDM_MODE_TIME, FVIRTKEY, 'T'},
        {IDM_MODE_FREQ, FVIRTKEY, 'F'},
        {IDC_MEASURE, FVIRTKEY, 'P'},
        {IDM_ADD_MARKER, FVIRTKEY, 'M'},
        {IDM_ADD_VLINE, FVIRTKEY, 'V'},
        {IDM_ADD_HLINE, FVIRTKEY, 'H'},
        {IDC_AUTOY, FVIRTKEY, 'A'},
        {IDM_VISMOOTH, FVIRTKEY, 'C'},
        {IDM_VPAN, FVIRTKEY, 'Y'},
        {IDM_THEME, FVIRTKEY, 'D'},
        {IDC_PLAY, FVIRTKEY, VK_SPACE},
        {IDC_ZOOMIN, FVIRTKEY, VK_OEM_PLUS},
        {IDC_ZOOMOUT, FVIRTKEY, VK_OEM_MINUS},
        {IDC_PANLEFT, FVIRTKEY, VK_LEFT},
        {IDC_PANRIGHT, FVIRTKEY, VK_RIGHT},
        {IDC_RESET, FVIRTKEY, VK_HOME},
        {IDC_GOTO_START, FVIRTKEY | FCONTROL, VK_HOME},
        {IDC_GOTO_END, FVIRTKEY | FCONTROL, VK_END},
        {IDM_CLEAR_POINTS, FVIRTKEY, VK_DELETE},
        {IDM_HOTKEYS, FVIRTKEY, VK_F1},
    };
}

void ensure_hotkeys_initialized() {
    if (g.hotkeys.empty()) g.hotkeys = default_hotkeys();
}

std::wstring key_name(WORD key) {
    switch (key) {
        case 0: return g_str == &kEn ? L"None" : L"Нет";
        case VK_SPACE: return g_str == &kEn ? L"Space" : L"Пробел";
        case VK_LEFT: return g_str == &kEn ? L"Left" : L"Влево";
        case VK_RIGHT: return g_str == &kEn ? L"Right" : L"Вправо";
        case VK_UP: return g_str == &kEn ? L"Up" : L"Вверх";
        case VK_DOWN: return g_str == &kEn ? L"Down" : L"Вниз";
        case VK_HOME: return L"Home";
        case VK_END: return L"End";
        case VK_DELETE: return L"Delete";
        case VK_ESCAPE: return L"Esc";
        case VK_OEM_PLUS: return L"+";
        case VK_OEM_MINUS: return L"-";
        default:
            if (key >= 'A' && key <= 'Z') return std::wstring(1, static_cast<wchar_t>(key));
            if (key >= '0' && key <= '9') return std::wstring(1, static_cast<wchar_t>(key));
            if (key >= VK_F1 && key <= VK_F12) return L"F" + std::to_wstring(key - VK_F1 + 1);
            return L"?";
    }
}

std::wstring hotkey_text(BYTE fvirt, WORD key) {
    if (key == 0) return g_str == &kEn ? L"Not assigned" : L"Не назначено";
    std::wstring out;
    if (fvirt & FCONTROL) out += L"Ctrl+";
    if (fvirt & FSHIFT) out += L"Shift+";
    if (fvirt & FALT) out += L"Alt+";
    out += key_name(key);
    return out;
}

std::wstring hotkey_text_for_command(int command) {
    const HotkeyBinding* hk = find_hotkey_binding(command);
    return hk ? hotkey_text(hk->fvirt, hk->key) : std::wstring();
}

std::wstring menu_text(const wchar_t* base, int command) {
    const HotkeyBinding* binding = find_hotkey_binding(command);
    if (!binding || binding->key == 0) return base;
    std::wstring hk = hotkey_text(binding->fvirt, binding->key);
    if (hk.empty()) return base;
    return std::wstring(base) + L"\t" + hk;
}

std::wstring command_name(int command) {
    const bool en = (g_str == &kEn);
    switch (command) {
        case IDC_OPEN: return en ? L"Open file" : L"Открыть файл";
        case IDC_SAVEPNG: return en ? L"Save PNG" : L"Сохранить PNG";
        case IDC_SAVECSV: return en ? L"Save CSV" : L"Сохранить CSV";
        case IDM_UNDO: return en ? L"Undo" : L"Отменить";
        case IDM_REDO: return en ? L"Redo" : L"Повторить";
        case IDM_MODE_TIME: return en ? L"Time view" : L"Режим времени";
        case IDM_MODE_FREQ: return en ? L"Hz / FFT view" : L"Режим Гц / БПФ";
        case IDC_MEASURE: return en ? L"Measurement points" : L"Точки измерения";
        case IDM_ADD_MARKER: return en ? L"Marker" : L"Маркер";
        case IDM_ADD_VLINE: return en ? L"Vertical line" : L"Вертикальная линия";
        case IDM_ADD_HLINE: return en ? L"Horizontal line" : L"Горизонтальная линия";
        case IDC_AUTOY: return en ? L"Auto zoom" : L"Авто масштабирование";
        case IDM_VISMOOTH: return en ? L"Smoothing" : L"Сглаживание";
        case IDM_VPAN: return en ? L"Vertical pan" : L"Вертикальное панорамирование";
        case IDM_THEME: return en ? L"Dark theme" : L"Тёмная тема";
        case IDC_PLAY: return en ? L"Play / Pause" : L"Play / Pause";
        case IDC_ZOOMIN: return en ? L"Zoom in" : L"Увеличить";
        case IDC_ZOOMOUT: return en ? L"Zoom out" : L"Уменьшить";
        case IDC_PANLEFT: return en ? L"Pan left" : L"Сдвиг влево";
        case IDC_PANRIGHT: return en ? L"Pan right" : L"Сдвиг вправо";
        case IDC_RESET: return en ? L"Reset view" : L"Сброс вида";
        case IDC_GOTO_START: return en ? L"Go to start" : L"В начало";
        case IDC_GOTO_END: return en ? L"Go to end" : L"В конец";
        case IDM_CLEAR_POINTS: return en ? L"Clear points" : L"Очистить точки";
        case IDM_HOTKEYS: return en ? L"Hotkeys help" : L"Справка по клавишам";
        default: return L"?";
    }
}

std::vector<int> hotkey_command_order() {
    return {
        IDC_OPEN, IDC_SAVEPNG, IDC_SAVECSV, IDM_UNDO, IDM_REDO,
        IDM_MODE_TIME, IDM_MODE_FREQ, IDC_MEASURE, IDM_ADD_MARKER,
        IDM_ADD_VLINE, IDM_ADD_HLINE, IDC_AUTOY, IDM_VISMOOTH,
        IDM_VPAN, IDM_THEME, IDC_PLAY, IDC_ZOOMIN, IDC_ZOOMOUT,
        IDC_PANLEFT, IDC_PANRIGHT, IDC_RESET, IDC_GOTO_START,
        IDC_GOTO_END, IDM_CLEAR_POINTS, IDM_HOTKEYS
    };
}

std::wstring hotkey_list_item_text(int command) {
    return command_name(command) + L"  [" + hotkey_text_for_command(command) + L"]";
}

std::wstring welcome_intro_text() {
    return (g_str == &kEn)
        ? L"A focused desktop viewer for LabVIEW logs: open .lvm or .txt files, inspect signals in Time mode, and switch to Hz / FFT without losing context."
        : L"Нативный просмотрщик логов LabVIEW: открывайте .lvm и .txt, изучайте сигналы во времени и быстро переходите к Hz / FFT по выбранному участку.";
}

std::wstring welcome_features_text() {
    const bool en = (g_str == &kEn);
    std::wstring out;
    out += en ? L"Time mode for navigation and Hz / FFT mode for the visible interval\r\n"
              : L"Time-режим для навигации и Hz / FFT-режим для видимого участка\r\n";
    out += en ? L"Measurement points, snapped markers, and guide lines\r\n"
              : L"Точки измерения, примагниченные маркеры и направляющие линии\r\n";
    out += en ? L"Channel rename, quick visibility control, and legend work\r\n"
              : L"Переименование каналов, быстрый контроль видимости и работа с легендой\r\n";
    out += en ? L"PNG / CSV / TXT export, custom hotkeys, themes, and language switch"
              : L"Экспорт PNG / CSV / TXT, настраиваемые горячие клавиши, темы и смена языка";
    return out;
}

const wchar_t* welcome_actions_title_text() {
    return (g_str == &kEn) ? L"Start Here" : L"Быстрый старт";
}

struct WelcomeLayout {
    RECT bounds{};
    RECT hero{};
    RECT action{};
    bool stacked = false;
};

int rect_width(const RECT& r) {
    return r.right - r.left;
}

int rect_height(const RECT& r) {
    return r.bottom - r.top;
}

WelcomeLayout compute_welcome_layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    WelcomeLayout layout{};
    const int client_w = max(0, static_cast<int>(rc.right - rc.left));
    const int client_h = max(0, static_cast<int>(rc.bottom - rc.top));
    const int outer = (client_w >= 1200) ? 36 : ((client_w >= 900) ? 28 : 20);
    const int gap = (client_w >= 900) ? 24 : 16;
    const int content_w = max(320, min(client_w - outer * 2, 1180));
    const int content_h = max(260, client_h - outer * 2);
    const int x0 = max(0, (client_w - content_w) / 2);
    const int y0 = max(0, (client_h - content_h) / 2);

    layout.bounds = { x0, y0, x0 + content_w, y0 + content_h };
    layout.stacked = (content_w < 920 || content_h < 590);

    if (layout.stacked) {
        int hero_h = std::clamp(content_h * 52 / 100, 240, 380);
        const int min_action_h = 300;
        hero_h = min(hero_h, max(220, content_h - gap - min_action_h));
        layout.hero = { x0, y0, x0 + content_w, y0 + hero_h };
        layout.action = { x0, layout.hero.bottom + gap, x0 + content_w, y0 + content_h };
    } else {
        const int action_w = std::clamp(content_w / 3, 300, 360);
        layout.hero = { x0, y0, x0 + content_w - action_w - gap, y0 + content_h };
        layout.action = { layout.hero.right + gap, y0, x0 + content_w, y0 + content_h };
    }

    return layout;
}

void layout_welcome_controls(HWND hwnd) {
    WelcomeLayout layout = compute_welcome_layout(hwnd);
    auto place = [&](int id, int x, int y, int w, int h) {
        HWND ctl = GetDlgItem(hwnd, id);
        if (ctl) MoveWindow(ctl, x, y, max(1, w), max(1, h), TRUE);
    };

    const int hero_pad = layout.stacked ? 22 : 30;
    const int hx = layout.hero.left + hero_pad;
    int hy = layout.hero.top + hero_pad;
    const int hw = max(180, rect_width(layout.hero) - hero_pad * 2);
    const int title_h = layout.stacked ? 46 : 52;
    const int subtitle_h = 24;
    const int intro_h = layout.stacked ? 64 : 76;

    place(IDW_TITLE, hx, hy, hw, title_h);
    hy += title_h + 8;
    place(IDW_SUBTITLE, hx, hy, hw, subtitle_h);
    hy += subtitle_h + 14;
    place(IDW_INTRO, hx, hy, hw, intro_h);
    hy += intro_h + 18;

    const int feature_bottom = layout.hero.bottom - hero_pad;
    place(IDW_FEATURES, hx, hy, hw, max(72, feature_bottom - hy));

    const int action_pad = layout.stacked ? 20 : 24;
    const int ax = layout.action.left + action_pad;
    int ay = layout.action.top + action_pad;
    const int aw = max(180, rect_width(layout.action) - action_pad * 2);
    const int lang_gap = 10;
    const int button_h = 40;
    const int button_gap = layout.stacked ? 10 : 12;
    place(IDW_LANG_LABEL, ax, ay, aw, 20);
    ay += 24;
    const int lang_w = max(80, (aw - lang_gap) / 2);
    place(IDM_LANG_RU, ax, ay, lang_w, 32);
    place(IDM_LANG_EN, ax + aw - lang_w, ay, lang_w, 32);
    ay += 48;
    place(IDW_ACTIONS_TITLE, ax, ay, aw, 24);
    ay += 34;
    place(IDC_OPEN, ax, ay, aw, button_h);
    ay += button_h + button_gap;
    place(IDC_PTSETTINGS, ax, ay, aw, button_h);
    ay += button_h + button_gap;
    place(IDM_HOTKEYS, ax, ay, aw, button_h);
    ay += button_h + button_gap;
    place(IDW_START, ax, ay, aw, button_h);
}

void append_hotkey_line(std::wstring& out, int command) {
    out += L"  " + hotkey_text_for_command(command) + L"\t— " + command_name(command) + L"\n";
}

std::wstring hotkeys_body_text() {
    const bool en = (g_str == &kEn);
    std::wstring out = en ? L"Files\n" : L"Файлы\n";
    append_hotkey_line(out, IDC_OPEN);
    append_hotkey_line(out, IDC_SAVEPNG);
    append_hotkey_line(out, IDC_SAVECSV);
    append_hotkey_line(out, IDM_UNDO);
    append_hotkey_line(out, IDM_REDO);
    out += L"\n";

    out += en ? L"Modes and tools\n" : L"Режимы и инструменты\n";
    append_hotkey_line(out, IDM_MODE_TIME);
    append_hotkey_line(out, IDM_MODE_FREQ);
    append_hotkey_line(out, IDC_MEASURE);
    append_hotkey_line(out, IDM_ADD_MARKER);
    append_hotkey_line(out, IDM_ADD_VLINE);
    append_hotkey_line(out, IDM_ADD_HLINE);
    out += en ? L"  Esc\t— Cancel current add mode\n\n" : L"  Esc\t— Отменить текущий режим добавления\n\n";

    out += en ? L"View\n" : L"Вид\n";
    append_hotkey_line(out, IDC_AUTOY);
    append_hotkey_line(out, IDM_VISMOOTH);
    append_hotkey_line(out, IDM_VPAN);
    append_hotkey_line(out, IDM_THEME);
    append_hotkey_line(out, IDC_PLAY);
    append_hotkey_line(out, IDC_ZOOMIN);
    append_hotkey_line(out, IDC_ZOOMOUT);
    append_hotkey_line(out, IDC_PANLEFT);
    append_hotkey_line(out, IDC_PANRIGHT);
    append_hotkey_line(out, IDC_RESET);
    append_hotkey_line(out, IDC_GOTO_START);
    append_hotkey_line(out, IDC_GOTO_END);
    append_hotkey_line(out, IDM_CLEAR_POINTS);
    out += L"\n";

    out += en ? L"Mouse\n" : L"Мышь\n";
    out += en ? L"  Wheel\t— Zoom under cursor\n" : L"  Колесо\t— Масштаб под курсором\n";
    out += en ? L"  Shift+Wheel\t— Pan left / right\n" : L"  Shift+колесо\t— Прокрутка влево / вправо\n";
    out += en ? L"  Ctrl+Wheel\t— Zoom Y\n" : L"  Ctrl+колесо\t— Масштаб по Y\n";
    out += en ? L"  Alt+Wheel\t— Pan up / down (Y)\n" : L"  Alt+колесо\t— Сдвиг вверх / вниз по Y\n";
    out += en ? L"  Left-drag\t— Pan view\n" : L"  ЛКМ + тяга\t— Панорамирование\n";
    out += en ? L"  Left-click\t— Place point / line / marker in active mode\n" : L"  ЛКМ\t— Поставить точку / линию / маркер в активном режиме\n";
    out += en ? L"  Right-click\t— Clear points\n\n" : L"  ПКМ\t— Очистить точки\n\n";
    append_hotkey_line(out, IDM_HOTKEYS);
    return out;
}

std::wstring toolbar_hover_text(HWND btn) {
    const bool en = (g_str == &kEn);
    if (btn == g.open) return g_str->hover_open;
    if (btn == g.play) return g.playing ? g_str->hover_pause : g_str->hover_play;
    if (btn == g.measure) return g_str->hover_measure;
    if (btn == g.reset) return g_str->hover_reset;
    if (btn == g.autoy) return g_str->hover_autoy;
    if (btn == g.ptsettings) return en ? L"Open general settings" : L"Открыть общие настройки";
    if (btn == g.mode_time) return en ? L"Switch to Time view" : L"Переключить в режим времени";
    if (btn == g.mode_freq) return en ? L"Switch to FFT spectrum" : L"Переключить в режим спектра БПФ";
    if (btn == g.marker_btn) return en ? L"Place a marker on the plot" : L"Поставить маркер на график";
    if (btn == g.vline_btn) return en ? L"Place a vertical guide line" : L"Поставить вертикальную линию";
    if (btn == g.hline_btn) return en ? L"Place a horizontal guide line" : L"Поставить горизонтальную линию";
    if (btn == g.show_all_btn) return en ? L"Show all channels" : L"Показать все каналы";
    if (btn == g.hide_all_btn) return en ? L"Hide all channels" : L"Скрыть все каналы";
    if (btn == g.savepng) return g_str->hover_png;
    if (btn == g.savecsv) return g_str->hover_csv;
    return L"";
}

const wchar_t* settings_button_text() {
    return (g_str == &kEn) ? L"Settings" : L"Настройки";
}

const wchar_t* channel_show_all_text() {
    return (g_str == &kEn) ? L"All" : L"Все";
}

const wchar_t* channel_hide_all_text() {
    return (g_str == &kEn) ? L"None" : L"Скрыть";
}

void set_all_channels_visible(bool visible) {
    for (std::size_t i = 0; i < g.visible.size(); ++i) {
        g.visible[i] = visible ? 1 : 0;
        if (i < g.checks.size()) {
            SendMessageW(g.checks[i], BM_SETCHECK, visible ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }
}

COLORREF mix_color(COLORREF a, COLORREF b, int weight_b) {
    weight_b = std::clamp(weight_b, 0, 255);
    const int weight_a = 255 - weight_b;
    const int r = (GetRValue(a) * weight_a + GetRValue(b) * weight_b) / 255;
    const int g = (GetGValue(a) * weight_a + GetGValue(b) * weight_b) / 255;
    const int bl = (GetBValue(a) * weight_a + GetBValue(b) * weight_b) / 255;
    return RGB(r, g, bl);
}

void fill_rounded_rect(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius) {
    HBRUSH bg = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, bg);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(bg);
}

void draw_button_with_colors(HDC dc, const RECT& r, const wchar_t* txt,
                             COLORREF bg_col, COLORREF border_col, COLORREF text_col,
                             bool pressed) {
    fill_rounded_rect(dc, r, bg_col, border_col, 6);

    RECT inner = { r.left + 1, r.top + 1, r.right - 1, r.top + 10 };
    HBRUSH gloss = CreateSolidBrush(mix_color(bg_col, RGB(255, 255, 255), pressed ? 8 : 20));
    FillRect(dc, &inner, gloss);
    DeleteObject(gloss);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text_col);
    HFONT f = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SelectObject(dc, f);
    SetTextAlign(dc, TA_CENTER | TA_TOP);
    SIZE sz{};
    GetTextExtentPoint32W(dc, txt, lstrlenW(txt), &sz);
    int tx = (r.left + r.right) / 2;
    int ty = r.top + (r.bottom - r.top - sz.cy) / 2;
    if (pressed) { tx += 1; ty += 1; }
    TextOutW(dc, tx, ty, txt, lstrlenW(txt));
}

void redraw_button(HWND btn) {
    if (!btn || !IsWindow(btn)) return;
    RedrawWindow(btn, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

void redraw_toolbar_buttons() {
    for (HWND btn : g.buttons) redraw_button(btn);
}

void draw_themed_button(HDC dc, const RECT& r, const wchar_t* txt, bool pressed, bool active, bool hover) {
    COLORREF bg_col, border_col, text_col;
    if (active && pressed) {
        bg_col = g_theme->accent_hover;
        border_col = g_theme->accent_hover;
        text_col = RGB(255, 255, 255);
    } else if (active) {
        bg_col = mix_color(g_theme->btn_bg, g_theme->accent, hover ? 64 : 42);
        border_col = g_theme->accent;
        text_col = g_theme->accent;
    } else if (pressed) {
        bg_col = mix_color(g_theme->btn_hover, g_theme->separator, 68);
        border_col = g_theme->separator;
        text_col = g_theme->text_primary;
    } else if (hover) {
        bg_col = g_theme->btn_hover;
        border_col = g_theme->btn_border;
        text_col = g_theme->text_primary;
    } else {
        bg_col = g_theme->btn_bg;
        border_col = g_theme->btn_border;
        text_col = g_theme->text_primary;
    }

    draw_button_with_colors(dc, r, txt, bg_col, border_col, text_col, pressed);
}

void show_hotkeys() {
    std::wstring text = hotkeys_body_text();
    MessageBoxW(g.main, text.c_str(), g_str->dlg_hotkeys_title, MB_OK | MB_ICONINFORMATION);
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
    chk(IDM_MODE_TIME, !g.freq_mode);
    chk(IDM_MODE_FREQ, g.freq_mode);
    chk(IDM_VISMOOTH, g.visual_smooth);
    chk(IDM_VPAN, g.vertical_pan);
    chk(IDC_MEASURE, g.measure_mode);
    chk(IDM_ADD_MARKER, g.pending_marker);
    chk(IDM_ADD_VLINE, g.pending_line == 1);
    chk(IDM_ADD_HLINE, g.pending_line == 2);
    chk(IDC_AUTOY, g.auto_y);
    chk(IDM_THEME, g_theme == &kDarkTheme);
    chk(IDM_LANG_RU, g_str == &kRu);
    chk(IDM_LANG_EN, g_str == &kEn);
    std::wstring theme_label = menu_text(g_theme == &kDarkTheme ? g_str->theme_light : g_str->theme_dark, IDM_THEME);
    ModifyMenuW(g.menu, IDM_THEME, MF_BYCOMMAND | MF_STRING, IDM_THEME, theme_label.c_str());
    redraw_toolbar_buttons();
}

void set_mode(bool freq_mode) {
    if (g.freq_mode == freq_mode) return;
    g.freq_mode = freq_mode;
    if (g.freq_mode) stop_play();
    if (g.freq_mode) {
        compute_spectrum_from_current_source();
        g.freq_start = 0.0;
        g.freq_end = g.spec_valid ? g.spec.nyquist : 1.0;
    }
    sync_menu();
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
}

HMENU make_menu() {
    HMENU bar = CreateMenu();
    const wchar_t* savetxt_menu = (g_str == &kEn) ? L"Export TXT…" : L"Выгрузить TXT…";

    {
        const bool en = (g_str == &kEn);

        HMENU file = CreatePopupMenu();
        std::wstring open_text = menu_text(en ? L"Open file…" : L"Открыть файл…", IDC_OPEN);
        std::wstring save_png_text = menu_text(en ? L"Save PNG…" : L"Сохранить PNG…", IDC_SAVEPNG);
        std::wstring save_csv_text = menu_text(en ? L"Save CSV…" : L"Сохранить CSV…", IDC_SAVECSV);
        std::wstring undo_text = menu_text(en ? L"Undo" : L"Отменить", IDM_UNDO);
        std::wstring redo_text = menu_text(en ? L"Redo" : L"Повторить", IDM_REDO);
        AppendMenuW(file, MF_STRING, IDC_OPEN, open_text.c_str());
        AppendMenuW(file, MF_STRING, IDC_SAVEPNG, save_png_text.c_str());
        AppendMenuW(file, MF_STRING, IDC_SAVECSV, save_csv_text.c_str());
        AppendMenuW(file, MF_STRING, IDC_SAVETXT, savetxt_menu);
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, IDM_UNDO, undo_text.c_str());
        AppendMenuW(file, MF_STRING, IDM_REDO, redo_text.c_str());
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, IDM_EXIT, en ? L"Exit\tAlt+F4" : L"Выход\tAlt+F4");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), en ? L"File" : L"Файл");

        HMENU view = CreatePopupMenu();
        std::wstring mode_time_text = menu_text(en ? L"Time" : L"Время", IDM_MODE_TIME);
        std::wstring mode_freq_text = menu_text(en ? L"Hz (FFT)" : L"Гц (FFT)", IDM_MODE_FREQ);
        std::wstring zoom_in_text = menu_text(en ? L"Zoom in" : L"Увеличить", IDC_ZOOMIN);
        std::wstring zoom_out_text = menu_text(en ? L"Zoom out" : L"Уменьшить", IDC_ZOOMOUT);
        std::wstring reset_text = menu_text(en ? L"Reset view" : L"Сбросить вид", IDC_RESET);
        std::wstring start_text = menu_text(en ? L"Go to start" : L"В начало", IDC_GOTO_START);
        std::wstring end_text = menu_text(en ? L"Go to end" : L"В конец", IDC_GOTO_END);
        std::wstring autoy_text = menu_text(en ? L"Auto zoom" : L"Авто масштабирование", IDC_AUTOY);
        std::wstring smooth_text = menu_text(en ? L"Smoothing" : L"Сглаживание", IDM_VISMOOTH);
        std::wstring vpan_text = menu_text(en ? L"Vertical pan" : L"Вертикальное панорамирование", IDM_VPAN);
        std::wstring play_text = menu_text(L"Play / Pause", IDC_PLAY);
        std::wstring theme_text = menu_text(en ? L"Dark theme" : L"Тёмная тема", IDM_THEME);
        AppendMenuW(view, MF_STRING, IDM_MODE_TIME, mode_time_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_MODE_FREQ, mode_freq_text.c_str());
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(view, MF_STRING, IDC_ZOOMIN, zoom_in_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_ZOOMOUT, zoom_out_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_RESET, reset_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_GOTO_START, start_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_GOTO_END, end_text.c_str());
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(view, MF_STRING, IDC_AUTOY, autoy_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_VISMOOTH, smooth_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_VPAN, vpan_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_PLAY, play_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_THEME, theme_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_SPEED_CUSTOM, speed_menu_text());
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), en ? L"View" : L"Вид");

        HMENU tools = CreatePopupMenu();
        std::wstring measure_text = menu_text(en ? L"Points" : L"Точки", IDC_MEASURE);
        std::wstring marker_text = menu_text(en ? L"Marker" : L"Маркер", IDM_ADD_MARKER);
        std::wstring vline_text = menu_text(en ? L"Vertical line" : L"Вертикальная линия", IDM_ADD_VLINE);
        std::wstring hline_text = menu_text(en ? L"Horizontal line" : L"Горизонтальная линия", IDM_ADD_HLINE);
        AppendMenuW(tools, MF_STRING, IDC_MEASURE, measure_text.c_str());
        AppendMenuW(tools, MF_STRING, IDM_ADD_MARKER, marker_text.c_str());
        AppendMenuW(tools, MF_STRING, IDM_ADD_VLINE, vline_text.c_str());
        AppendMenuW(tools, MF_STRING, IDM_ADD_HLINE, hline_text.c_str());
        AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(tools, MF_STRING, IDM_CLEAR_POINTS, en ? L"Clear points" : L"Очистить точки");
        AppendMenuW(tools, MF_STRING, IDM_CLEAR_MARKERS, en ? L"Clear markers" : L"Очистить маркеры");
        AppendMenuW(tools, MF_STRING, IDM_CLEAR_LINES, en ? L"Clear lines" : L"Очистить линии");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(tools), en ? L"Tools" : L"Инструменты");

        HMENU settings = CreatePopupMenu();
        HMENU lang = CreatePopupMenu();
        AppendMenuW(lang, MF_STRING, IDM_LANG_RU, g_str->lang_ru);
        AppendMenuW(lang, MF_STRING, IDM_LANG_EN, g_str->lang_en);
        AppendMenuW(settings, MF_STRING, IDC_PTSETTINGS, en ? L"General settings…" : L"Общие настройки…");
        AppendMenuW(settings, MF_POPUP, reinterpret_cast<UINT_PTR>(lang), en ? L"Language" : L"Язык");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(settings), en ? L"Settings" : L"Настройки");

        HMENU help = CreatePopupMenu();
        std::wstring hotkeys_text = menu_text(en ? L"Keyboard shortcuts…" : L"Горячие клавиши…", IDM_HOTKEYS);
        AppendMenuW(help, MF_STRING, IDM_HOTKEYS, hotkeys_text.c_str());
        AppendMenuW(help, MF_STRING, IDM_ABOUT, en ? L"About…" : L"О программе…");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), en ? L"Help" : L"Справка");
        return bar;
    }

    {
        const bool en = (g_str == &kEn);
        const wchar_t* menu_file = en ? L"File" : L"Файл";
        const wchar_t* menu_view = en ? L"View" : L"Вид";
        const wchar_t* menu_points = en ? L"Points" : L"Точки";
        const wchar_t* menu_lines = en ? L"Lines" : L"Линии";
        const wchar_t* menu_markers = en ? L"Markers" : L"Маркеры";
        const wchar_t* menu_help = en ? L"Help" : L"Справка";

        HMENU file = CreatePopupMenu();
        std::wstring open_text = menu_text(en ? L"Open file…" : L"Открыть файл…", IDC_OPEN);
        std::wstring save_png_text = menu_text(en ? L"Save PNG…" : L"Сохранить PNG…", IDC_SAVEPNG);
        std::wstring save_csv_text = menu_text(en ? L"Save CSV…" : L"Сохранить CSV…", IDC_SAVECSV);
        std::wstring undo_text = menu_text(en ? L"Undo" : L"Отменить", IDM_UNDO);
        std::wstring redo_text = menu_text(en ? L"Redo" : L"Повторить", IDM_REDO);
        AppendMenuW(file, MF_STRING, IDC_OPEN, open_text.c_str());
        AppendMenuW(file, MF_STRING, IDC_SAVEPNG, save_png_text.c_str());
        AppendMenuW(file, MF_STRING, IDC_SAVECSV, save_csv_text.c_str());
        AppendMenuW(file, MF_STRING, IDC_SAVETXT, savetxt_menu);
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, IDM_UNDO, undo_text.c_str());
        AppendMenuW(file, MF_STRING, IDM_REDO, redo_text.c_str());
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, IDM_EXIT, en ? L"Exit\tAlt+F4" : L"Выход\tAlt+F4");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), menu_file);

        HMENU view = CreatePopupMenu();
        std::wstring mode_time_text = menu_text(en ? L"Time" : L"Время", IDM_MODE_TIME);
        std::wstring mode_freq_text = menu_text(en ? L"Hz (FFT)" : L"Гц (FFT)", IDM_MODE_FREQ);
        std::wstring zoom_in_text = menu_text(en ? L"Zoom in" : L"Увеличить", IDC_ZOOMIN);
        std::wstring zoom_out_text = menu_text(en ? L"Zoom out" : L"Уменьшить", IDC_ZOOMOUT);
        std::wstring reset_text = menu_text(en ? L"Reset view" : L"Сбросить вид", IDC_RESET);
        std::wstring start_text = menu_text(en ? L"Go to start" : L"В начало", IDC_GOTO_START);
        std::wstring end_text = menu_text(en ? L"Go to end" : L"В конец", IDC_GOTO_END);
        std::wstring autoy_text = menu_text(en ? L"Auto zoom" : L"Авто масштабирование", IDC_AUTOY);
        std::wstring smooth_text = menu_text(en ? L"Smoothing" : L"Сглаживание", IDM_VISMOOTH);
        std::wstring vpan_text = menu_text(en ? L"Vertical pan" : L"Вертикальное панорамирование", IDM_VPAN);
        std::wstring play_text = menu_text(L"Play / Pause", IDC_PLAY);
        std::wstring theme_text = menu_text(en ? L"Dark theme" : L"Тёмная тема", IDM_THEME);
        AppendMenuW(view, MF_STRING, IDM_MODE_TIME, mode_time_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_MODE_FREQ, mode_freq_text.c_str());
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(view, MF_STRING, IDC_ZOOMIN, zoom_in_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_ZOOMOUT, zoom_out_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_RESET, reset_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_GOTO_START, start_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_GOTO_END, end_text.c_str());
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(view, MF_STRING, IDC_AUTOY, autoy_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_VISMOOTH, smooth_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_VPAN, vpan_text.c_str());
        AppendMenuW(view, MF_STRING, IDC_PLAY, play_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_THEME, theme_text.c_str());
        AppendMenuW(view, MF_STRING, IDM_SPEED_CUSTOM, speed_menu_text());
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), menu_view);

        HMENU meas = CreatePopupMenu();
        std::wstring measure_text = menu_text(en ? L"Points" : L"Точки", IDC_MEASURE);
        AppendMenuW(meas, MF_STRING, IDC_MEASURE, measure_text.c_str());
        AppendMenuW(meas, MF_STRING, IDC_PTSETTINGS, en ? L"Settings…" : L"Настройки…");
        AppendMenuW(meas, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(meas, MF_STRING, IDM_CLEAR_POINTS, en ? L"Clear\tDelete" : L"Очистить\tDelete");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(meas), menu_points);

        HMENU lines = CreatePopupMenu();
        std::wstring vline_text = menu_text(en ? L"Vertical" : L"Вертикальная", IDM_ADD_VLINE);
        std::wstring hline_text = menu_text(en ? L"Horizontal" : L"Горизонтальная", IDM_ADD_HLINE);
        AppendMenuW(lines, MF_STRING, IDM_ADD_VLINE, vline_text.c_str());
        AppendMenuW(lines, MF_STRING, IDM_ADD_HLINE, hline_text.c_str());
        AppendMenuW(lines, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(lines, MF_STRING, IDM_CLEAR_LINES, en ? L"Clear" : L"Очистить");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(lines), menu_lines);

        HMENU markers = CreatePopupMenu();
        std::wstring marker_text = menu_text(en ? L"Add" : L"Добавить", IDM_ADD_MARKER);
        AppendMenuW(markers, MF_STRING, IDM_ADD_MARKER, marker_text.c_str());
        AppendMenuW(markers, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(markers, MF_STRING, IDM_CLEAR_MARKERS, en ? L"Clear" : L"Очистить");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(markers), menu_markers);

        HMENU help = CreatePopupMenu();
        std::wstring hotkeys_text = menu_text(en ? L"Keyboard shortcuts…" : L"Горячие клавиши…", IDM_HOTKEYS);
        AppendMenuW(help, MF_STRING, IDM_HOTKEYS, hotkeys_text.c_str());
        AppendMenuW(help, MF_STRING, IDM_ABOUT, en ? L"About…" : L"О программе…");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), menu_help);
        return bar;
    }

    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDC_OPEN, L"Открыть файл…\tCtrl+O");
    AppendMenuW(file, MF_STRING, IDC_SAVEPNG, L"Сохранить PNG…\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDC_SAVECSV, L"Сохранить CSV…\tCtrl+E");
    AppendMenuW(file, MF_STRING, IDC_SAVETXT, savetxt_menu);
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
    AppendMenuW(view, MF_STRING, IDC_GOTO_START, L"В начало\tCtrl+Home");
    AppendMenuW(view, MF_STRING, IDC_GOTO_END, L"В конец\tCtrl+End");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, IDC_AUTOY, (g_str == &kEn) ? L"Auto zoom" : L"Авто масштабирование");
    AppendMenuW(view, MF_STRING, IDM_VISMOOTH, L"Сглаживание\tC");
    AppendMenuW(view, MF_STRING, IDM_VPAN, L"Вертикальное панорамирование\tP");
    AppendMenuW(view, MF_STRING, IDC_PLAY, L"Play / Pause\tПробел");
    AppendMenuW(view, MF_STRING, IDM_THEME, L"Тёмная тема\tT");
    AppendMenuW(view, MF_STRING, IDM_SPEED_CUSTOM, speed_menu_text());
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

const wchar_t* settings_window_title() {
    return (g_str == &kEn) ? L"Settings" : L"Настройки";
}

int hotkey_binding_index(int command) {
    for (std::size_t i = 0; i < g.hotkeys.size(); ++i)
        if (g.hotkeys[i].command == command) return static_cast<int>(i);
    return -1;
}

void set_hotkey_binding(int command, BYTE fvirt, WORD key) {
    int idx = hotkey_binding_index(command);
    if (idx >= 0) {
        g.hotkeys[static_cast<std::size_t>(idx)].fvirt = fvirt;
        g.hotkeys[static_cast<std::size_t>(idx)].key = key;
    } else {
        g.hotkeys.push_back({command, fvirt, key});
    }
}

HotkeyBinding default_hotkey_for_command(int command) {
    for (const auto& hk : default_hotkeys())
        if (hk.command == command) return hk;
    return {command, FVIRTKEY, 0};
}

int find_conflicting_hotkey(BYTE fvirt, WORD key, int except_command) {
    if (key == 0) return 0;
    for (const auto& hk : g.hotkeys) {
        if (hk.command == except_command) continue;
        if (hk.key == key && hk.fvirt == fvirt) return hk.command;
    }
    return 0;
}

void hotkey_combo_add(HWND combo, const wchar_t* text, WORD key) {
    int idx = static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)));
    SendMessageW(combo, CB_SETITEMDATA, idx, key);
}

void populate_hotkey_key_combo(HWND combo) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    hotkey_combo_add(combo, g_str == &kEn ? L"None" : L"Нет", 0);
    for (wchar_t ch = L'A'; ch <= L'Z'; ++ch) {
        wchar_t txt[2] = {ch, 0};
        hotkey_combo_add(combo, txt, static_cast<WORD>(ch));
    }
    for (wchar_t ch = L'0'; ch <= L'9'; ++ch) {
        wchar_t txt[2] = {ch, 0};
        hotkey_combo_add(combo, txt, static_cast<WORD>(ch));
    }
    for (int i = 1; i <= 12; ++i) {
        std::wstring txt = L"F" + std::to_wstring(i);
        hotkey_combo_add(combo, txt.c_str(), static_cast<WORD>(VK_F1 + i - 1));
    }
    hotkey_combo_add(combo, g_str == &kEn ? L"Space" : L"Пробел", VK_SPACE);
    hotkey_combo_add(combo, L"Home", VK_HOME);
    hotkey_combo_add(combo, L"End", VK_END);
    hotkey_combo_add(combo, L"Delete", VK_DELETE);
    hotkey_combo_add(combo, L"Esc", VK_ESCAPE);
    hotkey_combo_add(combo, g_str == &kEn ? L"Left" : L"Влево", VK_LEFT);
    hotkey_combo_add(combo, g_str == &kEn ? L"Right" : L"Вправо", VK_RIGHT);
    hotkey_combo_add(combo, g_str == &kEn ? L"Up" : L"Вверх", VK_UP);
    hotkey_combo_add(combo, g_str == &kEn ? L"Down" : L"Вниз", VK_DOWN);
    hotkey_combo_add(combo, L"+", VK_OEM_PLUS);
    hotkey_combo_add(combo, L"-", VK_OEM_MINUS);
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

int combo_index_by_key(HWND combo, WORD key) {
    int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i)
        if (static_cast<WORD>(SendMessageW(combo, CB_GETITEMDATA, i, 0)) == key) return i;
    return 0;
}

int settings_selected_hotkey_command(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SET_HOTKEY_LIST);
    if (!list) return 0;
    int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return 0;
    return static_cast<int>(SendMessageW(list, LB_GETITEMDATA, sel, 0));
}

void load_selected_hotkey_controls(HWND hwnd) {
    int command = settings_selected_hotkey_command(hwnd);
    const HotkeyBinding* hk = find_hotkey_binding(command);
    BYTE fvirt = hk ? hk->fvirt : FVIRTKEY;
    WORD key = hk ? hk->key : 0;
    SendMessageW(GetDlgItem(hwnd, IDC_SET_HOTKEY_CTRL), BM_SETCHECK, (fvirt & FCONTROL) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, IDC_SET_HOTKEY_SHIFT), BM_SETCHECK, (fvirt & FSHIFT) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, IDC_SET_HOTKEY_ALT), BM_SETCHECK, (fvirt & FALT) ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND combo = GetDlgItem(hwnd, IDC_SET_HOTKEY_KEY);
    if (combo) SendMessageW(combo, CB_SETCURSEL, combo_index_by_key(combo, key), 0);
}

void populate_hotkey_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SET_HOTKEY_LIST);
    if (!list) return;
    int selected_command = settings_selected_hotkey_command(hwnd);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int selected_index = 0;
    const auto order = hotkey_command_order();
    for (std::size_t i = 0; i < order.size(); ++i) {
        std::wstring item = hotkey_list_item_text(order[i]);
        int idx = static_cast<int>(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str())));
        SendMessageW(list, LB_SETITEMDATA, idx, order[i]);
        if (order[i] == selected_command) selected_index = idx;
    }
    SendMessageW(list, LB_SETCURSEL, selected_index, 0);
}

const wchar_t* transform_group_title_text() {
    return (g_str == &kEn) ? L"Signal transform" : L"Преобразование сигналов";
}

const wchar_t* transform_global_mul_text() {
    return (g_str == &kEn) ? L"Global multiplier:" : L"Общий множитель:";
}

const wchar_t* transform_global_add_text() {
    return (g_str == &kEn) ? L"Global addend:" : L"Общее слагаемое:";
}

const wchar_t* transform_channel_mul_text() {
    return (g_str == &kEn) ? L"Channel multiplier:" : L"Множитель канала:";
}

const wchar_t* transform_channel_add_text() {
    return (g_str == &kEn) ? L"Channel addend:" : L"Слагаемое канала:";
}

const wchar_t* transform_apply_text() {
    return (g_str == &kEn) ? L"Apply" : L"Применить";
}

const wchar_t* transform_reset_channel_text() {
    return (g_str == &kEn) ? L"Reset channel" : L"Сбросить канал";
}

const wchar_t* transform_reset_all_text() {
    return (g_str == &kEn) ? L"Reset all" : L"Сбросить всё";
}

const wchar_t* transform_hint_text() {
    return (g_str == &kEn)
        ? L"Result = raw * global * channel + global add + channel add"
        : L"Итог = исходное * общий * канал + общее слагаемое + слагаемое канала";
}

void populate_transform_channel_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SET_TRANSFORM_LIST);
    if (!list) return;
    ensure_channel_transform_vectors();
    int current_sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int select_index = 0;
    for (std::size_t i = 0; i < g.channel_labels.size(); ++i) {
        std::wstring label = g.channel_labels[i];
        int idx = static_cast<int>(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(list, LB_SETITEMDATA, idx, static_cast<LPARAM>(i));
        if (current_sel != LB_ERR && static_cast<int>(i) == current_sel) select_index = idx;
    }
    if (!g.channel_labels.empty()) {
        SendMessageW(list, LB_SETCURSEL, select_index, 0);
    }
}

int settings_selected_transform_channel(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SET_TRANSFORM_LIST);
    if (!list) return -1;
    int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return -1;
    return static_cast<int>(SendMessageW(list, LB_GETITEMDATA, sel, 0));
}

void load_selected_transform_controls(HWND hwnd) {
    HWND global_mul = GetDlgItem(hwnd, IDC_SET_GLOBAL_MUL);
    HWND global_add = GetDlgItem(hwnd, IDC_SET_GLOBAL_ADD);
    if (global_mul) SetWindowTextW(global_mul, format_edit_number(g.global_value_mul).c_str());
    if (global_add) SetWindowTextW(global_add, format_edit_number(g.global_value_add).c_str());

    const int ci = settings_selected_transform_channel(hwnd);
    const bool have_channel = ci >= 0 && ci < static_cast<int>(g.channel_value_mul.size());
    HWND channel_mul = GetDlgItem(hwnd, IDC_SET_CHANNEL_MUL);
    HWND channel_add = GetDlgItem(hwnd, IDC_SET_CHANNEL_ADD);
    HWND apply = GetDlgItem(hwnd, IDC_SET_TRANSFORM_APPLY);
    HWND reset_channel = GetDlgItem(hwnd, IDC_SET_TRANSFORM_RESET_CHANNEL);
    if (channel_mul) {
        SetWindowTextW(channel_mul, have_channel ? format_edit_number(g.channel_value_mul[static_cast<std::size_t>(ci)]).c_str() : L"1");
        EnableWindow(channel_mul, have_channel);
    }
    if (channel_add) {
        SetWindowTextW(channel_add, have_channel ? format_edit_number(g.channel_value_add[static_cast<std::size_t>(ci)]).c_str() : L"0");
        EnableWindow(channel_add, have_channel);
    }
    if (apply) EnableWindow(apply, TRUE);
    if (reset_channel) EnableWindow(reset_channel, have_channel);
}

bool apply_signal_transform_controls(HWND hwnd, bool reset_channel, bool reset_all) {
    double global_mul = g.global_value_mul;
    double global_add = g.global_value_add;
    if (!reset_all) {
        if (!read_edit_double(hwnd, IDC_SET_GLOBAL_MUL, global_mul) ||
            !read_edit_double(hwnd, IDC_SET_GLOBAL_ADD, global_add)) {
            MessageBoxW(hwnd,
                g_str == &kEn ? L"Enter valid numbers for the global transform." : L"Введите корректные числа для общего преобразования.",
                settings_window_title(), MB_OK | MB_ICONWARNING);
            return false;
        }
    }

    g.global_value_mul = reset_all ? 1.0 : global_mul;
    g.global_value_add = reset_all ? 0.0 : global_add;

    ensure_channel_transform_vectors();
    const int ci = settings_selected_transform_channel(hwnd);
    if (reset_all) {
        std::fill(g.channel_value_mul.begin(), g.channel_value_mul.end(), 1.0);
        std::fill(g.channel_value_add.begin(), g.channel_value_add.end(), 0.0);
    } else if (reset_channel) {
        if (ci >= 0) reset_channel_transform(static_cast<std::size_t>(ci));
    } else if (ci >= 0 && ci < static_cast<int>(g.channel_value_mul.size())) {
        double channel_mul = 1.0;
        double channel_add = 0.0;
        if (!read_edit_double(hwnd, IDC_SET_CHANNEL_MUL, channel_mul) ||
            !read_edit_double(hwnd, IDC_SET_CHANNEL_ADD, channel_add)) {
            MessageBoxW(hwnd,
                g_str == &kEn ? L"Enter valid numbers for the selected channel." : L"Введите корректные числа для выбранного канала.",
                settings_window_title(), MB_OK | MB_ICONWARNING);
            return false;
        }
        g.channel_value_mul[static_cast<std::size_t>(ci)] = channel_mul;
        g.channel_value_add[static_cast<std::size_t>(ci)] = channel_add;
    }

    load_selected_transform_controls(hwnd);
    on_signal_transform_changed();
    return true;
}

void refresh_settings_controls() {
    if (!g.settings_wnd) return;
    CheckRadioButton(g.settings_wnd, IDC_SET_LANG_RU, IDC_SET_LANG_EN, g_str == &kEn ? IDC_SET_LANG_EN : IDC_SET_LANG_RU);
    populate_transform_channel_list(g.settings_wnd);
    load_selected_transform_controls(g.settings_wnd);
    SendMessageW(GetDlgItem(g.settings_wnd, IDM_PT_NUM), BM_SETCHECK, g.pdisp.number ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(g.settings_wnd, IDM_PT_X), BM_SETCHECK, g.pdisp.x ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(g.settings_wnd, IDM_PT_Y), BM_SETCHECK, g.pdisp.y ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(g.settings_wnd, IDM_PT_DX), BM_SETCHECK, g.pdisp.dx ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(g.settings_wnd, IDM_PT_DY), BM_SETCHECK, g.pdisp.dy ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(g.settings_wnd, IDM_PT_INVDT), BM_SETCHECK, g.pdisp.inv_dt ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(g.settings_wnd, IDM_PT_DIST), BM_SETCHECK, g.pdisp.dist ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(g.settings_wnd, IDM_SNAP), BM_SETCHECK, g.snap_to_data ? BST_CHECKED : BST_UNCHECKED, 0);
    populate_hotkey_list(g.settings_wnd);
    load_selected_hotkey_controls(g.settings_wnd);
}

void rebuild_menu_bar() {
    if (!g.main) return;
    SetMenu(g.main, nullptr);
    if (g.menu) DestroyMenu(g.menu);
    g.menu = make_menu();
    SetMenu(g.main, g.menu);
    MENUINFO mi = { sizeof(mi) };
    mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
    mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
    SetMenuInfo(g.menu, &mi);
    sync_menu();
    DrawMenuBar(g.main);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
                HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
                    id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr, inst, nullptr);
                if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return c;
            };
            const bool en = (g_str == &kEn);
            mk(L"BUTTON", en ? L"General" : L"Общие", BS_GROUPBOX, 12, 10, 510, 72, 0);
            mk(L"BUTTON", g_str->lang_ru, BS_AUTORADIOBUTTON, 28, 36, 110, 22, IDC_SET_LANG_RU);
            mk(L"BUTTON", g_str->lang_en, BS_AUTORADIOBUTTON, 144, 36, 110, 22, IDC_SET_LANG_EN);

            mk(L"BUTTON", transform_group_title_text(), BS_GROUPBOX, 12, 92, 510, 206, 0);
            mk(L"STATIC", transform_global_mul_text(), SS_LEFT, 24, 118, 148, 20, 0);
            mk(L"EDIT", format_edit_number(g.global_value_mul).c_str(), WS_BORDER | ES_AUTOHSCROLL, 176, 114, 70, 24, IDC_SET_GLOBAL_MUL);
            mk(L"STATIC", transform_global_add_text(), SS_LEFT, 258, 118, 150, 20, 0);
            mk(L"EDIT", format_edit_number(g.global_value_add).c_str(), WS_BORDER | ES_AUTOHSCROLL, 410, 114, 88, 24, IDC_SET_GLOBAL_ADD);
            mk(L"STATIC", transform_hint_text(), SS_LEFT, 24, 146, 474, 18, 0);
            mk(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 24, 172, 206, 108, IDC_SET_TRANSFORM_LIST);
            mk(L"STATIC", transform_channel_mul_text(), SS_LEFT, 252, 176, 136, 20, 0);
            mk(L"EDIT", L"1", WS_BORDER | ES_AUTOHSCROLL, 394, 172, 104, 24, IDC_SET_CHANNEL_MUL);
            mk(L"STATIC", transform_channel_add_text(), SS_LEFT, 252, 210, 136, 20, 0);
            mk(L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, 394, 206, 104, 24, IDC_SET_CHANNEL_ADD);
            mk(L"BUTTON", transform_apply_text(), BS_OWNERDRAW, 252, 246, 110, 28, IDC_SET_TRANSFORM_APPLY);
            mk(L"BUTTON", transform_reset_channel_text(), BS_OWNERDRAW, 388, 246, 110, 28, IDC_SET_TRANSFORM_RESET_CHANNEL);
            mk(L"BUTTON", transform_reset_all_text(), BS_OWNERDRAW, 252, 278, 246, 28, IDC_SET_TRANSFORM_RESET_ALL);

            mk(L"BUTTON", en ? L"Markers and points" : L"Маркеры и точки", BS_GROUPBOX, 12, 310, 510, 238, 0);
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
            int y = 312;
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
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 16, y, 180, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDS_COLOR)), inst, nullptr);
            SendMessageW(col, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            mk(L"BUTTON", en ? L"Hotkeys" : L"Горячие клавиши", BS_GROUPBOX, 12, 558, 510, 188, 0);
            mk(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 24, 582, 240, 150, IDC_SET_HOTKEY_LIST);
            mk(L"BUTTON", L"Ctrl", BS_AUTOCHECKBOX, 284, 590, 70, 22, IDC_SET_HOTKEY_CTRL);
            mk(L"BUTTON", L"Shift", BS_AUTOCHECKBOX, 356, 590, 70, 22, IDC_SET_HOTKEY_SHIFT);
            mk(L"BUTTON", L"Alt", BS_AUTOCHECKBOX, 428, 590, 70, 22, IDC_SET_HOTKEY_ALT);
            mk(L"STATIC", en ? L"Key:" : L"Клавиша:", SS_LEFT, 284, 622, 80, 20, 0);
            HWND combo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER, 284, 642, 214, 260, IDC_SET_HOTKEY_KEY);
            populate_hotkey_key_combo(combo);
            mk(L"BUTTON", en ? L"Apply" : L"Применить", BS_OWNERDRAW, 284, 680, 100, 28, IDC_SET_HOTKEY_APPLY);
            mk(L"BUTTON", en ? L"Reset" : L"Сбросить", BS_OWNERDRAW, 398, 680, 100, 28, IDC_SET_HOTKEY_RESET);
            mk(L"BUTTON", en ? L"Clear" : L"Очистить", BS_OWNERDRAW, 284, 714, 100, 28, IDC_SET_HOTKEY_CLEAR);
            populate_transform_channel_list(hwnd);
            load_selected_transform_controls(hwnd);
            populate_hotkey_list(hwnd);
            load_selected_hotkey_controls(hwnd);
            CheckRadioButton(hwnd, IDC_SET_LANG_RU, IDC_SET_LANG_EN, g_str == &kEn ? IDC_SET_LANG_EN : IDC_SET_LANG_RU);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            auto checked = [&]() { return SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED; };
            switch (id) {
                case IDC_SET_LANG_RU:
                    if (HIWORD(wp) == BN_CLICKED && g_str != &kRu) { g_str = &kRu; rebuild_ui(); }
                    break;
                case IDC_SET_LANG_EN:
                    if (HIWORD(wp) == BN_CLICKED && g_str != &kEn) { g_str = &kEn; rebuild_ui(); }
                    break;
                case IDM_PT_NUM:   g.pdisp.number = checked(); break;
                case IDM_PT_X:     g.pdisp.x = checked(); break;
                case IDM_PT_Y:     g.pdisp.y = checked(); break;
                case IDM_PT_DX:    g.pdisp.dx = checked(); break;
                case IDM_PT_DY:    g.pdisp.dy = checked(); break;
                case IDM_PT_INVDT: g.pdisp.inv_dt = checked(); break;
                case IDM_PT_DIST:  g.pdisp.dist = checked(); break;
                case IDM_SNAP:     g.snap_to_data = checked(); break;
                case IDC_SET_TRANSFORM_LIST:
                    if (HIWORD(wp) == LBN_SELCHANGE) load_selected_transform_controls(hwnd);
                    return 0;
                case IDC_SET_TRANSFORM_APPLY:
                    apply_signal_transform_controls(hwnd, false, false);
                    return 0;
                case IDC_SET_TRANSFORM_RESET_CHANNEL:
                    apply_signal_transform_controls(hwnd, true, false);
                    return 0;
                case IDC_SET_TRANSFORM_RESET_ALL:
                    apply_signal_transform_controls(hwnd, false, true);
                    return 0;
                case IDC_SET_HOTKEY_LIST:
                    if (HIWORD(wp) == LBN_SELCHANGE) load_selected_hotkey_controls(hwnd);
                    return 0;
                case IDC_SET_HOTKEY_APPLY:
                case IDC_SET_HOTKEY_RESET:
                case IDC_SET_HOTKEY_CLEAR: {
                    const int command = settings_selected_hotkey_command(hwnd);
                    if (!command) return 0;
                    BYTE fvirt = FVIRTKEY;
                    WORD key = 0;
                    if (id == IDC_SET_HOTKEY_RESET) {
                        HotkeyBinding def = default_hotkey_for_command(command);
                        fvirt = def.fvirt;
                        key = def.key;
                    } else if (id != IDC_SET_HOTKEY_CLEAR) {
                        if (SendMessageW(GetDlgItem(hwnd, IDC_SET_HOTKEY_CTRL), BM_GETCHECK, 0, 0) == BST_CHECKED) fvirt |= FCONTROL;
                        if (SendMessageW(GetDlgItem(hwnd, IDC_SET_HOTKEY_SHIFT), BM_GETCHECK, 0, 0) == BST_CHECKED) fvirt |= FSHIFT;
                        if (SendMessageW(GetDlgItem(hwnd, IDC_SET_HOTKEY_ALT), BM_GETCHECK, 0, 0) == BST_CHECKED) fvirt |= FALT;
                        HWND combo = GetDlgItem(hwnd, IDC_SET_HOTKEY_KEY);
                        int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                        if (sel != CB_ERR) key = static_cast<WORD>(SendMessageW(combo, CB_GETITEMDATA, sel, 0));
                    }
                    int conflict = find_conflicting_hotkey(fvirt, key, command);
                    if (conflict) {
                        std::wstring msg = (g_str == &kEn ? L"Already used by: " : L"Уже используется действием: ");
                        msg += command_name(conflict);
                        MessageBoxW(hwnd, msg.c_str(), settings_window_title(), MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    set_hotkey_binding(command, fvirt, key);
                    rebuild_accelerators();
                    rebuild_menu_bar();
                    populate_hotkey_list(hwnd);
                    load_selected_hotkey_controls(hwnd);
                    set_status();
                    InvalidateRect(g.main, nullptr, TRUE);
                    return 0;
                }
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
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis->hwndItem) break;
            wchar_t txt[128];
            GetWindowTextW(dis->hwndItem, txt, 128);
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            draw_themed_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_panel);
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
            WS_EX_TOOLWINDOW, L"LvmPtSettings", settings_window_title(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 540, 800,
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
    refresh_settings_controls();
    ShowWindow(g.settings_wnd, SW_SHOW);
    SetForegroundWindow(g.settings_wnd);
}

// ---- welcome / start screen ----------------------------------------------

void rebuild_ui();

LRESULT CALLBACK WelcomeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance;
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto mkstatic = [&](const wchar_t* text, int id, HFONT use_font) {
                HWND ctl = CreateWindowExW(
                    0, L"STATIC", text,
                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                    0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(use_font ? use_font : font), TRUE);
                return ctl;
            };
            auto mkbtn = [&](const wchar_t* text, int id) {
                HWND ctl = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return ctl;
            };

            mkstatic(g_str->welcome_title, IDW_TITLE, g.title_font ? g.title_font : font);
            mkstatic(g_str->welcome_subtitle, IDW_SUBTITLE, g.bold_font ? g.bold_font : font);
            mkstatic(welcome_intro_text().c_str(), IDW_INTRO, font);
            mkstatic(welcome_features_text().c_str(), IDW_FEATURES, font);
            mkstatic(g_str->m_lang, IDW_LANG_LABEL, g.bold_font ? g.bold_font : font);
            mkbtn(g_str->lang_ru, IDM_LANG_RU);
            mkbtn(g_str->lang_en, IDM_LANG_EN);
            mkstatic(welcome_actions_title_text(), IDW_ACTIONS_TITLE, g.bold_font ? g.bold_font : font);
            mkbtn(g_str->welcome_btn_open, IDC_OPEN);
            mkbtn(settings_button_text(), IDC_PTSETTINGS);
            mkbtn(g_str->welcome_btn_hotkeys, IDM_HOTKEYS);
            mkbtn(g_str->welcome_btn_start, IDW_START);
            layout_welcome_controls(hwnd);
            return 0;
        }
        case WM_SIZE:
            layout_welcome_controls(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH bg = CreateSolidBrush(g_theme->bg_main);
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);

            WelcomeLayout layout = compute_welcome_layout(hwnd);
            fill_rounded_rect(hdc, layout.hero, g_theme->bg_panel, g_theme->separator, 18);
            fill_rounded_rect(hdc, layout.action, g_theme->btn_bg, g_theme->separator, 18);

            RECT hero_accent = {
                layout.hero.left + 22,
                layout.hero.top + 18,
                layout.hero.left + min(140, rect_width(layout.hero) - 44),
                layout.hero.top + 24
            };
            HBRUSH accent_brush = CreateSolidBrush(g_theme->accent);
            FillRect(hdc, &hero_accent, accent_brush);
            DeleteObject(accent_brush);

            RECT action_header = {
                layout.action.left + 20,
                layout.action.top + 18,
                layout.action.right - 20,
                layout.action.top + 22
            };
            HBRUSH action_accent = CreateSolidBrush(g_theme->separator);
            FillRect(hdc, &action_header, action_accent);
            DeleteObject(action_accent);

            HFONT old_font = reinterpret_cast<HFONT>(SelectObject(
                hdc, g.axis_font ? g.axis_font : (g.ui_font ? g.ui_font : GetStockObject(DEFAULT_GUI_FONT))));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_theme->text_secondary);
            RECT credit = {
                rc.left + 24,
                max(rc.top + 8, rc.bottom - 64),
                rc.right - 24,
                rc.bottom - 14
            };
            DrawTextW(
                hdc,
                L"Приложение разработал\r\nАлександр Мулеев\r\nal.muleev@gmail.com",
                -1,
                &credit,
                DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, old_font);
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
                case IDM_LANG_RU: if (g_str != &kRu) { g_str = &kRu; rebuild_ui(); } return 0;
                case IDM_LANG_EN: if (g_str != &kEn) { g_str = &kEn; rebuild_ui(); } return 0;
            }
            return 0;
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis->hwndItem) break;
            const HDC dc = dis->hDC;
            const RECT r = dis->rcItem;
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            int ctl_id = GetDlgCtrlID(dis->hwndItem);
            bool is_lang_button = (ctl_id == IDM_LANG_RU || ctl_id == IDM_LANG_EN);
            bool active = is_lang_button &&
                ((ctl_id == IDM_LANG_RU && g_str == &kRu) || (ctl_id == IDM_LANG_EN && g_str == &kEn));
            wchar_t txt[128]{};
            GetWindowTextW(dis->hwndItem, txt, 128);

            if (ctl_id == IDW_START) {
                COLORREF bg_col = pressed ? g_theme->accent_hover : g_theme->accent;
                draw_button_with_colors(dc, r, txt, bg_col, bg_col, RGB(255, 255, 255), pressed);
                return TRUE;
            }
            if (ctl_id == IDC_OPEN) {
                COLORREF bg_col = pressed ? g_theme->btn_hover : g_theme->bg_panel;
                draw_button_with_colors(dc, r, txt, bg_col, g_theme->accent, g_theme->text_primary, pressed);
                return TRUE;
            }
            if (ctl_id == IDC_PTSETTINGS || ctl_id == IDM_HOTKEYS) {
                COLORREF bg_col = pressed ? g_theme->btn_hover : g_theme->btn_bg;
                draw_button_with_colors(dc, r, txt, bg_col, g_theme->btn_border, g_theme->text_primary, pressed);
                return TRUE;
            }
            draw_themed_button(dc, r, txt, pressed, active, false);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            int ctl_id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if (ctl_id == IDW_SUBTITLE) {
                SetTextColor(dc, g_theme->text_secondary);
            } else if (ctl_id == IDW_LANG_LABEL || ctl_id == IDW_ACTIONS_TITLE) {
                SetTextColor(dc, g_theme->accent);
            } else {
                SetTextColor(dc, g_theme->text_primary);
            }
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
        RECT rc;
        GetClientRect(g.main, &rc);
        SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
        RedrawWindow(g.welcome_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
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
    SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
    RedrawWindow(g.welcome_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

// ---- UI rebuild (language switch) --------------------------------------
void rebuild_ui() {
    if (!g.main) return;
    finish_channel_rename(true);
    rebuild_menu_bar();
    
    // Update buttons
    SetWindowTextW(g.open, g_str->btn_open);
    SetWindowTextW(g.savepng, g_str->btn_png);
    SetWindowTextW(g.savecsv, g_str->btn_csv);
    SetWindowTextW(g.mode_time, g_str->st_time);
    SetWindowTextW(g.mode_freq, g_str->st_hz);
    SetWindowTextW(g.play, g.playing ? g_str->btn_pause : g_str->btn_play);
    SetWindowTextW(g.measure, g_str->btn_measure);
    SetWindowTextW(g.marker_btn, g_str == &kEn ? L"Marker" : L"Маркер");
    SetWindowTextW(g.vline_btn, g_str == &kEn ? L"V-Line" : L"V-линия");
    SetWindowTextW(g.hline_btn, g_str == &kEn ? L"H-Line" : L"H-линия");
    SetWindowTextW(g.reset, g_str->btn_reset);
    SetWindowTextW(g.autoy, g_str->btn_autoy);
    SetWindowTextW(g.ptsettings, settings_button_text());
    SetWindowTextW(g.show_all_btn, channel_show_all_text());
    SetWindowTextW(g.hide_all_btn, channel_hide_all_text());
    
    // Update welcome window if visible
    if (g.welcome_wnd && IsWindowVisible(g.welcome_wnd)) {
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        DestroyWindow(g.welcome_wnd);
        g.welcome_wnd = nullptr;
        show_welcome(inst);
    }
    
    if (g.settings_wnd) {
        bool was_visible = IsWindowVisible(g.settings_wnd) != FALSE;
        DestroyWindow(g.settings_wnd);
        g.settings_wnd = nullptr;
        if (was_visible) open_settings();
    }
    
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

            auto mk = [&](const wchar_t* text, int id, DWORD extra, bool in_toolbar = true) {
                HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | extra,
                                         0, 0, 10, 10, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (in_toolbar) g.buttons.push_back(b);
                else ShowWindow(b, SW_HIDE);
                return b;
            };
            g.open = mk(g_str->btn_open, IDC_OPEN, 0);
            g.savepng = mk(g_str->btn_png, IDC_SAVEPNG, 0, false);
            g.savecsv = mk(g_str->btn_csv, IDC_SAVECSV, 0, false);
            g.mode_time = mk(g_str->st_time, IDM_MODE_TIME, 0);
            g.mode_freq = mk(g_str->st_hz, IDM_MODE_FREQ, 0);
            g.play = mk(g_str->btn_play, IDC_PLAY, 0);
            g.measure = mk(g_str->btn_measure, IDC_MEASURE, 0);
            g.marker_btn = mk(g_str == &kEn ? L"Marker" : L"Маркер", IDM_ADD_MARKER, 0);
            g.vline_btn = mk(g_str == &kEn ? L"V-Line" : L"V-линия", IDM_ADD_VLINE, 0);
            g.hline_btn = mk(g_str == &kEn ? L"H-Line" : L"H-линия", IDM_ADD_HLINE, 0);
            g.reset = mk(g_str->btn_reset, IDC_RESET, 0);
            g.autoy = mk(g_str->btn_autoy, IDC_AUTOY, 0);
            g.ptsettings = mk(settings_button_text(), IDC_PTSETTINGS, 0);
            g.show_all_btn = mk(channel_show_all_text(), IDC_SHOW_ALL, 0);
            g.hide_all_btn = mk(channel_hide_all_text(), IDC_HIDE_ALL, 0);

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
            if (welcome_visible()) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
                RedrawWindow(g.welcome_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                return 0;
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
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_panel);
            SetTextColor(dc, g_theme->text_primary);
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
            bool active = false;
            if (btn == g.measure) {
                active = g.measure_mode;
            } else if (btn == g.autoy) {
                active = g.auto_y;
            } else if (btn == g.mode_time) {
                active = !g.freq_mode;
            } else if (btn == g.mode_freq) {
                active = g.freq_mode;
            } else if (btn == g.marker_btn) {
                active = g.pending_marker;
            } else if (btn == g.vline_btn) {
                active = g.pending_line == 1;
            } else if (btn == g.hline_btn) {
                active = g.pending_line == 2;
            }
            bool hover = (btn == g.hovered_btn);
            wchar_t txt[128];
            GetWindowTextW(btn, txt, 128);
            draw_themed_button(dc, r, txt, pressed, active, hover);
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
                    g.hover_status_text = toolbar_hover_text(new_hover);
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
                    // scrolling a little each frame вЂ” smooth, no big jumps.
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
                case IDC_SAVETXT: save_txt_dialog(); return 0;
                case IDM_EXIT: DestroyWindow(hwnd); return 0;
                case IDC_MODE:
                    set_mode(!g.freq_mode);
                    return 0;
                case IDM_MODE_TIME:
                    set_mode(false);
                    return 0;
                case IDM_MODE_FREQ:
                    set_mode(true);
                    return 0;
                case IDC_PLAY: toggle_play(); return 0;
                case IDC_SHOW_ALL:
                    set_all_channels_visible(true);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDC_HIDE_ALL:
                    set_all_channels_visible(false);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDC_MEASURE:
                    g.measure_mode = !g.measure_mode;
                    if (g.measure_mode) g.pending_line = 0;
                    if (g.measure_mode) g.pending_marker = false;
                    SendMessageW(g.measure, BM_SETCHECK,
                                 g.measure_mode ? BST_CHECKED : BST_UNCHECKED, 0);
                    InvalidateRect(g.measure, nullptr, FALSE);
                    sync_menu();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
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
                case IDM_VPAN:
                    g.vertical_pan = !g.vertical_pan;
                    sync_menu();
                    set_status();
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
                    g.measure_mode = false;
                    SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
                    status_msg(g_str->status_vline);
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDM_ADD_HLINE:
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    g.pending_line = 2;
                    g.pending_marker = false;
                    g.measure_mode = false;
                    SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
                    status_msg(g_str->status_hline);
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
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
                    g.measure_mode = false;
                    SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
                    status_msg(g_str->status_marker);
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDM_CLEAR_MARKERS:
                    if (!g.markers.empty()) {
                        UndoAction ua; ua.type = UndoAction::CLEAR_MARKERS; ua.saved_markers = g.markers;
                        push_undo(ua);
                        g.markers.clear(); InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    g.active_marker = -1;
                    set_status();
                    return 0;
                case IDM_SPEED_00001: set_play_speed(0.0001); return 0;
                case IDM_SPEED_0001: set_play_speed(0.001); return 0;
                case IDM_SPEED_001: set_play_speed(0.01); return 0;
                case IDM_SPEED_01: set_play_speed(0.1); return 0;
                case IDM_SPEED_05: set_play_speed(0.5); return 0;
                case IDM_SPEED_1: set_play_speed(1.0); return 0;
                case IDM_SPEED_2: set_play_speed(2.0); return 0;
                case IDM_SPEED_5: set_play_speed(5.0); return 0;
                case IDM_SPEED_10: set_play_speed(10.0); return 0;
                case IDM_SPEED_CUSTOM: {
                    double speed = g.play_speed;
                    if (prompt_custom_play_speed(speed)) set_play_speed(speed);
                    return 0;
                }
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
                case IDC_GOTO_START: goto_start(); return 0;
                case IDC_GOTO_END: goto_end(); return 0;
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
            } else if (id >= IDC_CHAN_LABEL_BASE &&
                       id < IDC_CHAN_LABEL_BASE + static_cast<int>(g.channel_labels.size()) &&
                       HIWORD(wp) == STN_CLICKED) {
                start_channel_rename(id - IDC_CHAN_LABEL_BASE);
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
                const bool selecting_fft_here =
                    !g.freq_mode && in_plot &&
                    (GetKeyState(VK_SHIFT) & 0x8000) != 0 &&
                    !g.measure_mode && !g.pending_line && !g.pending_marker;
                if (g.dragging || g.fft_selecting || g.measure_mode || g.pending_line || g.pending_marker || selecting_fft_here) {
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
                pan_y_by(up ? -0.1 : 0.1);
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
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (!g.freq_mode && shift && !g.pending_line && !g.pending_marker && !g.measure_mode) {
                const int pw = p.right - p.left;
                if (pw > 0) {
                    const int clamped_x = std::clamp(mx, static_cast<int>(p.left), static_cast<int>(p.right));
                    const double frac = static_cast<double>(clamped_x - p.left) / pw;
                    const double tt = g.win_start + frac * (g.win_end - g.win_start);
                    g.fft_selecting = true;
                    g.fft_select_anchor_x = clamped_x;
                    g.fft_select_current_x = clamped_x;
                    g.fft_select_anchor_t = tt;
                    g.fft_select_current_t = tt;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
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
                // Р РµР¶РёРј РјРЅРѕР¶РµСЃС‚РІРµРЅРЅРѕРіРѕ РґРѕР±Р°РІР»РµРЅРёСЏ: РѕСЃС‚Р°С‘С‚СЃСЏ Р°РєС‚РёРІРЅС‹Рј РґРѕ Esc
                set_status();
                sync_menu();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g.pending_marker) {
                double dx, dy;
                if (px_to_data(mx, my, dx, dy)) {
                    App::Marker mk;
                    int snapped_channel = -1;
                    bool snapped = false;
                    if (g.snap_to_data) snapped = snap_to_nearest_target(dx, dy, &snapped_channel);
                    mk.x = dx;
                    mk.y = dy;
                    mk.freq = g.freq_mode;
                    mk.snapped = snapped;
                    mk.channel = snapped ? snapped_channel : -1;
                    wchar_t buf[16];
                    swprintf(buf, 16, L"M%zu", g.markers.size() + 1);
                    mk.label = buf;
                    g.markers.push_back(mk);
                    g.active_marker = static_cast<int>(g.markers.size()) - 1;
                    UndoAction ua; ua.type = UndoAction::ADD_MARKER; ua.marker = mk;
                    push_undo(ua);
                }
                g.pending_marker = false;
                set_status();
                sync_menu();
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
            g.drag_y = my;
            g.drag_lo = *lo;
            g.drag_hi = *hi;
            // Remember Y range for vertical panning.
            if (g.freq_mode) {
                if (g.auto_y_amp) {
                    double ymax = 0.0;
                    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
                        int ci = channel_index_by_name(g.spec.names[j]);
                        if (ci < 0 || !g.visible[ci]) continue;
                        for (auto v : g.spec.amp[j]) if (v > ymax) ymax = v;
                    }
                    if (ymax <= 0) ymax = 1.0;
                    g.drag_y_hi = ymax * 1.08;
                } else {
                    g.drag_y_hi = g.y_amp_max;
                }
                g.drag_y_lo = 0.0;
            } else {
                if (g.auto_y) {
                    current_time_yrange(g.drag_y_lo, g.drag_y_hi);
                } else {
                    g.drag_y_lo = g.y_lock_min;
                    g.drag_y_hi = g.y_lock_max;
                }
            }
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
            if (g.fft_selecting) {
                const RECT p = plot_rect();
                const int pw = p.right - p.left;
                if (pw > 0) {
                    const int clamped_x = std::clamp(GET_X_LPARAM(lp), static_cast<int>(p.left), static_cast<int>(p.right));
                    const double frac = static_cast<double>(clamped_x - p.left) / pw;
                    g.fft_select_current_x = clamped_x;
                    g.fft_select_current_t = g.win_start + frac * (g.win_end - g.win_start);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            const int hovered_marker = hit_test_marker(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hovered_marker >= 0 && hovered_marker != g.active_marker) {
                g.active_marker = hovered_marker;
                set_status();
                RECT rc; GetClientRect(hwnd, &rc);
                RECT sr = {0, rc.bottom - kBottomBar, rc.right, rc.bottom};
                InvalidateRect(hwnd, &sr, FALSE);
            }
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
            // Vertical panning
            if (g.vertical_pan) {
                const int ph = p.bottom - p.top;
                if (ph > 0) {
                    const double dy = static_cast<double>(GET_Y_LPARAM(lp) - g.drag_y) / ph * (g.drag_y_hi - g.drag_y_lo);
                    if (g.freq_mode) {
                        double new_ytop = g.drag_y_hi + dy;
                        if (new_ytop < 1e-12) new_ytop = 1e-12;
                        g.y_amp_max = new_ytop;
                        g.auto_y_amp = false;
                    } else {
                        double new_lo = g.drag_y_lo + dy;
                        double new_hi = g.drag_y_hi + dy;
                        g.y_lock_min = new_lo;
                        g.y_lock_max = new_hi;
                        g.auto_y = false;
                        if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_UNCHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
                    }
                }
            }
            set_status();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
            if (g.fft_selecting) {
                g.fft_selecting = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                if (std::abs(g.fft_select_current_x - g.fft_select_anchor_x) >= 4) {
                    set_fft_window(g.fft_select_anchor_t, g.fft_select_current_t);
                }
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g.dragging) { g.dragging = false; ReleaseCapture(); }
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                if (g.pending_line || g.pending_marker) {
                    g.pending_line = 0;
                    g.pending_marker = false;
                    set_status();
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (g.fft_selecting) {
                    g.fft_selecting = false;
                    if (GetCapture() == hwnd) ReleaseCapture();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (has_fft_window()) {
                    clear_fft_window();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            break;
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wp);
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            if (count > 0) {
                wchar_t path[MAX_PATH];
                if (DragQueryFileW(hDrop, 0, path, MAX_PATH)) {
                    if (!load_path(path))
                        MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
                }
            }
            DragFinish(hDrop);
            return 0;
        }
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
    ensure_hotkeys_initialized();
    std::vector<ACCEL> acc;
    acc.reserve(g.hotkeys.size());
    for (const auto& hk : g.hotkeys) {
        if (hk.key == 0) continue;
        ACCEL a = {};
        a.fVirt = hk.fvirt;
        a.key = hk.key;
        a.cmd = static_cast<WORD>(hk.command);
        acc.push_back(a);
    }
    if (acc.empty()) return nullptr;
    return CreateAcceleratorTableW(acc.data(), static_cast<int>(acc.size()));
}

void rebuild_accelerators() {
    HACCEL fresh = make_accelerators();
    if (g.accel) DestroyAcceleratorTable(g.accel);
    g.accel = fresh;
}

bool should_bypass_accelerators() {
    HWND focus = GetFocus();
    if (!focus) return false;
    if (g.channel_edit && (focus == g.channel_edit || IsChild(g.channel_edit, focus) != FALSE)) return true;
    if (g.settings_wnd && (focus == g.settings_wnd || IsChild(g.settings_wnd, focus) != FALSE)) return true;
    return false;
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
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720,
                             nullptr, nullptr, inst, nullptr);
    if (!g.main) return 1;

    ShowWindow(g.main, show);
    UpdateWindow(g.main);
    DragAcceptFiles(g.main, TRUE);

    if (cmd && *cmd) {
        std::wstring path = cmd;
        if (!path.empty() && path.front() == L'"') path = path.substr(1, path.find_last_of(L'"') - 1);
        if (!load_path(path))
            MessageBoxW(g.main, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
    } else {
        show_welcome(inst);   // start screen when launched without a file
    }

    ensure_hotkeys_initialized();
    rebuild_accelerators();
    MSG m;
    while (GetMessage(&m, nullptr, 0, 0) > 0) {
        if (should_bypass_accelerators() || !g.accel || !TranslateAcceleratorW(g.main, g.accel, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    if (g.accel) DestroyAcceleratorTable(g.accel);
    Gdiplus::GdiplusShutdown(g_gdiplus_token);
    return 0;
}
