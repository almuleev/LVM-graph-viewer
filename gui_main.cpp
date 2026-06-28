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
//   g++ -std=c++17 -O2 -municode -static -mwindows -o LVM-graph-viewer-win-x64.exe
//       gui_main.cpp lvm_parser.cpp fft.cpp analysis.cpp
//       -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef APP_VERSION_W
#define APP_VERSION_W L"v0.0.0"
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
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
    IDC_SIDEPANEL,      // toolbar: show / hide the docked side panel
    IDC_SHOW_ALL,
    IDC_HIDE_ALL,

    // Menu-only commands (no toolbar button).
    IDM_EXIT = 1100,
    IDM_VISMOOTH,       // visual (spline) smoothing toggle
    IDM_VPAN,           // vertical pan toggle
    IDM_SNAP,           // snap measurement markers to data
    IDM_ADD_VLINE,      // arm: place a vertical guide line
    IDM_ADD_HLINE,      // arm: place a horizontal guide line
    IDM_ADD_VLINE_EXACT, // add a vertical guide line by exact value
    IDM_ADD_HLINE_EXACT, // add a horizontal guide line by exact value
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
    IDW_LIGHT_MODE = 1800,

    IDC_CHAN_BASE = 2000,
    IDC_CHAN_LABEL_BASE = 3000,
    IDC_CHAN_EDIT = 4000,

    IDC_SIDE_TAB_CHANNELS = 4200,
    IDC_SIDE_TAB_POINTS,
    IDC_SIDE_CHANNEL_COLOR,
    IDC_SIDE_CHANNEL_HINT,
    IDC_SIDE_GLOBAL_FORMULA_EDIT,
    IDC_SIDE_GLOBAL_FORMULA_APPLY,
    IDC_SIDE_FORMULA_EDIT,
    IDC_SIDE_FORMULA_APPLY_SELECTED,
    IDC_SIDE_FORMULA_APPLY_VISIBLE,
    IDC_SIDE_FORMULA_RESET_SELECTED,
    IDC_SIDE_FORMULA_RESET_ALL,
    IDC_SIDE_POINT_GROUP_LIST,
    IDC_SIDE_POINT_GROUP_VISIBLE,
    IDC_SIDE_POINT_GROUP_NEW,
    IDC_SIDE_POINT_GROUP_DELETE,
    IDC_SIDE_POINT_GROUP_NAME,
    IDC_SIDE_POINT_GROUP_RENAME,
    IDC_SIDE_POINT_COLOR_CURRENT,
    IDC_SIDE_POINT_GROUP_COLOR,
    IDC_SIDE_PT_NUM,
    IDC_SIDE_PT_X,
    IDC_SIDE_PT_Y,
    IDC_SIDE_PT_DX,
    IDC_SIDE_PT_DY,
    IDC_SIDE_PT_INVDT,
    IDC_SIDE_PT_DIST,
    IDC_SIDE_PT_SNAP,

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
    IDC_SET_POINT_GROUP_LIST = 5120,
    IDC_SET_POINT_GROUP_VISIBLE,
    IDC_SET_POINT_COLOR_CURRENT,
    IDC_SET_POINT_GROUP_COLOR,
    IDC_SET_POINT_GROUP_NEW,
    IDC_SET_LIGHT_MODE = 5130,
    IDC_SET_GAP_MARKERS,

    IDC_SET_GROUP_GENERAL = 5150,
    IDC_SET_GROUP_TRANSFORM,
    IDC_SET_GROUP_POINTS,
    IDC_SET_GROUP_HOTKEYS,

    IDW_TITLE = 5200,
    IDW_SUBTITLE,
    IDW_VERSION,
    IDW_INTRO,
    IDW_FEATURES,
    IDW_ACTIONS_TITLE,
    IDW_ACTIONS_HINT,
    IDW_LANG_LABEL,
    IDW_THEME_LABEL,
    IDW_THEME_LIGHT,
    IDW_THEME_DARK,
};

const int kTopBar = 72;        // two-row compact toolbar
const int kRightPanel = 312;
const int kBottomBar = 28;     // status-bar strip at the very bottom
const int kAxisBottom = 38;    // room under the plot for the X tick labels + title
const int kAxisLeft = 70;

const COLORREF kPalette[] = {
    RGB(31, 119, 180), RGB(255, 127, 14), RGB(44, 160, 44), RGB(214, 39, 40),
    RGB(148, 103, 189), RGB(140, 86, 75), RGB(227, 119, 194), RGB(127, 127, 127),
    RGB(188, 189, 34), RGB(23, 190, 207),
};
std::vector<COLORREF> g_channel_colors;
COLORREF channel_color(std::size_t i) {
    if (i < g_channel_colors.size()) return g_channel_colors[i];
    return kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

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
HBRUSH g_input_brush = nullptr;
HBRUSH g_welcome_brush = nullptr;
HBRUSH g_welcome_hero_brush = nullptr;
HBRUSH g_welcome_action_brush = nullptr;
std::wstring g_config_path;

struct OwnerDrawMenuEntry {
    std::wstring text;
    bool top_level = false;
    bool popup = false;
};

std::vector<std::unique_ptr<OwnerDrawMenuEntry>> g_menu_text_storage;

struct NumericPromptState {
    HWND wnd = nullptr;
    HWND edit = nullptr;
    bool done = false;
    bool accepted = false;
    bool positive_only = true;
    double value = 1.0;
    std::wstring title;
    std::wstring label;
    std::wstring apply_text;
    std::wstring cancel_text;
    std::wstring invalid_text;
};

NumericPromptState g_numeric_prompt;

struct RangePromptState {
    HWND wnd = nullptr;
    HWND start_edit = nullptr;
    HWND end_edit = nullptr;
    bool done = false;
    bool accepted = false;
    double start_value = 0.0;
    double end_value = 0.0;
    double min_value = 0.0;
    double max_value = 0.0;
    std::wstring title;
    std::wstring info_label;
    std::wstring start_label;
    std::wstring end_label;
    std::wstring apply_text;
    std::wstring cancel_text;
    std::wstring invalid_start_text;
    std::wstring invalid_end_text;
};

RangePromptState g_range_prompt;

struct HotkeysDialogState {
    HWND wnd = nullptr;
    HWND list = nullptr;
    bool done = false;
};

HotkeysDialogState g_hotkeys_dialog;

void update_theme_brushes() {
    if (g_panel_brush) DeleteObject(g_panel_brush);
    g_panel_brush = CreateSolidBrush(g_theme->bg_panel);
    if (g_input_brush) DeleteObject(g_input_brush);
    g_input_brush = CreateSolidBrush(g_theme->bg_plot);
    if (g_welcome_brush) DeleteObject(g_welcome_brush);
    g_welcome_brush = CreateSolidBrush(g_theme->bg_main);
    if (g_welcome_hero_brush) DeleteObject(g_welcome_hero_brush);
    g_welcome_hero_brush = CreateSolidBrush(g_theme->bg_panel);
    if (g_welcome_action_brush) DeleteObject(g_welcome_action_brush);
    g_welcome_action_brush = CreateSolidBrush(g_theme->btn_bg);
}

std::wstring app_config_path() {
    wchar_t path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full = (len > 0) ? std::wstring(path, path + len) : L"LVM Viewer.exe";
    std::size_t slash = full.find_last_of(L"\\/");
    if (slash != std::wstring::npos) full.resize(slash + 1);
    else full.clear();
    full += L"lvm_viewer.ini";
    return full;
}

void load_app_settings() {
    if (g_config_path.empty()) g_config_path = app_config_path();
    wchar_t theme_buf[32]{};
    GetPrivateProfileStringW(L"ui", L"theme", L"light", theme_buf, 32, g_config_path.c_str());
    if (lstrcmpiW(theme_buf, L"dark") == 0) g_theme = &kDarkTheme;
    else g_theme = &kLightTheme;
}

void save_app_settings() {
    if (g_config_path.empty()) g_config_path = app_config_path();
    WritePrivateProfileStringW(
        L"ui",
        L"theme",
        (g_theme == &kDarkTheme) ? L"dark" : L"light",
        g_config_path.c_str());
}

const OwnerDrawMenuEntry* stash_menu_entry(const std::wstring& text, bool top_level, bool popup) {
    auto entry = std::make_unique<OwnerDrawMenuEntry>();
    entry->text = text;
    entry->top_level = top_level;
    entry->popup = popup;
    g_menu_text_storage.push_back(std::move(entry));
    return g_menu_text_storage.back().get();
}

const int IDC_SPEED_PROMPT_EDIT = 6200;
const int IDC_SPEED_PROMPT_OK = 6201;
const int IDC_SPEED_PROMPT_CANCEL = 6202;
const int IDC_RANGE_PROMPT_START_EDIT = 6203;
const int IDC_RANGE_PROMPT_END_EDIT = 6204;
const int IDC_RANGE_PROMPT_OK = 6205;
const int IDC_RANGE_PROMPT_CANCEL = 6206;
const int IDC_HOTKEYS_DIALOG_LIST = 6207;
const int IDC_HOTKEYS_DIALOG_CLOSE = 6208;
const int IDC_LOADING_CANCEL = 6209;

constexpr UINT WM_APP_ASYNC_SCAN_DONE = WM_APP + 1;
constexpr UINT WM_APP_ASYNC_LOAD_DONE = WM_APP + 2;

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
    const wchar_t* light_mode;
    const wchar_t* light_mode_status;
    const wchar_t* light_mode_range_title;
    const wchar_t* light_mode_range_start;
    const wchar_t* light_mode_range_end;
    const wchar_t* light_mode_range_apply;
    const wchar_t* light_mode_range_invalid_start;
    const wchar_t* light_mode_range_invalid_end;
    const wchar_t* msg_loading;
    const wchar_t* msg_loading_light;
    const wchar_t* msg_scanning_range;
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
    L"Время / Гц\tM", L"Увеличить\t+", L"Уменьшить\t−", L"Сбросить вид\tHome", L"Автомасштабирование", L"Сглаживание\tC", L"Вертикальное панорамирование\tP", L"Play / Pause\tПробел", L"Тёмная тема\tT", L"Скорость",
    L"Точки\tV", L"Настройки…", L"Очистить\tDelete",
    L"Вертикальная\tL", L"Горизонтальная\tH", L"Очистить",
    L"Добавить\tK", L"Очистить",
    L"Горячие клавиши…\tF1", L"О программе…",
    L"Открыть", L"PNG", L"CSV", L"Время/Гц", L"▶ Старт", L"⏸ Пауза", L"Точки", L"Сброс", L"АвтоМасштаб", L"Настройки",
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
    L"Открыть файл…", L"Сохранить PNG", L"Сохранить CSV", L"Переключить Время / Гц", L"Воспроизведение", L"Пауза", L"Режим измерения точек", L"Сбросить вид", L"Автомасштабирование", L"Настройки точек",
    L"Русский", L"English", L"Язык",
    L"Лёгкий режим",
    L"   |   Лёгкий режим: открыт только выбранный временной фрагмент",
    L"Лёгкий режим: диапазон открытия",
    L"С какой секунды открыть фрагмент:",
    L"По какую секунду открыть фрагмент:",
    L"Открыть фрагмент",
    L"Введите конечное число не меньше 0, например 0, 1.5 или 12.",
    L"Введите конечное число больше начального времени.",
    L"Загрузка файла...\r\nПожалуйста, подождите",
    L"Лёгкий режим: загрузка фрагмента...\r\nПожалуйста, подождите",
    L"Лёгкий режим: сканирование диапазона времени...\r\nПожалуйста, подождите",
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
    L"Light mode",
    L"   |   Light mode: only the selected time fragment is open",
    L"Light mode: open time range",
    L"Open the fragment starting from this second:",
    L"Open the fragment until this second:",
    L"Open fragment",
    L"Enter a finite number greater than or equal to 0, for example 0, 1.5, or 12.",
    L"Enter a finite number greater than the start time.",
    L"Loading file...\r\nPlease wait",
    L"Light mode: loading fragment...\r\nPlease wait",
    L"Light mode: scanning time range...\r\nPlease wait",
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

struct PointGroup {
    std::wstring name;
    COLORREF color = RGB(0, 120, 215);
    bool visible = true;
    std::vector<std::pair<double, double>> points;
};

enum class FormulaOp {
    Number,
    Variable,
    Add,
    Sub,
    Mul,
    Div,
    Neg,
    FuncAbs,
    FuncSqrt,
    FuncSin,
    FuncCos,
    FuncTan,
    FuncLog,
    FuncExp
};

struct FormulaToken {
    FormulaOp op = FormulaOp::Number;
    double value = 0.0;
};

enum class TransformRuntimeKind : unsigned char {
    Identity = 0,
    Affine = 1,
    CachedFormula = 2
};

struct AffineFormulaInfo {
    bool valid = false;
    double mul = 0.0;
    double add = 0.0;
};

struct HotkeyBinding {
    int command = 0;
    BYTE fvirt = FVIRTKEY;
    WORD key = 0;
};

enum class AsyncLoadStage : unsigned char {
    None = 0,
    ScanningRange = 1,
    LoadingFile = 2
};

struct App {
    lvm::Dataset ds;
    std::vector<char> visible;
    std::vector<std::wstring> channel_labels;  // user-editable display names
    std::wstring global_formula = L"x";
    std::vector<FormulaToken> global_formula_rpn;
    bool formula_runtime_dirty = true;
    bool formula_ini_deferred = false;
    bool global_formula_identity = true;
    bool global_formula_affine = true;
    double global_formula_mul = 1.0;
    double global_formula_add = 0.0;
    std::vector<std::wstring> channel_formulas;
    std::vector<std::vector<FormulaToken>> channel_formula_rpn;
    std::vector<char> channel_formula_identity;
    std::vector<TransformRuntimeKind> channel_transform_kind;
    std::vector<double> channel_transform_mul;
    std::vector<double> channel_transform_add;
    std::vector<std::vector<double>> transformed_channel_cache;
    std::vector<char> transformed_channel_cache_valid;
    bool has_non_identity_formula = false;
    bool has_non_affine_formula = false;
    bool freq_mode = false;

    double data_t0 = 0.0, data_t1 = 1.0;
    double win_start = 0.0, win_end = 1.0;
    double freq_start = 0.0, freq_end = 1.0;
    double approx_dt = 1.0;

    lvm::Spectrum spec;
    bool spec_valid = false;
    std::vector<int> spec_channel_indices;
    std::vector<char> spec_visible_state;
    double cached_global_gap_step = 0.0;
    bool cached_global_gap_step_ready = false;

    bool visual_smooth = false;  // Catmull-Rom spline rendering (data unchanged)

    bool auto_y = true;            // auto-fit vertical scale (true=auto, false=fixed)
    double y_lock_min = -1.0, y_lock_max = 1.0;

    bool auto_y_amp = true;        // auto-fit amplitude in Hz mode
    double y_amp_max = 1.0;        // locked amplitude max in Hz mode

    bool measure_mode = false;
    bool snap_to_data = true;       // snap markers to the nearest real sample
    PointDisplay pdisp;             // which read-outs to draw at markers
    COLORREF marker_color = g_theme->marker_color;
    std::vector<PointGroup> point_groups;
    int active_point_group = -1;

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
    struct GapMarkerVisual {
        RECT rect = {0, 0, 0, 0};
        double duration = 0.0;
        long long estimated_missing_samples = 0;
    };
    struct TimeYRangeCache {
        bool valid = false;
        std::size_t lo = 0;
        std::size_t hi = 0;
        double win_start = 0.0;
        double win_end = 0.0;
        unsigned long long serial = 0;
        double ymin = -1.0;
        double ymax = 1.0;
    };
    std::vector<GapMarkerVisual> visible_gap_markers;
    TimeYRangeCache time_yrange_cache;
    unsigned long long plot_analysis_serial = 1;
    bool pending_marker = false;
    int active_marker = -1;
    bool light_mode = false;
    bool show_gap_markers = true;
    bool current_file_partial = false;
    double light_mode_open_start = 0.0;
    double light_mode_open_end = 10.0;
    AsyncLoadStage async_load_stage = AsyncLoadStage::None;
    unsigned long long async_load_token = 0;
    std::shared_ptr<std::atomic<bool>> async_load_cancel_flag;
    std::wstring cached_scan_path;
    double cached_scan_start = 0.0;
    double cached_scan_end = 0.0;
    bool cached_scan_valid = false;

    std::wstring file_name;
    std::string last_error;

    HWND main = nullptr;
    HWND open = nullptr, savepng = nullptr, savecsv = nullptr;
    HWND mode_time = nullptr, mode_freq = nullptr;
    HWND play = nullptr, measure = nullptr, marker_btn = nullptr;
    HWND vline_btn = nullptr, hline_btn = nullptr;
    HWND reset = nullptr, autoy = nullptr, ptsettings = nullptr, sidepanel_btn = nullptr;
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

    bool side_panel_visible = true;
    int side_panel_tab = 0; // 0 = channels, 1 = points
    int side_selected_channel = -1;
    int side_scroll_y = 0;
    int side_scroll_max = 0;
    int side_content_height_channels = 0;
    int side_content_height_points = 0;
    HWND side_tab_channels = nullptr;
    HWND side_tab_points = nullptr;
    HWND side_scrollbar = nullptr;
    HWND side_channel_hint = nullptr;
    HWND side_global_formula_label = nullptr;
    HWND side_global_formula_edit = nullptr;
    HWND side_global_formula_apply = nullptr;
    HWND side_channel_formula_label = nullptr;
    HWND side_formula_edit = nullptr;
    HWND side_channel_color = nullptr;
    HWND side_formula_apply_selected = nullptr;
    HWND side_formula_apply_visible = nullptr;
    HWND side_formula_reset_selected = nullptr;
    HWND side_formula_reset_all = nullptr;
    HWND side_point_group_list = nullptr;
    HWND side_point_group_visible = nullptr;
    HWND side_point_group_new = nullptr;
    HWND side_point_group_delete = nullptr;
    HWND side_point_group_name = nullptr;
    HWND side_point_group_rename = nullptr;
    HWND side_point_color_current = nullptr;
    HWND side_point_group_color = nullptr;
    HWND side_point_label_groups = nullptr;
    std::vector<HWND> side_channel_controls;
    std::vector<HWND> side_point_controls;

    HWND settings_wnd = nullptr; // measurement-point settings panel (modeless)
    HWND welcome_wnd = nullptr;  // start screen

    HMENU menu = nullptr;        // main menu bar
    HACCEL accel = nullptr;      // current accelerator table (rebuilt from hotkeys)
    HFONT ui_font = nullptr;     // Segoe UI for controls / labels
    HFONT menu_font = nullptr;   // menu bar font sized for owner-drawn top menu
    HFONT bold_font = nullptr;   // semibold for headings
    HFONT title_font = nullptr;  // large font for the welcome title
    HFONT axis_font = nullptr;   // 11px for axis tick labels
    // icon_font removed вЂ” toolbar now uses text labels with ui_font

    bool dragging = false;
    int drag_x = 0, drag_y = 0;
    double drag_lo = 0.0, drag_hi = 0.0;
    double drag_y_lo = 0.0, drag_y_hi = 0.0;
    bool gap_click_pending = false;
    int gap_click_index = -1;

    bool fft_window_active = false;
    double fft_window_start = 0.0, fft_window_end = 0.0;
    bool fft_selecting = false;
    int fft_select_anchor_x = 0, fft_select_current_x = 0;
    double fft_select_anchor_t = 0.0, fft_select_current_t = 0.0;

    double spec_source_start = 0.0, spec_source_end = 0.0;
    bool spec_source_from_selection = false;
    bool spec_source_valid = false;

    bool vertical_pan = true;  // enable vertical panning with left-drag

    HDC backbuffer_dc = nullptr;
    HBITMAP backbuffer_bmp = nullptr;
    HBITMAP backbuffer_prev_bmp = nullptr;
    int backbuffer_w = 0;
    int backbuffer_h = 0;
};

App g;
ULONG_PTR g_gdiplus_token = 0;

struct AsyncScanResult {
    unsigned long long token = 0;
    std::wstring path;
    bool ok = false;
    bool cancelled = false;
    double range_start = 0.0;
    double range_end = 0.0;
    std::string error;
};

struct AsyncLoadResult {
    unsigned long long token = 0;
    std::wstring path;
    lvm::Dataset ds;
    bool ok = false;
    bool cancelled = false;
    bool hide_channels = false;
    bool requested_time_window = false;
    double cached_global_gap_step = 0.0;
    bool cached_global_gap_step_ready = false;
    std::string error;
};

struct LegendItem { int channel; RECT rect; };
std::vector<LegendItem> g_legend_items;
RECT g_legend_box = {0,0,0,0};

// ---- undo / redo system ------------------------------------------------
struct SettingsSnapshot {
    std::vector<char> visible;
    std::vector<std::wstring> channel_labels;
    std::vector<COLORREF> channel_colors;
    std::wstring global_formula;
    std::vector<std::wstring> channel_formulas;
    PointDisplay pdisp;
    bool snap_to_data = true;
    COLORREF marker_color = RGB(0, 120, 215);
    std::vector<PointGroup> point_groups;
    int active_point_group = -1;
    std::vector<GuideLine> guides;
    std::vector<App::Marker> markers;
    int active_marker = -1;
    int pending_line = 0;
    bool pending_marker = false;
    bool measure_mode = false;
    bool auto_y = true;
    double y_lock_min = -1.0;
    double y_lock_max = 1.0;
    bool auto_y_amp = true;
    double y_amp_max = 1.0;
};

struct UndoAction {
    enum Type { NONE, ADD_POINT, ADD_LINE, ADD_MARKER, CLEAR_POINTS, CLEAR_LINES, CLEAR_MARKERS, SETTINGS_CHANGE } type = NONE;
    std::pair<double, double> point;
    int point_group_index = -1;
    bool point_group_created = false;
    PointGroup point_group_state;
    GuideLine line;
    App::Marker marker;
    std::vector<PointGroup> saved_point_groups;
    int saved_active_point_group = -1;
    std::vector<GuideLine> saved_lines;
    std::vector<App::Marker> saved_markers;
    SettingsSnapshot before_settings;
    SettingsSnapshot after_settings;
};
std::vector<UndoAction> g_undo;
std::vector<UndoAction> g_redo;
WNDPROC g_channel_edit_proc = nullptr;
void populate_point_group_list(HWND hwnd);
void refresh_side_panel_controls();
void refresh_settings_controls();
void apply_side_panel_visibility();
void set_side_panel_tab(int tab);
int side_panel_width();
void layout();
void update_side_panel_scrollbar(int viewport_top, int content_height);
bool side_panel_hit_test(const POINT& pt);
void scroll_side_panel(int delta);
void load_side_transform_controls();
std::wstring format_edit_number(double value);
COLORREF mix_color(COLORREF a, COLORREF b, int weight_b);
void ensure_channel_formula_vectors();
void invalidate_formula_runtime();
void invalidate_formula_runtime_channel(std::size_t channel_index);
void invalidate_transformed_channel_cache();
void ensure_transformed_channel_cache(std::size_t channel_index);
void load_channel_formulas_from_ini();
void finish_channel_rename(bool apply);
void save_runtime_settings();
void set_status();
void sync_menu();
void clear_spectrum_cache_state();
void compute_spectrum();
void compute_spectrum_from_current_source();
bool ensure_current_spectrum();
double visible_spectrum_ymax();
SettingsSnapshot capture_settings_snapshot();
void ensure_channel_formula_storage();
void ensure_channel_formulas_loaded();
bool settings_snapshot_differs(const SettingsSnapshot& a, const SettingsSnapshot& b);
void apply_settings_snapshot(const SettingsSnapshot& snapshot);
bool record_settings_change(const SettingsSnapshot& before);
bool is_toggle_checked(HWND hwnd);
void set_toggle_checked(HWND hwnd, bool checked);
void toggle_checked_state(HWND hwnd);
std::wstring time_gap_details_text(double duration, long long estimated_missing_samples);
const wchar_t* time_gap_details_title();
int hit_test_gap_marker(int x, int y);
void show_gap_details_dialog(HWND owner, int gap_index);

const wchar_t* gap_markers_toggle_text() {
    return (g_str == &kEn) ? L"Show gap markers" : L"Показывать разрывы";
}

const wchar_t* side_global_formula_label_text() {
    return (g_str == &kEn) ? L"Global coefficient for all charts:" : L"Общий коэффициент для всех графиков:";
}

const wchar_t* side_global_formula_apply_text() {
    return (g_str == &kEn) ? L"Apply to all charts" : L"Применить ко всем графикам";
}

const wchar_t* side_channel_formula_label_text() {
    return (g_str == &kEn) ? L"Coefficient for the selected channel:" : L"Коэффициент выбранного канала:";
}

const wchar_t* point_group_list_title() {
    return g_str == &kEn ? L"Point groups" : L"Группы точек";
}

const wchar_t* point_current_color_button_text() {
    return g_str == &kEn ? L"Colour for new points…" : L"Цвет новых точек…";
}

const wchar_t* point_selected_group_color_button_text() {
    return g_str == &kEn ? L"Selected group colour…" : L"Цвет выбранной группы…";
}

const wchar_t* point_group_visible_text() {
    return g_str == &kEn ? L"Show selected group" : L"Показывать выбранную группу";
}

const wchar_t* point_group_new_button_text() {
    return g_str == &kEn ? L"Start new group" : L"Новая группа";
}

const wchar_t* side_panel_button_text() {
    return (g_str == &kEn) ? L"Panel" : L"Панель";
}

const wchar_t* side_tab_channels_text() {
    return (g_str == &kEn) ? L"Channels" : L"Каналы";
}

const wchar_t* side_tab_points_text() {
    return (g_str == &kEn) ? L"Points" : L"Точки";
}

const wchar_t* side_channel_color_button_text() {
    return (g_str == &kEn) ? L"Channel colour…" : L"Цвет канала…";
}

const wchar_t* side_channel_hint_text() {
    return L"";
}

const wchar_t* side_formula_apply_selected_text() {
    return (g_str == &kEn) ? L"Apply to selected" : L"К выбранному";
}

const wchar_t* side_formula_apply_visible_text() {
    return (g_str == &kEn) ? L"Apply to visible" : L"К видимым";
}

const wchar_t* side_formula_reset_selected_text() {
    return (g_str == &kEn) ? L"Reset selected" : L"Сбросить канал";
}

const wchar_t* side_formula_reset_all_text() {
    return (g_str == &kEn) ? L"Reset all channels" : L"Сбросить все каналы";
}

const wchar_t* side_point_group_delete_text() {
    return (g_str == &kEn) ? L"Delete group" : L"Удалить группу";
}

const wchar_t* side_point_group_rename_text() {
    return (g_str == &kEn) ? L"Rename" : L"Переименовать";
}

const wchar_t* side_pt_num_text() {
    return (g_str == &kEn) ? L"Point #" : L"Номер";
}

const wchar_t* side_pt_x_text() {
    return (g_str == &kEn) ? L"X" : L"X";
}

const wchar_t* side_pt_y_text() {
    return (g_str == &kEn) ? L"Y" : L"Y";
}

const wchar_t* side_pt_dx_text() {
    return (g_str == &kEn) ? L"Δx" : L"Δx";
}

const wchar_t* side_pt_dy_text() {
    return (g_str == &kEn) ? L"Δy" : L"Δy";
}

const wchar_t* side_pt_invdt_text() {
    return (g_str == &kEn) ? L"1/Δt" : L"1/Δt";
}

const wchar_t* side_pt_dist_text() {
    return (g_str == &kEn) ? L"d" : L"d";
}

const wchar_t* side_pt_snap_text() {
    return (g_str == &kEn) ? L"Snap" : L"Привязка";
}

std::wstring default_channel_formula_text() {
    return L"x";
}

std::wstring normalize_formula_text(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch == L',') out.push_back(L'.');
        else out.push_back(ch);
    }
    std::size_t first = 0;
    while (first < out.size() && iswspace(out[first])) ++first;
    std::size_t last = out.size();
    while (last > first && iswspace(out[last - 1])) --last;
    out = out.substr(first, last - first);
    if (out.empty()) out = default_channel_formula_text();
    return out;
}

int formula_precedence(FormulaOp op) {
    switch (op) {
        case FormulaOp::Neg: return 3;
        case FormulaOp::Mul:
        case FormulaOp::Div: return 2;
        case FormulaOp::Add:
        case FormulaOp::Sub: return 1;
        default: return 0;
    }
}

bool formula_is_right_associative(FormulaOp op) {
    return op == FormulaOp::Neg;
}

bool formula_is_function(FormulaOp op) {
    return op == FormulaOp::FuncAbs || op == FormulaOp::FuncSqrt || op == FormulaOp::FuncSin ||
           op == FormulaOp::FuncCos || op == FormulaOp::FuncTan || op == FormulaOp::FuncLog ||
           op == FormulaOp::FuncExp;
}

bool formula_is_operator(FormulaOp op) {
    return op == FormulaOp::Add || op == FormulaOp::Sub || op == FormulaOp::Mul ||
           op == FormulaOp::Div || op == FormulaOp::Neg;
}

bool formula_function_from_name(const std::wstring& name, FormulaOp& out) {
    if (name == L"abs") { out = FormulaOp::FuncAbs; return true; }
    if (name == L"sqrt") { out = FormulaOp::FuncSqrt; return true; }
    if (name == L"sin") { out = FormulaOp::FuncSin; return true; }
    if (name == L"cos") { out = FormulaOp::FuncCos; return true; }
    if (name == L"tan") { out = FormulaOp::FuncTan; return true; }
    if (name == L"log") { out = FormulaOp::FuncLog; return true; }
    if (name == L"exp") { out = FormulaOp::FuncExp; return true; }
    return false;
}

bool compile_formula_rpn(const std::wstring& raw_text, std::vector<FormulaToken>& out, std::wstring& error) {
    const std::wstring text = normalize_formula_text(raw_text);
    struct StackEntry {
        FormulaOp op = FormulaOp::Add;
        bool is_lparen = false;
    };
    out.clear();
    std::vector<StackEntry> ops;
    bool expect_value = true;

    auto push_operator = [&](FormulaOp op) {
        while (!ops.empty() && !ops.back().is_lparen && formula_is_operator(ops.back().op)) {
            const int top_prec = formula_precedence(ops.back().op);
            const int cur_prec = formula_precedence(op);
            if (top_prec > cur_prec || (top_prec == cur_prec && !formula_is_right_associative(op))) {
                out.push_back({ops.back().op, 0.0});
                ops.pop_back();
            } else {
                break;
            }
        }
        ops.push_back({op, false});
    };

    std::size_t i = 0;
    while (i < text.size()) {
        const wchar_t ch = text[i];
        if (iswspace(ch)) { ++i; continue; }

        if ((ch >= L'0' && ch <= L'9') || ch == L'.') {
            const wchar_t* begin = text.c_str() + i;
            wchar_t* end = nullptr;
            double value = wcstod(begin, &end);
            if (begin == end) {
                error = (g_str == &kEn) ? L"Invalid number in coefficient." : L"Некорректное число в коэффициенте.";
                return false;
            }
            i = static_cast<std::size_t>(end - text.c_str());
            out.push_back({FormulaOp::Number, value});
            expect_value = false;
            continue;
        }

        if (iswalpha(ch)) {
            std::size_t j = i;
            while (j < text.size() && (iswalpha(text[j]) || iswdigit(text[j]) || text[j] == L'_')) ++j;
            std::wstring name = text.substr(i, j - i);
            for (wchar_t& c : name) c = static_cast<wchar_t>(towlower(c));
            if (name == L"x") {
                out.push_back({FormulaOp::Variable, 0.0});
                expect_value = false;
            } else {
                FormulaOp fn = FormulaOp::FuncAbs;
                if (!formula_function_from_name(name, fn)) {
                    error = ((g_str == &kEn) ? L"Unknown function: " : L"Неизвестная функция: ") + name;
                    return false;
                }
                ops.push_back({fn, false});
                expect_value = true;
            }
            i = j;
            continue;
        }

        if (ch == L'(') {
            ops.push_back({FormulaOp::Add, true});
            ++i;
            expect_value = true;
            continue;
        }
        if (ch == L')') {
            bool found_lparen = false;
            while (!ops.empty()) {
                if (ops.back().is_lparen) {
                    found_lparen = true;
                    ops.pop_back();
                    break;
                }
                out.push_back({ops.back().op, 0.0});
                ops.pop_back();
            }
            if (!found_lparen) {
                error = (g_str == &kEn) ? L"Mismatched parentheses." : L"Несогласованные скобки.";
                return false;
            }
            if (!ops.empty() && formula_is_function(ops.back().op)) {
                out.push_back({ops.back().op, 0.0});
                ops.pop_back();
            }
            ++i;
            expect_value = false;
            continue;
        }

        FormulaOp op = FormulaOp::Add;
        if (ch == L'+') op = FormulaOp::Add;
        else if (ch == L'-') op = expect_value ? FormulaOp::Neg : FormulaOp::Sub;
        else if (ch == L'*') op = FormulaOp::Mul;
        else if (ch == L'/') op = FormulaOp::Div;
        else {
            error = ((g_str == &kEn) ? L"Unsupported character in coefficient: " : L"Недопустимый символ в коэффициенте: ") + std::wstring(1, ch);
            return false;
        }
        if (expect_value && op != FormulaOp::Neg) {
            error = (g_str == &kEn) ? L"Operator position is invalid." : L"Некорректное положение оператора.";
            return false;
        }
        push_operator(op);
        ++i;
        expect_value = true;
    }

    if (expect_value) {
        error = (g_str == &kEn) ? L"Incomplete coefficient." : L"Коэффициент не завершён.";
        return false;
    }

    while (!ops.empty()) {
        if (ops.back().is_lparen) {
            error = (g_str == &kEn) ? L"Mismatched parentheses." : L"Несогласованные скобки.";
            return false;
        }
        out.push_back({ops.back().op, 0.0});
        ops.pop_back();
    }
    return !out.empty();
}

bool formula_rpn_is_identity(const std::vector<FormulaToken>& rpn) {
    return rpn.size() == 1 && rpn[0].op == FormulaOp::Variable;
}

