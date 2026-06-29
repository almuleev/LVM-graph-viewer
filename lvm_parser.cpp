#include "lvm_parser.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace lvm {
namespace {

const std::unordered_set<std::string>& metadata_keys() {
    static const std::unordered_set<std::string> keys = {
        "LabVIEW Measurement", "Writer_Version", "Reader_Version", "Separator",
        "Decimal_Separator", "Multi_Headings", "X_Columns", "Time_Pref",
        "Operator", "Date", "Time", "Channels", "Samples", "Y_Unit_Label",
        "X_Dimension", "X0", "Delta_X",
    };
    return keys;
}

// Trim leading/trailing ASCII whitespace (matches Python str.strip defaults).
std::string strip(const std::string& s) {
    const char* ws = " \t\r\n\f\v";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::string first_cell(const std::string& line) {
    const auto tab = line.find('\t');
    return strip(tab == std::string::npos ? line : line.substr(0, tab));
}

std::string second_cell(const std::string& line) {
    const auto first_tab = line.find('\t');
    if (first_tab == std::string::npos) return "";
    const auto second_tab = line.find('\t', first_tab + 1);
    return strip(line.substr(first_tab + 1, second_tab == std::string::npos ? std::string::npos : second_tab - first_tab - 1));
}

// First tab-delimited cell, stripped (Python: line.partition("\t")[0].strip()).
bool is_metadata_line(const std::string& line) {
    return metadata_keys().count(first_cell(line)) != 0;
}

// Parse a single cell to double, requiring the whole token to be numeric
// (matches Python float(): "1.2.3" -> ValueError -> NaN). Empty -> NaN.
double parse_cell(const std::string& cell, bool& is_numeric) {
    is_numeric = false;
    if (cell.empty()) return std::nan("");
    const char* begin = cell.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin || *end != '\0') return std::nan("");
    is_numeric = true;
    return value;
}

struct RowSummary {
    bool first_numeric = false;
    double first_value = std::nan("");
    int numeric_count = 0;
};

RowSummary summarize_numeric_row(const std::string& line) {
    RowSummary summary;
    std::string field;
    field.reserve(32);
    int column_index = 0;
    auto flush_field = [&]() {
        bool is_numeric = false;
        const std::string cell = strip(field);
        const double value = parse_cell(cell, is_numeric);
        if (column_index == 0) {
            summary.first_numeric = is_numeric;
            summary.first_value = value;
        }
        if (is_numeric) ++summary.numeric_count;
        field.clear();
        ++column_index;
    };

    for (char ch : line) {
        if (ch == ',') ch = '.';
        if (ch == '\t') {
            flush_field();
            if (summary.first_numeric && summary.numeric_count >= 2) break;
        } else {
            field.push_back(ch);
        }
    }
    if (!field.empty() || column_index == 0 || (!summary.first_numeric && summary.numeric_count < 2)) {
        flush_field();
    }
    return summary;
}

struct PendingSectionTime {
    bool have_date = false;
    bool have_time = false;
    bool have_x0 = false;
    std::string date_text;
    std::string time_text;
    double x0 = 0.0;

    void reset() {
        have_date = false;
        have_time = false;
        have_x0 = false;
        date_text.clear();
        time_text.clear();
        x0 = 0.0;
    }
};

struct ActiveSectionTime {
    bool valid = false;
    double offset_seconds = 0.0;
    double x0 = 0.0;
};

bool parse_labview_datetime(const std::string& date_text, const std::string& time_text, double& out_seconds) {
    int year = 0, month = 0, day = 0;
    if (std::sscanf(date_text.c_str(), "%d/%d/%d", &year, &month, &day) != 3) return false;

    std::string normalized_time = time_text;
    for (char& ch : normalized_time) {
        if (ch == ',') ch = '.';
    }

    int hour = 0, minute = 0;
    double seconds = 0.0;
    if (std::sscanf(normalized_time.c_str(), "%d:%d:%lf", &hour, &minute, &seconds) != 3) return false;

    const int whole_seconds = static_cast<int>(std::floor(seconds));
    const double fractional_seconds = seconds - static_cast<double>(whole_seconds);

    std::tm tm_value{};
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = hour;
    tm_value.tm_min = minute;
    tm_value.tm_sec = whole_seconds;
    tm_value.tm_isdst = -1;
    const std::time_t base = std::mktime(&tm_value);
    if (base == static_cast<std::time_t>(-1)) return false;

    out_seconds = static_cast<double>(base) + fractional_seconds;
    return true;
}

void update_section_metadata(const std::string& line, PendingSectionTime& pending) {
    const std::string key = first_cell(line);
    const std::string value = second_cell(line);
    if (value.empty()) return;

    if (key == "Date") {
        pending.have_date = true;
        pending.date_text = value;
        return;
    }
    if (key == "Time") {
        pending.have_time = true;
        pending.time_text = value;
        return;
    }
    if (key == "X0") {
        bool is_numeric = false;
        std::string normalized = value;
        for (char& ch : normalized) {
            if (ch == ',') ch = '.';
        }
        const double parsed = parse_cell(normalized, is_numeric);
        if (is_numeric && std::isfinite(parsed)) {
            pending.have_x0 = true;
            pending.x0 = parsed;
        }
    }
}

void activate_section_time(const PendingSectionTime& pending,
                           bool& have_anchor,
                           double& anchor_seconds,
                           ActiveSectionTime& active) {
    active.valid = false;
    if (!pending.have_date || !pending.have_time || !pending.have_x0) return;

    double absolute_seconds = 0.0;
    if (!parse_labview_datetime(pending.date_text, pending.time_text, absolute_seconds)) return;
    if (!have_anchor) {
        have_anchor = true;
        anchor_seconds = absolute_seconds;
    }

    active.valid = true;
    active.offset_seconds = absolute_seconds - anchor_seconds;
    active.x0 = pending.x0;
}

}  // namespace

