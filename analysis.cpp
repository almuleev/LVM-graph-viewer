#include "analysis.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

#include "fft.hpp"

namespace lvm {
namespace {

// numpy-style median of a non-empty vector (mutates by sorting a copy).
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t mid = v.size() / 2;
    return (v.size() % 2 == 0) ? 0.5 * (v[mid - 1] + v[mid]) : v[mid];
}

// Uniform decimation: take every k-th index so the sampling stays evenly
// spaced (keeps the frequency axis correct, unlike linspace skipping).
std::vector<std::size_t> decimated_indices(std::size_t rows, int max_samples) {
    std::vector<std::size_t> idx;
    std::size_t step = 1;
    if (max_samples > 0 && rows > static_cast<std::size_t>(max_samples)) {
        step = (rows + static_cast<std::size_t>(max_samples) - 1) /
               static_cast<std::size_t>(max_samples);
    }
    for (std::size_t i = 0; i < rows; i += step) idx.push_back(i);
    return idx;
}

}  // namespace

Spectrum compute_spectrum(const Dataset& ds, int max_samples) {
    Spectrum spec;
    const std::size_t rows = ds.rows();
    if (rows < 4) {
        spec.error = "Not enough samples for FFT (need at least 4).";
        return spec;
    }

    // Choose sample indices, optionally decimating to the cap.
    const std::vector<std::size_t> idx = decimated_indices(rows, max_samples);
    const int n = static_cast<int>(idx.size());

    // Sample spacing from the median positive time step.
    std::vector<double> positive_dt;
    positive_dt.reserve(idx.size());
    for (std::size_t i = 1; i < idx.size(); ++i) {
        const double d = ds.time[idx[i]] - ds.time[idx[i - 1]];
        if (d > 0.0) positive_dt.push_back(d);
    }
    if (positive_dt.empty()) {
        spec.error = "Time axis is not increasing; cannot derive sample rate.";
        return spec;
    }
    spec.sample_dt = median(positive_dt);
    if (spec.sample_dt <= 0.0) {
        spec.error = "Non-positive sample spacing.";
        return spec;
    }

    spec.n = n;
    spec.nyquist = 0.5 / spec.sample_dt;
    const int half = n / 2;  // rfft returns N/2 + 1 bins
    spec.freqs.resize(half + 1);
    for (int k = 0; k <= half; ++k) {
        spec.freqs[k] = static_cast<double>(k) / (static_cast<double>(n) * spec.sample_dt);
    }

    for (std::size_t c = 0; c < ds.channels.size(); ++c) {
        const auto& col = ds.channels[c];

        // Replace NaNs with the channel mean (needs >= 4 finite samples).
        double sum = 0.0;
        int finite = 0;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const double v = col[idx[i]];
            if (!std::isnan(v)) { sum += v; ++finite; }
        }
        if (finite < 4) continue;
        const double fill = sum / static_cast<double>(finite);

        std::vector<std::complex<double>> sig(idx.size());
        double total = 0.0;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const double v = col[idx[i]];
            const double clean = std::isnan(v) ? fill : v;
            sig[i] = std::complex<double>(clean, 0.0);
            total += clean;
        }
        const double mean = total / static_cast<double>(idx.size());
        for (auto& s : sig) s -= mean;  // remove DC

        const std::vector<std::complex<double>> spectrum = dft(sig);
        std::vector<double> amp(half + 1);
        const double scale = 2.0 / static_cast<double>(n);
        for (int k = 0; k <= half; ++k) amp[k] = scale * std::abs(spectrum[k]);

        spec.names.push_back(ds.names[c]);
        spec.amp.push_back(std::move(amp));
    }

    spec.ok = !spec.amp.empty();
    if (!spec.ok) spec.error = "No channel had enough finite samples for FFT.";
    return spec;
}

std::vector<Peak> find_peaks(const std::vector<double>& freqs,
                             const std::vector<double>& amp, int count) {
    std::vector<Peak> peaks;
    if (amp.size() < 2 || freqs.size() != amp.size()) return peaks;

    // Interior local maxima, skipping DC (k = 0).
    for (std::size_t k = 1; k + 1 < amp.size(); ++k) {
        if (amp[k] > amp[k - 1] && amp[k] >= amp[k + 1]) {
            peaks.push_back({freqs[k], amp[k]});
        }
    }
    // Fallback: no interior maxima -> take the single strongest non-DC bin.
    if (peaks.empty()) {
        std::size_t best = 1;
        for (std::size_t k = 1; k < amp.size(); ++k) {
            if (amp[k] > amp[best]) best = k;
        }
        peaks.push_back({freqs[best], amp[best]});
    }

    std::sort(peaks.begin(), peaks.end(),
              [](const Peak& a, const Peak& b) { return a.amp > b.amp; });
    if (count > 0 && static_cast<int>(peaks.size()) > count) {
        peaks.resize(static_cast<std::size_t>(count));
    }
    return peaks;
}

}  // namespace lvm