AffineFormulaInfo analyze_formula_rpn_affine(const std::vector<FormulaToken>& rpn) {
    if (rpn.empty()) return {};

    std::vector<AffineFormulaInfo> stack;
    stack.reserve(rpn.size());
    auto is_constant = [](const AffineFormulaInfo& value) {
        return value.valid && value.mul == 0.0;
    };
    auto push = [&](AffineFormulaInfo value) -> bool {
        if (!value.valid) return false;
        stack.push_back(value);
        return true;
    };
    auto pop1 = [&]() -> AffineFormulaInfo {
        if (stack.empty()) return {};
        AffineFormulaInfo value = stack.back();
        stack.pop_back();
        return value;
    };
    auto pop2 = [&](AffineFormulaInfo& a, AffineFormulaInfo& b) -> bool {
        if (stack.size() < 2) return false;
        b = stack.back();
        stack.pop_back();
        a = stack.back();
        stack.pop_back();
        return true;
    };
    auto push_constant_fn = [&](const AffineFormulaInfo& operand, double (*fn)(double)) -> bool {
        return is_constant(operand) && push({true, 0.0, fn(operand.add)});
    };

    for (const auto& token : rpn) {
        switch (token.op) {
            case FormulaOp::Number:
                if (!push({true, 0.0, token.value})) return {};
                break;
            case FormulaOp::Variable:
                if (!push({true, 1.0, 0.0})) return {};
                break;
            case FormulaOp::Add: {
                AffineFormulaInfo a, b;
                if (!pop2(a, b) || !push({a.valid && b.valid, a.mul + b.mul, a.add + b.add})) return {};
                break;
            }
            case FormulaOp::Sub: {
                AffineFormulaInfo a, b;
                if (!pop2(a, b) || !push({a.valid && b.valid, a.mul - b.mul, a.add - b.add})) return {};
                break;
            }
            case FormulaOp::Neg: {
                AffineFormulaInfo v = pop1();
                if (!push({v.valid, -v.mul, -v.add})) return {};
                break;
            }
            case FormulaOp::Mul: {
                AffineFormulaInfo a, b;
                if (!pop2(a, b)) return {};
                if (is_constant(a) && b.valid) {
                    if (!push({true, a.add * b.mul, a.add * b.add})) return {};
                } else if (is_constant(b) && a.valid) {
                    if (!push({true, b.add * a.mul, b.add * a.add})) return {};
                } else {
                    return {};
                }
                break;
            }
            case FormulaOp::Div: {
                AffineFormulaInfo a, b;
                if (!pop2(a, b) || !a.valid || !is_constant(b)) return {};
                const double denom = b.add;
                const double inv = (denom == 0.0) ? std::numeric_limits<double>::quiet_NaN() : (1.0 / denom);
                if (!push({true, a.mul * inv, a.add * inv})) return {};
                break;
            }
            case FormulaOp::FuncAbs: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::fabs(x); })) return {};
                break;
            }
            case FormulaOp::FuncSqrt: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) {
                    return x < 0.0 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(x);
                })) return {};
                break;
            }
            case FormulaOp::FuncSin: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::sin(x); })) return {};
                break;
            }
            case FormulaOp::FuncCos: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::cos(x); })) return {};
                break;
            }
            case FormulaOp::FuncTan: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::tan(x); })) return {};
                break;
            }
            case FormulaOp::FuncLog: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) {
                    return x <= 0.0 ? std::numeric_limits<double>::quiet_NaN() : std::log(x);
                })) return {};
                break;
            }
            case FormulaOp::FuncExp: {
                AffineFormulaInfo v = pop1();
                if (!push_constant_fn(v, [](double x) { return std::exp(x); })) return {};
                break;
            }
        }
    }

    return stack.size() == 1 ? stack.back() : AffineFormulaInfo{};
}

double eval_formula_rpn(const std::vector<FormulaToken>& rpn, double x) {
    if (rpn.empty()) return std::numeric_limits<double>::quiet_NaN();
    if (formula_rpn_is_identity(rpn)) return x;

    constexpr std::size_t kInlineStackCap = 32;
    std::array<double, kInlineStackCap> inline_stack{};
    std::vector<double> heap_stack;
    double* stack = inline_stack.data();
    std::size_t capacity = kInlineStackCap;
    if (rpn.size() > kInlineStackCap) {
        heap_stack.resize(rpn.size());
        stack = heap_stack.data();
        capacity = heap_stack.size();
    }

    std::size_t sp = 0;
    auto push = [&](double v) -> bool {
        if (sp >= capacity) return false;
        stack[sp++] = v;
        return true;
    };
    auto pop1 = [&]() -> double {
        if (sp == 0) return std::numeric_limits<double>::quiet_NaN();
        return stack[--sp];
    };
    auto pop2 = [&](double& a, double& b) -> bool {
        if (sp < 2) return false;
        b = stack[--sp];
        a = stack[--sp];
        return true;
    };

    for (const auto& token : rpn) {
        switch (token.op) {
            case FormulaOp::Number: if (!push(token.value)) return std::numeric_limits<double>::quiet_NaN(); break;
            case FormulaOp::Variable: if (!push(x)) return std::numeric_limits<double>::quiet_NaN(); break;
            case FormulaOp::Add: {
                double a, b;
                if (!pop2(a, b)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(a + b)) return std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case FormulaOp::Sub: {
                double a, b;
                if (!pop2(a, b)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(a - b)) return std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case FormulaOp::Mul: {
                double a, b;
                if (!pop2(a, b)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(a * b)) return std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case FormulaOp::Div: {
                double a, b;
                if (!pop2(a, b)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(b == 0.0 ? std::numeric_limits<double>::quiet_NaN() : (a / b))) {
                    return std::numeric_limits<double>::quiet_NaN();
                }
                break;
            }
            case FormulaOp::Neg: {
                double v = pop1();
                if (!std::isfinite(v) && !std::isnan(v)) return std::numeric_limits<double>::quiet_NaN();
                if (!push(-v)) return std::numeric_limits<double>::quiet_NaN();
                break;
            }
            case FormulaOp::FuncAbs: {
                double v = pop1(); if (!push(std::fabs(v))) return std::numeric_limits<double>::quiet_NaN(); break;
            }
            case FormulaOp::FuncSqrt: {
                double v = pop1(); if (!push(v < 0.0 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(v))) return std::numeric_limits<double>::quiet_NaN(); break;
            }
            case FormulaOp::FuncSin: { double v = pop1(); if (!push(std::sin(v))) return std::numeric_limits<double>::quiet_NaN(); break; }
            case FormulaOp::FuncCos: { double v = pop1(); if (!push(std::cos(v))) return std::numeric_limits<double>::quiet_NaN(); break; }
            case FormulaOp::FuncTan: { double v = pop1(); if (!push(std::tan(v))) return std::numeric_limits<double>::quiet_NaN(); break; }
            case FormulaOp::FuncLog: {
                double v = pop1(); if (!push(v <= 0.0 ? std::numeric_limits<double>::quiet_NaN() : std::log(v))) return std::numeric_limits<double>::quiet_NaN(); break;
            }
            case FormulaOp::FuncExp: { double v = pop1(); if (!push(std::exp(v))) return std::numeric_limits<double>::quiet_NaN(); break; }
        }
    }
    return sp == 1 ? stack[0] : std::numeric_limits<double>::quiet_NaN();
}

void normalize_active_point_group() {
    if (g.point_groups.empty()) {
        g.active_point_group = -1;
        return;
    }
    if (g.active_point_group >= 0 &&
        g.active_point_group < static_cast<int>(g.point_groups.size())) {
        return;
    }
    g.active_point_group = static_cast<int>(g.point_groups.size()) - 1;
}

PointGroup* active_point_group() {
    normalize_active_point_group();
    if (g.active_point_group < 0) return nullptr;
    return &g.point_groups[static_cast<std::size_t>(g.active_point_group)];
}

const PointGroup* active_point_group_readonly() {
    normalize_active_point_group();
    if (g.active_point_group < 0) return nullptr;
    return &g.point_groups[static_cast<std::size_t>(g.active_point_group)];
}

std::size_t total_measure_point_count() {
    std::size_t total = 0;
    for (const auto& group : g.point_groups) total += group.points.size();
    return total;
}

bool has_measure_points() {
    return total_measure_point_count() != 0;
}

void clear_measure_point_groups() {
    g.point_groups.clear();
    g.active_point_group = -1;
}

void erase_point_group(std::size_t index) {
    if (index >= g.point_groups.size()) return;
    g.point_groups.erase(g.point_groups.begin() + static_cast<std::ptrdiff_t>(index));
    if (g.point_groups.empty()) {
        g.active_point_group = -1;
        return;
    }
    if (g.active_point_group > static_cast<int>(index)) {
        --g.active_point_group;
    } else if (g.active_point_group == static_cast<int>(index)) {
        g.active_point_group = static_cast<int>(std::min<std::size_t>(index, g.point_groups.size() - 1));
    }
}

int create_point_group(COLORREF color) {
    PointGroup group;
    const std::size_t index = g.point_groups.size();
    if (g_str == &kEn) group.name = L"Group " + std::to_wstring(index + 1);
    else group.name = L"Группа " + std::to_wstring(index + 1);
    group.color = color;
    group.visible = true;
    g.point_groups.push_back(group);
    g.active_point_group = static_cast<int>(g.point_groups.size()) - 1;
    return g.active_point_group;
}

int ensure_point_group_for_measurement(bool force_new_group, bool* created_group = nullptr) {
    bool created = false;
    normalize_active_point_group();
    PointGroup* group = active_point_group();
    if (!group) {
        create_point_group(g.marker_color);
        created = true;
        group = active_point_group();
    } else {
        const bool active_has_points = !group->points.empty();
        if (force_new_group || (active_has_points && group->color != g.marker_color)) {
            create_point_group(g.marker_color);
            created = true;
            group = active_point_group();
        } else if (!active_has_points) {
            group->color = g.marker_color;
        }
    }
    if (group) group->visible = true;
    if (created_group) *created_group = created;
    return g.active_point_group;
}

std::wstring point_group_list_label(std::size_t index, const PointGroup& group) {
    wchar_t buf[160];
    const std::wstring base_name = group.name.empty()
        ? ((g_str == &kEn) ? (L"Group " + std::to_wstring(index + 1)) : (L"Группа " + std::to_wstring(index + 1)))
        : group.name;
    const wchar_t* status = L"";
    if (!group.visible) {
        status = (g_str == &kEn) ? L" [hidden]" : L" [скрыта]";
    } else if (static_cast<int>(index) == g.active_point_group) {
        status = (g_str == &kEn) ? L" [active]" : L" [активна]";
    }
    if (g_str == &kEn) {
        swprintf(buf, 160, L"%ls — %zu pts%s", base_name.c_str(), group.points.size(), status);
    } else {
        swprintf(buf, 160, L"%ls — %zu тчк%s", base_name.c_str(), group.points.size(), status);
    }
    return buf;
}

std::wstring measure_points_status_text() {
    const std::size_t total_points = total_measure_point_count();
    if (!total_points) return L"";
    std::size_t visible_groups = 0;
    for (const auto& group : g.point_groups) {
        if (group.visible && !group.points.empty()) ++visible_groups;
    }
    wchar_t buf[160];
    if (g_str == &kEn) {
        swprintf(buf, 160, L"   |   Points: %zu in %zu groups (%zu visible)",
            total_points, g.point_groups.size(), visible_groups);
    } else {
        swprintf(buf, 160, L"   |   Точки: %zu в %zu группах (%zu видимых)",
            total_points, g.point_groups.size(), visible_groups);
    }
    return buf;
}

void push_undo(const UndoAction& a) {
    g_undo.push_back(a);
    g_redo.clear(); // new action clears redo stack
}

SettingsSnapshot capture_settings_snapshot() {
    ensure_channel_formula_vectors();
    SettingsSnapshot snapshot;
    snapshot.visible = g.visible;
    snapshot.channel_labels = g.channel_labels;
    snapshot.channel_colors = g_channel_colors;
    snapshot.global_formula = g.global_formula;
    snapshot.channel_formulas = g.channel_formulas;
    snapshot.pdisp = g.pdisp;
    snapshot.snap_to_data = g.snap_to_data;
    snapshot.marker_color = g.marker_color;
    snapshot.point_groups = g.point_groups;
    snapshot.active_point_group = g.active_point_group;
    snapshot.guides = g.guides;
    snapshot.markers = g.markers;
    snapshot.active_marker = g.active_marker;
    snapshot.pending_line = g.pending_line;
    snapshot.pending_marker = g.pending_marker;
    snapshot.measure_mode = g.measure_mode;
    snapshot.auto_y = g.auto_y;
    snapshot.y_lock_min = g.y_lock_min;
    snapshot.y_lock_max = g.y_lock_max;
    snapshot.auto_y_amp = g.auto_y_amp;
    snapshot.y_amp_max = g.y_amp_max;
    return snapshot;
}

bool settings_snapshot_differs(const SettingsSnapshot& a, const SettingsSnapshot& b) {
    auto same_point_groups = [&](const std::vector<PointGroup>& lhs, const std::vector<PointGroup>& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i].name != rhs[i].name ||
                lhs[i].color != rhs[i].color ||
                lhs[i].visible != rhs[i].visible ||
                lhs[i].points != rhs[i].points) {
                return false;
            }
        }
        return true;
    };
    auto same_guides = [&](const std::vector<GuideLine>& lhs, const std::vector<GuideLine>& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i].vertical != rhs[i].vertical ||
                lhs[i].value != rhs[i].value ||
                lhs[i].freq != rhs[i].freq) {
                return false;
            }
        }
        return true;
    };
    auto same_markers = [&](const std::vector<App::Marker>& lhs, const std::vector<App::Marker>& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i].x != rhs[i].x ||
                lhs[i].y != rhs[i].y ||
                lhs[i].label != rhs[i].label ||
                lhs[i].freq != rhs[i].freq ||
                lhs[i].snapped != rhs[i].snapped ||
                lhs[i].channel != rhs[i].channel) {
                return false;
            }
        }
        return true;
    };
    return a.visible != b.visible ||
           a.channel_labels != b.channel_labels ||
           a.channel_colors != b.channel_colors ||
           a.global_formula != b.global_formula ||
           a.channel_formulas != b.channel_formulas ||
           a.pdisp.number != b.pdisp.number ||
           a.pdisp.x != b.pdisp.x ||
           a.pdisp.y != b.pdisp.y ||
           a.pdisp.dx != b.pdisp.dx ||
           a.pdisp.dy != b.pdisp.dy ||
           a.pdisp.inv_dt != b.pdisp.inv_dt ||
           a.pdisp.dist != b.pdisp.dist ||
           a.snap_to_data != b.snap_to_data ||
           a.marker_color != b.marker_color ||
           !same_point_groups(a.point_groups, b.point_groups) ||
           a.active_point_group != b.active_point_group ||
           !same_guides(a.guides, b.guides) ||
           !same_markers(a.markers, b.markers) ||
           a.active_marker != b.active_marker ||
           a.pending_line != b.pending_line ||
           a.pending_marker != b.pending_marker ||
           a.measure_mode != b.measure_mode ||
           a.auto_y != b.auto_y ||
           a.y_lock_min != b.y_lock_min ||
           a.y_lock_max != b.y_lock_max ||
           a.auto_y_amp != b.auto_y_amp ||
           a.y_amp_max != b.y_amp_max;
}

