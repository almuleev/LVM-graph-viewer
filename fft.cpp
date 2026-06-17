#include "fft.hpp"

#include <cmath>

namespace lvm {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void fft_radix2(std::vector<std::complex<double>>& a, bool inverse) {
    const std::size_t n = a.size();
    if (n <= 1) return;

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (auto& x : a) x /= static_cast<double>(n);
    }
}

std::vector<std::complex<double>> dft(const std::vector<std::complex<double>>& in) {
    const std::size_t n = in.size();
    if (n == 0) return {};

    // Power-of-two fast path.
    if ((n & (n - 1)) == 0) {
        std::vector<std::complex<double>> a = in;
        fft_radix2(a, false);
        return a;
    }

    // Bluestein's algorithm for arbitrary length.
    std::size_t m = 1;
    while (m < 2 * n - 1) m <<= 1;

    std::vector<std::complex<double>> chirp(n);
    for (std::size_t k = 0; k < n; ++k) {
        // angle = -pi * k^2 / n, reduced mod 2n to keep precision for large k.
        const unsigned long long sq = (static_cast<unsigned long long>(k) * k) % (2ULL * n);
        const double ang = -kPi * static_cast<double>(sq) / static_cast<double>(n);
        chirp[k] = std::complex<double>(std::cos(ang), std::sin(ang));
    }

    std::vector<std::complex<double>> a(m, {0.0, 0.0});
    std::vector<std::complex<double>> b(m, {0.0, 0.0});
    for (std::size_t k = 0; k < n; ++k) {
        a[k] = in[k] * chirp[k];
        b[k] = std::conj(chirp[k]);
        if (k != 0) b[m - k] = std::conj(chirp[k]);
    }

    fft_radix2(a, false);
    fft_radix2(b, false);
    for (std::size_t i = 0; i < m; ++i) a[i] *= b[i];
    fft_radix2(a, true);

    std::vector<std::complex<double>> out(n);
    for (std::size_t k = 0; k < n; ++k) out[k] = a[k] * chirp[k];
    return out;
}

}  // namespace lvm
