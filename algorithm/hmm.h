#pragma once

#include <vector>
#include <array>
#include <ranges>
#include <cmath>
#include <numeric>
#include <limits>
#include <algorithm>

namespace gb::yadro::algorithm
{
    /*
        Purpose:
        Fit a 2‑state HMM to the sign changes of a 1‑D time series.
        The implementation uses a scaled forward–backward recursion (Baum–Welch) to avoid underflow.
        After training it exposes the transition matrix A, emission matrix B, the initial distribution pi, 
        the expected sojourn time of each state, and a stationary‑weighted mean sojourn time.
    */
    
    struct hmm_2state_t
    {
        std::array<std::array<double, 2>, 2> A{};   // transitions
        std::array<std::array<double, 2>, 2> B{};   // emissions: B[i][0]=P(-1), B[i][1]=P(+1)
        std::array<double, 2> pi{};     // initial distribution

        std::array<double, 2> sojourn{};    // expected duration in each state (bars)
        double mean_sojourn{};  // stationary-weighted mean sojourn

        bool   valid{ false };    // set true if fit succeeded
    };

    // --------------------------------------------------------------------
    inline hmm_2state_t fit_hmm_on_signs(const std::ranges::sized_range auto& data,
        std::size_t max_iter = 50,
        double      tol = 1e-6)
    {
        hmm_2state_t m;
        constexpr double eps = std::numeric_limits<double>::epsilon();

        const std::size_t data_size = std::ranges::size(data);

        if (data_size < 20) // too few data points to fit reliably
            return m;

        // Build observation sequence: 0 = down/flat, 1 = up
        std::vector<int> obs;
        obs.reserve(data_size - 1);                                     // <-- reserve once

        for (auto it_prev = std::ranges::begin(data), it_curr = std::ranges::next(it_prev); 
            it_curr != std::ranges::end(data); ++it_prev, ++it_curr) {
            obs.push_back(*it_curr > *it_prev ? 1 : 0);
        }

        // ---- Initialise parameters ------------------------------------
        m.A = { {{0.9, 0.1}, {0.1, 0.9}} };
        m.B = { {{0.7, 0.3}, {0.3, 0.7}} };
        m.pi = { 0.5, 0.5 };

        const std::size_t size = obs.size();

        // Scaled forward–backward
        std::vector<std::array<double, 2>> alpha(size), beta(size), gamma(size);
        std::vector<std::array<std::array<double, 2>, 2>> xi(size - 1);
        std::vector<double> c(size); // scaling factors

        auto prev_log_likelihood = -std::numeric_limits<double>::infinity();

        for (std::size_t iter = 0; iter < max_iter; ++iter)
        {
            // ---- Forward with scaling ----
            auto sum0 = 0.0;
            for (auto s = 0; s < 2; ++s) {
                alpha[0][s] = m.pi[s] * m.B[s][obs[0]];
                sum0 += alpha[0][s];
            }
            if (sum0 <= 0.0) return m;
            c[0] = 1.0 / sum0;
            for (auto s = 0; s < 2; ++s)
                alpha[0][s] *= c[0];

            for (std::size_t t = 1; t < size; ++t) {
                auto sum = 0.0;
                for (auto j = 0; j < 2; ++j) {
                    auto a = 0.0;
                    for (auto i = 0; i < 2; ++i)
                        a += alpha[t - 1][i] * m.A[i][j];
                    alpha[t][j] = a * m.B[j][obs[t]];
                    sum += alpha[t][j];
                }
                if (sum <= 0.0) return m;
                c[t] = 1.0 / sum;
                for (auto j = 0; j < 2; ++j)
                    alpha[t][j] *= c[t];
            }

            /* log‑likelihood -------------------------------------------- */
            auto log_lik = 0.0;
            for (auto ci : c) log_lik -= std::log(ci);

            // ---- Backward with scaling ----
            beta[size - 1] = { c[size - 1], c[size - 1] };

            for (std::size_t t = size - 1; t-- > 0; ) {
                for (auto i = 0; i < 2; ++i) {
                    auto b = 0.0;
                    for (auto j = 0; j < 2; ++j)
                        b += m.A[i][j] * m.B[j][obs[t + 1]] * beta[t + 1][j];
                    beta[t][i] = b * c[t];
                }
            }

            // ---- Gamma and Xi ----
            for (std::size_t t = 0; t < size; ++t) {    
                auto denom = 0.0;
                for (auto i = 0; i < 2; ++i)
                    denom += alpha[t][i] * beta[t][i];
                denom = std::max(denom, eps); // avoid division by zero

                for (auto i = 0; i < 2; ++i)
                    gamma[t][i] = (alpha[t][i] * beta[t][i]) / denom;

                if (t < size - 1) {
                    auto denom_xi = 0.0;
                    for (auto i = 0; i < 2; ++i)
                        for (auto j = 0; j < 2; ++j)
                            denom_xi += alpha[t][i] * m.A[i][j] *
                            m.B[j][obs[t + 1]] * beta[t + 1][j];
                    denom_xi = std::max(denom_xi, eps); // avoid division by zero

                    for (auto i = 0; i < 2; ++i)
                        for (auto j = 0; j < 2; ++j)
                            xi[t][i][j] = alpha[t][i] * m.A[i][j] *
                            m.B[j][obs[t + 1]] * beta[t + 1][j] /
                            denom_xi;
                }
            }

            // ---- M-step ----
            for (auto i = 0; i < 2; ++i)
                m.pi[i] = gamma[0][i];

            for (auto i = 0; i < 2; ++i)
            {
                auto sum_gamma = 0.0;
                auto sum_xi = std::array<double, 2>{ 0.0, 0.0 };

                for (std::size_t t = 0; t < size - 1; ++t) {
                    sum_gamma += gamma[t][i];
                    for (auto j = 0; j < 2; ++j)
                        sum_xi[j] += xi[t][i][j];
                }

                const auto denom = sum_gamma + 1e-300;
                for (auto j = 0; j < 2; ++j)
                    m.A[i][j] = sum_xi[j] / denom;

                // Emissions
                auto sum_g = 0.0, sum_g1 = 0.0;
                for (std::size_t t = 0; t < size; ++t) {
                    sum_g += gamma[t][i];
                    sum_g1 += gamma[t][i] * obs[t];
                }
                const auto denom_e = std::max(sum_g, eps);
                m.B[i][1] = sum_g1 / denom_e;
                m.B[i][0] = 1.0 - m.B[i][1];
            }

            if (std::abs(log_lik - prev_log_likelihood) < tol)
                break;
            prev_log_likelihood = log_lik;
        }

        // Sojourn times
        for (auto i = 0; i < 2; ++i) {
            const auto stay = std::clamp(m.A[i][i], 0.0, 1.0 - eps);
            m.sojourn[i] = 1.0 / (1.0 - stay);
        }

        // Stationary distribution for 2-state chain
        const auto denom = m.A[0][1] + m.A[1][0];
        if (denom > eps) {
            const auto pi0 = m.A[1][0] / denom;
            const auto pi1 = 1.0 - pi0;
            m.mean_sojourn = pi0 * m.sojourn[0] + pi1 * m.sojourn[1];
        }
        m.valid = std::isfinite(m.mean_sojourn) && m.mean_sojourn > 0.0;

        return m;
    }
}