void sync_channel_controls_from_state() {
    for (std::size_t i = 0; i < g.checks.size() && i < g.visible.size(); ++i) {
        if (g.checks[i]) {
            set_toggle_checked(g.checks[i], g.visible[i] != 0);
        }
    }
    for (std::size_t i = 0; i < g.check_labels.size() && i < g.channel_labels.size(); ++i) {
        if (g.check_labels[i]) {
            SetWindowTextW(g.check_labels[i], g.channel_labels[i].c_str());
        }
    }
    if (g.measure) {
        SendMessageW(g.measure, BM_SETCHECK, g.measure_mode ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g.autoy) {
        SendMessageW(g.autoy, BM_SETCHECK, g.auto_y ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void rebuild_formula_cache_from_state() {
    g.global_formula_rpn.clear();
    g.channel_formula_rpn.assign(g.channel_formulas.size(), {});
    invalidate_formula_runtime();
    ensure_channel_formula_vectors();
}

void recompute_transforms_from_state() {
    clear_spectrum_cache_state();
    if (g.freq_mode) compute_spectrum();
    sync_menu();
}

void apply_settings_snapshot(const SettingsSnapshot& snapshot) {
    if (g.channel_edit) finish_channel_rename(false);
    g.visible = snapshot.visible;
    g.channel_labels = snapshot.channel_labels;
    g_channel_colors = snapshot.channel_colors;
    g.global_formula = snapshot.global_formula;
    g.channel_formulas = snapshot.channel_formulas;
    rebuild_formula_cache_from_state();
    g.pdisp = snapshot.pdisp;
    g.snap_to_data = snapshot.snap_to_data;
    g.marker_color = snapshot.marker_color;
    g.point_groups = snapshot.point_groups;
    g.active_point_group = snapshot.active_point_group;
    g.guides = snapshot.guides;
    g.markers = snapshot.markers;
    g.active_marker = snapshot.active_marker;
    g.pending_line = snapshot.pending_line;
    g.pending_marker = snapshot.pending_marker;
    g.measure_mode = snapshot.measure_mode;
    g.auto_y = snapshot.auto_y;
    g.y_lock_min = snapshot.y_lock_min;
    g.y_lock_max = snapshot.y_lock_max;
    g.auto_y_amp = snapshot.auto_y_amp;
    g.y_amp_max = snapshot.y_amp_max;
    sync_channel_controls_from_state();
    recompute_transforms_from_state();
    if (g.settings_wnd) refresh_settings_controls();
    refresh_side_panel_controls();
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
    save_runtime_settings();
}

bool record_settings_change(const SettingsSnapshot& before) {
    const SettingsSnapshot after = capture_settings_snapshot();
    if (!settings_snapshot_differs(before, after)) return false;
    UndoAction action;
    action.type = UndoAction::SETTINGS_CHANGE;
    action.before_settings = before;
    action.after_settings = after;
    push_undo(action);
    return true;
}

void pop_undo() {
    if (g_undo.empty()) return;
    UndoAction a = g_undo.back();
    g_undo.pop_back();
    switch (a.type) {
        case UndoAction::ADD_POINT:
            if (a.point_group_index >= 0 &&
                a.point_group_index < static_cast<int>(g.point_groups.size())) {
                auto& pts = g.point_groups[static_cast<std::size_t>(a.point_group_index)].points;
                if (!pts.empty()) {
                    g_redo.push_back(a);
                    pts.pop_back();
                    if (a.point_group_created && pts.empty()) {
                        erase_point_group(static_cast<std::size_t>(a.point_group_index));
                    } else {
                        g.active_point_group = a.point_group_index;
                    }
                    if (PointGroup* group = active_point_group()) g.marker_color = group->color;
                }
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
            g.point_groups = a.saved_point_groups;
            g.active_point_group = a.saved_active_point_group;
            if (PointGroup* group = active_point_group()) g.marker_color = group->color;
            break;
        case UndoAction::CLEAR_LINES:
            g_redo.push_back(a);
            g.guides = a.saved_lines;
            break;
        case UndoAction::CLEAR_MARKERS:
            g_redo.push_back(a);
            g.markers = a.saved_markers;
            break;
        case UndoAction::SETTINGS_CHANGE:
            g_redo.push_back(a);
            apply_settings_snapshot(a.before_settings);
            break;
        default: break;
    }
    if (g.settings_wnd) populate_point_group_list(g.settings_wnd);
    refresh_side_panel_controls();
}
void pop_redo() {
    if (g_redo.empty()) return;
    UndoAction a = g_redo.back();
    g_redo.pop_back();
    switch (a.type) {
        case UndoAction::ADD_POINT:
            if (a.point_group_created) {
                const std::size_t insert_at = (a.point_group_index >= 0 &&
                                               a.point_group_index <= static_cast<int>(g.point_groups.size()))
                    ? static_cast<std::size_t>(a.point_group_index)
                    : g.point_groups.size();
                PointGroup group = a.point_group_state;
                group.points.clear();
                g.point_groups.insert(g.point_groups.begin() + static_cast<std::ptrdiff_t>(insert_at), group);
            }
            if (a.point_group_index >= 0 &&
                a.point_group_index < static_cast<int>(g.point_groups.size())) {
                g.point_groups[static_cast<std::size_t>(a.point_group_index)].points.push_back(a.point);
                g.active_point_group = a.point_group_index;
                g.marker_color = g.point_groups[static_cast<std::size_t>(a.point_group_index)].color;
                g_undo.push_back(a);
            }
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
            clear_measure_point_groups();
            break;
        case UndoAction::CLEAR_LINES:
            g_undo.push_back(a);
            g.guides.clear();
            break;
        case UndoAction::CLEAR_MARKERS:
            g_undo.push_back(a);
            g.markers.clear();
            break;
        case UndoAction::SETTINGS_CHANGE:
            g_undo.push_back(a);
            apply_settings_snapshot(a.after_settings);
            break;
        default: break;
    }
    if (g.settings_wnd) populate_point_group_list(g.settings_wnd);
    refresh_side_panel_controls();
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

bool fft_window_contains_time(double t) {
    return has_fft_window() && t >= g.fft_window_start && t <= g.fft_window_end;
}

bool last_fft_source_window(double& start, double& end, bool& from_selection) {
    if (!g.spec_source_valid || g.spec_source_end <= g.spec_source_start) return false;
    start = g.spec_source_start;
    end = g.spec_source_end;
    from_selection = g.spec_source_from_selection;
    return true;
}

std::wstring fft_window_status(double start, double end, bool from_selection) {
    wchar_t buf[220];
    if (g_str == &kEn) {
        swprintf(buf, 220, from_selection
            ? L"   |   FFT window: selected %.6g..%.6g s (Shift-click or right-click to clear)"
            : L"   |   FFT window: visible %.6g..%.6g s",
            start, end);
    } else {
        swprintf(buf, 220, from_selection
            ? L"   |   FFT окно: выбранный участок %.6g..%.6g c (Shift-клик или ПКМ для сброса)"
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
        if (g.current_file_partial) s += g_str->light_mode_status;
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
    s += measure_points_status_text();
    const PointGroup* active_group = active_point_group_readonly();
    if (active_group && active_group->points.size() >= 2) {
        const auto& pts = active_group->points;
        const auto& a = pts[pts.size() - 2];
        const auto& b = pts.back();
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

void ensure_channel_formula_vectors();
double transform_channel_value(std::size_t ci, double raw);
int channel_index_by_name(const std::string& name);

void clear_spectrum_cache_state() {
    g.spec_valid = false;
    g.spec = lvm::Spectrum{};
    g.spec_channel_indices.clear();
    g.spec_visible_state.clear();
}

void refresh_spec_channel_indices() {
    g.spec_channel_indices.assign(g.spec.names.size(), -1);
    for (std::size_t i = 0; i < g.spec.names.size(); ++i) {
        g.spec_channel_indices[i] = channel_index_by_name(g.spec.names[i]);
    }
}

bool spectrum_visibility_changed() {
    if (!g.light_mode) return false;
    return g.spec_visible_state.size() != g.visible.size() || g.spec_visible_state != g.visible;
}

bool build_time_window_dataset(const lvm::Dataset& in, double start, double end, lvm::Dataset& out,
                               const std::vector<std::size_t>* selected_channels = nullptr) {
    out = lvm::Dataset{};
    out.stats = in.stats;
    out.ok = true;
    ensure_channel_formula_vectors();
    if (in.time.empty()) return true;

    std::vector<std::size_t> channel_indices;
    if (selected_channels && !selected_channels->empty()) {
        channel_indices = *selected_channels;
    } else {
        channel_indices.resize(in.channels.size());
        for (std::size_t i = 0; i < in.channels.size(); ++i) channel_indices[i] = i;
    }
    out.names.reserve(channel_indices.size());
    out.channels.resize(channel_indices.size());
    for (std::size_t channel_index : channel_indices) {
        if (channel_index < in.names.size()) out.names.push_back(in.names[channel_index]);
        else out.names.push_back("Channel_" + std::to_string(channel_index + 1));
    }

    const std::size_t lo = static_cast<std::size_t>(
        std::lower_bound(in.time.begin(), in.time.end(), start) - in.time.begin());
    const std::size_t hi = static_cast<std::size_t>(
        std::upper_bound(in.time.begin(), in.time.end(), end) - in.time.begin());
    if (lo >= hi) return true;

    out.time.assign(in.time.begin() + static_cast<std::ptrdiff_t>(lo),
                    in.time.begin() + static_cast<std::ptrdiff_t>(hi));
    for (std::size_t out_index = 0; out_index < channel_indices.size(); ++out_index) {
        const std::size_t c = channel_indices[out_index];
        out.channels[out_index].reserve(hi - lo);
        const TransformRuntimeKind kind = (c < g.channel_transform_kind.size())
            ? g.channel_transform_kind[c]
            : TransformRuntimeKind::Identity;
        if (kind == TransformRuntimeKind::Identity) {
            out.channels[out_index].insert(out.channels[out_index].end(),
                                   in.channels[c].begin() + static_cast<std::ptrdiff_t>(lo),
                                   in.channels[c].begin() + static_cast<std::ptrdiff_t>(hi));
            continue;
        }
        if (kind == TransformRuntimeKind::Affine) {
            const double mul = g.channel_transform_mul[c];
            const double add = g.channel_transform_add[c];
            for (std::size_t r = lo; r < hi; ++r) {
                out.channels[out_index].push_back(in.channels[c][r] * mul + add);
            }
            continue;
        }
        ensure_transformed_channel_cache(c);
        const auto& cache = g.transformed_channel_cache[c];
        out.channels[out_index].insert(out.channels[out_index].end(),
                               cache.begin() + static_cast<std::ptrdiff_t>(lo),
                               cache.begin() + static_cast<std::ptrdiff_t>(hi));
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
    bool has_visible_channel = false;
    for (char visible : g.visible) {
        if (visible) {
            has_visible_channel = true;
            break;
        }
    }
    if (!has_visible_channel) {
        clear_spectrum_cache_state();
        g.spec_source_valid = end > start;
        g.spec_visible_state = g.visible;
        return;
    }
    std::vector<std::size_t> visible_channels;
    if (g.light_mode) {
        visible_channels.reserve(g.visible.size());
        for (std::size_t i = 0; i < g.visible.size(); ++i) {
            if (g.visible[i]) visible_channels.push_back(i);
        }
    } else {
        visible_channels.resize(g.ds.channel_count());
        for (std::size_t i = 0; i < visible_channels.size(); ++i) visible_channels[i] = i;
    }
    if (g.light_mode && visible_channels.empty()) {
        clear_spectrum_cache_state();
        g.spec_source_valid = end > start;
        g.spec_visible_state = g.visible;
        return;
    }
    lvm::Dataset view;
    build_time_window_dataset(g.ds, start, end, view, &visible_channels);
    g.spec = lvm::compute_spectrum(view, 16384);
    g.spec_valid = g.spec.ok;
    refresh_spec_channel_indices();
    g.spec_visible_state = g.visible;
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

bool ensure_current_spectrum() {
    if (!has_data()) return false;
    if (!g.spec_valid || spectrum_visibility_changed()) compute_spectrum();
    return g.spec_valid;
}

int channel_index_by_name(const std::string& name) {
    for (std::size_t i = 0; i < g.ds.names.size(); ++i)
        if (g.ds.names[i] == name) return static_cast<int>(i);
    return -1;
}

void finish_channel_rename(bool apply) {
    if (g.editing_channel < 0 || g.editing_channel >= static_cast<int>(g.channel_labels.size())) return;
    const int ci = g.editing_channel;
    SettingsSnapshot before;
    bool track_change = false;
    if (apply && g.channel_edit) {
        before = capture_settings_snapshot();
        track_change = true;
    }
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
    if (track_change) record_settings_change(before);
    if (g.settings_wnd) refresh_settings_controls();
    set_status();
    InvalidateRect(g.main, nullptr, FALSE);
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

void finish_channel_rename_if_click_outside(HWND hwnd) {
    if (!g.channel_edit) return;
    RECT edit_rect;
    GetWindowRect(g.channel_edit, &edit_rect);
    MapWindowPoints(nullptr, hwnd, reinterpret_cast<LPPOINT>(&edit_rect), 2);
    POINT click_point;
    GetCursorPos(&click_point);
    ScreenToClient(hwnd, &click_point);
    if (!PtInRect(&edit_rect, click_point)) {
        finish_channel_rename(true);
        SetFocus(hwnd);
    }
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
    if (g.side_selected_channel < 0 || g.side_selected_channel >= static_cast<int>(g.ds.channel_count())) g.side_selected_channel = 0;
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    for (std::size_t i = 0; i < g.ds.channel_count(); ++i) {
        HWND c = CreateWindowExW(
            0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW, 0, 0, 10, 10, g.main,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_BASE + i)), inst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        set_toggle_checked(c, g.visible[i] != 0);
        g.checks.push_back(c);

        HWND lbl = CreateWindowExW(
            0, L"STATIC", g.channel_labels[i].c_str(),
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_NOTIFY | SS_CENTERIMAGE,
            0, 0, 10, 10, g.main,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_LABEL_BASE + i)), inst, nullptr);
        SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g.check_labels.push_back(lbl);
    }
}

void hide_ui_controls() {
    if (g.main) {
        SetMenu(g.main, nullptr);
        DrawMenuBar(g.main);
    }
    for (HWND b : g.buttons) ShowWindow(b, SW_HIDE);
    for (HWND c : g.checks) ShowWindow(c, SW_HIDE);
    for (HWND c : g.check_labels) ShowWindow(c, SW_HIDE);
    for (HWND c : g.side_channel_controls) ShowWindow(c, SW_HIDE);
    for (HWND c : g.side_point_controls) ShowWindow(c, SW_HIDE);
    if (g.channel_edit) ShowWindow(g.channel_edit, SW_HIDE);
    if (g.status) ShowWindow(g.status, SW_HIDE);
}

void show_ui_controls() {
    if (g.main) {
        SetMenu(g.main, g.menu);
        DrawMenuBar(g.main);
    }
    for (HWND b : g.buttons) ShowWindow(b, SW_SHOW);
    apply_side_panel_visibility();
    if (g.channel_edit && g.side_panel_visible && g.side_panel_tab == 0) ShowWindow(g.channel_edit, SW_SHOW);
    if (g.status) ShowWindow(g.status, SW_SHOW);
}

bool welcome_visible() {
    return g.welcome_wnd && IsWindow(g.welcome_wnd) && IsWindowVisible(g.welcome_wnd);
}

int side_panel_width() {
    return (!welcome_visible() && g.side_panel_visible) ? kRightPanel : 0;
}

int side_selected_point_group() {
    if (!g.side_point_group_list) return -1;
    int sel = static_cast<int>(SendMessageW(g.side_point_group_list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return -1;
    return static_cast<int>(SendMessageW(g.side_point_group_list, LB_GETITEMDATA, sel, 0));
}

void load_side_transform_controls() {
    const bool formulas_ready = !g.formula_ini_deferred;
    const std::wstring default_formula = default_channel_formula_text();
    if (g.side_global_formula_edit) {
        SetWindowTextW(g.side_global_formula_edit,
            formulas_ready ? g.global_formula.c_str() : default_formula.c_str());
        EnableWindow(g.side_global_formula_edit, has_data());
    }
    if (g.side_global_formula_apply) EnableWindow(g.side_global_formula_apply, has_data());
    const int ci = g.side_selected_channel;
    const bool valid = ci >= 0 && ci < static_cast<int>(g.ds.channel_count());
    if (g.side_formula_edit) {
        const wchar_t* text = default_formula.c_str();
        if (valid && static_cast<std::size_t>(ci) < g.channel_formulas.size()) {
            text = formulas_ready
                ? g.channel_formulas[static_cast<std::size_t>(ci)].c_str()
                : default_formula.c_str();
        }
        SetWindowTextW(g.side_formula_edit, text);
        EnableWindow(g.side_formula_edit, has_data());
    }
    if (g.side_channel_color) EnableWindow(g.side_channel_color, valid);
    if (g.side_formula_apply_selected) EnableWindow(g.side_formula_apply_selected, valid);
    if (g.side_formula_apply_visible) EnableWindow(g.side_formula_apply_visible, has_data());
    if (g.side_formula_reset_selected) EnableWindow(g.side_formula_reset_selected, valid);
    if (g.side_formula_reset_all) EnableWindow(g.side_formula_reset_all, has_data());
}

void load_side_point_group_controls() {
    const int index = side_selected_point_group();
    const bool valid = index >= 0 && index < static_cast<int>(g.point_groups.size());
    if (g.side_point_group_visible) {
        set_toggle_checked(
            g.side_point_group_visible,
            valid && g.point_groups[static_cast<std::size_t>(index)].visible);
        EnableWindow(g.side_point_group_visible, valid);
    }
    if (g.side_point_group_color) EnableWindow(g.side_point_group_color, valid);
    if (g.side_point_group_delete) EnableWindow(g.side_point_group_delete, valid);
    if (g.side_point_group_name) {
        SetWindowTextW(g.side_point_group_name,
            valid ? g.point_groups[static_cast<std::size_t>(index)].name.c_str() : L"");
        EnableWindow(g.side_point_group_name, valid);
    }
    if (g.side_point_group_rename) EnableWindow(g.side_point_group_rename, valid);
}

void populate_side_point_group_list() {
    if (!g.side_point_group_list) return;
    const int previous = side_selected_point_group();
    SendMessageW(g.side_point_group_list, LB_RESETCONTENT, 0, 0);
    normalize_active_point_group();
    int selected_index = LB_ERR;
    for (std::size_t i = 0; i < g.point_groups.size(); ++i) {
        std::wstring label = point_group_list_label(i, g.point_groups[i]);
        int idx = static_cast<int>(SendMessageW(g.side_point_group_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(g.side_point_group_list, LB_SETITEMDATA, idx, static_cast<LPARAM>(i));
        if (static_cast<int>(i) == previous || static_cast<int>(i) == g.active_point_group) selected_index = idx;
    }
    if (selected_index != LB_ERR) SendMessageW(g.side_point_group_list, LB_SETCURSEL, selected_index, 0);
    load_side_point_group_controls();
}

bool side_panel_hit_test(const POINT& pt) {
    if (!g.side_panel_visible || welcome_visible()) return false;
    RECT rc{};
    GetClientRect(g.main, &rc);
    const int panel_left = rc.right - side_panel_width();
    return pt.x >= panel_left && pt.x <= rc.right && pt.y >= kTopBar && pt.y <= rc.bottom - kBottomBar;
}

void update_side_panel_scrollbar(int viewport_top, int content_height) {
    g.side_content_height_channels = max(g.side_content_height_channels, 0);
    g.side_content_height_points = max(g.side_content_height_points, 0);
    RECT rc{};
    GetClientRect(g.main, &rc);
    const int viewport_bottom = rc.bottom - kBottomBar - 6;
    const int viewport_height = max(0, viewport_bottom - viewport_top);
    g.side_scroll_max = max(0, content_height - viewport_height);
    if (g.side_scroll_y > g.side_scroll_max) g.side_scroll_y = g.side_scroll_max;
    if (g.side_scroll_y < 0) g.side_scroll_y = 0;
    if (!g.side_scrollbar) return;

    if (!g.side_panel_visible || welcome_visible() || g.side_scroll_max <= 0) {
        ShowWindow(g.side_scrollbar, SW_HIDE);
        SetScrollPos(g.side_scrollbar, SB_CTL, 0, TRUE);
        return;
    }

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = content_height > 0 ? content_height - 1 : 0;
    si.nPage = static_cast<UINT>(viewport_height);
    si.nPos = g.side_scroll_y;
    SetScrollInfo(g.side_scrollbar, SB_CTL, &si, TRUE);
    ShowWindow(g.side_scrollbar, SW_SHOW);
}

void scroll_side_panel(int delta) {
    if (delta == 0 || g.side_scroll_max <= 0) return;
    const int next = std::clamp(g.side_scroll_y + delta, 0, g.side_scroll_max);
    if (next == g.side_scroll_y) return;
    g.side_scroll_y = next;
    layout();
    InvalidateRect(g.main, nullptr, FALSE);
}

void set_side_panel_tab(int tab) {
    g.side_panel_tab = (tab == 1) ? 1 : 0;
    const bool show_channels = g.side_panel_visible && g.side_panel_tab == 0 && !welcome_visible();
    const bool show_points = g.side_panel_visible && g.side_panel_tab == 1 && !welcome_visible();
    if (g.show_all_btn) ShowWindow(g.show_all_btn, show_channels ? SW_SHOW : SW_HIDE);
    if (g.hide_all_btn) ShowWindow(g.hide_all_btn, show_channels ? SW_SHOW : SW_HIDE);
    for (HWND h : g.side_channel_controls) if (h) ShowWindow(h, show_channels ? SW_SHOW : SW_HIDE);
    for (HWND h : g.side_point_controls) if (h) ShowWindow(h, show_points ? SW_SHOW : SW_HIDE);
    for (HWND h : g.checks) if (h) ShowWindow(h, show_channels ? SW_SHOW : SW_HIDE);
    for (HWND h : g.check_labels) if (h) ShowWindow(h, show_channels ? SW_SHOW : SW_HIDE);
    if (!show_channels && g.channel_edit) ShowWindow(g.channel_edit, SW_HIDE);
    if (g.side_tab_channels) InvalidateRect(g.side_tab_channels, nullptr, FALSE);
    if (g.side_tab_points) InvalidateRect(g.side_tab_points, nullptr, FALSE);
    if (show_channels) {
        load_side_transform_controls();
    } else if (show_points) {
        populate_side_point_group_list();
    }
}

void apply_side_panel_visibility() {
    const bool show = g.side_panel_visible && !welcome_visible();
    if (g.side_tab_channels) ShowWindow(g.side_tab_channels, show ? SW_SHOW : SW_HIDE);
    if (g.side_tab_points) ShowWindow(g.side_tab_points, show ? SW_SHOW : SW_HIDE);
    if (!show && g.side_scrollbar) ShowWindow(g.side_scrollbar, SW_HIDE);
    set_side_panel_tab(g.side_panel_tab);
}

void refresh_side_panel_controls() {
    if (g.sidepanel_btn) SetWindowTextW(g.sidepanel_btn, side_panel_button_text());
    if (g.side_tab_channels) SetWindowTextW(g.side_tab_channels, side_tab_channels_text());
    if (g.side_tab_points) SetWindowTextW(g.side_tab_points, side_tab_points_text());
    if (g.side_channel_hint) SetWindowTextW(g.side_channel_hint, side_channel_hint_text());
    if (g.side_global_formula_label) SetWindowTextW(g.side_global_formula_label, side_global_formula_label_text());
    if (g.side_global_formula_apply) SetWindowTextW(g.side_global_formula_apply, side_global_formula_apply_text());
    if (g.side_channel_formula_label) SetWindowTextW(g.side_channel_formula_label, side_channel_formula_label_text());
    if (g.side_channel_color) SetWindowTextW(g.side_channel_color, side_channel_color_button_text());
    if (g.side_point_group_visible) SetWindowTextW(g.side_point_group_visible, point_group_visible_text());
    if (g.side_point_group_new) SetWindowTextW(g.side_point_group_new, point_group_new_button_text());
    if (g.side_point_group_delete) SetWindowTextW(g.side_point_group_delete, side_point_group_delete_text());
    if (g.side_point_group_rename) SetWindowTextW(g.side_point_group_rename, side_point_group_rename_text());
    if (g.side_point_color_current) SetWindowTextW(g.side_point_color_current, point_current_color_button_text());
    if (g.side_point_group_color) SetWindowTextW(g.side_point_group_color, point_selected_group_color_button_text());
    if (g.side_point_label_groups) SetWindowTextW(g.side_point_label_groups, point_group_list_title());
    if (g.side_formula_apply_selected) SetWindowTextW(g.side_formula_apply_selected, side_formula_apply_selected_text());
    if (g.side_formula_apply_visible) SetWindowTextW(g.side_formula_apply_visible, side_formula_apply_visible_text());
    if (g.side_formula_reset_selected) SetWindowTextW(g.side_formula_reset_selected, side_formula_reset_selected_text());
    if (g.side_formula_reset_all) SetWindowTextW(g.side_formula_reset_all, side_formula_reset_all_text());

    const struct ToggleMap { int id; bool value; const wchar_t* text; } point_toggles[] = {
        {IDC_SIDE_PT_NUM, g.pdisp.number, side_pt_num_text()},
        {IDC_SIDE_PT_X, g.pdisp.x, side_pt_x_text()},
        {IDC_SIDE_PT_Y, g.pdisp.y, side_pt_y_text()},
        {IDC_SIDE_PT_DX, g.pdisp.dx, side_pt_dx_text()},
        {IDC_SIDE_PT_DY, g.pdisp.dy, side_pt_dy_text()},
        {IDC_SIDE_PT_INVDT, g.pdisp.inv_dt, side_pt_invdt_text()},
        {IDC_SIDE_PT_DIST, g.pdisp.dist, side_pt_dist_text()},
        {IDC_SIDE_PT_SNAP, g.snap_to_data, side_pt_snap_text()},
    };
    for (const auto& item : point_toggles) {
        HWND ctl = GetDlgItem(g.main, item.id);
        if (!ctl) continue;
        SetWindowTextW(ctl, item.text);
        set_toggle_checked(ctl, item.value);
    }
    apply_side_panel_visibility();
}

void redraw_button(HWND btn);
void redraw_toolbar_buttons();
void show_welcome(HINSTANCE inst);

void layout() {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    g.toolbar_seps.clear();
    int x = 8;
    auto place = [&](HWND h, int w, int row_y) { MoveWindow(h, x, row_y, w, 28, FALSE); x += w + 4; };
    auto sep = [&]() { g.toolbar_seps.push_back(x + 2); x += 8; };
    auto text_button_width = [&](HWND h, int min_w, int pad) {
        if (!h) return min_w;
        wchar_t text[128]{};
        GetWindowTextW(h, text, 128);
        HDC dc = GetDC(h);
        HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(dc, font);
        SIZE sz{};
        GetTextExtentPoint32W(dc, text, lstrlenW(text), &sz);
        SelectObject(dc, old_font);
        ReleaseDC(h, dc);
        return max(min_w, static_cast<int>(sz.cx) + pad);
    };

    // Row 1: frequent global actions
    x = 8;
    place(g.open, 100, 8);
    sep();
    place(g.mode_time, 78, 8);
    place(g.mode_freq, 88, 8);
    place(g.play, 92, 8);
    sep();
    place(g.reset, 80, 8);
    place(g.autoy, text_button_width(g.autoy, 104, 28), 8);

    // Row 2: graph tools and settings
    x = 8;
    place(g.measure, 80, 40);
    place(g.marker_btn, 80, 40);
    place(g.vline_btn, 80, 40);
    place(g.hline_btn, 92, 40);
    sep();
    place(g.ptsettings, 110, 40);
    place(g.sidepanel_btn, 86, 40);

    const int panel_w = side_panel_width();
    const int panel_left = cw - panel_w;
    const int panel_pad_left = 12;
    const int panel_pad_right = 16;
    const int panel_x = panel_left + panel_pad_left;
    const int viewport_bottom = ch - kBottomBar - 6;
    const int channels_viewport_top = kTopBar + 78;
    const int points_viewport_top = kTopBar + 48;

    MoveWindow(g.show_all_btn, panel_x, kTopBar + 42, 86, 28, FALSE);
    MoveWindow(g.hide_all_btn, panel_x + 92, kTopBar + 42, 86, 28, FALSE);
    if (g.side_tab_channels) MoveWindow(g.side_tab_channels, panel_x, kTopBar + 8, 132, 28, FALSE);
    if (g.side_tab_points) MoveWindow(g.side_tab_points, panel_x + 136, kTopBar + 8, 132, 28, FALSE);
    apply_side_panel_visibility();

    const int channels_content_estimate =
        122 + static_cast<int>(g.checks.size()) * 26 + 8 + 20 + 24 + 26 + 32 + 28 + 34 + 28 + 34 + 28;
    const int points_content_estimate = 390;
    const int active_viewport_top = (g.side_panel_tab == 0) ? channels_viewport_top : points_viewport_top;
    const int active_content_estimate = (g.side_panel_tab == 0) ? channels_content_estimate : points_content_estimate;
    const int viewport_height = max(0, viewport_bottom - active_viewport_top);
    const int scroll_w = GetSystemMetrics(SM_CXVSCROLL);
    const bool need_scroll = g.side_panel_visible && !welcome_visible() && active_content_estimate > viewport_height;
    const int content_w = max(
        60,
        panel_w - panel_pad_left - panel_pad_right - (need_scroll ? (scroll_w + 6) : 0));
    const bool show_channels = g.side_panel_visible && g.side_panel_tab == 0 && !welcome_visible();
    const bool show_points = g.side_panel_visible && g.side_panel_tab == 1 && !welcome_visible();

    if (g.side_scrollbar) {
        MoveWindow(g.side_scrollbar, panel_left + panel_w - scroll_w, active_viewport_top,
                   scroll_w, viewport_height, FALSE);
    }

    auto place_scrolled = [&](HWND ctl, int x0, int y_rel, int w, int h, int viewport_top, bool visible_in_tab) {
        if (!ctl) return;
        const int y_abs = viewport_top + y_rel - g.side_scroll_y;
        const bool visible = visible_in_tab && y_abs + h > viewport_top && y_abs < viewport_bottom;
        ShowWindow(ctl, visible ? SW_SHOW : SW_HIDE);
        if (visible) MoveWindow(ctl, x0, y_abs, max(1, w), max(1, h), FALSE);
    };

    if (g.side_channel_hint) ShowWindow(g.side_channel_hint, SW_HIDE);
    int y = 0;
    place_scrolled(g.side_global_formula_label, panel_x, y, content_w, 20, channels_viewport_top, show_channels);
    y += 24;
    place_scrolled(g.side_global_formula_edit, panel_x, y, content_w, 26, channels_viewport_top, show_channels);
    y += 32;
    place_scrolled(g.side_global_formula_apply, panel_x, y, content_w, 28, channels_viewport_top, show_channels);
    y += 38;
    for (std::size_t i = 0; i < g.checks.size(); ++i) {
        place_scrolled(g.checks[i], panel_x, y + 2, 18, 20, channels_viewport_top, show_channels);
        if (i < g.check_labels.size()) {
            place_scrolled(g.check_labels[i], panel_x + 24, y, max(60, content_w - 28), 24, channels_viewport_top, show_channels);
        }
        y += 26;
    }
    if (g.channel_edit && g.editing_channel >= 0 && g.editing_channel < static_cast<int>(g.check_labels.size())) {
        const bool edit_visible = show_channels && IsWindowVisible(g.check_labels[g.editing_channel]) != FALSE;
        ShowWindow(g.channel_edit, edit_visible ? SW_SHOW : SW_HIDE);
        if (edit_visible) {
            RECT r;
            GetWindowRect(g.check_labels[g.editing_channel], &r);
            MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&r), 2);
            MoveWindow(g.channel_edit, r.left - 2, r.top - 1, (r.right - r.left) + 4, (r.bottom - r.top) + 2, FALSE);
        }
    }

    if (g.side_formula_edit && g.side_channel_color && g.side_formula_apply_selected && g.side_formula_apply_visible && g.side_formula_reset_selected && g.side_formula_reset_all) {
        int cy = y + 8;
        place_scrolled(g.side_channel_formula_label, panel_x, cy, content_w, 20, channels_viewport_top, show_channels);
        cy += 24;
        place_scrolled(g.side_formula_edit, panel_x, cy, content_w, 26, channels_viewport_top, show_channels);
        cy += 32;
        place_scrolled(g.side_channel_color, panel_x, cy, content_w, 28, channels_viewport_top, show_channels);
        cy += 34;
        const int button_w = max(80, (content_w - 6) / 2);
        place_scrolled(g.side_formula_apply_selected, panel_x, cy, button_w, 28, channels_viewport_top, show_channels);
        place_scrolled(g.side_formula_apply_visible, panel_x + button_w + 6, cy, button_w, 28, channels_viewport_top, show_channels);
        cy += 34;
        place_scrolled(g.side_formula_reset_selected, panel_x, cy, button_w, 28, channels_viewport_top, show_channels);
        place_scrolled(g.side_formula_reset_all, panel_x + button_w + 6, cy, button_w, 28, channels_viewport_top, show_channels);
        g.side_content_height_channels = cy + 28;
    }

    const int points_left = panel_x;
    const int col_gap = 6;
    const int checkbox_col_w = max(110, (content_w - col_gap) / 2);
    const int checkbox_col2_x = points_left + checkbox_col_w + col_gap;
    int py = 0;
    auto place_side = [&](int id, int x0, int y0, int w, int h) {
        HWND ctl = GetDlgItem(g.main, id);
        place_scrolled(ctl, x0, y0, w, h, points_viewport_top, show_points);
    };
    place_side(IDC_SIDE_PT_NUM, points_left, py, checkbox_col_w, 22);
    place_side(IDC_SIDE_PT_X, checkbox_col2_x, py, checkbox_col_w, 22);
    py += 24;
    place_side(IDC_SIDE_PT_Y, points_left, py, checkbox_col_w, 22);
    place_side(IDC_SIDE_PT_DX, checkbox_col2_x, py, checkbox_col_w, 22);
    py += 24;
    place_side(IDC_SIDE_PT_DY, points_left, py, checkbox_col_w, 22);
    place_side(IDC_SIDE_PT_INVDT, checkbox_col2_x, py, checkbox_col_w, 22);
    py += 24;
    place_side(IDC_SIDE_PT_DIST, points_left, py, checkbox_col_w, 22);
    place_side(IDC_SIDE_PT_SNAP, checkbox_col2_x, py, checkbox_col_w, 22);
    py += 30;
    place_side(IDC_SIDE_POINT_COLOR_CURRENT, points_left, py, content_w, 28);
    py += 38;
    place_scrolled(g.side_point_label_groups, points_left, py, content_w, 20, points_viewport_top, show_points);
    py += 24;
    place_scrolled(g.side_point_group_list, points_left, py, content_w, 122, points_viewport_top, show_points);
    py += 130;
    place_scrolled(g.side_point_group_visible, points_left, py, content_w, 22, points_viewport_top, show_points);
    py += 30;
    place_scrolled(g.side_point_group_color, points_left, py, content_w, 28, points_viewport_top, show_points);
    py += 36;
    place_scrolled(g.side_point_group_name, points_left, py, content_w, 24, points_viewport_top, show_points);
    py += 30;
    place_scrolled(g.side_point_group_rename, points_left, py, content_w, 28, points_viewport_top, show_points);
    py += 36;
    if (g.side_point_group_new) place_scrolled(g.side_point_group_new, points_left, py, (content_w - 6) / 2, 28, points_viewport_top, show_points);
    if (g.side_point_group_delete) place_scrolled(g.side_point_group_delete, points_left + (content_w - 6) / 2 + 6, py, (content_w - 6) / 2, 28, points_viewport_top, show_points);
    g.side_content_height_points = py + 28;

    update_side_panel_scrollbar(active_viewport_top, g.side_panel_tab == 0 ? g.side_content_height_channels : g.side_content_height_points);

    MoveWindow(g.status, 8, ch - kBottomBar + 4, cw - 16, 20, FALSE);
}

RECT plot_rect() {
    RECT rc;
    GetClientRect(g.main, &rc);
    RECT p;
    p.left = kAxisLeft;
    p.top = kTopBar + 6;
    p.right = rc.right - side_panel_width();
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
        if (!ensure_current_spectrum() || g.spec.freqs.size() < 2) return false;
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
bool current_freq_yrange(double& ymin, double& ymax);
void set_mode(bool freq_mode);
void invalidate_plot();
void rebuild_ui();
void rebuild_accelerators();
HMENU make_menu();
std::wstring hotkey_text_for_command(int command);
bool parse_wide_double_text(const wchar_t* text, double& out);
bool prompt_numeric_value(const wchar_t* title, const wchar_t* label,
                          const wchar_t* apply_text, const wchar_t* cancel_text,
                          const wchar_t* invalid_text, double initial_value,
                          bool positive_only, double& out_value);
void fill_rounded_rect(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius);
void draw_welcome_action_button(HDC dc, const RECT& r, const wchar_t* txt, bool pressed, bool primary, bool outlined);

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

const wchar_t* guide_prompt_title_text(bool vertical) {
    if (g_str == &kEn) return vertical ? L"Vertical guide line" : L"Horizontal guide line";
    return vertical ? L"Вертикальная линия" : L"Горизонтальная линия";
}

const wchar_t* guide_prompt_label_text(bool vertical) {
    if (g_str == &kEn) {
        return vertical ? L"Enter the exact X-axis value:" : L"Enter the exact Y-axis value:";
    }
    return vertical ? L"Введите точное значение по оси X:" : L"Введите точное значение по оси Y:";
}

const wchar_t* guide_prompt_apply_text() {
    return (g_str == &kEn) ? L"Add" : L"Добавить";
}

const wchar_t* guide_prompt_cancel_text() {
    return speed_prompt_cancel_text();
}

const wchar_t* guide_prompt_invalid_text() {
    return (g_str == &kEn)
        ? L"Enter a finite number, for example -1, 0, or 2.75."
        : L"Введите конечное число, например -1, 0 или 2.75.";
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
    save_runtime_settings();
    set_status();
}

LRESULT CALLBACK NumericPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            CreateWindowExW(0, L"STATIC", g_numeric_prompt.label.c_str(),
                            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                            16, 16, 336, 44, hwnd, nullptr,
                            reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            g_numeric_prompt.edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", format_edit_number(g_numeric_prompt.value).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                16, 66, 336, 24, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_EDIT)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND ok = CreateWindowExW(
                0, L"BUTTON", g_numeric_prompt.apply_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                102, 104, 116, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_OK)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND cancel = CreateWindowExW(
                0, L"BUTTON", g_numeric_prompt.cancel_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                230, 104, 122, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_PROMPT_CANCEL)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            if (g_numeric_prompt.edit) SendMessageW(g_numeric_prompt.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
                    if (g_numeric_prompt.edit) GetWindowTextW(g_numeric_prompt.edit, buf, 128);
                    const bool valid = parse_wide_double_text(buf, value) &&
                                       std::isfinite(value) &&
                                       (!g_numeric_prompt.positive_only || value > 0.0);
                    if (!valid) {
                        MessageBoxW(hwnd, g_numeric_prompt.invalid_text.c_str(), g_numeric_prompt.title.c_str(), MB_OK | MB_ICONWARNING);
                        if (g_numeric_prompt.edit) SetFocus(g_numeric_prompt.edit);
                        return 0;
                    }
                    g_numeric_prompt.value = value;
                    g_numeric_prompt.accepted = true;
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
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            RECT rc{};
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
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            const int ctl_id = GetDlgCtrlID(dis->hwndItem);
            wchar_t txt[128]{};
            GetWindowTextW(dis->hwndItem, txt, 128);
            const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            if (ctl_id == IDC_SPEED_PROMPT_OK) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, true, false);
                return TRUE;
            }
            if (ctl_id == IDC_SPEED_PROMPT_CANCEL) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_panel);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_DESTROY:
            g_numeric_prompt.done = true;
            g_numeric_prompt.wnd = nullptr;
            g_numeric_prompt.edit = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK RangePromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            auto mkstatic = [&](const std::wstring& text, int x, int y, int w, int h) {
                HWND ctl = CreateWindowExW(0, L"STATIC", text.c_str(),
                                           WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                           x, y, w, h, hwnd, nullptr,
                                           reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
                if (ctl) SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return ctl;
            };
            mkstatic(g_range_prompt.info_label, 16, 14, 388, 38);
            mkstatic(g_range_prompt.start_label, 16, 60, 388, 18);
            g_range_prompt.start_edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", format_edit_number(g_range_prompt.start_value).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                16, 82, 388, 24, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_START_EDIT)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            mkstatic(g_range_prompt.end_label, 16, 116, 388, 18);
            g_range_prompt.end_edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", format_edit_number(g_range_prompt.end_value).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                16, 138, 388, 24, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_END_EDIT)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND ok = CreateWindowExW(
                0, L"BUTTON", g_range_prompt.apply_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                154, 178, 122, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_OK)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND cancel = CreateWindowExW(
                0, L"BUTTON", g_range_prompt.cancel_text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                286, 178, 118, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RANGE_PROMPT_CANCEL)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            if (g_range_prompt.start_edit) SendMessageW(g_range_prompt.start_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (g_range_prompt.end_edit) SendMessageW(g_range_prompt.end_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (ok) SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (cancel) SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RANGE_PROMPT_OK: {
                    double start = 0.0, end = 0.0;
                    wchar_t buf_start[128]{}, buf_end[128]{};
                    if (g_range_prompt.start_edit) GetWindowTextW(g_range_prompt.start_edit, buf_start, 128);
                    if (g_range_prompt.end_edit) GetWindowTextW(g_range_prompt.end_edit, buf_end, 128);
                    const bool start_valid = parse_wide_double_text(buf_start, start) &&
                                             std::isfinite(start) &&
                                             start >= g_range_prompt.min_value &&
                                             start < g_range_prompt.max_value;
                    if (!start_valid) {
                        MessageBoxW(hwnd, g_range_prompt.invalid_start_text.c_str(), g_range_prompt.title.c_str(), MB_OK | MB_ICONWARNING);
                        if (g_range_prompt.start_edit) SetFocus(g_range_prompt.start_edit);
                        return 0;
                    }
                    const bool end_valid = parse_wide_double_text(buf_end, end) &&
                                           std::isfinite(end) &&
                                           end > start &&
                                           end <= g_range_prompt.max_value;
                    if (!end_valid) {
                        MessageBoxW(hwnd, g_range_prompt.invalid_end_text.c_str(), g_range_prompt.title.c_str(), MB_OK | MB_ICONWARNING);
                        if (g_range_prompt.end_edit) SetFocus(g_range_prompt.end_edit);
                        return 0;
                    }
                    g_range_prompt.start_value = start;
                    g_range_prompt.end_value = end;
                    g_range_prompt.accepted = true;
                    DestroyWindow(hwnd);
                    return 0;
                }
                case IDC_RANGE_PROMPT_CANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            RECT rc{};
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
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            const int ctl_id = GetDlgCtrlID(dis->hwndItem);
            wchar_t txt[128]{};
            GetWindowTextW(dis->hwndItem, txt, 128);
            const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            if (ctl_id == IDC_RANGE_PROMPT_OK) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, true, false);
                return TRUE;
            }
            if (ctl_id == IDC_RANGE_PROMPT_CANCEL) {
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_panel);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_DESTROY:
            g_range_prompt.done = true;
            g_range_prompt.wnd = nullptr;
            g_range_prompt.start_edit = nullptr;
            g_range_prompt.end_edit = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool prompt_numeric_value(const wchar_t* title, const wchar_t* label,
                          const wchar_t* apply_text, const wchar_t* cancel_text,
                          const wchar_t* invalid_text, double initial_value,
                          bool positive_only, double& out_value) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = NumericPromptProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmNumericPrompt";
        atom = RegisterClassExW(&wc);
    }

    g_numeric_prompt.done = false;
    g_numeric_prompt.accepted = false;
    g_numeric_prompt.positive_only = positive_only;
    g_numeric_prompt.value = initial_value;
    g_numeric_prompt.title = title;
    g_numeric_prompt.label = label;
    g_numeric_prompt.apply_text = apply_text;
    g_numeric_prompt.cancel_text = cancel_text;
    g_numeric_prompt.invalid_text = invalid_text;
    g_numeric_prompt.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmNumericPrompt",
        g_numeric_prompt.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 384, 172,
        g.main, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_numeric_prompt.wnd) return false;

    RECT mr{}, wr{};
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_numeric_prompt.wnd, &wr);
    SetWindowPos(
        g_numeric_prompt.wnd, HWND_TOP,
        mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
        mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(g.main, FALSE);
    if (g_numeric_prompt.edit) {
        SetFocus(g_numeric_prompt.edit);
        SendMessageW(g_numeric_prompt.edit, EM_SETSEL, 0, -1);
    }

    MSG msg;
    while (!g_numeric_prompt.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_numeric_prompt.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(g.main, TRUE);
    SetForegroundWindow(g.main);
    if (!g_numeric_prompt.accepted) return false;
    out_value = g_numeric_prompt.value;
    return true;
}

bool prompt_light_mode_window(double range_start, double range_end, double& out_start, double& out_end) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = RangePromptProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmRangePrompt";
        atom = RegisterClassExW(&wc);
    }

    const bool en = (g_str == &kEn);
    g_range_prompt.done = false;
    g_range_prompt.accepted = false;
    g_range_prompt.min_value = range_start;
    g_range_prompt.max_value = range_end;
    g_range_prompt.start_value = std::clamp(g.light_mode_open_start, range_start, range_end);
    g_range_prompt.end_value = std::clamp(g.light_mode_open_end, g_range_prompt.start_value + 1e-9, range_end);
    if (g_range_prompt.end_value <= g_range_prompt.start_value) {
        g_range_prompt.end_value = std::min(range_end, g_range_prompt.start_value + 10.0);
    }
    g_range_prompt.title = g_str->light_mode_range_title;
    g_range_prompt.info_label = (en ? L"Available range: " : L"Доступный диапазон: ") +
                                format_edit_number(range_start) + L" .. " +
                                format_edit_number(range_end) + (en ? L" s" : L" c");
    g_range_prompt.start_label = g_str->light_mode_range_start;
    g_range_prompt.end_label = g_str->light_mode_range_end;
    g_range_prompt.apply_text = g_str->light_mode_range_apply;
    g_range_prompt.cancel_text = speed_prompt_cancel_text();
    g_range_prompt.invalid_start_text = g_str->light_mode_range_invalid_start;
    g_range_prompt.invalid_end_text = g_str->light_mode_range_invalid_end;
    g_range_prompt.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmRangePrompt",
        g_range_prompt.title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 430, 246,
        g.main, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_range_prompt.wnd) return false;

    RECT mr{}, wr{};
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_range_prompt.wnd, &wr);
    SetWindowPos(
        g_range_prompt.wnd, HWND_TOP,
        mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
        mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(g.main, FALSE);
    if (g_range_prompt.start_edit) {
        SetFocus(g_range_prompt.start_edit);
        SendMessageW(g_range_prompt.start_edit, EM_SETSEL, 0, -1);
    }

    MSG msg;
    while (!g_range_prompt.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_range_prompt.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(g.main, TRUE);
    SetForegroundWindow(g.main);
    if (!g_range_prompt.accepted) return false;
    g.light_mode_open_start = g_range_prompt.start_value;
    g.light_mode_open_end = g_range_prompt.end_value;
    save_runtime_settings();
    out_start = g_range_prompt.start_value;
    out_end = g_range_prompt.end_value;
    return true;
}

bool prompt_custom_play_speed(double& out_speed) {
    return prompt_numeric_value(
        speed_prompt_title_text(),
        speed_prompt_label_text(),
        speed_prompt_apply_text(),
        speed_prompt_cancel_text(),
        speed_prompt_invalid_text(),
        g.play_speed,
        true,
        out_speed);
}

bool prompt_exact_guide_value(bool vertical, double& out_value) {
    double default_value = 0.0;
    if (vertical) {
        default_value = g.freq_mode ? 0.5 * (g.freq_start + g.freq_end)
                                    : 0.5 * (g.win_start + g.win_end);
    } else if (g.freq_mode) {
        double ymin = 0.0, ymax = 0.0;
        if (!current_freq_yrange(ymin, ymax)) {
            ymin = 0.0;
            ymax = 1.0;
        }
        default_value = 0.5 * (ymin + ymax);
    } else {
        double ymin = 0.0, ymax = 0.0;
        if (!current_time_yrange(ymin, ymax)) {
            ymin = -1.0;
            ymax = 1.0;
        } else if (!g.auto_y) {
            ymin = g.y_lock_min;
            ymax = g.y_lock_max;
        }
        default_value = 0.5 * (ymin + ymax);
    }
    return prompt_numeric_value(
        guide_prompt_title_text(vertical),
        guide_prompt_label_text(vertical),
        guide_prompt_apply_text(),
        guide_prompt_cancel_text(),
        guide_prompt_invalid_text(),
        default_value,
        false,
        out_value);
}

void add_guide_line(bool vertical, double value) {
    GuideLine gl;
    gl.vertical = vertical;
    gl.value = value;
    gl.freq = g.freq_mode;
    g.guides.push_back(gl);
    UndoAction ua;
    ua.type = UndoAction::ADD_LINE;
    ua.line = gl;
    push_undo(ua);
    g.pending_line = 0;
    sync_menu();
    set_status();
    invalidate_plot();
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
    (void)center_frac;
    double ymax = visible_spectrum_ymax();
    if (ymax <= 0) ymax = 1.0;
    double ytop = ymax * 1.08;
    if (!g.auto_y_amp) ytop = g.y_amp_max;
    const double w = ytop;
    if (w <= 0) return;
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

bool prepare_plot_drag(int mx, int my) {
    double *lo, *hi, minb, maxb, minw;
    if (!active_axis(lo, hi, minb, maxb, minw)) return false;
    g.drag_x = mx;
    g.drag_y = my;
    g.drag_lo = *lo;
    g.drag_hi = *hi;
    if (g.freq_mode) {
        if (g.auto_y_amp) {
            double ymax = visible_spectrum_ymax();
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
    return true;
}

void pan_y_by(double frac) {
    if (g.freq_mode) {
        double ymax = visible_spectrum_ymax();
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
static HWND g_loading_cancel_btn = nullptr;
static std::wstring g_loading_text;
static bool g_loading_cancellable = false;

void request_async_load_cancel();

LRESULT CALLBACK LoadingProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_LOADING_CANCEL && HIWORD(wp) == BN_CLICKED) {
                request_async_load_cancel();
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);

            HBRUSH outer = CreateSolidBrush(g_theme->bg_main);
            FillRect(dc, &rc, outer);
            DeleteObject(outer);

            RECT card = rc;
            InflateRect(&card, -2, -2);
            fill_rounded_rect(dc, card, g_theme->bg_panel, g_theme->separator, 12);

            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ old_font = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);

            RECT text_rect = { card.left + 18, card.top + 18, card.right - 18, card.bottom - 18 };
            if (g_loading_cancellable) text_rect.bottom -= 42;
            DrawTextW(dc, g_loading_text.c_str(), -1, &text_rect,
                      DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

            SelectObject(dc, old_font);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void show_loading(const std::wstring& msg, bool cancellable = false) {
    if (g_loading_wnd) { DestroyWindow(g_loading_wnd); g_loading_wnd = nullptr; }
    g_loading_cancel_btn = nullptr;
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
    std::wstring display = msg;
    for (std::size_t i = 0; i < display.size(); ++i) {
        if (display[i] == L'\n' && (i == 0 || display[i - 1] != L'\r')) {
            display.insert(i, 1, L'\r');
            ++i;
        }
    }
    g_loading_text = display;
    g_loading_cancellable = cancellable;

    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = LoadingProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_WAIT);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmLoadingOverlay";
        atom = RegisterClassExW(&wc);
    }

    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HDC screen = GetDC(nullptr);
    HGDIOBJ old_font = SelectObject(screen, font);
    RECT text_rect = { 0, 0, 280, 0 };
    DrawTextW(screen, g_loading_text.c_str(), -1, &text_rect,
              DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(screen, old_font);
    ReleaseDC(nullptr, screen);

    const int text_width = static_cast<int>(text_rect.right - text_rect.left);
    const int text_height = static_cast<int>(text_rect.bottom - text_rect.top);
    const int width = std::clamp(text_width + 52, 236, 340);
    const int button_extra = cancellable ? 44 : 0;
    const int height = std::clamp(text_height + 42 + button_extra, 76, 164);
    g_loading_wnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"LvmLoadingOverlay", L"",
        WS_POPUP | WS_VISIBLE,
        0, 0, width, height, g.main, nullptr, inst, nullptr);
    if (!g_loading_wnd) return;
    RECT mr, wr;
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_loading_wnd, &wr);
    SetWindowPos(g_loading_wnd, HWND_TOPMOST,
                 mr.left + ((mr.right - mr.left) - (wr.right - wr.left)) / 2,
                 mr.top + ((mr.bottom - mr.top) - (wr.bottom - wr.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    if (cancellable) {
        g_loading_cancel_btn = CreateWindowExW(
            0, L"BUTTON", speed_prompt_cancel_text(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 92, 28, g_loading_wnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOADING_CANCEL)),
            inst, nullptr);
        if (g_loading_cancel_btn) {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            SendMessageW(g_loading_cancel_btn, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            MoveWindow(g_loading_cancel_btn, (width - 92) / 2, height - 38, 92, 26, FALSE);
        }
    }
    if (g.main && IsWindow(g.main)) EnableWindow(g.main, FALSE);
    UpdateWindow(g_loading_wnd);
}

void hide_loading() {
    g_loading_cancel_btn = nullptr;
    g_loading_cancellable = false;
    if (g_loading_wnd) { DestroyWindow(g_loading_wnd); g_loading_wnd = nullptr; }
    if (g.main && IsWindow(g.main)) EnableWindow(g.main, TRUE);
}

double precompute_global_gap_step(const std::vector<double>& time) {
    if (time.size() <= 1) return 0.0;
    std::vector<double> diffs;
    diffs.reserve(std::min<std::size_t>(4096, time.size() - 1));
    const std::size_t stride = std::max<std::size_t>(1, (time.size() - 1) / 4096);
    for (std::size_t i = 1; i < time.size() && diffs.size() < 4096; i += stride) {
        const double diff = time[i] - time[i - 1];
        if (std::isfinite(diff) && diff > 0.0) diffs.push_back(diff);
    }
    if (diffs.empty()) return 0.0;
    std::sort(diffs.begin(), diffs.end());
    const std::size_t mid = diffs.size() / 2;
    return (diffs.size() % 2 == 0)
        ? 0.5 * (diffs[mid - 1] + diffs[mid])
        : diffs[mid];
}

void ensure_global_gap_step_ready() {
    if (g.cached_global_gap_step_ready) return;
    g.cached_global_gap_step = precompute_global_gap_step(g.ds.time);
    g.cached_global_gap_step_ready = true;
}

void invalidate_plot_analysis_cache() {
    ++g.plot_analysis_serial;
    g.time_yrange_cache.valid = false;
}

template <typename TResult>
void post_async_result(UINT message, std::unique_ptr<TResult> result) {
    TResult* raw = result.release();
    if (!raw) return;
    if (!g.main || !IsWindow(g.main) || !PostMessageW(g.main, message, 0, reinterpret_cast<LPARAM>(raw))) {
        delete raw;
    }
}

void request_async_load_cancel() {
    if (g.async_load_cancel_flag) {
        g.async_load_cancel_flag->store(true, std::memory_order_relaxed);
    }
    if (g_loading_cancel_btn && IsWindow(g_loading_cancel_btn)) {
        EnableWindow(g_loading_cancel_btn, FALSE);
    }
}

void apply_loaded_dataset(lvm::Dataset ds, const std::wstring& wpath, bool hide_channels,
                          bool requested_time_window, double cached_global_gap_step,
                          bool cached_global_gap_step_ready) {
    if (ds.time.empty()) {
        g.last_error = "No time data available.";
        MessageBoxW(g.main, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
        return;
    }

    g.current_file_partial = requested_time_window || ds.partial;
    g.ds = std::move(ds);
    g.visible.assign(g.ds.channel_count(), hide_channels ? 0 : 1);
    g.channel_labels.clear();
    for (const auto& n : g.ds.names) g.channel_labels.push_back(to_w(n));
    g_channel_colors.clear();
    g_channel_colors.reserve(g.ds.channel_count());
    for (std::size_t i = 0; i < g.ds.channel_count(); ++i) g_channel_colors.push_back(kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))]);
    g.side_selected_channel = g.ds.channel_count() > 0 ? 0 : -1;
    g.side_scroll_y = 0;
    g.channel_formulas.assign(g.ds.channel_count(), default_channel_formula_text());
    g.channel_formula_rpn.assign(g.ds.channel_count(), {});
    g.global_formula = default_channel_formula_text();
    g.global_formula_rpn.clear();
    g.formula_runtime_dirty = true;
    g.formula_ini_deferred = g.light_mode;
    if (!g.formula_ini_deferred) load_channel_formulas_from_ini();
    g.data_t0 = g.ds.time.front();
    g.data_t1 = g.ds.time.back();
    if (g.data_t1 <= g.data_t0) g.data_t1 = g.data_t0 + 1.0;
    g.win_start = g.data_t0;
    g.win_end = g.data_t1;
    g.approx_dt = (g.data_t1 - g.data_t0) / static_cast<double>(g.ds.rows());
    g.cached_global_gap_step = cached_global_gap_step;
    g.cached_global_gap_step_ready = cached_global_gap_step_ready;
    invalidate_plot_analysis_cache();
    clear_measure_point_groups();
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
    if (hide_channels) g.auto_y_amp = true;
    if (g.autoy) { SendMessageW(g.autoy, BM_SETCHECK, BST_CHECKED, 0); InvalidateRect(g.autoy, nullptr, FALSE); }
    if (g.menu) CheckMenuItem(g.menu, IDC_AUTOY, MF_BYCOMMAND | MF_CHECKED);
    clear_spectrum_cache_state();
    g.spec_source_valid = false;
    g.freq_start = 0.0;
    g.freq_end = 1.0;
    if (!requested_time_window) {
        g.cached_scan_path = wpath;
        g.cached_scan_start = g.data_t0;
        g.cached_scan_end = g.data_t1;
        g.cached_scan_valid = true;
    }

    const wchar_t* base = wcsrchr(wpath.c_str(), L'\\');
    g.file_name = base ? base + 1 : wpath;
    SetWindowTextW(g.main, (std::wstring(g_str->app_title) + L" — " + g.file_name).c_str());
    if (g.welcome_wnd) { ShowWindow(g.welcome_wnd, SW_HIDE); show_ui_controls(); }

    rebuild_checks();
    refresh_side_panel_controls();
    refresh_settings_controls();
    layout();
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
    g.last_error.clear();
}

bool start_async_scan_task(const std::wstring& wpath) {
    if (g.async_load_stage != AsyncLoadStage::None) {
        g.last_error = "A file is already loading.";
        return false;
    }
    g.last_error.clear();
    g.async_load_stage = AsyncLoadStage::ScanningRange;
    const unsigned long long token = ++g.async_load_token;
    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    g.async_load_cancel_flag = cancel_flag;
    show_loading(g_str->msg_scanning_range, true);

    const std::wstring path_copy = wpath;
    const std::string narrow_path = to_acp(wpath.c_str());
    try {
        std::thread([token, path_copy, narrow_path, cancel_flag]() {
            auto result = std::make_unique<AsyncScanResult>();
            result->token = token;
            result->path = path_copy;
            std::string scan_error;
            result->ok = lvm::scan_time_bounds(narrow_path, result->range_start, result->range_end, scan_error, cancel_flag.get());
            result->cancelled = !result->ok && cancel_flag->load(std::memory_order_relaxed);
            if (!result->ok) result->error = std::move(scan_error);
            post_async_result(WM_APP_ASYNC_SCAN_DONE, std::move(result));
        }).detach();
    } catch (const std::exception& ex) {
        g.async_load_stage = AsyncLoadStage::None;
        g.async_load_cancel_flag.reset();
        hide_loading();
        g.last_error = ex.what();
        return false;
    }
    return true;
}

bool start_async_load_task(const std::wstring& wpath, const double* fragment_start = nullptr,
                           const double* fragment_end = nullptr, bool hide_channels = false) {
    if (g.async_load_stage != AsyncLoadStage::None) {
        g.last_error = "A file is already loading.";
        return false;
    }

    lvm::LoadOptions load_options{};
    if (fragment_start && fragment_end && std::isfinite(*fragment_start) &&
        std::isfinite(*fragment_end) && *fragment_end > *fragment_start) {
        load_options.use_time_window = true;
        load_options.time_start = *fragment_start;
        load_options.time_end = *fragment_end;
    }

    g.last_error.clear();
    g.async_load_stage = AsyncLoadStage::LoadingFile;
    const unsigned long long token = ++g.async_load_token;
    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    g.async_load_cancel_flag = cancel_flag;
    show_loading(hide_channels ? g_str->msg_loading_light : g_str->msg_loading, true);

    const std::wstring path_copy = wpath;
    const std::string narrow_path = to_acp(wpath.c_str());
    try {
        std::thread([token, path_copy, narrow_path, load_options, hide_channels, cancel_flag]() mutable {
            auto result = std::make_unique<AsyncLoadResult>();
            result->token = token;
            result->path = path_copy;
            result->hide_channels = hide_channels;
            result->requested_time_window = load_options.use_time_window;
            load_options.cancel_flag = cancel_flag.get();
            result->ds = lvm::read_lvm_file(narrow_path, load_options);
            result->ok = result->ds.ok;
            result->cancelled = !result->ok && cancel_flag->load(std::memory_order_relaxed);
            if (result->ok) {
                const std::vector<double> raw_time = result->ds.raw_time.empty() ? result->ds.time : result->ds.raw_time;
                lvm::drop_duplicate_time_channels(result->ds, raw_time);
                if (!load_options.use_time_window && !result->ds.time_rebuilt_from_headers) {
                    lvm::make_monotonic(result->ds.time);
                }
                if (!hide_channels) {
                    result->cached_global_gap_step = precompute_global_gap_step(result->ds.time);
                    result->cached_global_gap_step_ready = true;
                }
            } else {
                result->error = result->ds.error;
            }
            post_async_result(WM_APP_ASYNC_LOAD_DONE, std::move(result));
        }).detach();
    } catch (const std::exception& ex) {
        g.async_load_stage = AsyncLoadStage::None;
        g.async_load_cancel_flag.reset();
        hide_loading();
        g.last_error = ex.what();
        return false;
    }
    return true;
}

bool prompt_and_start_light_mode_load(const std::wstring& wpath, double range_start, double range_end) {
    double fragment_start = 0.0;
    double fragment_end = 0.0;
    if (!prompt_light_mode_window(range_start, range_end, fragment_start, fragment_end)) {
        g.last_error.clear();
        return false;
    }
    return start_async_load_task(wpath, &fragment_start, &fragment_end, true);
}

bool load_path_interactive(const std::wstring& wpath) {
    g.last_error.clear();
    if (g.light_mode) {
        if (g.cached_scan_valid && lstrcmpiW(g.cached_scan_path.c_str(), wpath.c_str()) == 0) {
            return prompt_and_start_light_mode_load(wpath, g.cached_scan_start, g.cached_scan_end);
        }
        return start_async_scan_task(wpath);
    }
    return start_async_load_task(wpath);
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
    if (!load_path_interactive(file) && !g.last_error.empty())
        MessageBoxW(g.main, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
}

// ---- series ---------------------------------------------------------------

struct ChannelRenderView {
    const std::vector<double>* raw = nullptr;
    const std::vector<double>* cache = nullptr;
    TransformRuntimeKind kind = TransformRuntimeKind::Identity;
    double mul = 1.0;
    double add = 0.0;
};

ChannelRenderView make_channel_render_view(std::size_t channel_index) {
    ChannelRenderView view;
    if (channel_index >= g.ds.channels.size()) return view;
    view.raw = &g.ds.channels[channel_index];
    view.kind = (channel_index < g.channel_transform_kind.size())
        ? g.channel_transform_kind[channel_index]
        : TransformRuntimeKind::Identity;
    if (view.kind == TransformRuntimeKind::Affine) {
        view.mul = g.channel_transform_mul[channel_index];
        view.add = g.channel_transform_add[channel_index];
    } else if (view.kind == TransformRuntimeKind::CachedFormula) {
        ensure_transformed_channel_cache(channel_index);
        view.cache = &g.transformed_channel_cache[channel_index];
    }
    return view;
}

inline double channel_render_value(const ChannelRenderView& view, std::size_t sample_index) {
    if (!view.raw || sample_index >= view.raw->size()) return std::nan("");
    if (view.cache) return (*view.cache)[sample_index];
    const double raw = (*view.raw)[sample_index];
    if (view.kind == TransformRuntimeKind::Affine) return raw * view.mul + view.add;
    return raw;
}

bool any_visible_channel() {
    for (char visible : g.visible) {
        if (visible) return true;
    }
    return false;
}

std::size_t light_mode_render_stride(std::size_t sample_count, std::size_t target_samples) {
    if (!g.light_mode || target_samples == 0 || sample_count <= target_samples) return 1;
    return (sample_count + target_samples - 1) / target_samples;
}

bool current_time_yrange_window(std::size_t lo, std::size_t hi, double& ymin, double& ymax) {
    if (!has_data() || hi <= lo) return false;
    if (!any_visible_channel()) {
        ymin = -1.0;
        ymax = 1.0;
        return true;
    }
    if (g.time_yrange_cache.valid &&
        g.time_yrange_cache.serial == g.plot_analysis_serial &&
        g.time_yrange_cache.lo == lo &&
        g.time_yrange_cache.hi == hi &&
        g.time_yrange_cache.win_start == g.win_start &&
        g.time_yrange_cache.win_end == g.win_end) {
        ymin = g.time_yrange_cache.ymin;
        ymax = g.time_yrange_cache.ymax;
        return true;
    }
    ensure_channel_formula_vectors();
    const std::size_t stride = light_mode_render_stride(hi - lo, g.light_mode ? 120000u : 250000u);
    ymin = 1e300; ymax = -1e300;
    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        const ChannelRenderView view = make_channel_render_view(c);
        for (std::size_t i = lo; i < hi; i += stride) {
            const double v = channel_render_value(view, i);
            if (std::isnan(v)) continue;
            if (v < ymin) ymin = v;
            if (v > ymax) ymax = v;
        }
        if (stride > 1) {
            const std::size_t last = hi - 1;
            const double v = channel_render_value(view, last);
            if (!std::isnan(v)) {
                if (v < ymin) ymin = v;
                if (v > ymax) ymax = v;
            }
        }
    }
    if (ymin > ymax) { ymin = -1; ymax = 1; }
    if (ymax - ymin < 1e-12) { ymin -= 1; ymax += 1; }
    const double pad = (ymax - ymin) * 0.05;
    ymin -= pad; ymax += pad;
    g.time_yrange_cache.valid = true;
    g.time_yrange_cache.lo = lo;
    g.time_yrange_cache.hi = hi;
    g.time_yrange_cache.win_start = g.win_start;
    g.time_yrange_cache.win_end = g.win_end;
    g.time_yrange_cache.serial = g.plot_analysis_serial;
    g.time_yrange_cache.ymin = ymin;
    g.time_yrange_cache.ymax = ymax;
    return true;
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
    return current_time_yrange_window(lo, hi, ymin, ymax);
}

// Auto-fit amplitude range over the currently visible FFT window (with 5% pad).
bool current_freq_yrange(double& ymin, double& ymax) {
    if (!g.freq_mode) return false;
    if (!ensure_current_spectrum() || g.spec.freqs.size() < 2) return false;
    ymin = 0.0;
    ymax = 0.0;
    std::size_t klo = static_cast<std::size_t>(
        std::lower_bound(g.spec.freqs.begin(), g.spec.freqs.end(), g.freq_start) - g.spec.freqs.begin());
    std::size_t khi = static_cast<std::size_t>(
        std::upper_bound(g.spec.freqs.begin(), g.spec.freqs.end(), g.freq_end) - g.spec.freqs.begin());
    if (klo > 0) --klo;
    if (khi < g.spec.freqs.size()) ++khi;
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
        if (ci < 0 || !g.visible[ci]) continue;
        for (std::size_t k = klo; k < khi; ++k) {
            if (g.spec.amp[j][k] > ymax) ymax = g.spec.amp[j][k];
        }
    }
    if (ymax <= 0.0) ymax = 1.0;
    const double pad = ymax * 0.05;
    ymax += pad;
    return true;
}

double visible_spectrum_ymax() {
    if (!ensure_current_spectrum()) return 0.0;
    double ymax = 0.0;
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
        if (ci < 0 || !g.visible[ci]) continue;
        for (double v : g.spec.amp[j]) {
            if (v > ymax) ymax = v;
        }
    }
    return ymax;
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
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, p.left, p.top, p.right, p.bottom);
    SelectObject(dc, old_brush);
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
        HBRUSH cb = CreateSolidBrush(col);
        HGDIOBJ old_b = SelectObject(dc, cb);
        HGDIOBJ old_p = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, box_x + pad, y + 6, box_x + pad + 14, y + 6 + 3, 2, 2);
        SelectObject(dc, old_p);
        SelectObject(dc, old_b);
        DeleteObject(cb);

        SetTextColor(dc, vis ? g_theme->text_primary : g_theme->text_secondary);
        SetTextAlign(dc, TA_LEFT | TA_TOP);
        std::wstring nm = g.channel_labels[i];
        const int text_x = box_x + pad + 18;
        TextOutW(dc, text_x, y, nm.c_str(), static_cast<int>(nm.size()));
        if (!vis) {
            SIZE text_size{};
            GetTextExtentPoint32W(dc, nm.c_str(), static_cast<int>(nm.size()), &text_size);
            HPEN line = CreatePen(PS_SOLID, 1, g_theme->text_secondary);
            HGDIOBJ old_lp = SelectObject(dc, line);
            const int line_y = y + max(6, item_h / 2);
            MoveToEx(dc, text_x, line_y, nullptr);
            LineTo(dc, text_x + text_size.cx, line_y);
            SelectObject(dc, old_lp);
            DeleteObject(line);
        }

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
    if (!has_measure_points() || !g.vvalid) return;
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

    HFONT mf = g.axis_font ? g.axis_font : g.ui_font;
    HGDIOBJ oldf = SelectObject(dc, mf);
    SetBkMode(dc, TRANSPARENT);
    wchar_t b[96];
    HBRUSH wb = CreateSolidBrush(g_theme->bg_plot);
    HPEN bp = CreatePen(PS_SOLID, 1, g_theme->frame);
    HGDIOBJ old = SelectObject(dc, bp);
    HGDIOBJ prev_brush = SelectObject(dc, wb);
    for (const auto& group : g.point_groups) {
        if (!group.visible || group.points.empty()) continue;

        HPEN seg = CreatePen(PS_DASH, 1, group.color);
        HGDIOBJ old_seg = SelectObject(dc, seg);
        for (std::size_t i = 1; i < group.points.size(); ++i) {
            MoveToEx(dc, mx(group.points[i - 1].first), my(group.points[i - 1].second), nullptr);
            LineTo(dc, mx(group.points[i].first), my(group.points[i].second));
        }
        SelectObject(dc, old_seg);
        DeleteObject(seg);

        HPEN pp = CreatePen(PS_SOLID, 2, group.color);
        HGDIOBJ old_point_pen = SelectObject(dc, pp);
        for (std::size_t i = 0; i < group.points.size(); ++i) {
            const int X = mx(group.points[i].first), Y = my(group.points[i].second);
            MoveToEx(dc, X - 8, Y, nullptr); LineTo(dc, X + 9, Y);
            MoveToEx(dc, X, Y - 8, nullptr); LineTo(dc, X, Y + 9);

            HBRUSH dot_brush = CreateSolidBrush(group.color);
            HGDIOBJ old_br = SelectObject(dc, dot_brush);
            HGDIOBJ old_pn = SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, X - 3, Y - 3, X + 4, Y + 4);
            SelectObject(dc, old_pn);
            SelectObject(dc, old_br);
            DeleteObject(dot_brush);

            std::wstring lab;
            if (g.pdisp.number) { swprintf(b, 96, L"#%zu ", i + 1); lab += b; }
            if (g.pdisp.x) { swprintf(b, 96, g_str->fmt_pt_x, group.points[i].first); lab += b; lab += xunit; lab += L" "; }
            if (g.pdisp.y) { swprintf(b, 96, g_str->fmt_y, group.points[i].second); lab += b; }
            if (!lab.empty()) {
                SetTextColor(dc, group.color);
                SetTextAlign(dc, TA_LEFT | TA_BOTTOM);
                SIZE ts;
                GetTextExtentPoint32W(dc, lab.c_str(), static_cast<int>(lab.size()), &ts);
                RoundRect(dc, X + 6, Y - 4 - ts.cy, X + 12 + ts.cx, Y + 2, 3, 3);
                TextOutW(dc, X + 8, Y - 2, lab.c_str(), static_cast<int>(lab.size()));
            }

            if (i >= 1) {
                const double dx = group.points[i].first - group.points[i - 1].first;
                const double dy = group.points[i].second - group.points[i - 1].second;
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
                    const int mxp = (mx(group.points[i].first) + mx(group.points[i - 1].first)) / 2;
                    const int myp = (my(group.points[i].second) + my(group.points[i - 1].second)) / 2;
                    SetTextColor(dc, group.color);
                    SetTextAlign(dc, TA_CENTER | TA_BOTTOM);
                    SIZE ts;
                    GetTextExtentPoint32W(dc, dl.c_str(), static_cast<int>(dl.size()), &ts);
                    RoundRect(dc, mxp - ts.cx/2 - 4, myp - 4 - ts.cy, mxp + ts.cx/2 + 6, myp + 2, 3, 3);
                    TextOutW(dc, mxp, myp - 2, dl.c_str(), static_cast<int>(dl.size()));
                }
            }
        }
        SelectObject(dc, old_point_pen);
        DeleteObject(pp);
    }
    SelectObject(dc, old);
    DeleteObject(bp);
    SelectObject(dc, prev_brush);
    DeleteObject(wb);
    SelectObject(dc, oldf);

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

// Render a polyline as a smooth Catmull-Rom spline through the given pixel
// points. Purely visual: it curves *between* samples but never moves them.
int catmull_rom_segment_steps(const POINT& a, const POINT& b) {
    const int dist = max(std::abs(b.x - a.x), std::abs(b.y - a.y));
    if (dist <= 2) return 1;
    const int max_steps = g.light_mode ? 5 : 8;
    return std::clamp(dist / 12 + 1, 1, max_steps);
}

bool should_render_smoothed_polyline(std::size_t point_count, int pixel_width) {
    if (!g.visual_smooth || point_count < 2) return false;
    const std::size_t soft_limit = g.light_mode
        ? std::max<std::size_t>(static_cast<std::size_t>(pixel_width) * 3, 600)
        : std::max<std::size_t>(static_cast<std::size_t>(pixel_width) * 6, 1800);
    return point_count <= soft_limit;
}

void draw_catmull_rom(HDC dc, const std::vector<POINT>& pts) {
    if (pts.size() < 2) return;
    if (pts.size() == 2) {
        MoveToEx(dc, pts[0].x, pts[0].y, nullptr);
        LineTo(dc, pts[1].x, pts[1].y);
        return;
    }
    MoveToEx(dc, pts[0].x, pts[0].y, nullptr);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const POINT& p0 = pts[i == 0 ? 0 : i - 1];
        const POINT& p1 = pts[i];
        const POINT& p2 = pts[i + 1];
        const POINT& p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
        const int steps = catmull_rom_segment_steps(p1, p2);
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

struct TimeStepEstimate {
    double step = 0.0;
    std::size_t count = 0;
};

TimeStepEstimate estimate_time_step(const std::vector<double>& time, std::size_t lo, std::size_t hi, std::size_t max_diffs) {
    if (hi <= lo + 1 || max_diffs == 0) return {};
    std::vector<double> diffs;
    diffs.reserve(max_diffs);
    const std::size_t span = hi - lo;
    const std::size_t stride = std::max<std::size_t>(1, span / std::max<std::size_t>(max_diffs, 1));
    for (std::size_t i = lo + 1; i < hi && diffs.size() < max_diffs; i += stride) {
        const double d = time[i] - time[i - 1];
        if (std::isfinite(d) && d > 0.0) diffs.push_back(d);
    }
    if (diffs.empty()) return {};

    std::sort(diffs.begin(), diffs.end());
    const std::size_t mid = diffs.size() / 2;
    double median = (diffs.size() % 2 == 0)
        ? 0.5 * (diffs[mid - 1] + diffs[mid])
        : diffs[mid];
    if (!(median > 0.0) || !std::isfinite(median)) return {};
    return { median, diffs.size() };
}

double effective_time_gap_step(const std::vector<double>& time, std::size_t lo, std::size_t hi) {
    const TimeStepEstimate local = estimate_time_step(time, lo, hi, 512);
    ensure_global_gap_step_ready();
    const double global_step = g.cached_global_gap_step;

    if (local.step > 0.0 && global_step > 0.0) {
        if (local.count < 8) return global_step;
        if (local.step > global_step * 8.0) return global_step;
        return std::min(local.step, global_step * 4.0);
    }
    if (local.step > 0.0) return local.step;
    if (global_step > 0.0) return global_step;
    return 0.0;
}

void draw_time(HDC dc, const RECT& p) {
    g.visible_gap_markers.clear();
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
    current_time_yrange_window(lo, hi, ymin, ymax);
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
    bool has_visible_fft_window = false;
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
        double sel0 = std::min(fft0, fft1);
        double sel1 = std::max(fft0, fft1);
        clamp_time_window(sel0, sel1);
        fft0 = sel0;
        fft1 = sel1;
        const double vis0 = std::max(sel0, g.win_start);
        const double vis1 = std::min(sel1, g.win_end);
        if (vis1 > vis0) {
            has_visible_fft_window = true;
            fft_left_px = mapx(vis0);
            fft_right_px = mapx(vis1);
            if (fft_right_px <= fft_left_px) fft_right_px = fft_left_px + 1;
        }
    }

    static std::vector<float> cmin, cmax;
    const bool visible_channels = any_visible_channel();
    const bool sparse = (hi - lo) <= static_cast<std::size_t>(pw) * 2;
    const bool allow_smoothing = should_render_smoothed_polyline(hi - lo, pw);
    const std::size_t light_mode_gap_budget = std::max<std::size_t>(static_cast<std::size_t>(pw) * 48, 120000);
    const std::size_t normal_gap_budget = std::max<std::size_t>(static_cast<std::size_t>(pw) * 96, 220000);
    const bool enable_gap_scan =
        g.show_gap_markers &&
        (!g.light_mode || visible_channels) &&
        (hi - lo <= (g.light_mode ? light_mode_gap_budget : normal_gap_budget));
    const double gap_step = enable_gap_scan ? effective_time_gap_step(t, lo, hi) : 0.0;
    const double gap_threshold = (gap_step > 0.0)
        ? (gap_step * 64.0)
        : std::numeric_limits<double>::infinity();

    struct TimeGapAnnotation {
        int left_px = 0;
        int right_px = 0;
        double duration = 0.0;
        long long estimated_missing_samples = 0;
    };
    std::vector<TimeGapAnnotation> gap_annotations;
    if (enable_gap_scan && std::isfinite(gap_threshold)) {
        for (std::size_t i = std::max<std::size_t>(lo + 1, 1); i < hi; ++i) {
            const double dt = t[i] - t[i - 1];
            if (!std::isfinite(dt) || dt <= gap_threshold) continue;

            int left_px = mapx(t[i - 1]);
            int right_px = mapx(t[i]);
            if (right_px < left_px) std::swap(left_px, right_px);
            left_px = std::clamp(left_px, static_cast<int>(p.left) + 1, static_cast<int>(p.right) - 1);
            right_px = std::clamp(right_px, static_cast<int>(p.left) + 1, static_cast<int>(p.right) - 1);
            if (right_px <= left_px) right_px = std::min(static_cast<int>(p.right) - 1, left_px + 1);

            long long estimated_missing_samples = 0;
            if (gap_step > 0.0) {
                estimated_missing_samples = static_cast<long long>(std::llround(dt / gap_step)) - 1;
                if (estimated_missing_samples < 0) estimated_missing_samples = 0;
            }

            gap_annotations.push_back(TimeGapAnnotation{
                left_px,
                right_px,
                dt,
                estimated_missing_samples
            });
        }
    }

    if (g.show_gap_markers && !gap_annotations.empty()) {
        const COLORREF gap_orange = RGB(245, 140, 32);
        const COLORREF gap_fill = (g_theme == &kDarkTheme)
            ? mix_color(g_theme->bg_plot, gap_orange, 44)
            : mix_color(g_theme->bg_plot, gap_orange, 32);
        HBRUSH gap_brush = CreateSolidBrush(gap_fill);
        HPEN gap_pen = CreatePen(PS_DOT, 1, gap_orange);
        HGDIOBJ old_gap_brush = SelectObject(dc, gap_brush);
        HGDIOBJ old_gap_pen = SelectObject(dc, gap_pen);

        for (const auto& gap : gap_annotations) {
            RECT gap_rect = { gap.left_px, p.top + 1, gap.right_px, p.bottom - 1 };
            if (gap_rect.right <= gap_rect.left) continue;
            Rectangle(dc, gap_rect.left, gap_rect.top, gap_rect.right, gap_rect.bottom);
            App::GapMarkerVisual visual;
            visual.rect = gap_rect;
            visual.duration = gap.duration;
            visual.estimated_missing_samples = gap.estimated_missing_samples;
            g.visible_gap_markers.push_back(visual);
        }

        SelectObject(dc, old_gap_pen);
        SelectObject(dc, old_gap_brush);
        DeleteObject(gap_pen);
        DeleteObject(gap_brush);
    }

    if (g.light_mode && !visible_channels) {
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);
        g_legend_items.clear();
        g_legend_box = {0, 0, 0, 0};
        g.vx0 = g.win_start; g.vx1 = g.win_end; g.vy0 = ymin; g.vy1 = ymax;
        g.vrect = p; g.vvalid = true;
        return;
    }

    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        const ChannelRenderView view = make_channel_render_view(c);
        HPEN pen = CreatePen(PS_SOLID, 1, channel_color(c));
        HGDIOBJ old = SelectObject(dc, pen);

        if (sparse) {
            // Collect contiguous (non-NaN) runs, then draw each as straight
            // segments or a visual spline. NaN gaps break the line.
            std::vector<POINT> run;
            run.reserve(std::min<std::size_t>(hi - lo, 2048));
            auto flush = [&]() {
                if (allow_smoothing) {
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
                const double v = channel_render_value(view, i);
                if (std::isnan(v)) { flush(); continue; }
                if (!run.empty() && i > lo) {
                    const double dt = t[i] - t[i - 1];
                    if (std::isfinite(dt) && dt > gap_threshold) flush();
                }
                run.push_back(POINT{mapx(t[i]), mapy(v)});
            }
            flush();

            // With visual smoothing on and few points in view, mark the real
            // samples so it's clear the curve only interpolates between them.
            if (allow_smoothing && (hi - lo) <= 400) {
                HBRUSH dot = CreateSolidBrush(channel_color(c));
                HGDIOBJ ob = SelectObject(dc, dot);
                HGDIOBJ opn = SelectObject(dc, GetStockObject(NULL_PEN));
                for (std::size_t i = lo; i < hi; ++i) {
                    const double v = channel_render_value(view, i);
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
            const std::size_t dense_budget = g.light_mode
                ? std::max<std::size_t>(static_cast<std::size_t>(pw) * 40, 4000)
                : std::max<std::size_t>(static_cast<std::size_t>(pw) * 96, 8000);
            const std::size_t dense_stride = light_mode_render_stride(hi - lo, dense_budget);
            for (std::size_t i = lo; i < hi; i += dense_stride) {
                const float v = static_cast<float>(channel_render_value(view, i));
                if (std::isnan(v)) continue;
                int cxp = mapx(t[i]) - p.left;
                if (cxp < 0 || cxp >= pw) continue;
                if (v < cmin[cxp]) cmin[cxp] = v;
                if (v > cmax[cxp]) cmax[cxp] = v;
            }
            if (dense_stride > 1) {
                const std::size_t last = hi - 1;
                const float v = static_cast<float>(channel_render_value(view, last));
                if (!std::isnan(v)) {
                    int cxp = mapx(t[last]) - p.left;
                    if (cxp >= 0 && cxp < pw) {
                        if (v < cmin[cxp]) cmin[cxp] = v;
                        if (v > cmax[cxp]) cmax[cxp] = v;
                    }
                }
            }
            int prev_x = -1, prev_y = 0;
            for (int cxp = 0; cxp < pw; ++cxp) {
                if (cmax[cxp] < -1e29f) continue;
                const int x = p.left + cxp;
                const int yhi = mapy(cmax[cxp]);
                const int ylo = mapy(cmin[cxp]);
                MoveToEx(dc, x, ylo, nullptr);
                LineTo(dc, x, yhi - 1);
                if (prev_x >= 0 && x - prev_x <= 2) {
                    MoveToEx(dc, prev_x, prev_y, nullptr);
                    LineTo(dc, x, (yhi + ylo) / 2);
                }
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

    if (show_fft_window) {
        HRGN overlay_clip = CreateRectRgn(p.left, p.top, p.right + 1, p.bottom + 1);
        SelectClipRgn(dc, overlay_clip);

        HPEN tick_pen = CreatePen(PS_SOLID, 2, g_theme->accent);
        HGDIOBJ old_tick_pen = SelectObject(dc, tick_pen);
        const int tick = 7;
        const int top_y = p.top + 2;
        const int bottom_y = p.bottom - 2;
        const int handle_margin = 9;
        const int tri_w = 6;
        const int tri_h = 7;
        const COLORREF handle_outline = g_theme->btn_border;
        const COLORREF handle_fill = g_theme->accent;
        const int min_x = static_cast<int>(p.left) + handle_margin;
        const int max_x = static_cast<int>(p.right) - handle_margin;
        auto clamp_handle_x = [&](int x) {
            if (min_x > max_x) return (static_cast<int>(p.left) + static_cast<int>(p.right)) / 2;
            return std::clamp(x, min_x, max_x);
        };
        const int left_x = clamp_handle_x(mapx(fft0));
        const int right_x = clamp_handle_x(mapx(fft1));
        {
            const int sel_left = has_visible_fft_window
                ? std::clamp(std::min(fft_left_px, fft_right_px), static_cast<int>(p.left) + 1, static_cast<int>(p.right) - 1)
                : static_cast<int>(p.right) - 1;
            const int sel_right = has_visible_fft_window
                ? std::clamp(std::max(fft_left_px, fft_right_px), static_cast<int>(p.left) + 1, static_cast<int>(p.right) - 1)
                : static_cast<int>(p.left) + 1;
            const int shade_top = static_cast<int>(p.top) + 1;
            const int shade_bottom = static_cast<int>(p.bottom) - 1;
            const int shade_h = shade_bottom - shade_top;
            if (shade_h > 0) {
                Gdiplus::Graphics gfx(dc);
                gfx.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
                const bool dark = (g_theme == &kDarkTheme);
                const Gdiplus::Color outer_fill_color(dark ? 48 : 34,
                    GetRValue(g_theme->text_secondary),
                    GetGValue(g_theme->text_secondary),
                    GetBValue(g_theme->text_secondary));
                const Gdiplus::Color outer_hatch_color(dark ? 92 : 76,
                    GetRValue(g_theme->accent),
                    GetGValue(g_theme->accent),
                    GetBValue(g_theme->accent));
                const Gdiplus::Color selected_fill_color(dark ? 14 : 10,
                    GetRValue(g_theme->accent),
                    GetGValue(g_theme->accent),
                    GetBValue(g_theme->accent));
                const Gdiplus::Color selected_edge_color(dark ? 140 : 120,
                    GetRValue(g_theme->accent),
                    GetGValue(g_theme->accent),
                    GetBValue(g_theme->accent));
                const Gdiplus::Color selected_edge_soft(dark ? 70 : 58,
                    GetRValue(g_theme->text_secondary),
                    GetGValue(g_theme->text_secondary),
                    GetBValue(g_theme->text_secondary));
                const Gdiplus::Color clear_color(0, 0, 0, 0);

                auto fill_rect = [&](int x0, int x1, const Gdiplus::Color& fill, const Gdiplus::Color& hatch_color, Gdiplus::HatchStyle hatch_style) {
                    if (x1 <= x0) return;
                    const int w = x1 - x0;
                    Gdiplus::SolidBrush fill_brush(fill);
                    gfx.FillRectangle(
                        &fill_brush,
                        static_cast<Gdiplus::REAL>(x0),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(w),
                        static_cast<Gdiplus::REAL>(shade_h));
                    Gdiplus::HatchBrush hatch(
                        hatch_style,
                        hatch_color,
                        clear_color);
                    gfx.FillRectangle(
                        &hatch,
                        static_cast<Gdiplus::REAL>(x0),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(w),
                        static_cast<Gdiplus::REAL>(shade_h));
                };

                auto tint_rect = [&](int x0, int x1, const Gdiplus::Color& fill) {
                    if (x1 <= x0) return;
                    const int w = x1 - x0;
                    Gdiplus::SolidBrush fill_brush(fill);
                    gfx.FillRectangle(
                        &fill_brush,
                        static_cast<Gdiplus::REAL>(x0),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(w),
                        static_cast<Gdiplus::REAL>(shade_h));
                };

                fill_rect(static_cast<int>(p.left) + 1, sel_left, outer_fill_color, outer_hatch_color, Gdiplus::HatchStyleWideDownwardDiagonal);
                fill_rect(sel_right, static_cast<int>(p.right), outer_fill_color, outer_hatch_color, Gdiplus::HatchStyleWideDownwardDiagonal);
                if (has_visible_fft_window && sel_right > sel_left) {
                    tint_rect(sel_left, sel_right, selected_fill_color);
                }

                if (has_visible_fft_window && sel_right > sel_left) {
                    HPEN sel_pen = CreatePen(PS_DOT, 1, g_theme->accent);
                    HGDIOBJ old_pen = SelectObject(dc, sel_pen);
                    MoveToEx(dc, sel_left, p.top, nullptr); LineTo(dc, sel_left, p.bottom);
                    MoveToEx(dc, sel_right, p.top, nullptr); LineTo(dc, sel_right, p.bottom);
                    SelectObject(dc, old_pen);
                    DeleteObject(sel_pen);

                    Gdiplus::Pen edge_pen(selected_edge_color, 1.0f);
                    gfx.DrawLine(&edge_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_bottom));
                    gfx.DrawLine(&edge_pen,
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_bottom));
                    gfx.DrawLine(&edge_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_top));
                    gfx.DrawLine(&edge_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_bottom),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_bottom));

                    Gdiplus::Pen soft_pen(selected_edge_soft, 1.0f);
                    gfx.DrawLine(&soft_pen,
                        static_cast<Gdiplus::REAL>(sel_left + 1),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_left + 1),
                        static_cast<Gdiplus::REAL>(shade_bottom));
                    gfx.DrawLine(&soft_pen,
                        static_cast<Gdiplus::REAL>(sel_right - 1),
                        static_cast<Gdiplus::REAL>(shade_top),
                        static_cast<Gdiplus::REAL>(sel_right - 1),
                        static_cast<Gdiplus::REAL>(shade_bottom));
                    gfx.DrawLine(&soft_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_top + 1),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_top + 1));
                    gfx.DrawLine(&soft_pen,
                        static_cast<Gdiplus::REAL>(sel_left),
                        static_cast<Gdiplus::REAL>(shade_bottom - 1),
                        static_cast<Gdiplus::REAL>(sel_right),
                        static_cast<Gdiplus::REAL>(shade_bottom - 1));
                }
            }
        }

        auto draw_handle = [&](int x) {
            MoveToEx(dc, x, top_y, nullptr); LineTo(dc, x + tick, top_y);
            MoveToEx(dc, x - tick, bottom_y, nullptr); LineTo(dc, x, bottom_y);

            POINT top_tri[3] = {
                {x, top_y + tri_h},
                {x - tri_w, top_y},
                {x + tri_w, top_y},
            };
            HBRUSH tri_brush = CreateSolidBrush(handle_fill);
            HPEN tri_pen = CreatePen(PS_SOLID, 1, handle_outline);
            HGDIOBJ old_brush = SelectObject(dc, tri_brush);
            HGDIOBJ old_pen2 = SelectObject(dc, tri_pen);
            Polygon(dc, top_tri, 3);
            SelectObject(dc, old_pen2);
            SelectObject(dc, old_brush);
            DeleteObject(tri_pen);
            DeleteObject(tri_brush);

            const int dot_r = 4;
            const int dot_y = bottom_y - 1;
            HBRUSH dot_brush = CreateSolidBrush(handle_fill);
            HPEN dot_pen = CreatePen(PS_SOLID, 1, handle_outline);
            old_brush = SelectObject(dc, dot_brush);
            old_pen2 = SelectObject(dc, dot_pen);
            Ellipse(dc, x - dot_r, dot_y - dot_r, x + dot_r + 1, dot_y + dot_r + 1);
            SelectObject(dc, old_pen2);
            SelectObject(dc, old_brush);
            DeleteObject(dot_pen);
            DeleteObject(dot_brush);
        };

        if (has_visible_fft_window) {
            draw_handle(left_x);
            draw_handle(right_x);
        }

        SelectObject(dc, old_tick_pen);
        DeleteObject(tick_pen);

        SelectClipRgn(dc, nullptr);
        DeleteObject(overlay_clip);
    }
    draw_legend(dc, p);

    g.vx0 = g.win_start; g.vx1 = g.win_end; g.vy0 = ymin; g.vy1 = ymax;
    g.vrect = p; g.vvalid = true;
}