Dataset read_lvm_file(const std::string& path, bool verbose) {
    return read_lvm_file(path, LoadOptions{}, verbose);
}

bool scan_time_bounds(const std::string& path, double& out_start, double& out_end, std::string& error,
                      const std::atomic<bool>* cancel_flag) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open file: " + path;
        return false;
    }

    bool have_time = false;
    double first_time = 0.0;
    double last_time = 0.0;
    double prev_raw_time = 0.0;
    double prev_adjusted_time = 0.0;
    double time_offset = 0.0;
    double fallback_step = 1e-6;
    bool have_section_anchor = false;
    double section_anchor_seconds = 0.0;
    PendingSectionTime pending_section{};
    ActiveSectionTime active_section{};

    std::string raw_line;
    long long line_index = 0;
    while (std::getline(in, raw_line)) {
        ++line_index;
        if (cancel_flag && (line_index & 0xFF) == 0 && cancel_flag->load(std::memory_order_relaxed)) {
            error = "Operation cancelled.";
            return false;
        }
        const std::string line = strip(raw_line);
        if (line.empty()) continue;
        if (starts_with(line, "***End_of_Header***")) {
            activate_section_time(pending_section, have_section_anchor, section_anchor_seconds, active_section);
            pending_section.reset();
            continue;
        }
        if (starts_with(line, "***")) continue;
        if (is_metadata_line(line)) {
            update_section_metadata(line, pending_section);
            continue;
        }

        const RowSummary row = summarize_numeric_row(line);
        if (row.numeric_count < 2 || !row.first_numeric || std::isnan(row.first_value)) continue;

        const double raw_time = row.first_value;
        double adjusted_time = raw_time;
        if (active_section.valid) {
            adjusted_time = active_section.offset_seconds + (raw_time - active_section.x0);
        } else if (have_time) {
            const double diff = raw_time - prev_raw_time;
            if (diff > 0.0) fallback_step = diff;
            adjusted_time = raw_time + time_offset;
            if (adjusted_time <= prev_adjusted_time) {
                time_offset = prev_adjusted_time + fallback_step - raw_time;
                adjusted_time = raw_time + time_offset;
            }
        } else {
            first_time = raw_time;
        }

        if (!have_time) first_time = adjusted_time;
        have_time = true;
        prev_raw_time = raw_time;
        prev_adjusted_time = adjusted_time;
        last_time = adjusted_time;
    }

    if (!have_time) {
        error = "No numeric data found. Expected time + numeric channel columns in a .lvm or tab-separated .txt file.";
        return false;
    }

    const auto normalize_bound = [](double v) {
        return (std::isfinite(v) && std::fabs(v) < 1e-12) ? 0.0 : v;
    };
    first_time = normalize_bound(first_time);
    last_time = normalize_bound(last_time);
    if (last_time < first_time) std::swap(last_time, first_time);

    out_start = first_time;
    out_end = last_time;
    return true;
}

