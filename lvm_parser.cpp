#include "lvm_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
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

// First tab-delimited cell, stripped (Python: line.partition("\t")[0].strip()).
bool is_metadata_line(const std::string& line) {
    const auto tab = line.find('\t');
    const std::string first_cell =
        strip(tab == std::string::npos ? line : line.substr(0, tab));
    return metadata_keys().count(first_cell) != 0;
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

}  // namespace

Dataset read_lvm_file(const std::string& path, bool verbose) {
    Dataset ds;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ds.error = "Cannot open file: " + path;
        return ds;
    }

    const double nan_value = std::nan("");

    std::vector<std::vector<double>> columns;
    std::vector<char> column_has_value;  // any non-NaN seen in this column
    long long row_count = 0;

    int header_count = 0;
    int section_hits = 0;
    bool section_has_data = false;

    std::string raw_line;
    long long line_index = 0;
    for (; std::getline(in, raw_line); ++line_index) {
        const std::string line = strip(raw_line);
        if (line.empty()) continue;

        if (starts_with(line, "***End_of_Header***")) {
            ++header_count;
            section_has_data = false;
            continue;
        }
        if (starts_with(line, "***") || is_metadata_line(line)) {
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

        if (!section_has_data) {
            section_has_data = true;
            ++section_hits;
            if (verbose) {
                // Mirror the Python verbose breadcrumb loosely.
                // (Section/line indices only; values omitted for brevity.)
            }
        }

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
        ++row_count;
    }

    ds.stats.header_markers = header_count;
    ds.stats.data_sections = section_hits;
    ds.stats.data_rows = row_count;
    ds.stats.max_columns = static_cast<int>(columns.size());

    if (row_count == 0 || columns.empty()) {
        ds.error =
            "No numeric data found. Expected time + numeric channel columns in "
            "a .lvm or tab-separated .txt file.";
        return ds;
    }

    // First column is time; keep channels that hold at least one real value.
    std::vector<double>& time_col = columns[0];
    std::vector<std::vector<double>> kept_channels;
    std::vector<std::string> kept_names;
    for (std::size_t i = 1; i < columns.size(); ++i) {
        if (i < column_has_value.size() && column_has_value[i]) {
            kept_names.push_back("Channel_" + std::to_string(i));
            kept_channels.push_back(std::move(columns[i]));
        }
    }

    // Drop rows whose time is NaN, keeping channels aligned.
    ds.time.reserve(time_col.size());
    ds.channels.assign(kept_channels.size(), {});
    for (auto& ch : ds.channels) ch.reserve(time_col.size());
    for (std::size_t r = 0; r < time_col.size(); ++r) {
        if (std::isnan(time_col[r])) continue;
        ds.time.push_back(time_col[r]);
        for (std::size_t c = 0; c < kept_channels.size(); ++c) {
            ds.channels[c].push_back(kept_channels[c][r]);
        }
    }
    ds.names = std::move(kept_names);
    ds.ok = !ds.channels.empty();
    if (!ds.ok) ds.error = "No data channels available.";
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
        if (candidate < time[i - 1]) {
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
    std::vector<std::vector<double>> kept_channels;
    std::vector<std::string> kept_names;

    for (std::size_t c = 0; c < ds.channels.size(); ++c) {
        const auto& col = ds.channels[c];
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
            dropped.push_back(ds.names[c]);
        } else {
            kept_names.push_back(ds.names[c]);
            kept_channels.push_back(ds.channels[c]);
        }
    }

    if (!dropped.empty()) {
        ds.channels = std::move(kept_channels);
        ds.names = std::move(kept_names);
    }
    return dropped;
}

}  // namespace lvm