void draw_freq(HDC dc, const RECT& p) {
    if (!ensure_current_spectrum() || g.spec.freqs.size() < 2) {
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
        const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
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
    const std::size_t visible_bins = khi > klo ? (khi - klo) : 0;
    const bool sparse = visible_bins <= static_cast<std::size_t>(pw) * 2;
    const bool allow_smoothing = should_render_smoothed_polyline(visible_bins, pw);

    HRGN clip = CreateRectRgn(p.left + 1, p.top + 1, p.right, p.bottom);
    SelectClipRgn(dc, clip);

    static std::vector<float> cmin, cmax;
    for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
        const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
        if (ci < 0 || !g.visible[ci]) continue;
        HPEN pen = CreatePen(PS_SOLID, 1, channel_color(ci));
        HGDIOBJ old = SelectObject(dc, pen);
        const auto& a = g.spec.amp[j];
        if (sparse) {
            if (allow_smoothing) {
                std::vector<POINT> pts;
                pts.reserve(visible_bins);
                for (std::size_t k = klo; k < khi; ++k) {
                    pts.push_back(POINT{mapx(f[k]), mapy(a[k])});
                }
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
                bool started = false;
                for (std::size_t k = klo; k < khi; ++k) {
                    const int x = mapx(f[k]);
                    const int y = mapy(a[k]);
                    if (!started) {
                        MoveToEx(dc, x, y, nullptr);
                        started = true;
                    } else {
                        LineTo(dc, x, y);
                    }
                }
            }
        } else {
            cmin.resize(pw, 1e30f);
            cmax.resize(pw, -1e30f);
            std::fill(cmin.begin(), cmin.end(), 1e30f);
            std::fill(cmax.begin(), cmax.end(), -1e30f);
            const std::size_t dense_stride = light_mode_render_stride(visible_bins, static_cast<std::size_t>(pw) * 128);
            for (std::size_t k = klo; k < khi; k += dense_stride) {
                const float v = static_cast<float>(a[k]);
                int cxp = mapx(f[k]) - p.left;
                if (cxp < 0 || cxp >= pw) continue;
                if (v < cmin[cxp]) cmin[cxp] = v;
                if (v > cmax[cxp]) cmax[cxp] = v;
            }
            if (dense_stride > 1 && khi > klo) {
                const std::size_t last = khi - 1;
                const float v = static_cast<float>(a[last]);
                int cxp = mapx(f[last]) - p.left;
                if (cxp >= 0 && cxp < pw) {
                    if (v < cmin[cxp]) cmin[cxp] = v;
                    if (v > cmax[cxp]) cmax[cxp] = v;
                }
            }
            int prev_x = -1;
            int prev_y = 0;
            for (int cxp = 0; cxp < pw; ++cxp) {
                if (cmax[cxp] < -1e29f) continue;
                const int x = p.left + cxp;
                const int yhi = mapy(cmax[cxp]);
                const int ylo = mapy(std::max(0.0f, cmin[cxp]));
                MoveToEx(dc, x, ylo, nullptr);
                LineTo(dc, x, yhi - 1);
                if (prev_x >= 0 && x - prev_x <= 2) {
                    MoveToEx(dc, prev_x, prev_y, nullptr);
                    LineTo(dc, x, (yhi + ylo) / 2);
                }
                prev_x = x;
                prev_y = (yhi + ylo) / 2;
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
        g_legend_items.clear();
        g_legend_box = {0, 0, 0, 0};
        g.visible_gap_markers.clear();
        SetTextAlign(dc, TA_CENTER | TA_BASELINE);
        SetTextColor(dc, g_theme->text_secondary);
        std::wstring msg = (g_str == &kEn)
            ? L"Open a .lvm or .txt file (" + hotkey_text_for_command(IDC_OPEN) + L")"
            : L"Откройте файл .lvm или .txt (" + hotkey_text_for_command(IDC_OPEN) + L")";
        TextOutW(dc, (p.left + p.right) / 2, (p.top + p.bottom) / 2, msg.c_str(), static_cast<int>(msg.size()));
        g.vvalid = false;
        return;
    }
    if (g.freq_mode) {
        g.visible_gap_markers.clear();
        draw_freq(dc, p);
    } else {
        draw_time(dc, p);
    }
    draw_guides(dc);
    draw_markers(dc);
    draw_measure(dc);
}

void release_backbuffer() {
    if (g.backbuffer_dc && g.backbuffer_prev_bmp) {
        SelectObject(g.backbuffer_dc, g.backbuffer_prev_bmp);
        g.backbuffer_prev_bmp = nullptr;
    }
    if (g.backbuffer_bmp) {
        DeleteObject(g.backbuffer_bmp);
        g.backbuffer_bmp = nullptr;
    }
    if (g.backbuffer_dc) {
        DeleteDC(g.backbuffer_dc);
        g.backbuffer_dc = nullptr;
    }
    g.backbuffer_w = 0;
    g.backbuffer_h = 0;
}

bool ensure_backbuffer(HDC hdc, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (g.backbuffer_dc && g.backbuffer_bmp &&
        g.backbuffer_w == width && g.backbuffer_h == height) {
        return true;
    }

    release_backbuffer();

    g.backbuffer_dc = CreateCompatibleDC(hdc);
    if (!g.backbuffer_dc) return false;

    g.backbuffer_bmp = CreateCompatibleBitmap(hdc, width, height);
    if (!g.backbuffer_bmp) {
        release_backbuffer();
        return false;
    }

    g.backbuffer_prev_bmp = static_cast<HBITMAP>(SelectObject(g.backbuffer_dc, g.backbuffer_bmp));
    g.backbuffer_w = width;
    g.backbuffer_h = height;
    return true;
}

void on_paint(HDC hdc) {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int cw = rc.right, ch = rc.bottom;

    HDC mem = nullptr;
    HBITMAP temp_bmp = nullptr;
    HBITMAP temp_prev_bmp = nullptr;
    const bool using_persistent_backbuffer = ensure_backbuffer(hdc, cw, ch);
    if (using_persistent_backbuffer) {
        mem = g.backbuffer_dc;
    } else {
        mem = CreateCompatibleDC(hdc);
        if (!mem) return;
        temp_bmp = CreateCompatibleBitmap(hdc, cw, ch);
        if (!temp_bmp) {
            DeleteDC(mem);
            return;
        }
        temp_prev_bmp = static_cast<HBITMAP>(SelectObject(mem, temp_bmp));
    }

    HBRUSH bg = CreateSolidBrush(g_theme->bg_main);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    if (welcome_visible()) {
        BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
        if (!using_persistent_backbuffer) {
            SelectObject(mem, temp_prev_bmp);
            DeleteObject(temp_bmp);
            DeleteDC(mem);
        }
        return;
    }

    // Toolbar band across the top.
    RECT topbar = {0, 0, cw, kTopBar};
    HBRUSH tbb = CreateSolidBrush(g_theme->bg_toolbar);
    FillRect(mem, &topbar, tbb);
    DeleteObject(tbb);

    const int panel_w = side_panel_width();
    const int panel_left = cw - panel_w;
    if (panel_w > 0) {
        RECT panel = {panel_left, kTopBar, cw, ch - kBottomBar};
        HBRUSH pbg = CreateSolidBrush(g_theme->bg_panel);
        FillRect(mem, &panel, pbg);
        DeleteObject(pbg);
    }

    // Hairline separators (under the toolbar, left of the panel, above status bar).
    HPEN sep = CreatePen(PS_SOLID, 1, g_theme->separator);
    HGDIOBJ oldpen = SelectObject(mem, sep);
    MoveToEx(mem, 0, kTopBar - 1, nullptr); LineTo(mem, cw, kTopBar - 1);
    if (panel_w > 0) {
        MoveToEx(mem, panel_left, kTopBar, nullptr); LineTo(mem, panel_left, ch - kBottomBar);
    }
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
    // Draw colored channel indicators next to checkboxes
    if (panel_w > 0 && g.side_panel_tab == 0) {
        for (std::size_t i = 0; i < g.checks.size(); ++i) {
            HWND c = g.checks[i];
            if (!IsWindowVisible(c)) continue;
            RECT cr;
            GetWindowRect(c, &cr);
            MapWindowPoints(nullptr, g.main, (LPPOINT)&cr, 2);
            if (static_cast<int>(i) == g.side_selected_channel && i < g.check_labels.size() && g.check_labels[i]) {
                RECT lr;
                GetWindowRect(g.check_labels[i], &lr);
                MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&lr), 2);
                RECT hi = {cr.left - 22, lr.top - 2, lr.right + 4, lr.bottom + 2};
                fill_rounded_rect(mem, hi,
                    mix_color(g_theme->bg_panel, g_theme->accent, (g_theme == &kDarkTheme) ? 20 : 12),
                    mix_color(g_theme->btn_border, g_theme->accent, (g_theme == &kDarkTheme) ? 52 : 44), 8);
            }
            int sq_y = cr.top + (cr.bottom - cr.top - 12) / 2;
            int sq_x = cr.left - 18;
            HBRUSH sq = CreateSolidBrush(channel_color(i));
            RECT sr = {sq_x, sq_y, sq_x + 12, sq_y + 12};
            FillRect(mem, &sr, sq);
            DeleteObject(sq);
            HPEN border = CreatePen(PS_SOLID, 1, g_theme->btn_border);
            HGDIOBJ old_sq_pen = SelectObject(mem, border);
            HGDIOBJ old_sq_brush = SelectObject(mem, GetStockObject(HOLLOW_BRUSH));
            Rectangle(mem, sr.left, sr.top, sr.right, sr.bottom);
            SelectObject(mem, old_sq_brush);
            SelectObject(mem, old_sq_pen);
            DeleteObject(border);
        }
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
    if (!using_persistent_backbuffer) {
        SelectObject(mem, temp_prev_bmp);
        DeleteObject(temp_bmp);
        DeleteDC(mem);
    }
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

void invalidate_formula_runtime() {
    g.formula_runtime_dirty = true;
    invalidate_transformed_channel_cache();
    invalidate_plot_analysis_cache();
}

void invalidate_formula_runtime_channel(std::size_t channel_index) {
    g.formula_runtime_dirty = true;
    if (channel_index < g.transformed_channel_cache_valid.size()) {
        g.transformed_channel_cache_valid[channel_index] = 0;
    } else {
        invalidate_transformed_channel_cache();
    }
    invalidate_plot_analysis_cache();
}

void invalidate_transformed_channel_cache() {
    const std::size_t n = g.ds.channel_count();
    g.transformed_channel_cache.resize(n);
    g.transformed_channel_cache_valid.assign(n, 0);
}

void ensure_channel_formula_storage() {
    const std::size_t n = g.ds.channel_count();
    if (g.channel_formulas.size() != n) g.channel_formulas.assign(n, default_channel_formula_text());
    if (g.channel_formula_rpn.size() != n) g.channel_formula_rpn.assign(n, {});
    if (g.channel_formula_identity.size() != n) g.channel_formula_identity.assign(n, 1);
    if (g.channel_transform_kind.size() != n) g.channel_transform_kind.assign(n, TransformRuntimeKind::Identity);
    if (g.channel_transform_mul.size() != n) g.channel_transform_mul.assign(n, 1.0);
    if (g.channel_transform_add.size() != n) g.channel_transform_add.assign(n, 0.0);
    if (g.transformed_channel_cache.size() != n || g.transformed_channel_cache_valid.size() != n) {
        invalidate_transformed_channel_cache();
    }
}

void ensure_channel_formulas_loaded() {
    if (!g.formula_ini_deferred) return;
    g.formula_ini_deferred = false;
    load_channel_formulas_from_ini();
}

void ensure_channel_formula_vectors() {
    const std::size_t n = g.ds.channel_count();
    ensure_channel_formula_storage();
    const bool vectors_ready =
        g.channel_formulas.size() == n &&
        g.channel_formula_rpn.size() == n &&
        g.channel_formula_identity.size() == n &&
        g.channel_transform_kind.size() == n &&
        g.channel_transform_mul.size() == n &&
        g.channel_transform_add.size() == n &&
        g.transformed_channel_cache.size() == n &&
        g.transformed_channel_cache_valid.size() == n;
    if (!g.formula_runtime_dirty && vectors_ready) return;

    ensure_channel_formulas_loaded();
    if (g.global_formula.empty()) g.global_formula = default_channel_formula_text();
    if (g.global_formula_rpn.empty()) {
        std::wstring error;
        compile_formula_rpn(g.global_formula, g.global_formula_rpn, error);
        if (g.global_formula_rpn.empty()) {
            g.global_formula = default_channel_formula_text();
            compile_formula_rpn(g.global_formula, g.global_formula_rpn, error);
        }
    }
    g.global_formula_identity = formula_rpn_is_identity(g.global_formula_rpn);
    const AffineFormulaInfo global_affine = analyze_formula_rpn_affine(g.global_formula_rpn);
    g.global_formula_affine = global_affine.valid;
    g.global_formula_mul = global_affine.valid ? global_affine.mul : 1.0;
    g.global_formula_add = global_affine.valid ? global_affine.add : 0.0;
    bool has_non_identity = !g.global_formula_identity;
    bool has_non_affine = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (g.channel_formulas[i].empty()) g.channel_formulas[i] = default_channel_formula_text();
        if (g.channel_formula_rpn[i].empty()) {
            std::wstring error;
            compile_formula_rpn(g.channel_formulas[i], g.channel_formula_rpn[i], error);
        }
        g.channel_formula_identity[i] = formula_rpn_is_identity(g.channel_formula_rpn[i]) ? 1 : 0;
        const AffineFormulaInfo local_affine = analyze_formula_rpn_affine(g.channel_formula_rpn[i]);
        TransformRuntimeKind kind = TransformRuntimeKind::CachedFormula;
        double mul = 1.0;
        double add = 0.0;
        if (g.global_formula_affine && local_affine.valid) {
            mul = local_affine.mul * g.global_formula_mul;
            add = local_affine.mul * g.global_formula_add + local_affine.add;
            kind = (mul == 1.0 && add == 0.0) ? TransformRuntimeKind::Identity : TransformRuntimeKind::Affine;
        } else if (g.global_formula_identity && g.channel_formula_identity[i]) {
            kind = TransformRuntimeKind::Identity;
        } else {
            has_non_affine = true;
        }
        g.channel_transform_kind[i] = kind;
        g.channel_transform_mul[i] = mul;
        g.channel_transform_add[i] = add;
        if (kind != TransformRuntimeKind::Identity) has_non_identity = true;
    }
    g.has_non_identity_formula = has_non_identity;
    g.has_non_affine_formula = has_non_affine;
    g.formula_runtime_dirty = false;
}

void ensure_transformed_channel_cache(std::size_t channel_index) {
    ensure_channel_formula_vectors();
    if (channel_index >= g.ds.channel_count()) return;
    if (channel_index >= g.channel_transform_kind.size() ||
        g.channel_transform_kind[channel_index] != TransformRuntimeKind::CachedFormula) {
        return;
    }
    if (channel_index >= g.transformed_channel_cache_valid.size()) invalidate_transformed_channel_cache();
    if (g.transformed_channel_cache_valid[channel_index]) return;

    const auto& src = g.ds.channels[channel_index];
    auto& dst = g.transformed_channel_cache[channel_index];
    dst.resize(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i] = transform_channel_value(channel_index, src[i]);
    }
    g.transformed_channel_cache_valid[channel_index] = 1;
}

