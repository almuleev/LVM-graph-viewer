// Small dependency-free FFT used for the Hz / spectrum mode.
//
// `dft` accepts an arbitrary length (not just powers of two) by falling back to
// Bluestein's algorithm, so the spectrum matches numpy's rfft used by the
// Python viewer.
#pragma once

#include <complex>
#include <vector>

namespace lvm {

// In-place iterative radix-2 FFT. `a.size()` must be a power of two.
// inverse=true computes the IFFT (normalised by 1/N).
void fft_radix2(std::vector<std::complex<double>>& a, bool inverse);

// Forward DFT of an arbitrary-length signal.
std::vector<std::complex<double>> dft(const std::vector<std::complex<double>>& in);

}  // namespace lvm
