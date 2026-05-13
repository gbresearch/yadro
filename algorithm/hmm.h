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
    // Fit a 2-state HMM to the signs of data, using the Baum–Welch algorithm with scaling for numerical stability
    struct hmm_2state_t
    {
        double A[2][2]{};   // transitions
        double B[2][2]{};   // emissions: B[i][0]=P(-1), B[i][1]=P(+1)
        double pi[2]{};     // initial distribution

        double sojourn[2]{};    // expected duration in each state (bars)
        double mean_sojourn{};  // stationary-weighted mean sojourn

        bool   valid{ false };    // set true if fit succeeded
    };

    // --------------------------------------------------------------------
    inline hmm_2state_t fit_hmm_on_signs(const std::ranges::sized_range auto& data,
        std::size_t max_iter = 50,
        double      tol = 1e-6)
    {
        hmm_2state_t m;

        // Build observation sequence: 0 = down/flat, 1 = up
        std::vector<int> obs;
        obs.reserve(data.size() - 1);
        for (std::size_t i = 1; i < data.size(); ++i) {
            obs.push_back(data[i] > data[i - 1] ? 1 : 0);
        }

        const std::size_t size = obs.size();
        if (size < 20)
            return m;

        // ---- Initialise parameters ------------------------------------
        m.A[0][0] = 0.9;  m.A[0][1] = 0.1;
        m.A[1][0] = 0.1;  m.A[1][1] = 0.9;
        m.B[0][0] = 0.7;  m.B[0][1] = 0.3;
        m.B[1][0] = 0.3;  m.B[1][1] = 0.7;
        m.pi[0] = 0.5;  m.pi[1] = 0.5;

        // Scaled forward–backward
        std::vector<std::array<double, 2>> alpha(size), beta(size), gamma(size);
        std::vector<std::array<std::array<double, 2>, 2>> xi(size - 1);
        std::vector<double> c(size); // scaling factors

        double prev_log_likelihood = -std::numeric_limits<double>::infinity();

        for (std::size_t iter = 0; iter < max_iter; ++iter)
        {
            // ---- Forward with scaling ----
            double sum0 = 0.0;
            for (int s = 0; s < 2; ++s) {
                alpha[0][s] = m.pi[s] * m.B[s][obs[0]];
                sum0 += alpha[0][s];
            }
            if (sum0 <= 0.0) return m;
            c[0] = 1.0 / sum0;
            for (int s = 0; s < 2; ++s)
                alpha[0][s] *= c[0];

            for (std::size_t t = 1; t < size; ++t) {
                double sum = 0.0;
                for (int j = 0; j < 2; ++j) {
                    double a = 0.0;
                    for (int i = 0; i < 2; ++i)
                        a += alpha[t - 1][i] * m.A[i][j];
                    alpha[t][j] = a * m.B[j][obs[t]];
                    sum += alpha[t][j];
                }
                if (sum <= 0.0) return m;
                c[t] = 1.0 / sum;
                for (int j = 0; j < 2; ++j)
                    alpha[t][j] *= c[t];
            }

            double log_lik = 0.0;
            for (std::size_t t = 0; t < size; ++t)
                log_lik -= std::log(c[t]);

            // ---- Backward with scaling ----
            beta[size - 1][0] = beta[size - 1][1] = 1.0 * c[size - 1];
            for (std::size_t t = size - 1; t-- > 0; ) {
                for (int i = 0; i < 2; ++i) {
                    double b = 0.0;
                    for (int j = 0; j < 2; ++j)
                        b += m.A[i][j] * m.B[j][obs[t + 1]] * beta[t + 1][j];
                    beta[t][i] = b * c[t];
                }
            }

            // ---- Gamma and Xi ----
            for (std::size_t t = 0; t < size; ++t) {
                double denom = 0.0;
                for (int i = 0; i < 2; ++i)
                    denom += alpha[t][i] * beta[t][i];
                if (denom <= 0.0) denom = 1e-300;

                for (int i = 0; i < 2; ++i)
                    gamma[t][i] = (alpha[t][i] * beta[t][i]) / denom;

                if (t < size - 1) {
                    double denom_xi = 0.0;
                    for (int i = 0; i < 2; ++i)
                        for (int j = 0; j < 2; ++j)
                            denom_xi += alpha[t][i] * m.A[i][j] *
                            m.B[j][obs[t + 1]] * beta[t + 1][j];
                    if (denom_xi <= 0.0) denom_xi = 1e-300;

                    for (int i = 0; i < 2; ++i)
                        for (int j = 0; j < 2; ++j)
                            xi[t][i][j] = alpha[t][i] * m.A[i][j] *
                            m.B[j][obs[t + 1]] * beta[t + 1][j] /
                            denom_xi;
                }
            }

            // ---- M-step ----
            for (int i = 0; i < 2; ++i)
                m.pi[i] = gamma[0][i];

            for (int i = 0; i < 2; ++i)
            {
                double sum_gamma = 0.0;
                double sum_xi[2]{ 0.0, 0.0 };

                for (std::size_t t = 0; t < size - 1; ++t) {
                    sum_gamma += gamma[t][i];
                    for (int j = 0; j < 2; ++j)
                        sum_xi[j] += xi[t][i][j];
                }

                const double denom = sum_gamma + 1e-300;
                for (int j = 0; j < 2; ++j)
                    m.A[i][j] = sum_xi[j] / denom;

                // Emissions
                double sum_g = 0.0, sum_g1 = 0.0;
                for (std::size_t t = 0; t < size; ++t) {
                    sum_g += gamma[t][i];
                    sum_g1 += gamma[t][i] * obs[t];
                }
                const double denom_e = sum_g + 1e-300;
                m.B[i][1] = sum_g1 / denom_e;
                m.B[i][0] = 1.0 - m.B[i][1];
            }

            if (std::abs(log_lik - prev_log_likelihood) < tol)
                break;
            prev_log_likelihood = log_lik;
        }

        // Sojourn times
        for (int i = 0; i < 2; ++i) {
            const double stay = std::clamp(m.A[i][i], 0.0, 0.999999);
            m.sojourn[i] = 1.0 / (1.0 - stay);
        }

        // Stationary distribution for 2-state chain
        const double denom = m.A[0][1] + m.A[1][0];
        if (denom <= 0.0)
            return m;

        const double pi0 = m.A[1][0] / denom;
        const double pi1 = 1.0 - pi0;

        m.mean_sojourn = pi0 * m.sojourn[0] + pi1 * m.sojourn[1];
        m.valid = std::isfinite(m.mean_sojourn) && m.mean_sojourn > 0.0;

        return m;
    }
}
