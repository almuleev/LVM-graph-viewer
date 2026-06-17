// LVM Reader CLI — a C++ command-line companion to the Python LVM Signal Viewer.
//
// Reads .lvm / tab-separated .txt files, reports structure and per-channel
// statistics, prints data rows, computes an FFT spectrum, and exports CSV.
#include "analysis.hpp"
#include "lvm_parser.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* kVersion = "1.1.0";
const int kDefaultFftSamples = 0;  // 0 = use all samples (correct frequency axis)
const int kDefaultPeaks = 5;

void print_usage(const char* prog) {
    std::cout <<
        "LVM Reader CLI " << kVersion << "\n"
        "Usage: " << prog << " <file.lvm|file.txt> [options]\n"
        "\n"
        "Actions:\n"
        "  -i, --info          Show file structure / header info\n"
        "  -s, --stats         Show per-channel statistics (min/max/mean/count)\n"
        "  -H, --head N        Print the first N data rows\n"
        "  -c, --csv FILE      Export the (selected) data to a CSV file\n"
        "      --fft           Show the strongest spectral peaks per channel\n"
        "      --peaks N       Number of peaks to show (default 5; implies --fft)\n"
        "      --fft-csv FILE  Export the magnitude spectrum to a CSV file\n"
        "\n"
        "Selection (applied to stats / head / csv / fft):\n"
        "      --start T       Keep rows with time >= T\n"
        "      --end T         Keep rows with time <= T\n"
        "      --channels LIST Comma-separated 1-based channel positions, e.g. 1,3\n"
        "\n"
        "Parsing / spectrum options:\n"
        "  -m, --monotonic     Rebuild a monotonic timeline (for sectioned files)\n"
        "      --keep-dup-time Keep channels that duplicate the time axis\n"
        "      --fft-samples N  Decimate to at most N samples for FFT (0 = use all)\n"
        "  -v, --verbose       Verbose parser output\n"
        "  -h, --help          Show this help\n"
        "\n"
        "With no action flag, --info and --stats are shown.\n";
}

// Format a double compactly; empty string for NaN (matches pandas CSV output).
std::string fmt(double v) {
    if (std::isnan(v)) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.15g", v);
    return buf;
}

struct ChannelStat {
    long long count = 0;  // non-NaN samples
    double min = std::numeric_limits<double>::quiet_NaN();
    double max = std::numeric_limits<double>::quiet_NaN();
    double mean = std::numeric_limits<double>::quiet_NaN();
};