Dataset read_lvm_file(const std::string& path, const LoadOptions& options, bool verbose) {
    Dataset ds;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ds.error = "Cannot open file: " + path;
        return ds;
    }

    const double nan_value = std::nan("");

    std::vector<std::vector<double>> columns;
    std::vector<char> column_has_value;  // any non-NaN seen in this column
    std::vector<double> raw_time_rows;
    bool has_nan_time_rows = false;
    long long row_count = 0;

    int header_count = 0;
    int section_hits = 0;
    bool section_has_data = false;

    std::string raw_line;
    long long line_index = 0;
    bool have_time_state = false;
    double prev_raw_time = 0.0;
    double prev_adjusted_time = 0.0;
    double time_offset = 0.0;
    double fallback_step = 1e-6;
    bool have_section_anchor = false;
    double section_anchor_seconds = 0.0;
    PendingSectionTime pending_section{};
    ActiveSectionTime active_section{};
    bool used_header_time_rebuild = false;
    for (; std::getline(in, raw_line); ++line_index) {
        if (options.cancel_flag && (line_index & 0xFF) == 0 &&
            options.cancel_flag->load(std::memory_order_relaxed)) {
            ds.error = "Operation cancelled.";
            return ds;
        }
        const std::string line = strip(raw_line);
        if (line.empty()) continue;

        if (starts_with(line, "***End_of_Header***")) {
            ++header_count;
            section_has_data = false;
            activate_section_time(pending_section, have_section_anchor, section_anchor_seconds, active_section);
            pending_section.reset();
            continue;
        }
        if (starts_with(line, "***")) {
            continue;
        }
        if (is_metadata_line(line)) {
            update_section_metadata(line, pending_section);
            continue;
        }

        // Normalize decimal commas, then split by tabs preserving positions.
        std::vector<double> parsed;
        int numeric_count = 0;
        std::string field;
        auto flush_field = [&]() {
            bool is_numeric = false;
            const std::string cell = strip(field);
            const double v = parse_cell(cell, is_numeric);
            parsed.push_back(v);
            if (is_numeric) ++numeric_count;
            field.clear();
        };
        for (char ch : line) {
            if (ch == ',') ch = '.';
            if (ch == '\t') {
                flush_field();
            } else {
                field.push_back(ch);
            }
        }
        flush_field();

        const int part_count = static_cast<int>(parsed.size());
        if (numeric_count < 2) continue;

        const double raw_time = parsed.empty() ? std::nan("") : parsed[0];
        const bool have_raw_time = !std::isnan(raw_time);
        bool used_header_time = false;
        if (have_raw_time && active_section.valid) {
            parsed[0] = active_section.offset_seconds + (raw_time - active_section.x0);
            used_header_time = true;
            used_header_time_rebuild = true;
        }

        bool keep_row = true;
        if (options.use_time_window) {
            if (!have_raw_time) continue;

            double adjusted_time = parsed[0];
            if (!used_header_time) {
                adjusted_time = raw_time;
                if (have_time_state) {
                    const double diff = raw_time - prev_raw_time;
                    if (diff > 0.0) fallback_step = diff;
                    adjusted_time = raw_time + time_offset;
                    if (adjusted_time <= prev_adjusted_time) {
                        time_offset = prev_adjusted_time + fallback_step - raw_time;
                        adjusted_time = raw_time + time_offset;
                    }
                }
            }

            have_time_state = true;
            prev_raw_time = raw_time;
            prev_adjusted_time = adjusted_time;

            if (adjusted_time < options.time_start) {
                keep_row = false;
            } else if (adjusted_time > options.time_end) {
                ds.partial = true;
                break;
            }
            parsed[0] = adjusted_time;
        }

        if (!section_has_data) {
            section_has_data = true;
            ++section_hits;
            if (verbose) {
                // Mirror the Python verbose breadcrumb loosely.
                // (Section/line indices only; values omitted for brevity.)
            }
        }

        if (!keep_row) continue;
        if (parsed.empty() || std::isnan(parsed[0])) has_nan_time_rows = true;

        // Widen the column store to fit this row, back-filling earlier rows.
        if (part_count > static_cast<int>(columns.size())) {
            const int extra = part_count - static_cast<int>(columns.size());
            for (int k = 0; k < extra; ++k) {
                columns.emplace_back(static_cast<std::size_t>(row_count), nan_value);
                column_has_value.push_back(0);
            }
        }
        const int col_count = static_cast<int>(columns.size());

        for (int c = 0; c < part_count; ++c) {
            const double v = parsed[c];
            columns[c].push_back(v);
            if (!std::isnan(v)) column_has_value[c] = 1;
        }
        for (int c = part_count; c < col_count; ++c) {
            columns[c].push_back(nan_value);
        }
        raw_time_rows.push_back(raw_time);
        ++row_count;
    }

    ds.stats.header_markers = header_count;
    ds.stats.data_sections = section_hits;
    ds.stats.data_rows = row_count;
    ds.stats.max_columns = static_cast<int>(columns.size());
    ds.time_rebuilt_from_headers = used_header_time_rebuild;

    if (row_count == 0 || columns.empty()) {
        ds.error = options.use_time_window
            ? "No data found in the selected time range."
            : "No numeric data found. Expected time + numeric channel columns in a .lvm or tab-separated .txt file.";
        return ds;
    }

    // First column is time; keep channels that hold at least one real value.
    std::vector<double>& time_col = columns[0];
    std::vector<std::vector<double>> kept_channels;
    std::vector<std::string> kept_names;
    for (std::size_t i = 1; i < columns.size(); ++i) {
        if (i < column_has_value.size() && column_has_value[i]) {
            kept_names.push_back("Channel_" + std::to_string(kept_channels.size() + 1));
            kept_channels.push_back(std::move(columns[i]));
        }
    }

    // Drop rows whose time is NaN, keeping channels aligned.
    if (!has_nan_time_rows && raw_time_rows.size() == time_col.size()) {
        ds.time = std::move(time_col);
        ds.raw_time = std::move(raw_time_rows);
        ds.channels = std::move(kept_channels);
    } else {
        ds.time.reserve(time_col.size());
        ds.raw_time.reserve(time_col.size());
        ds.channels.assign(kept_channels.size(), {});
        for (auto& ch : ds.channels) ch.reserve(time_col.size());
        for (std::size_t r = 0; r < time_col.size(); ++r) {
            if (std::isnan(time_col[r])) continue;
            ds.time.push_back(time_col[r]);
            ds.raw_time.push_back((r < raw_time_rows.size()) ? raw_time_rows[r] : time_col[r]);
            for (std::size_t c = 0; c < kept_channels.size(); ++c) {
                ds.channels[c].push_back(kept_channels[c][r]);
            }
        }
    }
    ds.names = std::move(kept_names);
    ds.ok = !ds.channels.empty();
    if (ds.time.empty() && options.use_time_window) {
        ds.ok = false;
        ds.error = "No data found in the selected time range.";
    } else if (!ds.ok) {
        ds.error = "No data channels available.";
    }
    return ds;
}

