// Spectral analysis built on top of the parsed Dataset.
//
// Mirrors the Python viewer's Hz mode: optional linspace downsampling to a
// sample cap, per-channel mean removal, real FFT, and amplitude = 2/N * |X|.
#pragma once

#include <string>
#include <vector>

#include "lvm_parser.hpp"

namespace lvm {

struct Peak {
    double freq = 0.0;
    double amp = 0.0;
};

struct Spectrum {
    std::vector<double> freqs;             // length N/2 + 1
    std::vector<std::string> names;        // channels that produced a spectrum
    std::vector<std::vector<double>> amp;  // per channel, length N/2 + 1
    double sample_dt = 0.0;
    double nyquist = 0.0;
    int n = 0;                             // number of samples used
    bool ok = false;
    std::string error;
};

// Compute the magnitude spectrum for every channel. `max_samples > 0` caps the
// sample count via evenly spaced (linspace) indices, as the Python viewer does.
Spectrum compute_spectrum(const Dataset& ds, int max_samples);

// Return up to `count` strongest spectral peaks (local maxima), excluding DC.
std::vector<Peak> find_peaks(const std::vector<double>& freqs,
                             const std::vector<double>& amp, int count);

}  // namespace lvm