ChannelStat compute_stat(const std::vector<double>& col) {
    ChannelStat s;
    double sum = 0.0;
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (double v : col) {
        if (std::isnan(v)) continue;
        ++s.count;
        sum += v;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (s.count > 0) {
        s.min = lo;
        s.max = hi;
        s.mean = sum / static_cast<double>(s.count);
    }
    return s;
}

// Build a new Dataset limited to the time window [start, end] and the chosen
// channels (1-based positions; empty = keep all). Returns false on bad input.
bool apply_selection(const lvm::Dataset& in, double start, double end,
                     const std::vector<int>& channel_pos, lvm::Dataset& out,
                     std::string& error) {
    std::vector<std::size_t> chans;
    if (channel_pos.empty()) {
        for (std::size_t c = 0; c < in.channels.size(); ++c) chans.push_back(c);
    } else {
        for (int pos : channel_pos) {
            if (pos < 1 || pos > static_cast<int>(in.channels.size())) {
                error = "Channel position out of range: " + std::to_string(pos) +
                        " (have " + std::to_string(in.channels.size()) + ")";
                return false;
            }
            chans.push_back(static_cast<std::size_t>(pos - 1));
        }
    }

    out = lvm::Dataset{};
    out.stats = in.stats;
    out.ok = true;
    for (std::size_t c : chans) out.names.push_back(in.names[c]);
    out.channels.resize(chans.size());

    for (std::size_t r = 0; r < in.time.size(); ++r) {
        const double t = in.time[r];
        if (t < start || t > end) continue;
        out.time.push_back(t);
        for (std::size_t k = 0; k < chans.size(); ++k) {
            out.channels[k].push_back(in.channels[chans[k]][r]);
        }
    }
    return true;
}

void print_info(const lvm::Dataset& ds) {
    std::cout << "File structure\n";
    std::cout << "  Header end markers : " << ds.stats.header_markers << "\n";
    std::cout << "  Data sections      : " << ds.stats.data_sections << "\n";
    std::cout << "  Data rows (raw)    : " << ds.stats.data_rows << "\n";
    std::cout << "  Max columns        : " << ds.stats.max_columns << "\n";
    std::cout << "  Rows selected      : " << ds.rows() << "\n";
    std::cout << "  Channels           : " << ds.channel_count() << "\n";
    if (!ds.time.empty()) {
        std::cout << "  Time range         : " << fmt(ds.time.front()) << " .. "
                  << fmt(ds.time.back()) << "\n";
    }
}

void print_stats(const lvm::Dataset& ds) {
    std::cout << "\nChannel statistics\n";
    std::printf("  %-20s %12s %14s %14s %14s\n", "channel", "count", "min", "max", "mean");
    for (std::size_t c = 0; c < ds.channels.size(); ++c) {
        const ChannelStat s = compute_stat(ds.channels[c]);
        std::printf("  %-20s %12lld %14s %14s %14s\n",
                    ds.names[c].c_str(), s.count,
                    fmt(s.min).c_str(), fmt(s.max).c_str(), fmt(s.mean).c_str());
    }
}

void print_head(const lvm::Dataset& ds, long long n) {
    const long long rows = std::min<long long>(n, static_cast<long long>(ds.rows()));
    std::cout << "\nFirst " << rows << " rows\n";
    std::cout << "Time";
    for (std::size_t c = 0; c < ds.channels.size(); ++c) std::cout << "\t" << ds.names[c];
    std::cout << "\n";
    for (long long r = 0; r < rows; ++r) {
        std::cout << fmt(ds.time[r]);
        for (std::size_t c = 0; c < ds.channels.size(); ++c) {
            std::cout << "\t" << fmt(ds.channels[c][r]);
        }
        std::cout << "\n";
    }
}

bool export_csv(const lvm::Dataset& ds, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "CSV export failed: cannot open " << path << "\n";
        return false;
    }
    out << "Time";
    for (std::size_t c = 0; c < ds.channels.size(); ++c) out << "," << ds.names[c];
    out << "\n";
    for (std::size_t r = 0; r < ds.rows(); ++r) {
        out << fmt(ds.time[r]);
        for (std::size_t c = 0; c < ds.channels.size(); ++c) {
            out << "," << fmt(ds.channels[c][r]);
        }
        out << "\n";
    }
    return true;
}

void print_peaks(const lvm::Spectrum& spec, int peak_count) {
    std::cout << "\nFFT spectrum (N=" << spec.n << ", dt=" << fmt(spec.sample_dt)
              << " s, Nyquist=" << fmt(spec.nyquist) << " Hz)\n";
    for (std::size_t c = 0; c < spec.names.size(); ++c) {
        const auto peaks = lvm::find_peaks(spec.freqs, spec.amp[c], peak_count);
        std::cout << "  " << spec.names[c] << ":";
        for (const auto& p : peaks) {
            std::cout << "  " << fmt(p.freq) << " Hz (amp " << fmt(p.amp) << ")";
        }
        std::cout << "\n";
    }
}

bool export_fft_csv(const lvm::Spectrum& spec, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "FFT CSV export failed: cannot open " << path << "\n";
        return false;
    }
    out << "Frequency";
    for (const auto& name : spec.names) out << "," << name;
    out << "\n";
    for (std::size_t k = 0; k < spec.freqs.size(); ++k) {
        out << fmt(spec.freqs[k]);
        for (std::size_t c = 0; c < spec.amp.size(); ++c) out << "," << fmt(spec.amp[c][k]);
        out << "\n";
    }
    return true;
}