void make_monotonic(std::vector<double>& time) {
    if (time.size() <= 1) return;

    std::vector<double> positive_diffs;
    positive_diffs.reserve(time.size());
    for (std::size_t i = 1; i < time.size(); ++i) {
        const double d = time[i] - time[i - 1];
        if (d > 0.0) positive_diffs.push_back(d);
    }
    double fallback_step = 1e-6;
    if (!positive_diffs.empty()) {
        std::sort(positive_diffs.begin(), positive_diffs.end());
        const std::size_t mid = positive_diffs.size() / 2;
        fallback_step = (positive_diffs.size() % 2 == 0)
                            ? 0.5 * (positive_diffs[mid - 1] + positive_diffs[mid])
                            : positive_diffs[mid];
    }
    if (fallback_step <= 0.0) fallback_step = 1e-6;

    const std::vector<double> raw = time;
    double offset = 0.0;
    for (std::size_t i = 1; i < time.size(); ++i) {
        double candidate = raw[i] + offset;
        if (candidate <= time[i - 1]) {
            offset = time[i - 1] + fallback_step - raw[i];
            candidate = raw[i] + offset;
        }
        time[i] = candidate;
    }
}

std::vector<std::string> drop_duplicate_time_channels(Dataset& ds,
                                                      const std::vector<double>& raw_time) {
    const auto allclose = [](double a, double b) {
        const double rtol = 1e-9, atol = 1e-12;
        return std::fabs(a - b) <= atol + rtol * std::fabs(b);
    };

    std::vector<std::string> dropped;
    const std::vector<std::vector<double>> original_channels = ds.channels;
    const std::vector<std::string> original_names = ds.names;
    std::vector<std::vector<double>> kept_channels;
    std::vector<std::string> kept_names;

    for (std::size_t c = 0; c < original_channels.size(); ++c) {
        const auto& col = original_channels[c];
        bool any_valid = false;
        bool duplicate = true;
        for (std::size_t r = 0; r < col.size() && r < raw_time.size(); ++r) {
            if (std::isnan(col[r]) || std::isnan(raw_time[r])) continue;
            any_valid = true;
            if (!allclose(col[r], raw_time[r])) {
                duplicate = false;
                break;
            }
        }
        if (any_valid && duplicate) {
            if (c < original_names.size()) dropped.push_back(original_names[c]);
        } else {
            if (c < original_names.size()) kept_names.push_back(original_names[c]);
            kept_channels.push_back(col);
        }
    }

    ds.channels = std::move(kept_channels);
    ds.names = std::move(kept_names);
    return dropped;
}

}  // namespace lvm
