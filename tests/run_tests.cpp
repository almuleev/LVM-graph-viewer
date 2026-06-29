// Unit tests for the LVM parser and spectrum analysis.
//
// A tiny assertion harness (no external framework). Each test writes a small
// temporary .lvm/.txt file, parses it, and checks the result. Exit code is
// non-zero if any check fails, so it works as a `make test` gate.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "analysis.hpp"
#include "lvm_parser.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void check_near(double a, double b, double tol, const std::string& what) {
    ++g_checks;
    if (!(std::fabs(a - b) <= tol)) {
        ++g_failures;
        std::printf("  FAIL: %s (got %.9g, expected %.9g)\n", what.c_str(), a, b);
    }
}

// Write text to a temp file path; returns the path.
std::string write_temp(const std::string& name, const std::string& content) {
    const std::string path = "tests/_tmp_" + name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path;
}

void test_basic_parse() {
    std::printf("test_basic_parse\n");
    const std::string content =
        "LabVIEW Measurement\n"
        "Separator\tTab\n"
        "Multi_Headings\tNo\n"
        "***End_of_Header***\n"
        "Time\tChannel_1[V]\tChannel_2[V]\n"
        "0.0\t1.0\t10.0\n"
        "0.1\t2.0\t20.0\n"
        "0.2\t3.0\t30.0\n";
    const std::string path = write_temp("basic.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(path);

    check(ds.ok, "parse ok");
    check(ds.stats.header_markers == 1, "one header marker");
    check(ds.rows() == 3, "three rows kept");
    check(ds.channel_count() == 2, "two channels");
    check(ds.names.size() == 2 && ds.names[0] == "Channel_1" && ds.names[1] == "Channel_2",
          "channel names Channel_1/Channel_2");
    check_near(ds.time[0], 0.0, 1e-12, "time[0]");
    check_near(ds.time[2], 0.2, 1e-12, "time[2]");
    check_near(ds.channels[1][2], 30.0, 1e-12, "channel_2 last value");
}

void test_metadata_and_nan() {
    std::printf("test_metadata_and_nan\n");
    const std::string content =
        "LabVIEW Measurement\n"
        "Date\t11/12/2025\n"
        "Time\t12:00:00.0000000\n"     // metadata timestamp -> skipped
        "***End_of_Header***\n"
        "Time\tCh1\tCh2\n"             // first cell "Time" is metadata -> skipped
        "0.0\t1.0\t2.0\n"
        "0.1\t\t4.0\n"                 // empty cell -> NaN, two numeric? only 0.1,4.0 -> kept
        "0.2\tbad\t6.0\n"              // non-numeric -> NaN
        "junk line with one 5\n";      // < 2 numeric values -> skipped
    const std::string path = write_temp("nan.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(path);

    check(ds.ok, "parse ok");
    check(ds.rows() == 3, "three data rows (junk skipped)");
    check(ds.channel_count() == 2, "two channels");
    check(std::isnan(ds.channels[0][1]), "empty cell -> NaN");
    check(std::isnan(ds.channels[0][2]), "non-numeric cell -> NaN");
    check_near(ds.channels[1][1], 4.0, 1e-12, "ch2 row 1 value");
}

void test_decimal_comma() {
    std::printf("test_decimal_comma\n");
    const std::string content =
        "***End_of_Header***\n"
        "0,0\t1,5\n"
        "0,1\t2,5\n"
        "0,2\t3,5\n"
        "0,3\t4,5\n";
    const std::string path = write_temp("comma.txt", content);
    lvm::Dataset ds = lvm::read_lvm_file(path);

    check(ds.ok, "parse ok");
    check(ds.rows() == 4, "four rows");
    check_near(ds.time[1], 0.1, 1e-12, "comma decimal in time");
    check_near(ds.channels[0][0], 1.5, 1e-12, "comma decimal in channel");
}

void test_multi_header() {
    std::printf("test_multi_header\n");
    const std::string content =
        "***End_of_Header***\n"
        "0.0\t1.0\n"
        "0.1\t2.0\n"
        "***End_of_Header***\n"        // second section
        "0.0\t3.0\n"                   // local time resets
        "0.1\t4.0\n";
    const std::string path = write_temp("multi.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(path);

    check(ds.stats.header_markers == 2, "two header markers");
    check(ds.stats.data_sections == 2, "two data sections");
    check(ds.rows() == 4, "four combined rows");

    // raw time is non-monotonic (0,0.1,0,0.1) -> make_monotonic fixes it.
    std::vector<double> t = ds.time;
    lvm::make_monotonic(t);
    bool increasing = true;
    for (std::size_t i = 1; i < t.size(); ++i) increasing = increasing && (t[i] > t[i - 1]);
    check(increasing, "make_monotonic yields strictly increasing time");
}

void test_make_monotonic_equal_times() {
    std::printf("test_make_monotonic_equal_times\n");
    std::vector<double> t = {0.0, 0.1, 0.1, 0.2};
    lvm::make_monotonic(t);

    bool increasing = true;
    for (std::size_t i = 1; i < t.size(); ++i) increasing = increasing && (t[i] > t[i - 1]);
    check(increasing, "equal timestamps are pushed forward to stay strictly increasing");
    check_near(t[2], 0.2, 1e-12, "duplicate timestamp advanced by the fallback step");
}

void test_make_monotonic_backward_jump() {
    std::printf("test_make_monotonic_backward_jump\n");
    std::vector<double> t = {0.0, 0.1, 0.05, 0.15};
    lvm::make_monotonic(t);

    bool increasing = true;
    for (std::size_t i = 1; i < t.size(); ++i) increasing = increasing && (t[i] > t[i - 1]);
    check(increasing, "backward jumps are pushed forward to stay strictly increasing");
    check(t[2] > t[1], "reordered point is moved ahead of the previous sample");
}

void test_drop_duplicate_time() {
    std::printf("test_drop_duplicate_time\n");
    // Channel_1 duplicates time exactly; Channel_2 is real data.
    const std::string content =
        "***End_of_Header***\n"
        "0.0\t0.0\t5.0\n"
        "1.0\t1.0\t6.0\n"
        "2.0\t2.0\t7.0\n"
        "3.0\t3.0\t8.0\n";
    const std::string path = write_temp("dup.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(path);
    check(ds.channel_count() == 2, "two channels before drop");

    const std::vector<double> raw_time = ds.time;
    const auto dropped = lvm::drop_duplicate_time_channels(ds, raw_time);
    check(dropped.size() == 1 && dropped[0] == "Channel_1", "Channel_1 dropped as time dup");
    check(ds.channel_count() == 1 && ds.names[0] == "Channel_2", "Channel_2 remains");
}

void test_interleaved_channel_names() {
    std::printf("test_interleaved_channel_names\n");
    const std::string content =
        "LabVIEW Measurement\n"
        "Writer_Version\t0.92\n"
        "Reader_Version\t1\n"
        "Separator\tTab\n"
        "Multi_Headings\tYes\n"
        "X_Columns\tMulti\n"
        "***End_of_Header***\n"
        "Channels\t8\t\t\t\t\t\t\t\n"
        "Samples\t2\t2\t2\t2\t2\t2\t2\t2\n"
        "Date\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\t2009/05/15\n"
        "Time\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\t00:00:00,000\n"
        "X0\t0\t0\t0\t0\t0\t0\t0\t0\n"
        "Delta_X\t0.1\t0.1\t0.1\t0.1\t0.1\t0.1\t0.1\t0.1\n"
        "***End_of_Header***\n"
        "X_Value\tg1\tX_Value\tg2\tX_Value\tg3\tX_Value\tg4\tX_Value\tg5\tX_Value\tg6\tX_Value\tg7\tX_Value\tg8\tComment\n"
        "0.0\t1.0\t0.0\t2.0\t0.0\t3.0\t0.0\t4.0\t0.0\t5.0\t0.0\t6.0\t0.0\t7.0\t0.0\t8.0\tok\n"
        "0.1\t1.1\t0.1\t2.1\t0.1\t3.1\t0.1\t4.1\t0.1\t5.1\t0.1\t6.1\t0.1\t7.1\t0.1\t8.1\tok\n";
    const std::string path = write_temp("interleaved_names.lvm", content);
    lvm::Dataset ds = lvm::read_lvm_file(path);
    check(ds.ok, "interleaved file parses");
    check(ds.channel_count() == 15, "interleaved file initially has 15 channels before de-dup");

    const std::vector<double> raw_time = ds.raw_time.empty() ? ds.time : ds.raw_time;
    const auto dropped = lvm::drop_duplicate_time_channels(ds, raw_time);
    check(dropped.size() == 7, "seven duplicate time columns dropped");
    check(ds.channel_count() == 8, "eight data channels remain");
    const std::vector<std::string> expected = {"g1", "g2", "g3", "g4", "g5", "g6", "g7", "g8"};
    check(ds.names == expected, "channel names preserved as g1..g8");
}

void test_reference_test_lvm() {
    std::printf("test_reference_test_lvm\n");
    lvm::Dataset ds = lvm::read_lvm_file("lvm_files_for_tests/test.lvm");
    check(ds.ok, "reference test.lvm parses");
    check(ds.rows() == 10000, "reference test.lvm row count");
    check(ds.channel_count() == 3, "reference test.lvm channel count");
    check(ds.names.size() == 3 && ds.names[0] == "Channel_1" && ds.names[1] == "Channel_2" &&
              ds.names[2] == "Channel_3",
          "reference test.lvm channel names");
    if (ds.ok && ds.rows() == 10000 && ds.channel_count() == 3) {
        check_near(ds.time.front(), 0.0, 1e-12, "reference time starts at zero");
        check_near(ds.time.back(), 9.999, 1e-12, "reference time ends at 9.999");
    }
}

void test_fft_peak() {
    std::printf("test_fft_peak\n");
    // 50 Hz sine sampled at 1000 Hz for 1 s -> peak near 50 Hz.
    lvm::Dataset ds;
    ds.names.push_back("Channel_1");
    ds.channels.resize(1);
    const int n = 1000;
    const double fs = 1000.0, freq = 50.0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        ds.time.push_back(t);
        ds.channels[0].push_back(std::sin(2.0 * 3.14159265358979323846 * freq * t));
    }
    ds.ok = true;

    lvm::Spectrum spec = lvm::compute_spectrum(ds, 0);  // no cap
    check(spec.ok, "spectrum ok");
    check_near(spec.sample_dt, 1.0 / fs, 1e-9, "sample_dt");
    check_near(spec.nyquist, fs / 2.0, 1e-6, "nyquist");

    const auto peaks = lvm::find_peaks(spec.freqs, spec.amp[0], 1);
    check(!peaks.empty(), "found a peak");
    if (!peaks.empty()) {
        check_near(peaks[0].freq, freq, 1.0, "peak at ~50 Hz");
        check_near(peaks[0].amp, 1.0, 0.05, "peak amplitude ~1.0");
    }
}

void test_fft_nyquist_amplitude() {
    std::printf("test_fft_nyquist_amplitude\n");
    lvm::Dataset ds;
    ds.names.push_back("Channel_1");
    ds.channels.resize(1);
    const int n = 8;
    const double fs = 8.0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        ds.time.push_back(t);
        ds.channels[0].push_back((i % 2 == 0) ? 1.0 : -1.0);
    }
    ds.ok = true;

    const lvm::Spectrum spec = lvm::compute_spectrum(ds, 0);
    check(spec.ok, "nyquist spectrum ok");
    check(spec.amp.size() == 1, "one channel in nyquist spectrum");
    if (spec.ok && spec.amp.size() == 1) {
        check_near(spec.freqs.back(), fs / 2.0, 1e-12, "nyquist frequency bin");
        check_near(spec.amp[0].back(), 1.0, 1e-12, "nyquist amplitude is not doubled");
    }
}

void test_fft_sample_cap_too_small() {
    std::printf("test_fft_sample_cap_too_small\n");
    lvm::Dataset ds;
    ds.names.push_back("Channel_1");
    ds.channels.resize(1);
    for (int i = 0; i < 16; ++i) {
        ds.time.push_back(static_cast<double>(i));
        ds.channels[0].push_back(static_cast<double>(i));
    }
    ds.ok = true;

    const lvm::Spectrum spec = lvm::compute_spectrum(ds, 3);
    check(!spec.ok, "tiny fft sample cap is rejected");
    check(!spec.error.empty(), "tiny fft sample cap reports an error");
}

void test_missing_file() {
    std::printf("test_missing_file\n");
    lvm::Dataset ds = lvm::read_lvm_file("tests/_does_not_exist.lvm");
    check(!ds.ok, "missing file -> not ok");
    check(!ds.error.empty(), "error message set");
}

}  // namespace

int main() {
    test_basic_parse();
    test_metadata_and_nan();
    test_decimal_comma();
    test_multi_header();
    test_make_monotonic_equal_times();
    test_make_monotonic_backward_jump();
    test_drop_duplicate_time();
    test_interleaved_channel_names();
    test_reference_test_lvm();
    test_fft_peak();
    test_fft_nyquist_amplitude();
    test_fft_sample_cap_too_small();
    test_missing_file();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