std::vector<int> parse_channel_list(const std::string& text) {
    std::vector<int> out;
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        out.push_back(std::stoi(token));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string file;
    bool want_info = false, want_stats = false, want_monotonic = false;
    bool keep_dup_time = false, verbose = false, want_fft = false;
    long long head_n = -1;
    std::string csv_path, fft_csv_path, channels_text;
    int peak_count = kDefaultPeaks;
    int fft_samples = kDefaultFftSamples;
    double start = -std::numeric_limits<double>::infinity();
    double end = std::numeric_limits<double>::infinity();

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
        else if (a == "-i" || a == "--info") want_info = true;
        else if (a == "-s" || a == "--stats") want_stats = true;
        else if (a == "-m" || a == "--monotonic") want_monotonic = true;
        else if (a == "--keep-dup-time") keep_dup_time = true;
        else if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "-H" || a == "--head") head_n = std::stoll(next("--head"));
        else if (a == "-c" || a == "--csv") csv_path = next("--csv");
        else if (a == "--fft") want_fft = true;
        else if (a == "--peaks") { peak_count = std::stoi(next("--peaks")); want_fft = true; }
        else if (a == "--fft-csv") fft_csv_path = next("--fft-csv");
        else if (a == "--fft-samples") fft_samples = std::stoi(next("--fft-samples"));
        else if (a == "--start") start = std::stod(next("--start"));
        else if (a == "--end") end = std::stod(next("--end"));
        else if (a == "--channels") channels_text = next("--channels");
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            return 2;
        } else if (file.empty()) {
            file = a;
        } else {
            std::cerr << "Unexpected extra argument: " << a << "\n";
            return 2;
        }
    }

    if (file.empty()) {
        print_usage(argv[0]);
        return 2;
    }

    lvm::Dataset ds = lvm::read_lvm_file(file, verbose);
    if (!ds.ok) {
        std::cerr << "Error: " << ds.error << "\n";
        return 1;
    }

    // Keep a copy of raw time for duplicate-channel detection before any rewrite.
    const std::vector<double> raw_time = ds.time;
    if (!keep_dup_time) {
        const auto dropped = lvm::drop_duplicate_time_channels(ds, raw_time);
        if (!dropped.empty() && verbose) {
            std::cout << "Dropped time-axis duplicate channels:";
            for (const auto& d : dropped) std::cout << " " << d;
            std::cout << "\n";
        }
    }
    if (want_monotonic) lvm::make_monotonic(ds.time);

    // Apply time-range / channel selection.
    std::vector<int> channel_pos;
    if (!channels_text.empty()) channel_pos = parse_channel_list(channels_text);
    lvm::Dataset view;
    std::string sel_error;
    if (!apply_selection(ds, start, end, channel_pos, view, sel_error)) {
        std::cerr << "Error: " << sel_error << "\n";
        return 2;
    }
    if (view.rows() == 0) {
        std::cerr << "Error: selection is empty (no rows in the given time range).\n";
        return 1;
    }

    const bool want_fft_any = want_fft || !fft_csv_path.empty();
    const bool any_action =
        want_info || want_stats || head_n >= 0 || !csv_path.empty() || want_fft_any;
    if (!any_action) { want_info = true; want_stats = true; }

    if (want_info) print_info(view);
    if (want_stats) print_stats(view);
    if (head_n >= 0) print_head(view, head_n);
    if (!csv_path.empty()) {
        if (!export_csv(view, csv_path)) return 1;
        std::cout << "\nCSV exported: " << csv_path << " (" << view.rows()
                  << " rows, " << view.channel_count() << " channels)\n";
    }

    if (want_fft_any) {
        const lvm::Spectrum spec = lvm::compute_spectrum(view, fft_samples);
        if (!spec.ok) {
            std::cerr << "FFT error: " << spec.error << "\n";
            return 1;
        }
        if (want_fft) print_peaks(spec, peak_count);
        if (!fft_csv_path.empty()) {
            if (!export_fft_csv(spec, fft_csv_path)) return 1;
            std::cout << "\nSpectrum exported: " << fft_csv_path << " ("
                      << spec.freqs.size() << " bins, " << spec.names.size()
                      << " channels)\n";
        }
    }
    return 0;
}
