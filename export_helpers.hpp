#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

enum class ExportDataScope {
    CurrentView,
    LoadedFragment,
    RawData,
};

const wchar_t* export_prompt_title_text(bool english);
const wchar_t* export_prompt_intro_text(bool english);
std::wstring export_scope_title_text(ExportDataScope scope, bool english);
std::wstring export_scope_detail_text(ExportDataScope scope, bool english);
std::wstring export_scope_choice_text(ExportDataScope scope, bool english);
const wchar_t* export_prompt_continue_text(bool english);
const wchar_t* export_prompt_cancel_text(bool english);
std::wstring export_default_name(const std::wstring& stem,
                                ExportDataScope scope,
                                bool csv,
                                bool freq_mode);
std::pair<std::size_t, std::size_t> export_range_bounds(const std::vector<double>& values,
                                                        double start,
                                                        double end);
std::wstring export_status_prefix(const wchar_t* format_name,
                                  ExportDataScope scope,
                                  bool english);
const wchar_t* export_error_text(bool english, bool csv);