double transform_channel_value(std::size_t ci, double raw) {
    if (std::isnan(raw)) return raw;
    if (!g.has_non_identity_formula) return raw;
    if (ci >= g.channel_transform_kind.size()) return raw;
    const TransformRuntimeKind kind = g.channel_transform_kind[ci];
    if (kind == TransformRuntimeKind::Identity) return raw;
    if (kind == TransformRuntimeKind::Affine) {
        return raw * g.channel_transform_mul[ci] + g.channel_transform_add[ci];
    }
    const double global_value = g.global_formula_identity
        ? raw
        : (g.global_formula_affine
            ? (raw * g.global_formula_mul + g.global_formula_add)
            : eval_formula_rpn(g.global_formula_rpn, raw));
    const double base = (std::isfinite(global_value) || std::isnan(global_value)) ? global_value : raw;
    if (ci < g.channel_formula_identity.size() && g.channel_formula_identity[ci]) return base;
    const double value = eval_formula_rpn(g.channel_formula_rpn[ci], base);
    return std::isfinite(value) || std::isnan(value) ? value : raw;
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

void reset_channel_transform(std::size_t ci) {
    ensure_channel_formula_vectors();
    if (ci < g.channel_formulas.size()) g.channel_formulas[ci] = default_channel_formula_text();
    if (ci < g.channel_formula_rpn.size()) {
        g.channel_formula_rpn[ci].clear();
        std::wstring error;
        compile_formula_rpn(g.channel_formulas[ci], g.channel_formula_rpn[ci], error);
    }
    invalidate_formula_runtime_channel(ci);
    ensure_channel_formula_vectors();
}

void reset_all_channel_transforms() {
    ensure_channel_formula_vectors();
    for (std::size_t i = 0; i < g.channel_formulas.size(); ++i) {
        reset_channel_transform(i);
    }
}

void clear_transform_sensitive_overlays(bool clear_history = true) {
    clear_measure_point_groups();
    g.markers.clear();
    g.active_marker = -1;
    g.guides.erase(
        std::remove_if(g.guides.begin(), g.guides.end(), [](const GuideLine& gl) { return !gl.vertical; }),
        g.guides.end());
    g.pending_line = 0;
    g.pending_marker = false;
    g.measure_mode = false;
    if (clear_history) {
        g_undo.clear();
        g_redo.clear();
    }
    if (g.measure) SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
}

void on_signal_transform_changed(bool preserve_history = false) {
    if (!has_data()) return;
    clear_transform_sensitive_overlays(!preserve_history);
    g.auto_y = true;
    g.auto_y_amp = true;
    if (g.autoy) SendMessageW(g.autoy, BM_SETCHECK, BST_CHECKED, 0);
    clear_spectrum_cache_state();
    if (g.freq_mode) compute_spectrum();
    sync_menu();
    set_status();
    InvalidateRect(g.main, nullptr, TRUE);
    save_runtime_settings();
}

// Export the visible segment: time-domain rows (Time mode) or spectrum (Hz).
bool save_csv(const std::wstring& path) {
    std::ofstream out(to_acp(path.c_str()), std::ios::binary);
    if (!out) return false;
    ensure_channel_formula_vectors();
    if (g.freq_mode) {
        if (!ensure_current_spectrum()) return false;
        std::vector<std::size_t> cols;
        out << g_str->csv_freq;
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
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
    ensure_channel_formula_vectors();
    if (g.freq_mode) {
        if (!ensure_current_spectrum()) return false;
        std::vector<std::size_t> cols;
        out << "Frequency";
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
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

// Snap a clicked coordinate to the nearest visible real sample (Time mode) or
// spectrum point (Hz mode) by on-screen distance, so a marker lands on the
// visually closest data point. The stored data is never modified вЂ” only the
// marker is adjusted.
bool snap_to_nearest_target(double& dx, double& dy, int* out_channel = nullptr) {
    if (!g.vvalid) return false;
    const RECT& p = g.vrect;
    const int pw = p.right - p.left;
    const int ph = p.bottom - p.top;
    if (pw <= 0 || ph <= 0) return false;

    auto to_px = [&](double x) -> double {
        return static_cast<double>(p.left) + (x - g.vx0) / (g.vx1 - g.vx0) * pw;
    };
    auto to_py = [&](double y) -> double {
        return static_cast<double>(p.bottom) - (y - g.vy0) / (g.vy1 - g.vy0) * ph;
    };

    const double target_px = to_px(dx);
    const double target_py = to_py(dy);
    double best_dist2 = std::numeric_limits<double>::max();
    double best_x = dx;
    double best_y = dy;
    int best_ci = -1;

    if (g.freq_mode) {
        if (!ensure_current_spectrum() || g.spec.freqs.empty() || g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return false;
        const auto& f = g.spec.freqs;
        std::size_t lo = static_cast<std::size_t>(std::lower_bound(f.begin(), f.end(), g.vx0) - f.begin());
        std::size_t hi = static_cast<std::size_t>(std::upper_bound(f.begin(), f.end(), g.vx1) - f.begin());
        if (lo >= hi) return false;
        for (std::size_t j = 0; j < g.spec.amp.size(); ++j) {
            const int ci = (j < g.spec_channel_indices.size()) ? g.spec_channel_indices[j] : -1;
            if (ci < 0 || !g.visible[ci]) continue;
            for (std::size_t k = lo; k < hi; ++k) {
                const double x = f[k];
                const double y = g.spec.amp[j][k];
                if (!std::isfinite(y)) continue;
                const double px = to_px(x);
                const double py = to_py(y);
                const double dxp = px - target_px;
                const double dyp = py - target_py;
                const double dist2 = dxp * dxp + dyp * dyp;
                if (dist2 < best_dist2) {
                    best_dist2 = dist2;
                    best_x = x;
                    best_y = y;
                    best_ci = ci;
                }
            }
        }
        if (best_ci >= 0) {
            dx = best_x;
            dy = best_y;
            if (out_channel) *out_channel = best_ci;
            return true;
        }
        return false;
    }
    if (!has_data()) return false;
    ensure_channel_formula_vectors();
    const auto& t = g.ds.time;
    if (t.empty() || g.vx1 <= g.vx0 || g.vy1 <= g.vy0) return false;
    std::size_t lo = static_cast<std::size_t>(std::lower_bound(t.begin(), t.end(), g.vx0) - t.begin());
    std::size_t hi = static_cast<std::size_t>(std::upper_bound(t.begin(), t.end(), g.vx1) - t.begin());
    if (lo >= hi) return false;
    for (std::size_t c = 0; c < g.ds.channel_count(); ++c) {
        if (!g.visible[c]) continue;
        const auto& col = g.ds.channels[c];
        const TransformRuntimeKind kind = (c < g.channel_transform_kind.size())
            ? g.channel_transform_kind[c]
            : TransformRuntimeKind::Identity;
        const std::vector<double>* cache = nullptr;
        double mul = 1.0;
        double add = 0.0;
        if (kind == TransformRuntimeKind::Affine) {
            mul = g.channel_transform_mul[c];
            add = g.channel_transform_add[c];
        } else if (kind == TransformRuntimeKind::CachedFormula) {
            ensure_transformed_channel_cache(c);
            cache = &g.transformed_channel_cache[c];
        }
        for (std::size_t i = lo; i < hi; ++i) {
            const double y = cache ? (*cache)[i]
                                   : (kind == TransformRuntimeKind::Affine ? (col[i] * mul + add) : col[i]);
            if (!std::isfinite(y)) continue;
            const double x = t[i];
            const double px = to_px(x);
            const double py = to_py(y);
            const double dxp = px - target_px;
            const double dyp = py - target_py;
            const double dist2 = dxp * dxp + dyp * dyp;
            if (dist2 < best_dist2) {
                best_dist2 = dist2;
                best_x = x;
                best_y = y;
                best_ci = static_cast<int>(c);
            }
        }
    }
    if (best_ci >= 0) {
        dx = best_x;
        dy = best_y;
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

int read_ini_int(const wchar_t* section, const wchar_t* key, int def_value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    return static_cast<int>(GetPrivateProfileIntW(section, key, def_value, g_config_path.c_str()));
}

double read_ini_double(const wchar_t* section, const wchar_t* key, double def_value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    wchar_t buf[64]{};
    std::wstring def = format_edit_number(def_value);
    GetPrivateProfileStringW(section, key, def.c_str(), buf, 64, g_config_path.c_str());
    double value = def_value;
    if (parse_wide_double_text(buf, value)) return value;
    return def_value;
}

void write_ini_double(const wchar_t* section, const wchar_t* key, double value) {
    if (g_config_path.empty()) g_config_path = app_config_path();
    std::wstring text = format_edit_number(value);
    WritePrivateProfileStringW(section, key, text.c_str(), g_config_path.c_str());
}

void load_channel_formulas_from_ini() {
    ensure_channel_formula_storage();
    wchar_t global_buf[512]{};
    GetPrivateProfileStringW(L"transform", L"global_formula", default_channel_formula_text().c_str(), global_buf, 512, g_config_path.c_str());
    g.global_formula = normalize_formula_text(global_buf);
    g.global_formula_rpn.clear();
    std::wstring global_error;
    if (!compile_formula_rpn(g.global_formula, g.global_formula_rpn, global_error)) {
        g.global_formula = default_channel_formula_text();
        g.global_formula_rpn.clear();
        compile_formula_rpn(g.global_formula, g.global_formula_rpn, global_error);
    }
    const int stored_count = read_ini_int(L"transform", L"formula_count", static_cast<int>(g.channel_formulas.size()));
    for (std::size_t i = 0; i < g.channel_formulas.size(); ++i) {
        wchar_t key_name[32]{};
        swprintf(key_name, 32, L"formula_%u", static_cast<unsigned int>(i));
        wchar_t buf[512]{};
        const wchar_t* def = (static_cast<int>(i) < stored_count) ? default_channel_formula_text().c_str() : default_channel_formula_text().c_str();
        GetPrivateProfileStringW(L"transform", key_name, def, buf, 512, g_config_path.c_str());
        g.channel_formulas[i] = normalize_formula_text(buf);
        g.channel_formula_rpn[i].clear();
        std::wstring error;
        if (!compile_formula_rpn(g.channel_formulas[i], g.channel_formula_rpn[i], error)) {
            g.channel_formulas[i] = default_channel_formula_text();
            g.channel_formula_rpn[i].clear();
            compile_formula_rpn(g.channel_formulas[i], g.channel_formula_rpn[i], error);
        }
    }
    invalidate_formula_runtime();
    ensure_channel_formula_vectors();
}

void load_runtime_settings() {
    if (g_config_path.empty()) g_config_path = app_config_path();

    wchar_t lang_buf[16]{};
    GetPrivateProfileStringW(L"ui", L"language", L"ru", lang_buf, 16, g_config_path.c_str());
    g_str = (lstrcmpiW(lang_buf, L"en") == 0) ? &kEn : &kRu;

    g.visual_smooth = read_ini_int(L"ui", L"smoothing", g.visual_smooth ? 1 : 0) != 0;
    g.vertical_pan = read_ini_int(L"ui", L"vertical_pan", g.vertical_pan ? 1 : 0) != 0;
    g.snap_to_data = read_ini_int(L"ui", L"snap_to_data", g.snap_to_data ? 1 : 0) != 0;
    g.light_mode = read_ini_int(L"ui", L"light_mode", g.light_mode ? 1 : 0) != 0;
    g.show_gap_markers = read_ini_int(L"ui", L"show_gap_markers", g.show_gap_markers ? 1 : 0) != 0;
    g.side_panel_visible = read_ini_int(L"ui", L"side_panel_visible", g.side_panel_visible ? 1 : 0) != 0;
    g.side_panel_tab = read_ini_int(L"ui", L"side_panel_tab", g.side_panel_tab);
    if (g.side_panel_tab != 1) g.side_panel_tab = 0;
    g.play_speed = read_ini_double(L"ui", L"play_speed", g.play_speed);
    if (!(g.play_speed > 0.0) || !std::isfinite(g.play_speed)) g.play_speed = 1.0;
    g.light_mode_open_start = read_ini_double(L"ui", L"light_mode_open_start", g.light_mode_open_start);
    g.light_mode_open_end = read_ini_double(L"ui", L"light_mode_open_end", g.light_mode_open_end);
    if (!std::isfinite(g.light_mode_open_start) || g.light_mode_open_start < 0.0) g.light_mode_open_start = 0.0;
    if (!std::isfinite(g.light_mode_open_end) || g.light_mode_open_end <= g.light_mode_open_start) {
        g.light_mode_open_end = g.light_mode_open_start + 10.0;
    }

    g.pdisp.number = read_ini_int(L"points", L"number", g.pdisp.number ? 1 : 0) != 0;
    g.pdisp.x = read_ini_int(L"points", L"x", g.pdisp.x ? 1 : 0) != 0;
    g.pdisp.y = read_ini_int(L"points", L"y", g.pdisp.y ? 1 : 0) != 0;
    g.pdisp.dx = read_ini_int(L"points", L"dx", g.pdisp.dx ? 1 : 0) != 0;
    g.pdisp.dy = read_ini_int(L"points", L"dy", g.pdisp.dy ? 1 : 0) != 0;
    g.pdisp.inv_dt = read_ini_int(L"points", L"inv_dt", g.pdisp.inv_dt ? 1 : 0) != 0;
    g.pdisp.dist = read_ini_int(L"points", L"dist", g.pdisp.dist ? 1 : 0) != 0;

    g.marker_color = static_cast<COLORREF>(read_ini_int(
        L"ui", L"marker_color", static_cast<int>(g_theme->marker_color)));

    g.hotkeys = default_hotkeys();
    for (auto& hk : g.hotkeys) {
        wchar_t key_name[32]{};
        swprintf(key_name, 32, L"cmd_%d", hk.command);
        wchar_t value[64]{};
        GetPrivateProfileStringW(L"hotkeys", key_name, L"", value, 64, g_config_path.c_str());
        if (!value[0]) continue;
        unsigned int fvirt = hk.fvirt;
        unsigned int key = hk.key;
        if (swscanf(value, L"%u,%u", &fvirt, &key) == 2) {
            hk.fvirt = static_cast<BYTE>(fvirt);
            hk.key = static_cast<WORD>(key);
        }
    }
}

void save_runtime_settings() {
    if (g_config_path.empty()) g_config_path = app_config_path();
    ensure_channel_formulas_loaded();

    WritePrivateProfileStringW(L"ui", L"language", (g_str == &kEn) ? L"en" : L"ru", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"smoothing", g.visual_smooth ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"vertical_pan", g.vertical_pan ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"snap_to_data", g.snap_to_data ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"light_mode", g.light_mode ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"show_gap_markers", g.show_gap_markers ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"side_panel_visible", g.side_panel_visible ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"ui", L"side_panel_tab", g.side_panel_tab == 1 ? L"1" : L"0", g_config_path.c_str());
    write_ini_double(L"ui", L"play_speed", g.play_speed);
    write_ini_double(L"ui", L"light_mode_open_start", g.light_mode_open_start);
    write_ini_double(L"ui", L"light_mode_open_end", g.light_mode_open_end);

    WritePrivateProfileStringW(L"points", L"number", g.pdisp.number ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"x", g.pdisp.x ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"y", g.pdisp.y ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"dx", g.pdisp.dx ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"dy", g.pdisp.dy ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"inv_dt", g.pdisp.inv_dt ? L"1" : L"0", g_config_path.c_str());
    WritePrivateProfileStringW(L"points", L"dist", g.pdisp.dist ? L"1" : L"0", g_config_path.c_str());

    wchar_t color_buf[32]{};
    swprintf(color_buf, 32, L"%u", static_cast<unsigned int>(g.marker_color));
    WritePrivateProfileStringW(L"ui", L"marker_color", color_buf, g_config_path.c_str());
    WritePrivateProfileStringW(L"transform", L"global_formula", g.global_formula.c_str(), g_config_path.c_str());
    wchar_t count_buf[32]{};
    swprintf(count_buf, 32, L"%u", static_cast<unsigned int>(g.channel_formulas.size()));
    WritePrivateProfileStringW(L"transform", L"formula_count", count_buf, g_config_path.c_str());
    for (std::size_t i = 0; i < g.channel_formulas.size(); ++i) {
        wchar_t key_name[32]{};
        swprintf(key_name, 32, L"formula_%u", static_cast<unsigned int>(i));
        WritePrivateProfileStringW(L"transform", key_name, g.channel_formulas[i].c_str(), g_config_path.c_str());
    }

    for (const auto& hk : g.hotkeys) {
        wchar_t key_name[32]{};
        wchar_t value[64]{};
        swprintf(key_name, 32, L"cmd_%d", hk.command);
        swprintf(value, 64, L"%u,%u", static_cast<unsigned int>(hk.fvirt), static_cast<unsigned int>(hk.key));
        WritePrivateProfileStringW(L"hotkeys", key_name, value, g_config_path.c_str());
    }
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

std::wstring hotkey_display_text_for_command(int command) {
    std::wstring text = hotkey_text_for_command(command);
    if (!text.empty()) return text;
    return (g_str == &kEn) ? L"Not assigned" : L"Не назначено";
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
        case IDM_ADD_VLINE_EXACT: return en ? L"Vertical line (exact)" : L"Вертикальная линия (точно)";
        case IDM_ADD_HLINE_EXACT: return en ? L"Horizontal line (exact)" : L"Горизонтальная линия (точно)";
        case IDC_AUTOY: return en ? L"Auto zoom" : L"Автомасштабирование";
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
    return command_name(command) + L"  [" + hotkey_display_text_for_command(command) + L"]";
}

std::wstring welcome_intro_text() {
    return (g_str == &kEn)
        ? L"A focused desktop viewer for LabVIEW logs: open .lvm or .txt files, inspect signals in Time mode, and switch to Hz / FFT without losing context."
        : L"Нативный просмотрщик логов LabVIEW: открывайте .lvm и .txt, изучайте сигналы во времени и быстро переходите к Hz / FFT по выбранному участку.";
}

std::wstring welcome_version_text() {
    return (g_str == &kEn)
        ? (std::wstring(L"Build: ") + APP_VERSION_W)
        : (std::wstring(L"Версия сборки: ") + APP_VERSION_W);
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

const wchar_t* welcome_author_credit_text() {
    return (g_str == &kEn)
        ? L"Application developed by Alexander Muleev  |  al.muleev@gmail.com"
        : L"Приложение разработал Мулеев Александр  |  al.muleev@gmail.com";
}

const wchar_t* welcome_actions_title_text() {
    return L"";
}

struct WelcomeLayout {
    RECT bounds{};
    RECT hero{};
    RECT action{};
    bool stacked = false;
    bool compact = false;
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
    layout.stacked = (content_w < 760);

    if (layout.stacked) {
        int hero_h = std::clamp(content_h * 48 / 100, 220, 340);
        const int min_action_h = 320;
        hero_h = min(hero_h, max(180, content_h - gap - min_action_h));
        layout.hero = { x0, y0, x0 + content_w, y0 + hero_h };
        layout.action = { x0, layout.hero.bottom + gap, x0 + content_w, y0 + content_h };
    } else {
        const int action_w = std::clamp(content_w / 3, 320, 380);
        layout.hero = { x0, y0, x0 + content_w - action_w - gap, y0 + content_h };
        layout.action = { layout.hero.right + gap, y0, x0 + content_w, y0 + content_h };
    }

    layout.compact = rect_height(layout.action) < 560 || rect_width(layout.action) < 340;

    return layout;
}

void layout_welcome_controls(HWND hwnd) {
    WelcomeLayout layout = compute_welcome_layout(hwnd);
    auto place = [&](int id, int x, int y, int w, int h) {
        HWND ctl = GetDlgItem(hwnd, id);
        if (ctl) MoveWindow(ctl, x, y, max(1, w), max(1, h), TRUE);
    };

    const int hero_pad = layout.compact ? 20 : (layout.stacked ? 22 : 30);
    const int hx = layout.hero.left + hero_pad;
    int hy = layout.hero.top + hero_pad;
    const int hw = max(180, rect_width(layout.hero) - hero_pad * 2);
    const int title_h = layout.compact ? 40 : (layout.stacked ? 46 : 52);
    const int subtitle_h = layout.compact ? 22 : 24;
    const int version_h = layout.compact ? 18 : 20;
    const int intro_h = layout.compact ? 54 : (layout.stacked ? 64 : 76);

    place(IDW_TITLE, hx, hy, hw, title_h);
    hy += title_h + (layout.compact ? 6 : 8);
    place(IDW_SUBTITLE, hx, hy, hw, subtitle_h);
    hy += subtitle_h + (layout.compact ? 4 : 6);
    place(IDW_VERSION, hx, hy, hw, version_h);
    hy += version_h + (layout.compact ? 10 : 12);
    place(IDW_INTRO, hx, hy, hw, intro_h);
    hy += intro_h + (layout.compact ? 12 : 18);

    const int footer_reserve = layout.compact ? 18 : (layout.stacked ? 28 : 20);
    const int feature_bottom = layout.hero.bottom - hero_pad - footer_reserve;
    place(IDW_FEATURES, hx, hy, hw, max(72, feature_bottom - hy));

    const int action_pad = layout.compact ? 16 : (layout.stacked ? 20 : 24);
    const int ax = layout.action.left + action_pad;
    int ay = layout.action.top + action_pad;
    const int aw = max(180, rect_width(layout.action) - action_pad * 2);
    const int segment_gap = 10;
    const int segment_button_h = layout.compact ? 28 : 32;
    const int action_button_h = layout.compact ? 34 : 40;
    const int button_gap = layout.compact ? 8 : (layout.stacked ? 10 : 12);
    place(IDW_LANG_LABEL, ax, ay, aw, 20);
    ay += layout.compact ? 22 : 24;
    const int lang_w = max(80, (aw - segment_gap) / 2);
    place(IDM_LANG_RU, ax, ay, lang_w, segment_button_h);
    place(IDM_LANG_EN, ax + aw - lang_w, ay, lang_w, segment_button_h);
    ay += segment_button_h + (layout.compact ? 8 : 12);
    place(IDW_THEME_LABEL, ax, ay, aw, 20);
    ay += layout.compact ? 22 : 24;
    const int theme_w = max(80, (aw - segment_gap) / 2);
    place(IDW_THEME_LIGHT, ax, ay, theme_w, segment_button_h);
    place(IDW_THEME_DARK, ax + aw - theme_w, ay, theme_w, segment_button_h);
    ay += segment_button_h + (layout.compact ? 12 : 16);
    place(IDW_LIGHT_MODE, ax, ay, aw, 28);
    ay += layout.compact ? 38 : 42;
    place(IDW_ACTIONS_TITLE, ax, ay, aw, layout.compact ? 20 : 24);
    ay += layout.compact ? 28 : 34;
    place(IDC_PTSETTINGS, ax, ay, aw, action_button_h);
    ay += action_button_h + button_gap;
    place(IDM_HOTKEYS, ax, ay, aw, action_button_h);
    ay += action_button_h + button_gap;
    place(IDW_START, ax, ay, aw, action_button_h);
}

void append_hotkey_line(std::wstring& out, int command) {
    out += L"  " + hotkey_display_text_for_command(command) + L"\t— " + command_name(command) + L"\n";
}

std::wstring hotkeys_body_text() {
    ensure_hotkeys_initialized();
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

const wchar_t* hotkeys_dialog_close_text() {
    return (g_str == &kEn) ? L"Close" : L"Закрыть";
}

std::vector<std::wstring> hotkeys_dialog_lines() {
    std::vector<std::wstring> lines;
    std::wstring body = hotkeys_body_text();
    std::size_t start = 0;
    while (start <= body.size()) {
        std::size_t end = body.find(L'\n', start);
        std::wstring line = body.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return lines;
}

SIZE hotkeys_dialog_client_size() {
    const auto lines = hotkeys_dialog_lines();
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND measure_wnd = g.main ? g.main : GetDesktopWindow();
    HDC dc = GetDC(measure_wnd);
    HFONT old_font = dc ? reinterpret_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    TEXTMETRICW tm{};
    if (dc) GetTextMetricsW(dc, &tm);

    int max_line_width = 0;
    for (const std::wstring& line : lines) {
        SIZE text_size{};
        const wchar_t* text = line.empty() ? L" " : line.c_str();
        int length = line.empty() ? 1 : static_cast<int>(line.size());
        if (dc && GetTextExtentPoint32W(dc, text, length, &text_size)) {
            max_line_width = max(max_line_width, static_cast<int>(text_size.cx));
        }
    }

    if (dc) {
        if (old_font) SelectObject(dc, old_font);
        ReleaseDC(measure_wnd, dc);
    }

    const int title_h = max(20, static_cast<int>(tm.tmHeight + tm.tmExternalLeading));
    const int line_h = max(18, static_cast<int>(tm.tmHeight + tm.tmExternalLeading + 2));
    const int button_h = 30;
    const int client_w = max(620, max_line_width + 56);
    const int client_h = 16 + title_h + 8 + static_cast<int>(lines.size()) * line_h + 8 + 12 + button_h + 18;
    return { client_w, max(client_h, 300) };
}

void populate_hotkeys_dialog_list(HWND list) {
    if (!list) return;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    const auto lines = hotkeys_dialog_lines();
    for (const std::wstring& line : lines) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
}

LRESULT CALLBACK HotkeysDialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int pad = 18;
            const int title_y = 16;
            const int title_h = 20;
            const int list_y = 44;
            const int button_h = 30;
            const int button_w = 122;
            const int button_y = rc.bottom - pad - button_h;
            const int list_w = max(200, static_cast<int>(rc.right - pad * 2));
            const int list_h = max(120, button_y - 12 - list_y);
            HWND label = CreateWindowExW(
                0, L"STATIC",
                (g_str == &kEn) ? L"Configured keyboard shortcuts" : L"Настроенные горячие клавиши",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                pad, title_y, list_w, title_h, hwnd, nullptr,
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            g_hotkeys_dialog.list = CreateWindowExW(
                0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT,
                pad, list_y, list_w, list_h, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HOTKEYS_DIALOG_LIST)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            HWND close = CreateWindowExW(
                0, L"BUTTON", hotkeys_dialog_close_text(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                rc.right - pad - button_w, button_y, button_w, button_h, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HOTKEYS_DIALOG_CLOSE)),
                reinterpret_cast<LPCREATESTRUCT>(lp)->hInstance, nullptr);
            if (label) SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (g_hotkeys_dialog.list) SendMessageW(g_hotkeys_dialog.list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (close) SendMessageW(close, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            populate_hotkeys_dialog_list(g_hotkeys_dialog.list);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_HOTKEYS_DIALOG_CLOSE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            RECT rc{};
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
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!dis || !dis->hwndItem) break;
            if (GetDlgCtrlID(dis->hwndItem) == IDC_HOTKEYS_DIALOG_CLOSE) {
                wchar_t txt[64]{};
                GetWindowTextW(dis->hwndItem, txt, 64);
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                draw_welcome_action_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            g_hotkeys_dialog.done = true;
            g_hotkeys_dialog.wnd = nullptr;
            g_hotkeys_dialog.list = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::wstring toolbar_hover_text(HWND btn) {
    const bool en = (g_str == &kEn);
    if (btn == g.open) return g_str->hover_open;
    if (btn == g.play) return g.playing ? g_str->hover_pause : g_str->hover_play;
    if (btn == g.measure) return g_str->hover_measure;
    if (btn == g.reset) return g_str->hover_reset;
    if (btn == g.autoy) return g_str->hover_autoy;
    if (btn == g.ptsettings) return en ? L"Open general settings" : L"Открыть общие настройки";
    if (btn == g.sidepanel_btn) return en ? L"Show or hide the right-side work panel" : L"Показать или скрыть рабочую панель справа";
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

std::wstring time_gap_details_text(double duration, long long estimated_missing_samples) {
    wchar_t buf[192]{};
    if (estimated_missing_samples <= 0) {
        if (g_str == &kEn) swprintf(buf, 192, L"Gap duration: %.6g s", duration);
        else swprintf(buf, 192, L"Длительность разрыва: %.6g c", duration);
    } else {
        if (g_str == &kEn) swprintf(buf, 192, L"Gap duration: %.6g s\nApprox. missing samples: ~%lld", duration, estimated_missing_samples);
        else swprintf(buf, 192, L"Длительность разрыва: %.6g c\nПропущено примерно: ~%lld отсч.", duration, estimated_missing_samples);
    }
    return buf;
}

const wchar_t* time_gap_details_title() {
    return (g_str == &kEn) ? L"Gap details" : L"Информация о разрыве";
}

int hit_test_gap_marker(int x, int y) {
    POINT pt{ x, y };
    for (int i = static_cast<int>(g.visible_gap_markers.size()) - 1; i >= 0; --i) {
        if (PtInRect(&g.visible_gap_markers[static_cast<std::size_t>(i)].rect, pt)) return i;
    }
    return -1;
}

void show_gap_details_dialog(HWND owner, int gap_index) {
    if (gap_index < 0 || gap_index >= static_cast<int>(g.visible_gap_markers.size())) return;
    const auto& gap = g.visible_gap_markers[static_cast<std::size_t>(gap_index)];
    const std::wstring text = time_gap_details_text(gap.duration, gap.estimated_missing_samples);
    MessageBoxW(owner, text.c_str(), time_gap_details_title(), MB_OK | MB_ICONINFORMATION);
}

void append_menu_popup_owner_draw(HMENU bar, HMENU popup, const std::wstring& text) {
    AppendMenuW(
        bar,
        MF_OWNERDRAW | MF_POPUP,
        reinterpret_cast<UINT_PTR>(popup),
        reinterpret_cast<LPCWSTR>(stash_menu_entry(text, true, true)));
}

void append_menu_item_owner_draw(HMENU menu, UINT id, const std::wstring& text) {
    AppendMenuW(
        menu,
        MF_OWNERDRAW | MF_STRING,
        id,
        reinterpret_cast<LPCWSTR>(stash_menu_entry(text, false, false)));
}

void modify_menu_item_owner_draw(HMENU menu, UINT id, const std::wstring& text) {
    ModifyMenuW(
        menu,
        id,
        MF_BYCOMMAND | MF_OWNERDRAW,
        id,
        reinterpret_cast<LPCWSTR>(stash_menu_entry(text, false, false)));
}

std::wstring menu_item_left_text(const std::wstring& text) {
    std::size_t tab = text.find(L'\t');
    return (tab == std::wstring::npos) ? text : text.substr(0, tab);
}

std::wstring menu_item_right_text(const std::wstring& text) {
    std::size_t tab = text.find(L'\t');
    return (tab == std::wstring::npos) ? L"" : text.substr(tab + 1);
}

void measure_owner_draw_menu(MEASUREITEMSTRUCT* mis) {
    if (!mis || mis->CtlType != ODT_MENU) return;
    const OwnerDrawMenuEntry* entry = reinterpret_cast<const OwnerDrawMenuEntry*>(mis->itemData);
    const std::wstring text = entry ? entry->text : L"";
    const std::wstring left = menu_item_left_text(text);
    const std::wstring right = menu_item_right_text(text);
    HDC dc = GetDC(g.main ? g.main : nullptr);
    HFONT font = (entry && entry->top_level && g.menu_font)
        ? g.menu_font
        : (g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    HGDIOBJ old_font = SelectObject(dc, font);
    SIZE left_sz{};
    SIZE right_sz{};
    GetTextExtentPoint32W(dc, left.c_str(), static_cast<int>(left.size()), &left_sz);
    GetTextExtentPoint32W(dc, right.c_str(), static_cast<int>(right.size()), &right_sz);
    SelectObject(dc, old_font);
    ReleaseDC(g.main ? g.main : nullptr, dc);
    if (entry && entry->top_level) {
        mis->itemWidth = left_sz.cx + 24;
        mis->itemHeight = max(34u, static_cast<UINT>(left_sz.cy + 18));
        return;
    }

    const UINT check_col = 28;
    const UINT left_pad = 10;
    const UINT right_pad = 12;
    const UINT gap = right.empty() ? 0 : 24;
    const UINT arrow_space = (entry && entry->popup) ? 18 : 0;
    mis->itemWidth = check_col + left_pad + left_sz.cx + gap + right_sz.cx + arrow_space + right_pad;
    mis->itemHeight = max(24u, static_cast<UINT>(max(left_sz.cy, right_sz.cy) + 10));
}

void draw_owner_draw_menu(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_MENU) return;
    auto blend = [](COLORREF a, COLORREF b, int weight_b) {
        weight_b = std::clamp(weight_b, 0, 255);
        const int weight_a = 255 - weight_b;
        return RGB(
            (GetRValue(a) * weight_a + GetRValue(b) * weight_b) / 255,
            (GetGValue(a) * weight_a + GetGValue(b) * weight_b) / 255,
            (GetBValue(a) * weight_a + GetBValue(b) * weight_b) / 255);
    };
    const OwnerDrawMenuEntry* entry = reinterpret_cast<const OwnerDrawMenuEntry*>(dis->itemData);
    const std::wstring text = entry ? entry->text : L"";
    const std::wstring left = menu_item_left_text(text);
    const std::wstring right = menu_item_right_text(text);
    RECT r = dis->rcItem;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool hot = selected || (dis->itemState & ODS_HOTLIGHT) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool checked = (dis->itemState & ODS_CHECKED) != 0;
    COLORREF bg = hot ? g_theme->btn_hover : g_theme->bg_toolbar;
    COLORREF text_col = disabled ? g_theme->text_secondary : g_theme->text_primary;
    HBRUSH bg_brush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &r, bg_brush);
    DeleteObject(bg_brush);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text_col);
    HFONT font = (entry && entry->top_level && g.menu_font)
        ? g.menu_font
        : (g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    HGDIOBJ old_font = SelectObject(dis->hDC, font);
    if (entry && entry->top_level) {
        RECT text_rect = r;
        text_rect.bottom -= 1;
        if (hot && !disabled) {
            RECT hi = r;
            hi.left += 1;
            hi.right -= 1;
            hi.top += 1;
            hi.bottom -= 1;
            const int accent_weight = (g_theme == &kDarkTheme) ? 42 : 58;
            const COLORREF hi_fill = blend(g_theme->bg_toolbar, g_theme->accent, accent_weight);
            const COLORREF hi_border = blend(g_theme->btn_border, g_theme->accent, (g_theme == &kDarkTheme) ? 88 : 120);
            fill_rounded_rect(dis->hDC, hi, hi_fill, hi_border, 4);
            text_col = (g_theme == &kDarkTheme) ? RGB(255, 255, 255) : g_theme->accent_hover;
            SetTextColor(dis->hDC, text_col);

            RECT underline = { hi.left + 5, hi.bottom - 1, hi.right - 5, hi.bottom };
            HBRUSH underline_brush = CreateSolidBrush(g_theme->accent);
            FillRect(dis->hDC, &underline, underline_brush);
            DeleteObject(underline_brush);
        }
        DrawTextW(
            dis->hDC, left.c_str(), -1, &text_rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dis->hDC, old_font);
        return;
    }

    RECT check_rect = r;
    check_rect.right = check_rect.left + 28;
    RECT left_rect = r;
    left_rect.left = check_rect.right + 10;
    left_rect.right = r.right - 12;
    if (!right.empty()) left_rect.right -= 80;
    if (entry && entry->popup) left_rect.right -= 18;
    RECT right_rect = r;
    right_rect.left = max(left_rect.right + 8, r.right - 92);
    right_rect.right = r.right - ((entry && entry->popup) ? 24 : 12);
    DrawTextW(
        dis->hDC, left.c_str(), -1, &left_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (!right.empty()) {
        DrawTextW(
            dis->hDC, right.c_str(), -1, &right_rect,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (checked) {
        RECT box = check_rect;
        box.left += 7;
        box.right = box.left + 14;
        box.top = r.top + max(0L, ((r.bottom - r.top) - 14) / 2);
        box.bottom = box.top + 14;
        HBRUSH accent_brush = CreateSolidBrush(g_theme->btn_active);
        FillRect(dis->hDC, &box, accent_brush);
        DeleteObject(accent_brush);

        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ old_pen = SelectObject(dis->hDC, pen);
        MoveToEx(dis->hDC, box.left + 3, box.top + 7, nullptr);
        LineTo(dis->hDC, box.left + 6, box.top + 10);
        LineTo(dis->hDC, box.right - 3, box.top + 4);
        SelectObject(dis->hDC, old_pen);
        DeleteObject(pen);
    }

    if (entry && entry->popup) {
        POINT pts[3] = {
            { r.right - 12, r.top + (r.bottom - r.top) / 2 - 4 },
            { r.right - 12, r.top + (r.bottom - r.top) / 2 + 4 },
            { r.right - 7,  r.top + (r.bottom - r.top) / 2 }
        };
        HBRUSH arrow_brush = CreateSolidBrush(text_col);
        HPEN arrow_pen = CreatePen(PS_SOLID, 1, text_col);
        HGDIOBJ old_brush = SelectObject(dis->hDC, arrow_brush);
        HGDIOBJ old_pen = SelectObject(dis->hDC, arrow_pen);
        Polygon(dis->hDC, pts, 3);
        SelectObject(dis->hDC, old_pen);
        SelectObject(dis->hDC, old_brush);
        DeleteObject(arrow_pen);
        DeleteObject(arrow_brush);
    }
    SelectObject(dis->hDC, old_font);
}

void measure_settings_combo_item(MEASUREITEMSTRUCT* mis) {
    if (!mis || mis->CtlType != ODT_COMBOBOX) return;
    mis->itemHeight = 22;
}

void draw_settings_combo_item(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_COMBOBOX) return;
    HWND combo = dis->hwndItem;
    RECT r = dis->rcItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    const bool combo_edit = (dis->itemState & ODS_COMBOBOXEDIT) != 0;
    COLORREF bg = combo_edit ? g_theme->bg_plot : (selected ? g_theme->btn_hover : g_theme->bg_plot);
    COLORREF text_col = disabled ? g_theme->text_secondary : g_theme->text_primary;

    HBRUSH bg_brush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &r, bg_brush);
    DeleteObject(bg_brush);

    if (combo_edit) {
        HPEN border = CreatePen(PS_SOLID, 1, g_theme->btn_border);
        HGDIOBJ old_pen = SelectObject(dis->hDC, border);
        HGDIOBJ old_brush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, r.left, r.top, r.right, r.bottom);
        SelectObject(dis->hDC, old_brush);
        SelectObject(dis->hDC, old_pen);
        DeleteObject(border);
    }

    UINT item = dis->itemID;
    if (item == static_cast<UINT>(-1) && combo) {
        LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) item = static_cast<UINT>(sel);
    }

    std::wstring text;
    if (combo && item != static_cast<UINT>(-1)) {
        LRESULT len = SendMessageW(combo, CB_GETLBTEXTLEN, item, 0);
        if (len >= 0) {
            text.resize(static_cast<std::size_t>(len));
            SendMessageW(combo, CB_GETLBTEXT, item, reinterpret_cast<LPARAM>(text.data()));
        }
    }

    RECT text_rect = r;
    text_rect.left += 8;
    if (combo_edit) text_rect.right -= 8;
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text_col);
    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ old_font = SelectObject(dis->hDC, font);
    DrawTextW(dis->hDC, text.c_str(), -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dis->hDC, old_font);
}

void draw_settings_group_box(HDC dc, const RECT& r, const wchar_t* text) {
    RECT text_rect = r;
    text_rect.left += 10;
    text_rect.right -= 10;
    text_rect.top += 1;

    HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_theme->text_primary);

    SIZE sz{};
    const int text_len = lstrlenW(text);
    GetTextExtentPoint32W(dc, text, text_len, &sz);

    const int text_x = r.left + 12;
    const int text_y = r.top + 1;
    const int gap_left = text_x - 6;
    const int gap_right = text_x + sz.cx + 6;
    const int line_y = r.top + 10;

    HPEN pen = CreatePen(PS_SOLID, 1, g_theme->btn_border);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, r.left, line_y, nullptr); LineTo(dc, static_cast<int>(max<LONG>(r.left, gap_left)), line_y);
    MoveToEx(dc, static_cast<int>(min<LONG>(r.right, gap_right)), line_y, nullptr); LineTo(dc, r.right, line_y);
    MoveToEx(dc, r.left, line_y, nullptr); LineTo(dc, r.left, r.bottom);
    MoveToEx(dc, r.right - 1, line_y, nullptr); LineTo(dc, r.right - 1, r.bottom);
    MoveToEx(dc, r.left, r.bottom - 1, nullptr); LineTo(dc, r.right, r.bottom - 1);
    SelectObject(dc, old_pen);
    DeleteObject(pen);

    SetBkColor(dc, g_theme->bg_panel);
    ExtTextOutW(dc, text_x, text_y, ETO_OPAQUE, nullptr, text, text_len, nullptr);
    SelectObject(dc, old_font);
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
            set_toggle_checked(g.checks[i], visible);
        }
    }
    invalidate_plot_analysis_cache();
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
    HGDIOBJ old_font = SelectObject(dc, f);
    RECT text_rect = { r.left + 6, r.top, r.right - 6, r.bottom };
    if (pressed) OffsetRect(&text_rect, 0, 1);
    DrawTextW(dc, txt, -1, &text_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old_font);
}

bool is_channel_checkbox_id(int id) {
    return id >= IDC_CHAN_BASE && id < IDC_CHAN_BASE + static_cast<int>(g.visible.size());
}

bool is_side_toggle_id(int id) {
    return id == IDC_SIDE_PT_NUM ||
           id == IDC_SIDE_PT_X ||
           id == IDC_SIDE_PT_Y ||
           id == IDC_SIDE_PT_DX ||
           id == IDC_SIDE_PT_DY ||
           id == IDC_SIDE_PT_INVDT ||
           id == IDC_SIDE_PT_DIST ||
           id == IDC_SIDE_PT_SNAP ||
           id == IDC_SIDE_POINT_GROUP_VISIBLE;
}

bool is_settings_toggle_button_id(int id) {
    return id == IDC_SET_LANG_RU ||
           id == IDC_SET_LANG_EN ||
           id == IDC_SET_HOTKEY_CTRL ||
           id == IDC_SET_HOTKEY_SHIFT ||
           id == IDC_SET_HOTKEY_ALT;
}

bool is_settings_checkbox_id(int id) {
    return id == IDC_SET_LIGHT_MODE ||
           id == IDC_SET_GAP_MARKERS;
}

bool is_welcome_checkbox_id(int id) {
    return id == IDW_LIGHT_MODE;
}

bool uses_manual_toggle_state(HWND hwnd) {
    if (!hwnd) return false;
    const int id = GetDlgCtrlID(hwnd);
    return is_channel_checkbox_id(id) ||
           is_side_toggle_id(id) ||
           id == IDC_SET_LIGHT_MODE ||
           id == IDC_SET_GAP_MARKERS ||
           is_welcome_checkbox_id(id) ||
           id == IDC_SET_HOTKEY_CTRL ||
           id == IDC_SET_HOTKEY_SHIFT ||
           id == IDC_SET_HOTKEY_ALT;
}

bool is_toggle_checked(HWND hwnd) {
    if (!hwnd) return false;
    if (uses_manual_toggle_state(hwnd)) {
        return GetPropW(hwnd, L"LvmToggleChecked") != nullptr;
    }
    return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void set_toggle_checked(HWND hwnd, bool checked) {
    if (!hwnd) return;
    if (uses_manual_toggle_state(hwnd)) {
        if (checked) SetPropW(hwnd, L"LvmToggleChecked", reinterpret_cast<HANDLE>(1));
        else RemovePropW(hwnd, L"LvmToggleChecked");
    }
    SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void toggle_checked_state(HWND hwnd) {
    set_toggle_checked(hwnd, !is_toggle_checked(hwnd));
}

void draw_themed_check_control(HDC dc, const RECT& r, const wchar_t* txt,
                               bool checked, bool pressed, bool enabled,
                               bool radio, bool compact,
                               COLORREF surface_bg = CLR_INVALID) {
    const COLORREF base_bg = (surface_bg == CLR_INVALID) ? g_theme->bg_panel : surface_bg;
    HBRUSH bg = CreateSolidBrush(base_bg);
    FillRect(dc, &r, bg);
    DeleteObject(bg);

    const int box_size = compact ? 14 : 16;
    const int box_left = compact ? (r.left + ((r.right - r.left - box_size) / 2)) : (r.left + 4);
    const int box_top = r.top + ((r.bottom - r.top - box_size) / 2);
    RECT box = { box_left, box_top, box_left + box_size, box_top + box_size };

    COLORREF fill = checked ? (pressed ? g_theme->accent_hover : g_theme->accent)
                            : (pressed ? g_theme->btn_hover : g_theme->bg_plot);
    COLORREF border = checked ? g_theme->accent : g_theme->btn_border;
    COLORREF text = enabled ? g_theme->text_primary : g_theme->text_secondary;
    if (!enabled) {
        fill = mix_color(fill, base_bg, 96);
        border = mix_color(border, base_bg, 96);
    }

    if (radio) {
        HBRUSH outer = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ old_brush = SelectObject(dc, outer);
        HGDIOBJ old_pen = SelectObject(dc, pen);
        Ellipse(dc, box.left, box.top, box.right, box.bottom);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(pen);
        DeleteObject(outer);
        if (checked) {
            RECT dot = { box.left + 4, box.top + 4, box.right - 4, box.bottom - 4 };
            HBRUSH dot_brush = CreateSolidBrush(RGB(255, 255, 255));
            HGDIOBJ old_dot = SelectObject(dc, dot_brush);
            HPEN dot_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HGDIOBJ old_dot_pen = SelectObject(dc, dot_pen);
            Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
            SelectObject(dc, old_dot_pen);
            SelectObject(dc, old_dot);
            DeleteObject(dot_pen);
            DeleteObject(dot_brush);
        }
    } else {
        fill_rounded_rect(dc, box, fill, border, 4);
        if (checked) {
            HPEN check_pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HGDIOBJ old_pen = SelectObject(dc, check_pen);
            MoveToEx(dc, box.left + 3, box.top + 8, nullptr);
            LineTo(dc, box.left + 6, box.bottom - 4);
            LineTo(dc, box.right - 3, box.top + 4);
            SelectObject(dc, old_pen);
            DeleteObject(check_pen);
        }
    }

    if (!compact && txt && txt[0]) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, text);
        HFONT font = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ old_font = SelectObject(dc, font);
        RECT text_rect = { box.right + 10, r.top, r.right - 4, r.bottom };
        if (pressed) OffsetRect(&text_rect, 0, 1);
        DrawTextW(dc, txt, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old_font);
    }
}

void draw_welcome_action_button(HDC dc, const RECT& r, const wchar_t* txt,
                                bool pressed, bool primary, bool outlined) {
    COLORREF bg_col = g_theme->btn_bg;
    COLORREF border_col = g_theme->btn_border;
    COLORREF text_col = g_theme->text_primary;

    if (primary) {
        bg_col = pressed ? g_theme->accent_hover : g_theme->accent;
        border_col = pressed ? g_theme->accent_hover : g_theme->accent;
        text_col = RGB(255, 255, 255);
    } else if (outlined) {
        if (g_theme == &kDarkTheme) {
            bg_col = pressed ? mix_color(g_theme->btn_hover, g_theme->accent, 26)
                             : mix_color(g_theme->btn_bg, g_theme->accent, 18);
            border_col = mix_color(g_theme->btn_border, g_theme->accent, 72);
            text_col = RGB(235, 240, 248);
        } else {
            bg_col = pressed ? g_theme->btn_hover : g_theme->bg_panel;
            border_col = g_theme->accent;
            text_col = g_theme->text_primary;
        }
    } else {
        if (g_theme == &kDarkTheme) {
            bg_col = pressed ? g_theme->btn_hover : mix_color(g_theme->btn_bg, g_theme->bg_panel, 70);
            border_col = mix_color(g_theme->btn_border, g_theme->separator, 48);
            text_col = g_theme->text_primary;
        } else {
            bg_col = pressed ? g_theme->btn_hover : g_theme->btn_bg;
            border_col = g_theme->btn_border;
            text_col = g_theme->text_primary;
        }
    }

    fill_rounded_rect(dc, r, bg_col, border_col, 6);

    if (g_theme != &kDarkTheme) {
        RECT inner = { r.left + 1, r.top + 1, r.right - 1, r.top + 10 };
        HBRUSH gloss = CreateSolidBrush(mix_color(bg_col, RGB(255, 255, 255), pressed ? 8 : 20));
        FillRect(dc, &inner, gloss);
        DeleteObject(gloss);
    } else if (!primary) {
        RECT inner = { r.left + 1, r.top + 1, r.right - 1, min(r.bottom - 1, r.top + 8) };
        HBRUSH sheen = CreateSolidBrush(mix_color(bg_col, RGB(255, 255, 255), pressed ? 4 : 10));
        FillRect(dc, &inner, sheen);
        DeleteObject(sheen);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text_col);
    HFONT f = g.ui_font ? g.ui_font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ old_font = SelectObject(dc, f);
    RECT text_rect = r;
    if (pressed) OffsetRect(&text_rect, 0, 1);
    DrawTextW(dc, txt, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

void redraw_button(HWND btn) {
    if (!btn || !IsWindow(btn)) return;
    RedrawWindow(btn, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

void redraw_window_with_children(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void redraw_toolbar_buttons() {
    for (HWND btn : g.buttons) redraw_button(btn);
}

void refresh_theme_windows() {
    auto redraw = [](HWND wnd) {
        if (!wnd || !IsWindow(wnd)) return;
        RedrawWindow(
            wnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    };
    redraw(g.main);
    redraw(g.settings_wnd);
    redraw(g.welcome_wnd);
    redraw(g.channel_edit);
    for (HWND h : g.checks) redraw(h);
    for (HWND h : g.check_labels) redraw(h);
}

void apply_theme_choice(const Theme* theme) {
    if (!theme || g_theme == theme) return;
    g_theme = theme;
    save_app_settings();
    update_theme_brushes();
    if (g.menu) {
        MENUINFO mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
        mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
        SetMenuInfo(g.menu, &mi);
    }
    sync_menu();
    if (g.settings_wnd) refresh_settings_controls();
    refresh_theme_windows();
}

void draw_themed_button(HDC dc, const RECT& r, const wchar_t* txt, bool pressed, bool active, bool hover) {
    COLORREF bg_col, border_col, text_col;
    if (active && pressed) {
        bg_col = g_theme->accent_hover;
        border_col = g_theme->accent_hover;
        text_col = (g_theme == &kDarkTheme) ? RGB(255, 255, 255) : RGB(12, 42, 78);
    } else if (active) {
        bg_col = mix_color(g_theme->btn_bg, g_theme->accent, hover ? 64 : 42);
        border_col = g_theme->accent;
        text_col = (g_theme == &kDarkTheme) ? RGB(255, 255, 255) : RGB(12, 42, 78);
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
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HotkeysDialogProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"LvmHotkeysDialog";
        atom = RegisterClassExW(&wc);
    }

    if (g_hotkeys_dialog.wnd && IsWindow(g_hotkeys_dialog.wnd)) {
        ShowWindow(g_hotkeys_dialog.wnd, SW_RESTORE);
        SetForegroundWindow(g_hotkeys_dialog.wnd);
        if (g_hotkeys_dialog.list) SetFocus(g_hotkeys_dialog.list);
        return;
    }

    RECT work_area{};
    if (HMONITOR monitor = MonitorFromWindow(g.main ? g.main : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST)) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(monitor, &mi)) work_area = mi.rcWork;
    }
    if (work_area.right <= work_area.left || work_area.bottom <= work_area.top) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    }

    const SIZE desired_client_size = hotkeys_dialog_client_size();
    const int max_client_w = max(420, static_cast<int>((work_area.right - work_area.left) - 24));
    const int max_client_h = max(300, static_cast<int>((work_area.bottom - work_area.top) - 24));
    RECT dlg_rc{
        0, 0,
        min(static_cast<int>(desired_client_size.cx), max_client_w),
        min(static_cast<int>(desired_client_size.cy), max_client_h)
    };
    AdjustWindowRectEx(&dlg_rc, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW);

    g_hotkeys_dialog.done = false;
    g_hotkeys_dialog.wnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        L"LvmHotkeysDialog",
        g_str->dlg_hotkeys_title,
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, dlg_rc.right - dlg_rc.left, dlg_rc.bottom - dlg_rc.top,
        g.main, nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(g.main, GWLP_HINSTANCE)),
        nullptr);
    if (!g_hotkeys_dialog.wnd) return;

    RECT mr{}, wr{};
    GetWindowRect(g.main, &mr);
    GetWindowRect(g_hotkeys_dialog.wnd, &wr);
    const int window_w = wr.right - wr.left;
    const int window_h = wr.bottom - wr.top;
    int x = mr.left + ((mr.right - mr.left) - window_w) / 2;
    int y = mr.top + ((mr.bottom - mr.top) - window_h) / 2;
    x = std::clamp(x,
        static_cast<int>(work_area.left),
        max(static_cast<int>(work_area.left), static_cast<int>(work_area.right - window_w)));
    y = std::clamp(y,
        static_cast<int>(work_area.top),
        max(static_cast<int>(work_area.top), static_cast<int>(work_area.bottom - window_h)));
    SetWindowPos(
        g_hotkeys_dialog.wnd, HWND_TOP,
        x, y,
        0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    ShowWindow(g_hotkeys_dialog.wnd, SW_SHOWNORMAL);
    UpdateWindow(g_hotkeys_dialog.wnd);
    if (g_hotkeys_dialog.list) SetFocus(g_hotkeys_dialog.list);
}

void show_about() {
    show_welcome(GetModuleHandleW(nullptr));
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
    modify_menu_item_owner_draw(g.menu, IDM_THEME, theme_label);
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
        append_menu_item_owner_draw(file, IDC_OPEN, open_text);
        append_menu_item_owner_draw(file, IDC_SAVEPNG, save_png_text);
        append_menu_item_owner_draw(file, IDC_SAVECSV, save_csv_text);
        append_menu_item_owner_draw(file, IDC_SAVETXT, savetxt_menu);
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(file, IDM_UNDO, undo_text);
        append_menu_item_owner_draw(file, IDM_REDO, redo_text);
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(file, IDM_EXIT, en ? L"Exit\tAlt+F4" : L"Выход\tAlt+F4");
        append_menu_popup_owner_draw(bar, file, en ? L"File" : L"Файл");

        HMENU view = CreatePopupMenu();
        std::wstring mode_time_text = menu_text(en ? L"Time" : L"Время", IDM_MODE_TIME);
        std::wstring mode_freq_text = menu_text(en ? L"Hz (FFT)" : L"Гц (FFT)", IDM_MODE_FREQ);
        std::wstring zoom_in_text = menu_text(en ? L"Zoom in" : L"Увеличить", IDC_ZOOMIN);
        std::wstring zoom_out_text = menu_text(en ? L"Zoom out" : L"Уменьшить", IDC_ZOOMOUT);
        std::wstring reset_text = menu_text(en ? L"Reset view" : L"Сбросить вид", IDC_RESET);
        std::wstring start_text = menu_text(en ? L"Go to start" : L"В начало", IDC_GOTO_START);
        std::wstring end_text = menu_text(en ? L"Go to end" : L"В конец", IDC_GOTO_END);
        std::wstring autoy_text = menu_text(en ? L"Auto zoom" : L"Автомасштабирование", IDC_AUTOY);
        std::wstring smooth_text = menu_text(en ? L"Smoothing" : L"Сглаживание", IDM_VISMOOTH);
        std::wstring vpan_text = menu_text(en ? L"Vertical pan" : L"Вертикальное панорамирование", IDM_VPAN);
        std::wstring play_text = menu_text(L"Play / Pause", IDC_PLAY);
        std::wstring theme_text = menu_text(en ? L"Dark theme" : L"Тёмная тема", IDM_THEME);
        append_menu_item_owner_draw(view, IDM_MODE_TIME, mode_time_text);
        append_menu_item_owner_draw(view, IDM_MODE_FREQ, mode_freq_text);
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(view, IDC_ZOOMIN, zoom_in_text);
        append_menu_item_owner_draw(view, IDC_ZOOMOUT, zoom_out_text);
        append_menu_item_owner_draw(view, IDC_RESET, reset_text);
        append_menu_item_owner_draw(view, IDC_GOTO_START, start_text);
        append_menu_item_owner_draw(view, IDC_GOTO_END, end_text);
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(view, IDC_AUTOY, autoy_text);
        append_menu_item_owner_draw(view, IDM_VISMOOTH, smooth_text);
        append_menu_item_owner_draw(view, IDM_VPAN, vpan_text);
        append_menu_item_owner_draw(view, IDC_PLAY, play_text);
        append_menu_item_owner_draw(view, IDM_THEME, theme_text);
        append_menu_item_owner_draw(view, IDM_SPEED_CUSTOM, speed_menu_text());
        append_menu_popup_owner_draw(bar, view, en ? L"View" : L"Вид");

        HMENU tools = CreatePopupMenu();
        std::wstring measure_text = menu_text(en ? L"Points" : L"Точки", IDC_MEASURE);
        std::wstring marker_text = menu_text(en ? L"Marker" : L"Маркер", IDM_ADD_MARKER);
        std::wstring vline_text = menu_text(en ? L"Vertical line" : L"Вертикальная линия", IDM_ADD_VLINE);
        std::wstring vline_exact_text = menu_text(en ? L"Vertical line (exact)..." : L"Вертикальная линия (точно)...", IDM_ADD_VLINE_EXACT);
        std::wstring hline_text = menu_text(en ? L"Horizontal line" : L"Горизонтальная линия", IDM_ADD_HLINE);
        std::wstring hline_exact_text = menu_text(en ? L"Horizontal line (exact)..." : L"Горизонтальная линия (точно)...", IDM_ADD_HLINE_EXACT);
        append_menu_item_owner_draw(tools, IDC_MEASURE, measure_text);
        append_menu_item_owner_draw(tools, IDM_ADD_MARKER, marker_text);
        append_menu_item_owner_draw(tools, IDM_ADD_VLINE, vline_text);
        append_menu_item_owner_draw(tools, IDM_ADD_VLINE_EXACT, vline_exact_text);
        append_menu_item_owner_draw(tools, IDM_ADD_HLINE, hline_text);
        append_menu_item_owner_draw(tools, IDM_ADD_HLINE_EXACT, hline_exact_text);
        AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(tools, IDM_CLEAR_POINTS, en ? L"Clear points" : L"Очистить точки");
        append_menu_item_owner_draw(tools, IDM_CLEAR_MARKERS, en ? L"Clear markers" : L"Очистить маркеры");
        append_menu_item_owner_draw(tools, IDM_CLEAR_LINES, en ? L"Clear lines" : L"Очистить линии");
        append_menu_popup_owner_draw(bar, tools, en ? L"Tools" : L"Инструменты");

        HMENU settings = CreatePopupMenu();
        append_menu_item_owner_draw(settings, IDC_PTSETTINGS, en ? L"General settings…" : L"Общие настройки…");
        append_menu_popup_owner_draw(bar, settings, en ? L"Settings" : L"Настройки");

        HMENU help = CreatePopupMenu();
        std::wstring hotkeys_text = menu_text(en ? L"Keyboard shortcuts…" : L"Горячие клавиши…", IDM_HOTKEYS);
        append_menu_item_owner_draw(help, IDM_HOTKEYS, hotkeys_text);
        append_menu_item_owner_draw(help, IDM_ABOUT, en ? L"About…" : L"О программе…");
        append_menu_popup_owner_draw(bar, help, en ? L"Help" : L"Справка");
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
        append_menu_item_owner_draw(file, IDC_OPEN, open_text);
        append_menu_item_owner_draw(file, IDC_SAVEPNG, save_png_text);
        append_menu_item_owner_draw(file, IDC_SAVECSV, save_csv_text);
        append_menu_item_owner_draw(file, IDC_SAVETXT, savetxt_menu);
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(file, IDM_UNDO, undo_text);
        append_menu_item_owner_draw(file, IDM_REDO, redo_text);
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(file, IDM_EXIT, en ? L"Exit\tAlt+F4" : L"Выход\tAlt+F4");
        append_menu_popup_owner_draw(bar, file, menu_file);

        HMENU view = CreatePopupMenu();
        std::wstring mode_time_text = menu_text(en ? L"Time" : L"Время", IDM_MODE_TIME);
        std::wstring mode_freq_text = menu_text(en ? L"Hz (FFT)" : L"Гц (FFT)", IDM_MODE_FREQ);
        std::wstring zoom_in_text = menu_text(en ? L"Zoom in" : L"Увеличить", IDC_ZOOMIN);
        std::wstring zoom_out_text = menu_text(en ? L"Zoom out" : L"Уменьшить", IDC_ZOOMOUT);
        std::wstring reset_text = menu_text(en ? L"Reset view" : L"Сбросить вид", IDC_RESET);
        std::wstring start_text = menu_text(en ? L"Go to start" : L"В начало", IDC_GOTO_START);
        std::wstring end_text = menu_text(en ? L"Go to end" : L"В конец", IDC_GOTO_END);
        std::wstring autoy_text = menu_text(en ? L"Auto zoom" : L"Автомасштабирование", IDC_AUTOY);
        std::wstring smooth_text = menu_text(en ? L"Smoothing" : L"Сглаживание", IDM_VISMOOTH);
        std::wstring vpan_text = menu_text(en ? L"Vertical pan" : L"Вертикальное панорамирование", IDM_VPAN);
        std::wstring play_text = menu_text(L"Play / Pause", IDC_PLAY);
        std::wstring theme_text = menu_text(en ? L"Dark theme" : L"Тёмная тема", IDM_THEME);
        append_menu_item_owner_draw(view, IDM_MODE_TIME, mode_time_text);
        append_menu_item_owner_draw(view, IDM_MODE_FREQ, mode_freq_text);
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(view, IDC_ZOOMIN, zoom_in_text);
        append_menu_item_owner_draw(view, IDC_ZOOMOUT, zoom_out_text);
        append_menu_item_owner_draw(view, IDC_RESET, reset_text);
        append_menu_item_owner_draw(view, IDC_GOTO_START, start_text);
        append_menu_item_owner_draw(view, IDC_GOTO_END, end_text);
        AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(view, IDC_AUTOY, autoy_text);
        append_menu_item_owner_draw(view, IDM_VISMOOTH, smooth_text);
        append_menu_item_owner_draw(view, IDM_VPAN, vpan_text);
        append_menu_item_owner_draw(view, IDC_PLAY, play_text);
        append_menu_item_owner_draw(view, IDM_THEME, theme_text);
        append_menu_item_owner_draw(view, IDM_SPEED_CUSTOM, speed_menu_text());
        append_menu_popup_owner_draw(bar, view, menu_view);

        HMENU meas = CreatePopupMenu();
        std::wstring measure_text = menu_text(en ? L"Points" : L"Точки", IDC_MEASURE);
        append_menu_item_owner_draw(meas, IDC_MEASURE, measure_text);
        append_menu_item_owner_draw(meas, IDC_PTSETTINGS, en ? L"Settings…" : L"Настройки…");
        AppendMenuW(meas, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(meas, IDM_CLEAR_POINTS, en ? L"Clear\tDelete" : L"Очистить\tDelete");
        append_menu_popup_owner_draw(bar, meas, menu_points);

        HMENU lines = CreatePopupMenu();
        std::wstring vline_text = menu_text(en ? L"Vertical (click)" : L"Вертикальная (клик)", IDM_ADD_VLINE);
        std::wstring vline_exact_text = menu_text(en ? L"Vertical (exact)..." : L"Вертикальная (точно)...", IDM_ADD_VLINE_EXACT);
        std::wstring hline_text = menu_text(en ? L"Horizontal (click)" : L"Горизонтальная (клик)", IDM_ADD_HLINE);
        std::wstring hline_exact_text = menu_text(en ? L"Horizontal (exact)..." : L"Горизонтальная (точно)...", IDM_ADD_HLINE_EXACT);
        append_menu_item_owner_draw(lines, IDM_ADD_VLINE, vline_text);
        append_menu_item_owner_draw(lines, IDM_ADD_VLINE_EXACT, vline_exact_text);
        append_menu_item_owner_draw(lines, IDM_ADD_HLINE, hline_text);
        append_menu_item_owner_draw(lines, IDM_ADD_HLINE_EXACT, hline_exact_text);
        AppendMenuW(lines, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(lines, IDM_CLEAR_LINES, en ? L"Clear" : L"Очистить");
        append_menu_popup_owner_draw(bar, lines, menu_lines);

        HMENU markers = CreatePopupMenu();
        std::wstring marker_text = menu_text(en ? L"Add" : L"Добавить", IDM_ADD_MARKER);
        append_menu_item_owner_draw(markers, IDM_ADD_MARKER, marker_text);
        AppendMenuW(markers, MF_SEPARATOR, 0, nullptr);
        append_menu_item_owner_draw(markers, IDM_CLEAR_MARKERS, en ? L"Clear" : L"Очистить");
        append_menu_popup_owner_draw(bar, markers, menu_markers);

        HMENU help = CreatePopupMenu();
        std::wstring hotkeys_text = menu_text(en ? L"Keyboard shortcuts…" : L"Горячие клавиши…", IDM_HOTKEYS);
        append_menu_item_owner_draw(help, IDM_HOTKEYS, hotkeys_text);
        append_menu_item_owner_draw(help, IDM_ABOUT, en ? L"About…" : L"О программе…");
        append_menu_popup_owner_draw(bar, help, menu_help);
        return bar;
    }

    HMENU file = CreatePopupMenu();
    append_menu_item_owner_draw(file, IDC_OPEN, L"Открыть файл…\tCtrl+O");
    append_menu_item_owner_draw(file, IDC_SAVEPNG, L"Сохранить PNG…\tCtrl+S");
    append_menu_item_owner_draw(file, IDC_SAVECSV, L"Сохранить CSV…\tCtrl+E");
    append_menu_item_owner_draw(file, IDC_SAVETXT, savetxt_menu);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(file, IDM_UNDO, L"Отменить\tCtrl+Z");
    append_menu_item_owner_draw(file, IDM_REDO, L"Повторить\tCtrl+Shift+Z");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(file, IDM_EXIT, L"Выход\tAlt+F4");
    append_menu_popup_owner_draw(bar, file, L"Файл");

    HMENU view = CreatePopupMenu();
    append_menu_item_owner_draw(view, IDC_MODE, L"Время / Гц\tM");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(view, IDC_ZOOMIN, L"Увеличить\t+");
    append_menu_item_owner_draw(view, IDC_ZOOMOUT, L"Уменьшить\t−");
    append_menu_item_owner_draw(view, IDC_RESET, L"Сбросить вид\tHome");
    append_menu_item_owner_draw(view, IDC_GOTO_START, L"В начало\tCtrl+Home");
    append_menu_item_owner_draw(view, IDC_GOTO_END, L"В конец\tCtrl+End");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(view, IDC_AUTOY, (g_str == &kEn) ? L"Auto zoom" : L"Автомасштабирование");
    append_menu_item_owner_draw(view, IDM_VISMOOTH, L"Сглаживание\tC");
    append_menu_item_owner_draw(view, IDM_VPAN, L"Вертикальное панорамирование\tP");
    append_menu_item_owner_draw(view, IDC_PLAY, L"Play / Pause\tПробел");
    append_menu_item_owner_draw(view, IDM_THEME, L"Тёмная тема\tT");
    append_menu_item_owner_draw(view, IDM_SPEED_CUSTOM, speed_menu_text());
    append_menu_popup_owner_draw(bar, view, L"Вид");

    HMENU meas = CreatePopupMenu();
    append_menu_item_owner_draw(meas, IDC_MEASURE, L"Точки\tV");
    append_menu_item_owner_draw(meas, IDC_PTSETTINGS, L"Настройки…");
    AppendMenuW(meas, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(meas, IDM_CLEAR_POINTS, L"Очистить\tDelete");
    append_menu_popup_owner_draw(bar, meas, L"Точки");

    HMENU lines = CreatePopupMenu();
    append_menu_item_owner_draw(lines, IDM_ADD_VLINE, L"Вертикальная\tL");
    append_menu_item_owner_draw(lines, IDM_ADD_VLINE_EXACT, L"Вертикальная (точно)...");
    append_menu_item_owner_draw(lines, IDM_ADD_HLINE, L"Горизонтальная\tH");
    append_menu_item_owner_draw(lines, IDM_ADD_HLINE_EXACT, L"Горизонтальная (точно)...");
    AppendMenuW(lines, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(lines, IDM_CLEAR_LINES, L"Очистить");
    append_menu_popup_owner_draw(bar, lines, L"Линии");

    HMENU markers = CreatePopupMenu();
    append_menu_item_owner_draw(markers, IDM_ADD_MARKER, L"Добавить\tK");
    AppendMenuW(markers, MF_SEPARATOR, 0, nullptr);
    append_menu_item_owner_draw(markers, IDM_CLEAR_MARKERS, L"Очистить");
    append_menu_popup_owner_draw(bar, markers, L"Маркеры");

    HMENU help = CreatePopupMenu();
    append_menu_item_owner_draw(help, IDM_HOTKEYS, L"Горячие клавиши…\tF1");
    append_menu_item_owner_draw(help, IDM_ABOUT, L"О программе…");
    append_menu_popup_owner_draw(bar, help, L"Справка");

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
    set_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_CTRL), (fvirt & FCONTROL) != 0);
    set_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_SHIFT), (fvirt & FSHIFT) != 0);
    set_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_ALT), (fvirt & FALT) != 0);
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

bool read_formula_edit(HWND edit, std::wstring& formula, std::vector<FormulaToken>& compiled, std::wstring& error) {
    if (!edit) {
        error = (g_str == &kEn) ? L"Coefficient field is unavailable." : L"Поле коэффициента недоступно.";
        return false;
    }
    wchar_t buf[512]{};
    GetWindowTextW(edit, buf, 512);
    formula = normalize_formula_text(buf);
    if (formula.empty()) formula = default_channel_formula_text();
    return compile_formula_rpn(formula, compiled, error);
}

void assign_formula_to_channel(std::size_t channel_index, const std::wstring& formula, const std::vector<FormulaToken>& compiled) {
    ensure_channel_formula_vectors();
    if (channel_index >= g.channel_formulas.size() || channel_index >= g.channel_formula_rpn.size()) return;
    g.channel_formulas[channel_index] = formula;
    g.channel_formula_rpn[channel_index] = compiled;
    invalidate_formula_runtime_channel(channel_index);
    ensure_channel_formula_vectors();
}

void assign_global_formula(const std::wstring& formula, const std::vector<FormulaToken>& compiled) {
    g.global_formula = formula;
    g.global_formula_rpn = compiled;
    invalidate_formula_runtime();
    ensure_channel_formula_vectors();
}

int settings_selected_point_group(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SET_POINT_GROUP_LIST);
    if (!list) return -1;
    int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return -1;
    return static_cast<int>(SendMessageW(list, LB_GETITEMDATA, sel, 0));
}

void load_selected_point_group_controls(HWND hwnd) {
    const int index = settings_selected_point_group(hwnd);
    const bool valid = index >= 0 && index < static_cast<int>(g.point_groups.size());
    HWND visible = GetDlgItem(hwnd, IDC_SET_POINT_GROUP_VISIBLE);
    HWND recolor = GetDlgItem(hwnd, IDC_SET_POINT_GROUP_COLOR);
    if (visible) {
        set_toggle_checked(
            visible,
            valid && g.point_groups[static_cast<std::size_t>(index)].visible);
        EnableWindow(visible, valid);
    }
    if (recolor) EnableWindow(recolor, valid);
    HWND current_color = GetDlgItem(hwnd, IDC_SET_POINT_COLOR_CURRENT);
    if (current_color) SetWindowTextW(current_color, point_current_color_button_text());
    if (recolor) SetWindowTextW(recolor, point_selected_group_color_button_text());
    HWND create_new = GetDlgItem(hwnd, IDC_SET_POINT_GROUP_NEW);
    if (create_new) SetWindowTextW(create_new, point_group_new_button_text());
}

void populate_point_group_list(HWND hwnd) {
    HWND list = GetDlgItem(hwnd, IDC_SET_POINT_GROUP_LIST);
    if (!list) return;
    const int previous = settings_selected_point_group(hwnd);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    normalize_active_point_group();
    int selected_index = LB_ERR;
    for (std::size_t i = 0; i < g.point_groups.size(); ++i) {
        std::wstring label = point_group_list_label(i, g.point_groups[i]);
        int idx = static_cast<int>(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(list, LB_SETITEMDATA, idx, static_cast<LPARAM>(i));
        if (static_cast<int>(i) == previous || static_cast<int>(i) == g.active_point_group) selected_index = idx;
    }
    if (selected_index != LB_ERR) SendMessageW(list, LB_SETCURSEL, selected_index, 0);
    load_selected_point_group_controls(hwnd);
    refresh_side_panel_controls();
}

void refresh_settings_controls() {
    if (!g.settings_wnd) return;
    CheckRadioButton(g.settings_wnd, IDC_SET_LANG_RU, IDC_SET_LANG_EN, g_str == &kEn ? IDC_SET_LANG_EN : IDC_SET_LANG_RU);
    if (HWND light = GetDlgItem(g.settings_wnd, IDC_SET_LIGHT_MODE)) SetWindowTextW(light, g_str->light_mode);
    set_toggle_checked(GetDlgItem(g.settings_wnd, IDC_SET_LIGHT_MODE), g.light_mode);
    set_toggle_checked(GetDlgItem(g.settings_wnd, IDC_SET_GAP_MARKERS), g.show_gap_markers);
    if (HWND gap = GetDlgItem(g.settings_wnd, IDC_SET_GAP_MARKERS)) SetWindowTextW(gap, gap_markers_toggle_text());
    populate_hotkey_list(g.settings_wnd);
    load_selected_hotkey_controls(g.settings_wnd);
}

void rebuild_menu_bar() {
    if (!g.main) return;
    SetMenu(g.main, nullptr);
    if (g.menu) DestroyMenu(g.menu);
    g_menu_text_storage.clear();
    g.menu = make_menu();
    SetMenu(g.main, welcome_visible() ? nullptr : g.menu);
    MENUINFO mi{};
    mi.cbSize = sizeof(mi);
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
            auto is_settings_group = [](int id) {
                return id == IDC_SET_GROUP_GENERAL ||
                       id == IDC_SET_GROUP_HOTKEYS;
            };
            auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
                HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
                    id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr, inst, nullptr);
                if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                if (c && is_settings_group(id)) EnableWindow(c, FALSE);
                return c;
            };
            auto mkcheck = [&](const wchar_t* text, int x, int y, int w, int h, int id) {
                return mk(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, h, id);
            };
            const bool en = (g_str == &kEn);
            mk(L"BUTTON", en ? L"General" : L"Общие", BS_OWNERDRAW, 12, 10, 510, 138, IDC_SET_GROUP_GENERAL);
            mk(L"BUTTON", g_str->lang_ru, BS_OWNERDRAW, 28, 36, 110, 22, IDC_SET_LANG_RU);
            mk(L"BUTTON", g_str->lang_en, BS_OWNERDRAW, 144, 36, 110, 22, IDC_SET_LANG_EN);
            mkcheck(g_str->light_mode, 28, 70, 278, 28, IDC_SET_LIGHT_MODE);
            mkcheck(gap_markers_toggle_text(), 28, 102, 278, 28, IDC_SET_GAP_MARKERS);

            mk(L"BUTTON", en ? L"Hotkeys" : L"Горячие клавиши", BS_OWNERDRAW, 12, 158, 510, 188, IDC_SET_GROUP_HOTKEYS);
            mk(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 24, 182, 240, 146, IDC_SET_HOTKEY_LIST);
            mk(L"BUTTON", L"Ctrl", BS_OWNERDRAW, 284, 190, 70, 22, IDC_SET_HOTKEY_CTRL);
            mk(L"BUTTON", L"Shift", BS_OWNERDRAW, 356, 190, 70, 22, IDC_SET_HOTKEY_SHIFT);
            mk(L"BUTTON", L"Alt", BS_OWNERDRAW, 428, 190, 70, 22, IDC_SET_HOTKEY_ALT);
            mk(L"STATIC", en ? L"Key:" : L"Клавиша:", SS_LEFT, 284, 222, 80, 20, 0);
            HWND combo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_BORDER, 284, 242, 214, 260, IDC_SET_HOTKEY_KEY);
            populate_hotkey_key_combo(combo);
            mk(L"BUTTON", en ? L"Apply" : L"Применить", BS_OWNERDRAW, 284, 280, 100, 28, IDC_SET_HOTKEY_APPLY);
            mk(L"BUTTON", en ? L"Reset" : L"Сбросить", BS_OWNERDRAW, 398, 280, 100, 28, IDC_SET_HOTKEY_RESET);
            mk(L"BUTTON", en ? L"Clear" : L"Очистить", BS_OWNERDRAW, 284, 314, 100, 28, IDC_SET_HOTKEY_CLEAR);
            populate_hotkey_list(hwnd);
            load_selected_hotkey_controls(hwnd);
            CheckRadioButton(hwnd, IDC_SET_LANG_RU, IDC_SET_LANG_EN, g_str == &kEn ? IDC_SET_LANG_EN : IDC_SET_LANG_RU);
            set_toggle_checked(GetDlgItem(hwnd, IDC_SET_LIGHT_MODE), g.light_mode);
            set_toggle_checked(GetDlgItem(hwnd, IDC_SET_GAP_MARKERS), g.show_gap_markers);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (!ctl && id) ctl = GetDlgItem(hwnd, id);
            auto checked = [&]() { return is_toggle_checked(ctl); };
            switch (id) {
                case IDC_SET_LANG_RU:
                    if (HIWORD(wp) == BN_CLICKED && g_str != &kRu) { g_str = &kRu; save_runtime_settings(); rebuild_ui(); }
                    break;
                case IDC_SET_LANG_EN:
                    if (HIWORD(wp) == BN_CLICKED && g_str != &kEn) { g_str = &kEn; save_runtime_settings(); rebuild_ui(); }
                    break;
                case IDC_SET_LIGHT_MODE:
                    if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                        toggle_checked_state(ctl);
                        g.light_mode = checked();
                        invalidate_plot_analysis_cache();
                        save_runtime_settings();
                        refresh_settings_controls();
                    }
                    return 0;
                case IDC_SET_GAP_MARKERS:
                    if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                        toggle_checked_state(ctl);
                        g.show_gap_markers = checked();
                        invalidate_plot_analysis_cache();
                        save_runtime_settings();
                        InvalidateRect(g.main, nullptr, FALSE);
                        refresh_settings_controls();
                    }
                    return 0;
                case IDC_SET_HOTKEY_CTRL:
                case IDC_SET_HOTKEY_SHIFT:
                case IDC_SET_HOTKEY_ALT:
                    if (HIWORD(wp) == BN_CLICKED) toggle_checked_state(ctl);
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
                        if (is_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_CTRL))) fvirt |= FCONTROL;
                        if (is_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_SHIFT))) fvirt |= FSHIFT;
                        if (is_toggle_checked(GetDlgItem(hwnd, IDC_SET_HOTKEY_ALT))) fvirt |= FALT;
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
                    save_runtime_settings();
                    set_status();
                    InvalidateRect(g.main, nullptr, TRUE);
                    return 0;
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
            if (dis && dis->CtlType == ODT_COMBOBOX && dis->CtlID == IDC_SET_HOTKEY_KEY) {
                draw_settings_combo_item(dis);
                return TRUE;
            }
            if (!dis->hwndItem) break;
            const int ctl_id = GetDlgCtrlID(dis->hwndItem);
            if (ctl_id == IDC_SET_GROUP_GENERAL ||
                ctl_id == IDC_SET_GROUP_HOTKEYS) {
                wchar_t txt[128]{};
                GetWindowTextW(dis->hwndItem, txt, 128);
                draw_settings_group_box(dis->hDC, dis->rcItem, txt);
                return TRUE;
            }
            wchar_t txt[128];
            GetWindowTextW(dis->hwndItem, txt, 128);
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            if (is_settings_toggle_button_id(ctl_id)) {
                bool active = false;
                if (ctl_id == IDC_SET_LANG_RU) active = g_str == &kRu;
                else if (ctl_id == IDC_SET_LANG_EN) active = g_str == &kEn;
                else active = is_toggle_checked(dis->hwndItem);
                draw_themed_button(dis->hDC, dis->rcItem, txt, pressed, active, false);
                return TRUE;
            }
            if (is_settings_checkbox_id(ctl_id)) {
                const bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
                draw_themed_check_control(dis->hDC, dis->rcItem, txt,
                    is_toggle_checked(dis->hwndItem), pressed, enabled, false, false);
                return TRUE;
            }
            draw_themed_button(dis->hDC, dis->rcItem, txt, pressed, false, false);
            return TRUE;
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis && mis->CtlType == ODT_COMBOBOX && mis->CtlID == IDC_SET_HOTKEY_KEY) {
                measure_settings_combo_item(mis);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
        }
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_plot);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_input_brush ? g_input_brush : g_panel_brush);
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
            WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 540, 392,
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
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_NOPREFIX,
                    0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(use_font ? use_font : font), TRUE);
                return ctl;
            };
            auto mkbtn = [&](const wchar_t* text, int id) {
                HWND ctl = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                    0, 0, 10, 10, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return ctl;
            };

            mkstatic(g_str->welcome_title, IDW_TITLE, g.title_font ? g.title_font : font);
            mkstatic(g_str->welcome_subtitle, IDW_SUBTITLE, g.bold_font ? g.bold_font : font);
            mkstatic(welcome_version_text().c_str(), IDW_VERSION, g.axis_font ? g.axis_font : font);
            mkstatic(welcome_intro_text().c_str(), IDW_INTRO, font);
            mkstatic(welcome_features_text().c_str(), IDW_FEATURES, font);
            mkstatic(g_str->m_lang, IDW_LANG_LABEL, g.bold_font ? g.bold_font : font);
            mkbtn(g_str->lang_ru, IDM_LANG_RU);
            mkbtn(g_str->lang_en, IDM_LANG_EN);
            mkstatic((g_str == &kEn) ? L"Theme" : L"Тема", IDW_THEME_LABEL, g.bold_font ? g.bold_font : font);
            mkbtn(g_str->theme_light, IDW_THEME_LIGHT);
            mkbtn(g_str->theme_dark, IDW_THEME_DARK);
            HWND light_mode = CreateWindowExW(
                0, L"BUTTON", g_str->light_mode,
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 10, 10, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDW_LIGHT_MODE)), inst, nullptr);
            SendMessageW(light_mode, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            set_toggle_checked(light_mode, g.light_mode);
            mkstatic(welcome_actions_title_text(), IDW_ACTIONS_TITLE, g.bold_font ? g.bold_font : font);
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
                max(rc.left + 220, layout.bounds.right - 420),
                max(layout.bounds.bottom + 2, rc.bottom - 24),
                rc.right - 24,
                rc.bottom - 8
            };
            DrawTextW(
                hdc,
                welcome_author_credit_text(),
                -1,
                &credit,
                DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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
                case IDW_LIGHT_MODE:
                    if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == BN_DOUBLECLICKED) {
                        HWND light = GetDlgItem(hwnd, IDW_LIGHT_MODE);
                        toggle_checked_state(light);
                        g.light_mode = is_toggle_checked(light);
                        invalidate_plot_analysis_cache();
                    }
                    save_runtime_settings();
                    if (g.settings_wnd) refresh_settings_controls();
                    return 0;
                case IDW_THEME_LIGHT: apply_theme_choice(&kLightTheme); return 0;
                case IDW_THEME_DARK: apply_theme_choice(&kDarkTheme); return 0;
                case IDM_LANG_RU: if (g_str != &kRu) { g_str = &kRu; save_runtime_settings(); rebuild_ui(); } return 0;
                case IDM_LANG_EN: if (g_str != &kEn) { g_str = &kEn; save_runtime_settings(); rebuild_ui(); } return 0;
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
            bool is_theme_button = (ctl_id == IDW_THEME_LIGHT || ctl_id == IDW_THEME_DARK);
            bool active = (is_lang_button &&
                ((ctl_id == IDM_LANG_RU && g_str == &kRu) || (ctl_id == IDM_LANG_EN && g_str == &kEn))) ||
                (is_theme_button &&
                ((ctl_id == IDW_THEME_LIGHT && g_theme == &kLightTheme) || (ctl_id == IDW_THEME_DARK && g_theme == &kDarkTheme)));
            wchar_t txt[128]{};
            GetWindowTextW(dis->hwndItem, txt, 128);

            if (ctl_id == IDW_START) {
                draw_welcome_action_button(dc, r, txt, pressed, true, false);
                return TRUE;
            }
            if (ctl_id == IDC_OPEN) {
                draw_welcome_action_button(dc, r, txt, pressed, false, true);
                return TRUE;
            }
            if (ctl_id == IDC_PTSETTINGS || ctl_id == IDM_HOTKEYS) {
                draw_welcome_action_button(dc, r, txt, pressed, false, false);
                return TRUE;
            }
            if (ctl_id == IDW_LIGHT_MODE) {
                draw_themed_check_control(
                    dc, r, txt,
                    is_toggle_checked(dis->hwndItem), pressed,
                    IsWindowEnabled(dis->hwndItem) != FALSE,
                    false, false, g_theme->btn_bg);
                return TRUE;
            }
            draw_themed_button(dc, r, txt, pressed, active, false);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            int ctl_id = GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if (ctl_id == IDW_SUBTITLE || ctl_id == IDW_VERSION) {
                SetTextColor(dc, g_theme->text_secondary);
            } else if (ctl_id == IDW_LANG_LABEL || ctl_id == IDW_THEME_LABEL || ctl_id == IDW_ACTIONS_TITLE) {
                SetTextColor(dc, g_theme->accent);
            } else {
                SetTextColor(dc, g_theme->text_primary);
            }
            if (ctl_id == IDW_TITLE || ctl_id == IDW_SUBTITLE || ctl_id == IDW_VERSION || ctl_id == IDW_INTRO || ctl_id == IDW_FEATURES) {
                return reinterpret_cast<LRESULT>(g_welcome_hero_brush ? g_welcome_hero_brush : g_welcome_brush);
            }
            return reinterpret_cast<LRESULT>(g_welcome_action_brush ? g_welcome_action_brush : g_welcome_brush);
        }
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_welcome_action_brush ? g_welcome_action_brush : g_welcome_brush);
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
    SetWindowTextW(g.sidepanel_btn, side_panel_button_text());
    SetWindowTextW(g.show_all_btn, channel_show_all_text());
    SetWindowTextW(g.hide_all_btn, channel_hide_all_text());
    refresh_side_panel_controls();
    
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
            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
                g.menu_font = CreateFontIndirectW(&ncm.lfMenuFont);
            }
            if (!g.menu_font) {
                g.menu_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            }
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
            MENUINFO mi{};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
            mi.hbrBack = CreateSolidBrush(g_theme->bg_toolbar);
            SetMenuInfo(g.menu, &mi);

            auto mk = [&](const wchar_t* text, int id, DWORD extra, bool in_toolbar = true) {
                HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_OWNERDRAW | extra,
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
            g.sidepanel_btn = mk(side_panel_button_text(), IDC_SIDEPANEL, 0);
            g.show_all_btn = mk(channel_show_all_text(), IDC_SHOW_ALL, 0);
            g.hide_all_btn = mk(channel_hide_all_text(), IDC_HIDE_ALL, 0);

            auto mk_panel_btn = [&](const wchar_t* text, int id, std::vector<HWND>& bucket) {
                HWND b = mk(text, id, 0);
                bucket.push_back(b);
                return b;
            };
            auto mk_panel_ctl = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, std::vector<HWND>& bucket) {
                HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                                         0, 0, 10, 10, hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                if (c) {
                    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                    bucket.push_back(c);
                }
                return c;
            };

            g.side_tab_channels = mk(side_tab_channels_text(), IDC_SIDE_TAB_CHANNELS, 0);
            g.side_tab_points = mk(side_tab_points_text(), IDC_SIDE_TAB_POINTS, 0);
            g.side_scrollbar = CreateWindowExW(
                0, L"SCROLLBAR", nullptr,
                WS_CHILD | WS_CLIPSIBLINGS | SBS_VERT,
                0, 0, 10, 10, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHAN_BASE - 1)), inst, nullptr);
            g.side_channel_hint = mk_panel_ctl(L"STATIC", side_channel_hint_text(),
                                               SS_LEFT | SS_NOPREFIX, IDC_SIDE_CHANNEL_HINT, g.side_channel_controls);
            g.side_global_formula_label = mk_panel_ctl(L"STATIC", side_global_formula_label_text(), SS_LEFT, 0, g.side_channel_controls);
            g.side_global_formula_edit = mk_panel_ctl(L"EDIT", default_channel_formula_text().c_str(),
                                                      WS_BORDER | ES_AUTOHSCROLL, IDC_SIDE_GLOBAL_FORMULA_EDIT, g.side_channel_controls);
            g.side_global_formula_apply = mk_panel_btn(side_global_formula_apply_text(), IDC_SIDE_GLOBAL_FORMULA_APPLY, g.side_channel_controls);
            g.side_channel_formula_label = mk_panel_ctl(L"STATIC", side_channel_formula_label_text(), SS_LEFT, 0, g.side_channel_controls);
            g.side_formula_edit = mk_panel_ctl(L"EDIT", default_channel_formula_text().c_str(),
                                               WS_BORDER | ES_AUTOHSCROLL, IDC_SIDE_FORMULA_EDIT, g.side_channel_controls);
            g.side_channel_color = mk_panel_btn(side_channel_color_button_text(), IDC_SIDE_CHANNEL_COLOR, g.side_channel_controls);
            g.side_formula_apply_selected = mk_panel_btn(side_formula_apply_selected_text(), IDC_SIDE_FORMULA_APPLY_SELECTED, g.side_channel_controls);
            g.side_formula_apply_visible = mk_panel_btn(side_formula_apply_visible_text(), IDC_SIDE_FORMULA_APPLY_VISIBLE, g.side_channel_controls);
            g.side_formula_reset_selected = mk_panel_btn(side_formula_reset_selected_text(), IDC_SIDE_FORMULA_RESET_SELECTED, g.side_channel_controls);
            g.side_formula_reset_all = mk_panel_btn(side_formula_reset_all_text(), IDC_SIDE_FORMULA_RESET_ALL, g.side_channel_controls);

            const struct PointToggleSeed { int id; const wchar_t* text; bool on; } point_toggle_seeds[] = {
                {IDC_SIDE_PT_NUM, side_pt_num_text(), g.pdisp.number},
                {IDC_SIDE_PT_X, side_pt_x_text(), g.pdisp.x},
                {IDC_SIDE_PT_Y, side_pt_y_text(), g.pdisp.y},
                {IDC_SIDE_PT_DX, side_pt_dx_text(), g.pdisp.dx},
                {IDC_SIDE_PT_DY, side_pt_dy_text(), g.pdisp.dy},
                {IDC_SIDE_PT_INVDT, side_pt_invdt_text(), g.pdisp.inv_dt},
                {IDC_SIDE_PT_DIST, side_pt_dist_text(), g.pdisp.dist},
                {IDC_SIDE_PT_SNAP, side_pt_snap_text(), g.snap_to_data},
            };
            for (const auto& seed : point_toggle_seeds) {
                HWND c = mk_panel_ctl(L"BUTTON", seed.text, BS_OWNERDRAW, seed.id, g.side_point_controls);
                if (c) set_toggle_checked(c, seed.on);
            }
            g.side_point_color_current = mk_panel_btn(point_current_color_button_text(), IDC_SIDE_POINT_COLOR_CURRENT, g.side_point_controls);
            g.side_point_label_groups = mk_panel_ctl(L"STATIC", point_group_list_title(), SS_LEFT, 0, g.side_point_controls);
            g.side_point_group_list = mk_panel_ctl(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, IDC_SIDE_POINT_GROUP_LIST, g.side_point_controls);
            g.side_point_group_visible = mk_panel_ctl(L"BUTTON", point_group_visible_text(), BS_OWNERDRAW, IDC_SIDE_POINT_GROUP_VISIBLE, g.side_point_controls);
            g.side_point_group_color = mk_panel_btn(point_selected_group_color_button_text(), IDC_SIDE_POINT_GROUP_COLOR, g.side_point_controls);
            g.side_point_group_new = mk_panel_btn(point_group_new_button_text(), IDC_SIDE_POINT_GROUP_NEW, g.side_point_controls);
            g.side_point_group_delete = mk_panel_btn(side_point_group_delete_text(), IDC_SIDE_POINT_GROUP_DELETE, g.side_point_controls);
            g.side_point_group_name = mk_panel_ctl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, IDC_SIDE_POINT_GROUP_NAME, g.side_point_controls);
            g.side_point_group_rename = mk_panel_btn(side_point_group_rename_text(), IDC_SIDE_POINT_GROUP_RENAME, g.side_point_controls);

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
            refresh_side_panel_controls();
            set_status();
            return 0;
        }
        case WM_SIZE:
            layout();
            if (welcome_visible()) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                SetWindowPos(g.welcome_wnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_SHOWWINDOW);
                InvalidateRect(g.welcome_wnd, nullptr, FALSE);
                return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* m = reinterpret_cast<MINMAXINFO*>(lp);
            m->ptMinTrackSize.x = 980;
            m->ptMinTrackSize.y = 560;
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PARENTNOTIFY:
            if (LOWORD(wp) == WM_LBUTTONDOWN) {
                finish_channel_rename_if_click_outside(hwnd);
            }
            break;
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
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_panel);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, g_theme->bg_panel);
            SetTextColor(dc, g_theme->text_primary);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis && mis->CtlType == ODT_MENU) {
                measure_owner_draw_menu(mis);
                return TRUE;
            }
            break;
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
            if (dis && dis->CtlType == ODT_MENU) {
                draw_owner_draw_menu(dis);
                return TRUE;
            }
            HWND btn = dis->hwndItem;
            if (!btn) break;
            HDC dc = dis->hDC;
            const int ctl_id = GetDlgCtrlID(btn);
            if (is_channel_checkbox_id(ctl_id) || is_side_toggle_id(ctl_id)) {
                wchar_t txt[128]{};
                GetWindowTextW(btn, txt, 128);
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                const bool enabled = IsWindowEnabled(btn) != FALSE;
                const bool compact = is_channel_checkbox_id(ctl_id);
                draw_themed_check_control(dc, dis->rcItem, txt,
                    is_toggle_checked(btn), pressed, enabled, false, compact);
                return TRUE;
            }
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
            } else if (btn == g.sidepanel_btn) {
                active = g.side_panel_visible;
            } else if (btn == g.side_tab_channels) {
                active = g.side_panel_tab == 0;
            } else if (btn == g.side_tab_points) {
                active = g.side_panel_tab == 1;
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
                {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    set_all_channels_visible(true);
                    record_settings_change(before);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                case IDC_HIDE_ALL:
                {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    set_all_channels_visible(false);
                    record_settings_change(before);
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
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
                case IDC_SIDEPANEL:
                    g.side_panel_visible = !g.side_panel_visible;
                    save_runtime_settings();
                    layout();
                    set_status();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDC_SIDE_TAB_CHANNELS:
                    g.side_scroll_y = 0;
                    set_side_panel_tab(0);
                    save_runtime_settings();
                    layout();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDC_SIDE_TAB_POINTS:
                    g.side_scroll_y = 0;
                    set_side_panel_tab(1);
                    save_runtime_settings();
                    layout();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDC_SIDE_GLOBAL_FORMULA_EDIT:
                case IDC_SIDE_FORMULA_EDIT:
                    if (HIWORD(wp) == EN_SETFOCUS && g.formula_ini_deferred) {
                        ensure_channel_formulas_loaded();
                        load_side_transform_controls();
                    }
                    return 0;
                case IDC_SIDE_GLOBAL_FORMULA_APPLY: {
                    if (!has_data()) return 0;
                    const SettingsSnapshot before = capture_settings_snapshot();
                    std::wstring formula;
                    std::wstring error;
                    std::vector<FormulaToken> compiled;
                    if (!read_formula_edit(g.side_global_formula_edit, formula, compiled, error)) {
                        std::wstring message = (g_str == &kEn) ? L"Invalid global coefficient:\n" : L"Некорректный общий коэффициент:\n";
                        message += error;
                        MessageBoxW(hwnd, message.c_str(), settings_window_title(), MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    assign_global_formula(formula, compiled);
                    on_signal_transform_changed(true);
                    record_settings_change(before);
                    if (g.settings_wnd) refresh_settings_controls();
                    load_side_transform_controls();
                    return 0;
                }
                case IDC_SIDE_CHANNEL_COLOR:
                    if (g.side_selected_channel >= 0 &&
                        g.side_selected_channel < static_cast<int>(g.ds.channel_count())) {
                        CHOOSECOLORW cc = {};
                        cc.lStructSize = sizeof(cc);
                        cc.hwndOwner = hwnd;
                        cc.lpCustColors = g_custom_colors;
                        cc.rgbResult = channel_color(static_cast<std::size_t>(g.side_selected_channel));
                        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                        if (ChooseColorW(&cc)) {
                            const SettingsSnapshot before = capture_settings_snapshot();
                            if (static_cast<std::size_t>(g.side_selected_channel) >= g_channel_colors.size()) {
                                g_channel_colors.resize(g.ds.channel_count());
                            }
                            g_channel_colors[static_cast<std::size_t>(g.side_selected_channel)] = cc.rgbResult;
                            record_settings_change(before);
                            if (g.settings_wnd) refresh_settings_controls();
                            refresh_side_panel_controls();
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    }
                    return 0;
                case IDC_SIDE_FORMULA_APPLY_SELECTED:
                case IDC_SIDE_FORMULA_APPLY_VISIBLE: {
                    if (!has_data()) return 0;
                    const SettingsSnapshot before = capture_settings_snapshot();
                    std::wstring formula;
                    std::wstring error;
                    std::vector<FormulaToken> compiled;
                    if (!read_formula_edit(g.side_formula_edit, formula, compiled, error)) {
                        std::wstring message = (g_str == &kEn) ? L"Invalid coefficient:\n" : L"Некорректный коэффициент:\n";
                        message += error;
                        MessageBoxW(hwnd, message.c_str(), settings_window_title(), MB_OK | MB_ICONWARNING);
                        return 0;
                    }

                    ensure_channel_formula_vectors();
                    bool changed = false;
                    if (LOWORD(wp) == IDC_SIDE_FORMULA_APPLY_SELECTED) {
                        if (g.side_selected_channel >= 0 &&
                            g.side_selected_channel < static_cast<int>(g.channel_formulas.size())) {
                            assign_formula_to_channel(static_cast<std::size_t>(g.side_selected_channel), formula, compiled);
                            changed = true;
                        }
                    } else {
                        for (std::size_t i = 0; i < g.visible.size() && i < g.channel_formulas.size(); ++i) {
                            if (!g.visible[i]) continue;
                            assign_formula_to_channel(i, formula, compiled);
                            changed = true;
                        }
                    }

                    if (changed) {
                        on_signal_transform_changed(true);
                        record_settings_change(before);
                        if (g.settings_wnd) refresh_settings_controls();
                        load_side_transform_controls();
                    }
                    return 0;
                }
                case IDC_SIDE_FORMULA_RESET_SELECTED:
                    if (g.side_selected_channel >= 0) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        reset_channel_transform(static_cast<std::size_t>(g.side_selected_channel));
                        on_signal_transform_changed(true);
                        record_settings_change(before);
                        if (g.settings_wnd) refresh_settings_controls();
                        load_side_transform_controls();
                    }
                    return 0;
                case IDC_SIDE_FORMULA_RESET_ALL:
                    if (has_data()) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        reset_all_channel_transforms();
                        on_signal_transform_changed(true);
                        record_settings_change(before);
                        if (g.settings_wnd) refresh_settings_controls();
                        load_side_transform_controls();
                    }
                    return 0;
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
                    save_runtime_settings();
                    sync_menu();
                    set_status();
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                case IDM_VPAN:
                    g.vertical_pan = !g.vertical_pan;
                    save_runtime_settings();
                    sync_menu();
                    set_status();
                    return 0;
                case IDM_THEME:
                    apply_theme_choice((g_theme == &kLightTheme) ? &kDarkTheme : &kLightTheme);
                    return 0;
                case IDM_ADD_VLINE:
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    if (g.pending_line == 1) {
                        g.pending_line = 0;
                        set_status();
                        sync_menu();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    g.pending_line = 1;
                    g.pending_marker = false;
                    g.measure_mode = false;
                    SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
                    status_msg(g_str->status_vline);
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDM_ADD_VLINE_EXACT: {
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    double value = 0.0;
                    if (!prompt_exact_guide_value(true, value)) return 0;
                    add_guide_line(true, value);
                    return 0;
                }
                case IDM_ADD_HLINE:
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    if (g.pending_line == 2) {
                        g.pending_line = 0;
                        set_status();
                        sync_menu();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    g.pending_line = 2;
                    g.pending_marker = false;
                    g.measure_mode = false;
                    SendMessageW(g.measure, BM_SETCHECK, BST_UNCHECKED, 0);
                    status_msg(g_str->status_hline);
                    sync_menu();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case IDM_ADD_HLINE_EXACT: {
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    double value = 0.0;
                    if (!prompt_exact_guide_value(false, value)) return 0;
                    add_guide_line(false, value);
                    return 0;
                }
                case IDM_CLEAR_LINES:
                    if (!g.guides.empty()) {
                        UndoAction ua; ua.type = UndoAction::CLEAR_LINES; ua.saved_lines = g.guides;
                        push_undo(ua);
                        g.guides.clear(); InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    set_status();
                    return 0;
                case IDM_CLEAR_POINTS:
                    if (has_measure_points()) {
                        UndoAction ua;
                        ua.type = UndoAction::CLEAR_POINTS;
                        ua.saved_point_groups = g.point_groups;
                        ua.saved_active_point_group = g.active_point_group;
                        push_undo(ua);
                        clear_measure_point_groups();
                        if (g.settings_wnd) populate_point_group_list(g.settings_wnd);
                        refresh_side_panel_controls();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    set_status();
                    return 0;
                case IDM_ADD_MARKER:
                    if (!has_data()) { MessageBoxW(hwnd, g_str->msg_openfirst, g_str->msg_nodata, MB_ICONINFORMATION); return 0; }
                    if (g.pending_marker) {
                        g.pending_marker = false;
                        set_status();
                        sync_menu();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
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
                case IDM_LANG_RU: g_str = &kRu; save_runtime_settings(); rebuild_ui(); return 0;
                case IDM_LANG_EN: g_str = &kEn; save_runtime_settings(); rebuild_ui(); return 0;
                case IDC_SIDE_POINT_GROUP_LIST:
                    if (HIWORD(wp) == LBN_SELCHANGE) {
                        const int index = side_selected_point_group();
                        if (index >= 0 && index < static_cast<int>(g.point_groups.size())) {
                            g.active_point_group = index;
                            g.marker_color = g.point_groups[static_cast<std::size_t>(index)].color;
                            save_runtime_settings();
                            if (g.settings_wnd) refresh_settings_controls();
                            load_side_point_group_controls();
                            set_status();
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    }
                    return 0;
                case IDC_SIDE_POINT_GROUP_VISIBLE: {
                    const int index = side_selected_point_group();
                    if (index >= 0 && index < static_cast<int>(g.point_groups.size())) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        toggle_checked_state(g.side_point_group_visible);
                        const bool checked = is_toggle_checked(g.side_point_group_visible);
                        g.point_groups[static_cast<std::size_t>(index)].visible = checked;
                        record_settings_change(before);
                        save_runtime_settings();
                        if (g.settings_wnd) refresh_settings_controls();
                        load_side_point_group_controls();
                        set_status();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_POINT_GROUP_NEW: {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    create_point_group(g.marker_color);
                    record_settings_change(before);
                    save_runtime_settings();
                    if (g.settings_wnd) refresh_settings_controls();
                    refresh_side_panel_controls();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                case IDC_SIDE_POINT_GROUP_DELETE: {
                    const int index = side_selected_point_group();
                    if (index >= 0 && index < static_cast<int>(g.point_groups.size())) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        erase_point_group(static_cast<std::size_t>(index));
                        if (PointGroup* group = active_point_group()) g.marker_color = group->color;
                        record_settings_change(before);
                        save_runtime_settings();
                        if (g.settings_wnd) refresh_settings_controls();
                        refresh_side_panel_controls();
                        set_status();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_POINT_GROUP_RENAME: {
                    const int index = side_selected_point_group();
                    if (index >= 0 && index < static_cast<int>(g.point_groups.size()) && g.side_point_group_name) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        wchar_t buf[256]{};
                        GetWindowTextW(g.side_point_group_name, buf, 256);
                        std::wstring name = buf;
                        if (name.empty()) {
                            name = (g_str == &kEn) ? (L"Group " + std::to_wstring(index + 1)) : (L"Группа " + std::to_wstring(index + 1));
                        }
                        g.point_groups[static_cast<std::size_t>(index)].name = name;
                        record_settings_change(before);
                        if (g.settings_wnd) refresh_settings_controls();
                        refresh_side_panel_controls();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_POINT_COLOR_CURRENT: {
                    CHOOSECOLORW cc = {};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.lpCustColors = g_custom_colors;
                    cc.rgbResult = g.marker_color;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        g.marker_color = cc.rgbResult;
                        PointGroup* group = active_point_group();
                        if (group && group->points.empty()) group->color = g.marker_color;
                        record_settings_change(before);
                        save_runtime_settings();
                        if (g.settings_wnd) refresh_settings_controls();
                        refresh_side_panel_controls();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_POINT_GROUP_COLOR: {
                    const int index = side_selected_point_group();
                    if (index < 0 || index >= static_cast<int>(g.point_groups.size())) return 0;
                    CHOOSECOLORW cc = {};
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = hwnd;
                    cc.lpCustColors = g_custom_colors;
                    cc.rgbResult = g.point_groups[static_cast<std::size_t>(index)].color;
                    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                    if (ChooseColorW(&cc)) {
                        const SettingsSnapshot before = capture_settings_snapshot();
                        g.point_groups[static_cast<std::size_t>(index)].color = cc.rgbResult;
                        g.active_point_group = index;
                        g.marker_color = cc.rgbResult;
                        record_settings_change(before);
                        save_runtime_settings();
                        if (g.settings_wnd) refresh_settings_controls();
                        refresh_side_panel_controls();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                case IDC_SIDE_PT_NUM:
                case IDC_SIDE_PT_X:
                case IDC_SIDE_PT_Y:
                case IDC_SIDE_PT_DX:
                case IDC_SIDE_PT_DY:
                case IDC_SIDE_PT_INVDT:
                case IDC_SIDE_PT_DIST:
                case IDC_SIDE_PT_SNAP: {
                    const SettingsSnapshot before = capture_settings_snapshot();
                    toggle_checked_state(GetDlgItem(hwnd, id));
                    auto checked = [&](int ctl_id) {
                        return is_toggle_checked(GetDlgItem(hwnd, ctl_id));
                    };
                    g.pdisp.number = checked(IDC_SIDE_PT_NUM);
                    g.pdisp.x = checked(IDC_SIDE_PT_X);
                    g.pdisp.y = checked(IDC_SIDE_PT_Y);
                    g.pdisp.dx = checked(IDC_SIDE_PT_DX);
                    g.pdisp.dy = checked(IDC_SIDE_PT_DY);
                    g.pdisp.inv_dt = checked(IDC_SIDE_PT_INVDT);
                    g.pdisp.dist = checked(IDC_SIDE_PT_DIST);
                    g.snap_to_data = checked(IDC_SIDE_PT_SNAP);
                    record_settings_change(before);
                    save_runtime_settings();
                    if (g.settings_wnd) refresh_settings_controls();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                default: break;
            }
            if (id >= IDC_CHAN_BASE && id < IDC_CHAN_BASE + static_cast<int>(g.visible.size())) {
                const int ci = id - IDC_CHAN_BASE;
                const SettingsSnapshot before = capture_settings_snapshot();
                g.side_selected_channel = ci;
                toggle_checked_state(g.checks[ci]);
                g.visible[ci] = is_toggle_checked(g.checks[ci]);
                invalidate_plot_analysis_cache();
                record_settings_change(before);
                load_side_transform_controls();
                InvalidateRect(hwnd, nullptr, TRUE);
            } else if (id >= IDC_CHAN_LABEL_BASE &&
                       id < IDC_CHAN_LABEL_BASE + static_cast<int>(g.channel_labels.size())) {
                const int ci = id - IDC_CHAN_LABEL_BASE;
                g.side_selected_channel = ci;
                load_side_transform_controls();
                if (HIWORD(wp) == STN_DBLCLK) {
                    start_channel_rename(ci);
                } else {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
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
        case WM_VSCROLL:
            if (reinterpret_cast<HWND>(lp) == g.side_scrollbar) {
                SCROLLINFO si{};
                si.cbSize = sizeof(si);
                si.fMask = SIF_ALL;
                GetScrollInfo(g.side_scrollbar, SB_CTL, &si);
                int next = g.side_scroll_y;
                switch (LOWORD(wp)) {
                    case SB_LINEUP: next -= 24; break;
                    case SB_LINEDOWN: next += 24; break;
                    case SB_PAGEUP: next -= static_cast<int>(si.nPage); break;
                    case SB_PAGEDOWN: next += static_cast<int>(si.nPage); break;
                    case SB_THUMBPOSITION:
                    case SB_THUMBTRACK: next = si.nTrackPos; break;
                    case SB_TOP: next = 0; break;
                    case SB_BOTTOM: next = g.side_scroll_max; break;
                }
                scroll_side_panel(next - g.side_scroll_y);
                return 0;
            }
            break;
        case WM_MOUSEWHEEL: {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (side_panel_hit_test(pt) && g.side_scroll_max > 0) {
                const int direction = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -48 : 48;
                scroll_side_panel(direction);
                return 0;
            }
            if (!has_data()) return 0;
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
            finish_channel_rename_if_click_outside(hwnd);
            const int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            const RECT p = plot_rect();
            if (!has_data()) {
                if (mx >= p.left && mx <= p.right && my >= p.top && my <= p.bottom) {
                    open_file();
                }
                return 0;
            }

            // --- Legend click handling (toggle / solo) ---
            if (mx >= g_legend_box.left && mx < g_legend_box.right &&
                my >= g_legend_box.top && my < g_legend_box.bottom) {
                for (const auto& li : g_legend_items) {
                    if (mx >= li.rect.left && mx < li.rect.right &&
                        my >= li.rect.top && my < li.rect.bottom) {
                        const int ci = li.channel;
                        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                            // Solo: show only this channel
                            const SettingsSnapshot before = capture_settings_snapshot();
                            for (std::size_t j = 0; j < g.visible.size(); ++j) {
                                g.visible[j] = (static_cast<int>(j) == ci);
                                if (j < g.checks.size())
                                    set_toggle_checked(g.checks[j], g.visible[j] != 0);
                            }
                            invalidate_plot_analysis_cache();
                            record_settings_change(before);
                        } else {
                            // Toggle
                            const SettingsSnapshot before = capture_settings_snapshot();
                            g.visible[ci] = !g.visible[ci];
                            if (ci < static_cast<int>(g.checks.size()))
                                set_toggle_checked(g.checks[ci], g.visible[ci] != 0);
                            invalidate_plot_analysis_cache();
                            record_settings_change(before);
                        }
                        InvalidateRect(hwnd, nullptr, TRUE);
                        return 0;
                    }
                }
            }

            if (!g.freq_mode && g.show_gap_markers) {
                const int gap_index = hit_test_gap_marker(mx, my);
                if (gap_index >= 0) {
                    g.gap_click_pending = false;
                    g.gap_click_index = -1;
                    if (prepare_plot_drag(mx, my)) {
                        g.gap_click_pending = true;
                        g.gap_click_index = gap_index;
                        SetCapture(hwnd);
                    }
                    return 0;
                }
            }

            if (g.side_panel_visible && g.side_panel_tab == 0) {
                for (std::size_t i = 0; i < g.checks.size(); ++i) {
                    HWND c = g.checks[i];
                    if (!c || !IsWindowVisible(c)) continue;
                    RECT cr;
                    GetWindowRect(c, &cr);
                    MapWindowPoints(nullptr, g.main, reinterpret_cast<LPPOINT>(&cr), 2);
                    RECT sr = {cr.left - 18, cr.top + (cr.bottom - cr.top - 12) / 2, cr.left - 6, cr.top + (cr.bottom - cr.top - 12) / 2 + 12};
                    if (mx >= sr.left && mx < sr.right && my >= sr.top && my < sr.bottom) {
                        CHOOSECOLORW cc = {};
                        cc.lStructSize = sizeof(cc);
                        cc.hwndOwner = hwnd;
                        cc.lpCustColors = g_custom_colors;
                        cc.rgbResult = channel_color(i);
                        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                        if (ChooseColorW(&cc)) {
                            const SettingsSnapshot before = capture_settings_snapshot();
                            if (i >= g_channel_colors.size()) g_channel_colors.resize(g.ds.channel_count());
                            g_channel_colors[i] = cc.rgbResult;
                            record_settings_change(before);
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
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
                    if (fft_window_contains_time(tt)) {
                        clear_fft_window();
                        set_status();
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
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
                set_status();
                sync_menu();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g.measure_mode) {
                double dx, dy;
                if (px_to_data(mx, my, dx, dy)) {
                    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    if (g.snap_to_data) snap_to_nearest(dx, dy);
                    bool created_group = false;
                    const int group_index = ensure_point_group_for_measurement(ctrl, &created_group);
                    if (group_index < 0 || group_index >= static_cast<int>(g.point_groups.size())) return 0;
                    g.point_groups[static_cast<std::size_t>(group_index)].points.push_back({dx, dy});
                    UndoAction ua;
                    ua.type = UndoAction::ADD_POINT;
                    ua.point = {dx, dy};
                    ua.point_group_index = group_index;
                    ua.point_group_created = created_group;
                    ua.point_group_state = g.point_groups[static_cast<std::size_t>(group_index)];
                    ua.point_group_state.points.clear();
                    push_undo(ua);
                    if (g.settings_wnd) populate_point_group_list(g.settings_wnd);
                    refresh_side_panel_controls();
                    set_status();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (!prepare_plot_drag(mx, my)) return 0;
            g.gap_click_pending = false;
            g.gap_click_index = -1;
            g.dragging = true;
            SetCapture(hwnd);
            return 0;
        }
        case WM_RBUTTONDOWN:
            if (has_fft_window()) {
                clear_fft_window();
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (has_measure_points()) {
                UndoAction ua;
                ua.type = UndoAction::CLEAR_POINTS;
                ua.saved_point_groups = g.point_groups;
                ua.saved_active_point_group = g.active_point_group;
                push_undo(ua);
                clear_measure_point_groups();
                if (g.settings_wnd) populate_point_group_list(g.settings_wnd);
                set_status();
                InvalidateRect(hwnd, nullptr, FALSE);
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
            if (g.gap_click_pending && !g.dragging) {
                const int dx = GET_X_LPARAM(lp) - g.drag_x;
                const int dy = GET_Y_LPARAM(lp) - g.drag_y;
                if (std::abs(dx) >= 4 || std::abs(dy) >= 4) {
                    g.gap_click_pending = false;
                    g.gap_click_index = -1;
                    g.dragging = true;
                } else {
                    return 0;
                }
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
            if (g.gap_click_pending) {
                const int pending_gap_index = g.gap_click_index;
                g.gap_click_pending = false;
                g.gap_click_index = -1;
                if (GetCapture() == hwnd) ReleaseCapture();
                if (!g.freq_mode && g.show_gap_markers) {
                    const int released_gap_index = hit_test_gap_marker(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                    if (released_gap_index >= 0 && released_gap_index == pending_gap_index) {
                        show_gap_details_dialog(hwnd, released_gap_index);
                    }
                }
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
        case WM_APP_ASYNC_SCAN_DONE: {
            std::unique_ptr<AsyncScanResult> result(reinterpret_cast<AsyncScanResult*>(lp));
            if (!result) return 0;
            if (result->token != g.async_load_token || g.async_load_stage != AsyncLoadStage::ScanningRange) return 0;
            g.async_load_stage = AsyncLoadStage::None;
            g.async_load_cancel_flag.reset();
            hide_loading();
            if (result->cancelled) {
                g.last_error.clear();
                return 0;
            }
            if (!result->ok) {
                g.last_error = result->error;
                MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
                return 0;
            }
            g.cached_scan_path = result->path;
            g.cached_scan_start = result->range_start;
            g.cached_scan_end = result->range_end;
            g.cached_scan_valid = true;
            if (!prompt_and_start_light_mode_load(result->path, result->range_start, result->range_end) &&
                !g.last_error.empty()) {
                MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
            }
            return 0;
        }
        case WM_APP_ASYNC_LOAD_DONE: {
            std::unique_ptr<AsyncLoadResult> result(reinterpret_cast<AsyncLoadResult*>(lp));
            if (!result) return 0;
            if (result->token != g.async_load_token || g.async_load_stage != AsyncLoadStage::LoadingFile) return 0;
            g.async_load_stage = AsyncLoadStage::None;
            g.async_load_cancel_flag.reset();
            hide_loading();
            if (result->cancelled) {
                g.last_error.clear();
                return 0;
            }
            if (!result->ok) {
                g.last_error = result->error;
                MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
                return 0;
            }
            apply_loaded_dataset(std::move(result->ds), result->path, result->hide_channels,
                                 result->requested_time_window, result->cached_global_gap_step,
                                 result->cached_global_gap_step_ready);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wp);
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            if (count > 0) {
                wchar_t path[MAX_PATH];
                if (DragQueryFileW(hDrop, 0, path, MAX_PATH)) {
                    if (!load_path_interactive(path) && !g.last_error.empty())
                        MessageBoxW(hwnd, to_w(g.last_error).c_str(), g_str->msg_read_err, MB_ICONERROR | MB_OK);
                }
            }
            DragFinish(hDrop);
            return 0;
        }
        case WM_DESTROY:
            request_async_load_cancel();
            hide_loading();
            save_runtime_settings();
            save_app_settings();
            stop_play();
            KillTimer(hwnd, 2);
            release_backbuffer();
            if (g.ui_font && g.ui_font != reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)))
                DeleteObject(g.ui_font);
            if (g.menu_font) DeleteObject(g.menu_font);
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
    wchar_t cls[64]{};
    for (HWND h = focus; h; h = GetParent(h)) {
        GetClassNameW(h, cls, 64);
        if (lstrcmpiW(cls, L"EDIT") == 0 ||
            lstrcmpiW(cls, L"COMBOBOX") == 0 ||
            lstrcmpiW(cls, L"ComboBoxEx32") == 0 ||
            wcsstr(cls, L"RICHEDIT") == cls) {
            return true;
        }
        if (h == g.main) break;
    }
    return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int show) {
    Gdiplus::GdiplusStartupInput gdi_in;
    Gdiplus::GdiplusStartup(&g_gdiplus_token, &gdi_in, nullptr);

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    load_app_settings();
    load_runtime_settings();

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
        if (!load_path_interactive(path) && !g.last_error.empty())
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
