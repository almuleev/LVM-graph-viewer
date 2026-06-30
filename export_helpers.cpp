#include "export_helpers.hpp"

#include <algorithm>

const wchar_t* export_prompt_title_text(bool english) {
    return english ? L"Export data" : L"Экспорт данных";
}

const wchar_t* export_prompt_intro_text(bool english) {
    return english
        ? L"Choose what should be exported."
        : L"Выберите, что нужно выгрузить.";
}

std::wstring export_scope_title_text(ExportDataScope scope, bool english) {
    switch (scope) {
        case ExportDataScope::CurrentView:
            return english ? L"Current view" : L"Текущий вид";
        case ExportDataScope::LoadedFragment:
            return english ? L"Loaded fragment" : L"Загруженный фрагмент";
        case ExportDataScope::RawData:
            return english ? L"Raw data without formulas" : L"Сырые данные без формул";
    }
    return L"";
}

std::wstring export_scope_detail_text(ExportDataScope scope, bool english) {
    switch (scope) {
        case ExportDataScope::CurrentView:
            return english
                ? L"Only the part currently shown on screen."
                : L"Только та часть, которая сейчас показана на экране.";
        case ExportDataScope::LoadedFragment:
            return english
                ? L"The whole loaded fragment in the current mode."
                : L"Весь загруженный фрагмент в текущем режиме.";
        case ExportDataScope::RawData:
            return english
                ? L"Original time-domain data, all channels, no formulas."
                : L"Исходные временные данные, все каналы, без формул.";
    }
    return L"";
}

std::wstring export_scope_choice_text(ExportDataScope scope, bool english) {
    return export_scope_title_text(scope, english) + L"\r\n" +
           export_scope_detail_text(scope, english);
}

const wchar_t* export_prompt_continue_text(bool english) {
    return english ? L"Continue" : L"Продолжить";
}

const wchar_t* export_prompt_cancel_text(bool english) {
    return english ? L"Cancel" : L"Отмена";
}

std::wstring export_default_name(const std::wstring& stem,
                                ExportDataScope scope,
                                bool csv,
                                bool freq_mode) {
    std::wstring out = stem;
    switch (scope) {
        case ExportDataScope::CurrentView:
            out += freq_mode ? L"_view_freq" : L"_view_time";
            break;
        case ExportDataScope::LoadedFragment:
            out += freq_mode ? L"_fragment_freq" : L"_fragment_time";
            break;
        case ExportDataScope::RawData:
            out += L"_raw_time";
            break;
    }
    out += csv ? L".csv" : L".txt";
    return out;
}

std::pair<std::size_t, std::size_t> export_range_bounds(const std::vector<double>& values,
                                                        double start,
                                                        double end) {
    if (start > end) std::swap(start, end);
    const auto first = std::lower_bound(values.begin(), values.end(), start);
    const auto last = std::upper_bound(values.begin(), values.end(), end);
    return {
        static_cast<std::size_t>(first - values.begin()),
        static_cast<std::size_t>(last - values.begin())
    };
}

std::wstring export_status_prefix(const wchar_t* format_name,
                                  ExportDataScope scope,
                                  bool english) {
    const std::wstring scope_text = export_scope_title_text(scope, english);
    if (english) {
        return std::wstring(L"Exported ") + format_name + L" (" + scope_text + L"): ";
    }
    return std::wstring(L"Выгружен ") + format_name + L" (" + scope_text + L"): ";
}

const wchar_t* export_error_text(bool english, bool csv) {
    if (csv) return english ? L"Failed to export CSV." : L"Не удалось выгрузить CSV.";
    return english ? L"Failed to export TXT." : L"Не удалось выгрузить TXT.";
}
