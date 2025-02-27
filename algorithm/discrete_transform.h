//-----------------------------------------------------------------------------
//  Copyright (C) 2025, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//
//  Permission is hereby granted, free of charge, to any person or organization
//  obtaining a copy of the software and accompanying documentation covered by
//  this license (the "Software") to use, reproduce, display, distribute,
//  execute, and transmit the Software, and to prepare derivative works of the
//  Software, and to permit third-parties to whom the Software is furnished to
//  do so, all subject to the following:
//
//  The copyright notices in the Software and this entire statement, including
//  the above license grant, this restriction and the following disclaimer,
//  must be included in all copies of the Software, in whole or in part, and
//  all derivative works of the Software, unless such copies or derivative
//  works are solely in the form of machine-executable object code generated by
//  a source language processor.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
//  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
//  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#pragma once

#include <vector>
#include <tuple>
#include <complex>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>
#include "../util/gberror.h"

namespace gb::yadro::algorithm
{
    // recursive implementation of the Fast Fourier Transform
    inline void fft_recursive(std::vector<std::complex<double>>& a) 
    {
        size_t N = a.size();
        if (N <= 1) return;

        std::vector<std::complex<double>> even(N / 2), odd(N / 2);
        for (size_t i = 0; i < N / 2; ++i) 
        {
            even[i] = a[i * 2];
            odd[i] = a[i * 2 + 1];
        }

        fft_recursive(even);
        fft_recursive(odd);

        for (size_t k = 0; k < N / 2; ++k) 
        {
            std::complex<double> t = std::polar(1.0, -2 * std::numbers::pi * k / N) * odd[k];
            a[k] = even[k] + t;
            a[k + N / 2] = even[k] - t;
        }
    }

    // Decompose a signal into its frequency components using the Fast Fourier Transform
    // Returns a vector of tuples, each containing the magnitude, frequency, and phase of a component
    // The components are sorted by magnitude in descending order
    // The frequency is normalized to the range [0, 1], where 1 corresponds to the Nyquist frequency
    // The phase is in radians
    // The input data must have a size that is a power of 2
    template <std::ranges::sized_range Sequence>
    std::vector<std::tuple<double, double, double>> fft_decompose(const Sequence& data)
    {
        static_assert(std::is_convertible_v<std::ranges::range_value_t<Sequence>, double>, "Data must be convertible to double");
        size_t data_size = data.size();
        util::gbassert(data_size != 0, "data must not be empty");
        util::gbassert((data_size & (data_size - 1)) == 0, "Data size must be a power of 2");

        std::vector<std::complex<double>> x(data_size);
        for (size_t i = 0; i < data_size; ++i) {
            x[i] = std::complex<double>(data[i], 0.0);
        }

        fft_recursive(x);

        // Store only positive frequency components
        std::vector<std::tuple<double, double, double>> components;
        for (size_t k = 0; k <= data_size / 2; ++k) {
            // negative frequencies are discarded, positive frequencies are doubled
            auto magnitude = (k == 0 ? 1 : 2) * std::abs(x[k]) / data_size;
            auto frequency = static_cast<double>(k) / data_size;
            auto phase = std::atan2(x[k].imag(), x[k].real());
            components.emplace_back(magnitude, frequency, phase);
        }

        // Sort components by magnitude in descending order
        std::sort(components.begin(), components.end(), [](auto a, auto b) { return std::get<0>(a) > std::get<0>(b); });

        return components;
    }

    // In-place inverse FFT using the conjugation method.
    inline void ifft(std::vector<std::complex<double>>& a) 
    {
        for (auto& x : a)
            x = conj(x);
    
        fft_recursive(a);
        
        for (auto& x : a)
            x = conj(x) / static_cast<double>(a.size());
    }

    // Helper: Compute next power of 2 ? n.
    inline size_t next_power_of_two(size_t n) 
    {
        size_t power = 1;
        while (power < n)
            power *= 2;
        return power;
    }

    // Bluestein's algorithm for arbitrary-length FFT.
    // Computes the unnormalized DFT, then returns only the positive frequency components
    // (indices 0 .. floor(N/2)) as tuples: (magnitude, frequency, phase),
    // with the same normalization as our power-of-2 FFT (i.e. we later divide by N).
    template <std::ranges::sized_range Sequence>
    std::vector<std::tuple<double, double, double>> bluestein(const Sequence& data) 
    {
        size_t N = data.size();
        // Choose M as the next power of 2 >= 2*N - 1.
        size_t M = next_power_of_two(2 * N - 1);

        // Allocate and initialize vectors A and B of length M.
        std::vector<std::complex<double>> A(M, 0), B(M, 0);

        // Compute chirp factors.
        // Define:
        //    A[n] = x[n] * exp(-i pi n^2/N)    for n = 0,...,N-1
        //    B[n] = exp(i pi n^2/N)            for n = 0,...,N-1,
        // and set B[M-n] = B[n] for n > 0 (to achieve the proper convolution).
        for (size_t n = 0; n < N; n++) {
            double angle = std::numbers::pi * (static_cast<double>(n * n)) / N;
            A[n] = data[n] * std::exp(std::complex<double>(0, -angle));
            B[n] = std::exp(std::complex<double>(0, angle));
            if (n > 0) {
                B[M - n] = B[n];  // set B[-n] = B[n]
            }
        }

        // Compute FFTs of A and B (length M).
        std::vector<std::complex<double>> A_fft = A;
        std::vector<std::complex<double>> B_fft = B;
        fft_recursive(A_fft);
        fft_recursive(B_fft);

        // Pointwise multiply: C = A_fft * B_fft.
        std::vector<std::complex<double>> C(M);
        for (size_t i = 0; i < M; i++) {
            C[i] = A_fft[i] * B_fft[i];
        }

        // Compute inverse FFT to get the convolution result.
        ifft(C);
        // NOTE: Do NOT multiply by M here.
        // Our ifft implementation already returns the correct convolution result.

        // Now compute the DFT result:
        //   X[k] = exp(-i pi k^2/N) * C[k]    for k = 0,...,N-1,
        // and then normalize by dividing by N.
        std::vector<std::complex<double>> X(N);
        for (size_t k = 0; k < N; k++) {
            double angle = -std::numbers::pi * (static_cast<double>(k * k)) / N;
            X[k] = C[k] * std::exp(std::complex<double>(0, angle));
            X[k] /= static_cast<double>(N);
        }

        // Extract only the positive frequency components (DC through Nyquist).
        size_t upper = (N % 2 == 0) ? (N / 2 + 1) : ((N + 1) / 2);
        std::vector<std::tuple<double, double, double>> components;
        for (size_t k = 0; k < upper; k++) {
            double magnitude = (k == 0 ? 1 : 2) * std::abs(X[k]);
            double frequency = static_cast<double>(k) / N;
            double phase = std::atan2(X[k].imag(), X[k].real());
            components.emplace_back(magnitude, frequency, phase);
        }

        // Sort the components by magnitude in descending order.
        std::sort(components.begin(), components.end(), [](const auto& a, const auto& b) { return get<0>(a) > get<0>(b); });

        return components;
    }

}
