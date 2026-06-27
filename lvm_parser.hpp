// LVM parser (C++ port of the Python `read_lvm_file` / `prepare_loaded_data`).
//
// Parses LabVIEW Measurement (.lvm) files and tab-separated numeric .txt files
// into a Time column plus numeric channel columns. The parsing rules mirror the
// reference Python implementation in lvm_viewer.py so results stay consistent.
#pragma once

#include <string>
#include <vector>

namespace lvm {

// Structural counters collected while streaming the file.
struct ParseStats {
    int header_markers = 0;   // number of ***End_of_Header*** lines
    int data_sections = 0;    // sections that contained numeric data
    long long data_rows = 0;  // numeric rows collected (before dropping NaN time)
    int max_columns = 0;      // widest row seen
};

struct LoadOptions {
    bool use_time_window = false;
    double time_start = 0.0;
    double time_end = 0.0;
};

// Parsed dataset: a time vector and aligned channel columns.
struct Dataset {
    std::vector<double> time;                  // first column (X / time)
    std::vector<double> raw_time;              // original first column before any normalization
    std::vector<std::string> names;            // channel names, e.g. "Channel_1"
    std::vector<std::vector<double>> channels; // one vector per channel, aligned with time
    ParseStats stats;
    bool partial = false;                      // true when loading stopped early by options
    bool time_rebuilt_from_headers = false;    // true when section Date/Time + X0 rebuilt the timeline
    bool ok = false;
    std::string error;

    std::size_t rows() const { return time.size(); }
    std::size_t channel_count() const { return channels.size(); }
};

// Read and parse a file. Returns ok=false with `error` set on failure.
Dataset read_lvm_file(const std::string& path, bool verbose = false);
Dataset read_lvm_file(const std::string& path, const LoadOptions& options, bool verbose = false);
bool scan_time_bounds(const std::string& path, double& out_start, double& out_end, std::string& error);

// Rebuild a monotonically increasing timeline (mirrors the Python "prepare"
// step that flattens Multi_Headings sections which reset local time).
void make_monotonic(std::vector<double>& time);

// Drop channels that merely duplicate the time/X axis. Uses the supplied raw
// time as the reference. Returns the names of dropped channels.
std::vector<std::string> drop_duplicate_time_channels(Dataset& ds,
                                                       const std::vector<double>& raw_time);

}  // namespace lvm
