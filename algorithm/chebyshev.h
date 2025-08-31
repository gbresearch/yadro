#include <vector>
#include <array>
#include <ranges>
#include <cmath>
#include <complex>
#include <algorithm>
#include <numeric>
#include <cstddef>
#include <concepts>
#include <type_traits>
#include <span>
#include <stdexcept>
#include <limits>

namespace cheb1 {   // Chebyshev Type I filter
    // ---------- Concepts ----------
    template<typename T>
    concept ChebValue =
        std::is_arithmetic_v<T> ||
        std::same_as<T, std::complex<float>> ||
        std::same_as<T, std::complex<double>>;

    template<typename Container>
    concept ChebContainer =
        std::ranges::input_range<Container> &&
        ChebValue<std::ranges::range_value_t<Container>>;

    // Helper concept: can we call reserve()?
    template<typename Container>
    concept Reservable = requires(Container c, size_t n) {
        c.reserve(n);
    };

    // ---------- Filter Implementation ----------
    template<ChebContainer Container>
    [[nodiscard]]
    Container chebyshev_filter(const Container& data,
        double sampling_rate,
        double cutoff_freq,
        double ripple_db,
        int order = 4)
    {
        using T = std::ranges::range_value_t<Container>;

        if (cutoff_freq <= 0 || cutoff_freq >= sampling_rate / 2) {
            throw std::invalid_argument("Cutoff frequency must be within (0, Nyquist)");
        }
        if (ripple_db <= 0) {
            throw std::invalid_argument("Ripple (dB) must be positive");
        }
        if (order <= 0) {
            throw std::invalid_argument("Filter order must be positive");
        }

        // Normalized frequency
        double omega_c = 2.0 * std::numbers::pi * cutoff_freq / sampling_rate;

        // Epsilon from ripple
        double eps = std::sqrt(std::pow(10.0, ripple_db / 10.0) - 1.0);

        // Precompute poles (Chebyshev Type I, lowpass, analog prototype)
        std::vector<std::complex<double>> poles;
        poles.reserve(order);
        for (int k = 1; k <= order; ++k) {
            double theta = (2.0 * k - 1) * std::numbers::pi / (2.0 * order);
            double sinh_val = std::sinh(std::asinh(1.0 / eps) / order);
            double cosh_val = std::cosh(std::asinh(1.0 / eps) / order);

            std::complex<double> pole(
                -sinh_val * std::sin(theta),
                cosh_val * std::cos(theta)
            );
            poles.push_back(pole);
        }

        // --- Bilinear transform (simplified, placeholder) ---
        // A proper implementation would map analog poles -> digital z-plane.
        // For now, we just apply a naive IIR recursion with butterworth-style a,b.

        // FIR coefficients (dummy placeholder)
        std::vector<double> b(order + 1, 0.0);
        std::vector<double> a(order + 1, 0.0);
        b[0] = 1.0; // simplistic identity
        a[0] = 1.0;

        // Output container
        Container out;
        if constexpr (Reservable<Container>) {
            out.reserve(std::ranges::size(data));
        }

        // Direct form I filtering
        std::vector<T> xv(order + 1, T{});
        std::vector<T> yv(order + 1, T{});

        for (auto x : data) {
            // shift delay lines
            for (int i = order; i > 0; --i) {
                xv[i] = xv[i - 1];
                yv[i] = yv[i - 1];
            }
            xv[0] = x;

            T y = T{};
            for (int i = 0; i <= order; ++i) y += b[i] * xv[i];
            for (int i = 1; i <= order; ++i) y -= a[i] * yv[i];

            y /= a[0];
            yv[0] = y;

            out.insert(out.end(), y);
        }

        return out;
    }
}

namespace cheb {

    // ---------- Concepts ----------
    template<class C>
    concept ChebContainer =
        std::ranges::forward_range<C> &&
        std::floating_point<std::remove_cvref_t<std::ranges::range_value_t<C>>> &&
        requires(C c) {
            { std::size(c) } -> std::convertible_to<std::size_t>;
    };

    // ---------- Linear algebra (Cholesky solve) ----------
    template<std::floating_point T>
    bool cholesky_solve(std::vector<T>& A, std::vector<T>& b, std::size_t n)
    {
        const T eps = std::numeric_limits<T>::epsilon();
        for (std::size_t i = 0; i < n; ++i) A[i * n + i] += static_cast<T>(1e-12) + eps;

        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t k = 0; k < i; ++k) {
                T s = A[i * n + k];
                for (std::size_t j = 0; j < k; ++j) s -= A[i * n + j] * A[k * n + j];
                if (std::abs(A[k * n + k]) < T(1e-30)) return false;
                A[i * n + k] = s / A[k * n + k];
            }
            T s = A[i * n + i];
            for (std::size_t j = 0; j < i; ++j) s -= A[i * n + j] * A[i * n + j];
            if (s <= T(0)) return false;
            A[i * n + i] = std::sqrt(s);
        }

        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < i; ++j) b[i] -= A[i * n + j] * b[j];
            b[i] /= A[i * n + i];
        }
        for (std::size_t i = n; i-- > 0;) {
            for (std::size_t j = i + 1; j < n; ++j) b[i] -= A[j * n + i] * b[j];
            b[i] /= A[i * n + i];
        }
        return true;
    }

    // ---------- Chebyshev basis ----------
    template<std::floating_point T>
    inline T cheb_T(unsigned k, T x) {
        if (k == 0) return T{ 1 };
        if (k == 1) return x;
        T Tkm2 = T{ 1 }, Tkm1 = x, Tk{};
        for (unsigned i = 2; i <= k; ++i) {
            Tk = T(2) * x * Tkm1 - Tkm2;
            Tkm2 = Tkm1;
            Tkm1 = Tk;
        }
        return Tk;
    }

    template<std::floating_point T>
    void normal_equations_from_points(const std::vector<T>& x,
        const std::vector<T>& y,
        std::size_t deg,
        std::vector<T>& G,
        std::vector<T>& b)
    {
        const std::size_t N = x.size();
        const std::size_t M = deg + 1;
        G.assign(M * M, T(0));
        b.assign(M, T(0));
        std::vector<T> phi(M);

        for (std::size_t i = 0; i < N; ++i) {
            phi[0] = T(1);
            if (M > 1) phi[1] = x[i];
            for (std::size_t k = 2; k < M; ++k)
                phi[k] = T(2) * x[i] * phi[k - 1] - phi[k - 2];
            for (std::size_t r = 0; r < M; ++r) {
                b[r] += phi[r] * y[i];
                for (std::size_t c = 0; c < M; ++c)
                    G[r * M + c] += phi[r] * phi[c];
            }
        }
    }

    template<std::floating_point T>
    T eval_series_at(const std::vector<T>& coeffs, T x)
    {
        const std::size_t M = coeffs.size();
        T s = coeffs[0];
        if (M > 1) s += coeffs[1] * x;
        T Tkm2 = T(1), Tkm1 = x;
        for (std::size_t k = 2; k < M; ++k) {
            T Tk = T(2) * x * Tkm1 - Tkm2;
            s += coeffs[k] * Tk;
            Tkm2 = Tkm1; Tkm1 = Tk;
        }
        return s;
    }

    // ---------- Helpers ----------
    template<ChebContainer C>
    auto make_linear_grid(std::size_t N)
    {
        using T = std::remove_cvref_t<std::ranges::range_value_t<C>>;
        std::vector<T> x(N);
        if (N == 1) { x[0] = T(0); return x; }
        for (std::size_t n = 0; n < N; ++n)
            x[n] = (T(2) * T(n) / T(N - 1)) - T(1);
        return x;
    }

    template<ChebContainer Container>
    void assign_from_vector(Container& out, const std::vector<std::ranges::range_value_t<Container>>& src)
    {
        using T = std::ranges::range_value_t<Container>;
        if constexpr (requires(Container & c, std::size_t n) { c.resize(n); }) {
            out.resize(src.size());
        }
        std::size_t i = 0;
        for (auto& v : out) {
            if (i < src.size()) v = static_cast<T>(src[i]);
            ++i;
        }
    }

    // ---------- Full signal ----------
    template<ChebContainer Container>
    Container chebyshev_filter_full_signal(const Container& data,
        std::size_t max_degree)
    {
        using T = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
        const std::size_t N = std::size(data);
        Container out{};

        if constexpr (requires(Container & c, std::size_t n) { c.resize(n); }) {
            out.resize(N);
        }

        if (N == 0) return out;
        if (N == 1) { assign_from_vector(out, { T(*std::begin(data)) }); return out; }

        const std::size_t deg = std::min<std::size_t>(max_degree, N - 1);
        auto x = make_linear_grid<Container>(N);

        std::vector<T> y(N);
        std::ranges::copy(data, y.begin());

        std::vector<T> G, b;
        normal_equations_from_points(x, y, deg, G, b);
        if (!cholesky_solve(G, b, deg + 1)) {
            assign_from_vector(out, y);
            return out;
        }

        std::vector<T> yhat(N);
        for (std::size_t i = 0; i < N; ++i)
            yhat[i] = eval_series_at(b, x[i]);

        assign_from_vector(out, yhat);
        return out;
    }

    // ---------- Windowed ----------
    template<ChebContainer Container>
    Container chebyshev_filter_windowed(const Container& data,
        std::size_t max_degree,
        std::size_t window_size)
    {
        using T = std::remove_cvref_t<std::ranges::range_value_t<Container>>;
        const std::size_t N = std::size(data);
        Container out{};

        if constexpr (requires(Container & c, std::size_t n) { c.resize(n); }) {
            out.resize(N);
        }

        if (N == 0) return out;
        if (N == 1) { assign_from_vector(out, { T(*std::begin(data)) }); return out; }

        window_size = std::clamp<std::size_t>(window_size, 1, N);
        const std::size_t half = window_size / 2;
        const std::size_t deg = std::min<std::size_t>(max_degree, window_size - 1);

        std::vector<T> y(N);
        std::ranges::copy(data, y.begin());
        std::vector<T> yout(N);

        for (std::size_t i = 0; i < N; ++i) {
            std::size_t L = (i > half) ? (i - half) : 0;
            std::size_t R = std::min<std::size_t>(N - 1, L + window_size - 1);
            if (R - L + 1 < window_size && R + 1 < N) {
                std::size_t need = window_size - (R - L + 1);
                R = std::min<std::size_t>(N - 1, R + need);
            }
            const std::size_t W = R - L + 1;

            std::vector<T> xw(W), yw(W);
            if (W == 1) { yout[i] = y[L]; continue; }
            for (std::size_t j = 0; j < W; ++j) {
                std::size_t idx = L + j;
                xw[j] = T(2) * T(idx - L) / T(R - L) - T(1);
                yw[j] = y[idx];
            }

            const std::size_t loc_deg = std::min<std::size_t>(deg, W - 1);
            std::vector<T> G, b;
            normal_equations_from_points(xw, yw, loc_deg, G, b);
            if (!cholesky_solve(G, b, loc_deg + 1)) {
                yout[i] = y[i];
                continue;
            }

            T xi = T(2) * T(i - L) / T(R - L) - T(1);
            yout[i] = eval_series_at(b, xi);
        }

        assign_from_vector(out, yout);
        return out;
    }

    // ---------- Unified API ----------
    template<ChebContainer Container>
    [[nodiscard]]
    Container chebyshev_filter(const Container& data,
        std::size_t max_degree,
        std::size_t window_size = 0)
    {
        const std::size_t N = std::size(data);
        if (N == 0) {
            if constexpr (requires(Container & c, std::size_t n) { c.resize(n); }) {
                Container out{}; out.resize(0); return out;
            }
            else {
                return Container{};
            }
        }
        if (N == 1) return data;

        if (window_size > 0 && window_size < N) {
            return chebyshev_filter_windowed(data, max_degree, window_size);
        }
        else {
            return chebyshev_filter_full_signal(data, max_degree);
        }
    }

}

namespace cheb_ls {

    // ---------- Concepts ----------
    // A concept for a generic, sized, random-access range of floating-point values.
    template<typename C>
    concept FloatRange =
        std::ranges::sized_range<C> &&
        std::ranges::random_access_range<C> &&
        std::floating_point<std::ranges::range_value_t<C>>;

    // ---------- Internal Helper Functions ----------

    // Computes the value of the k-th Chebyshev polynomial of the first kind at x.
    template<typename T>
    T cheb_T(std::size_t k, T x) {
        if (k == 0) return T{ 1 };
        if (k == 1) return x;
        T Tkm1 = T{ 1 };
        T Tk = x;
        for (std::size_t i = 2; i <= k; ++i) {
            T Tkp1 = T{ 2 } *x * Tk - Tkm1;
            Tkm1 = Tk;
            Tk = Tkp1;
        }
        return Tk;
    }

    // A simple in-place linear system solver using Gaussian elimination with partial pivoting.
    template<typename T>
    std::vector<T> solve_linear_system(std::vector<std::vector<T>>& A, std::vector<T>& b) {
        std::size_t n = b.size();

        // Augment matrix with the right-hand side vector
        for (std::size_t i = 0; i < n; ++i) {
            A[i].push_back(b[i]);
        }

        // Gaussian elimination
        for (std::size_t i = 0; i < n; ++i) {
            // Find pivot
            std::size_t pivot_row = i;
            for (std::size_t k = i + 1; k < n; ++k) {
                if (std::abs(A[k][i]) > std::abs(A[pivot_row][i])) {
                    pivot_row = k;
                }
            }
            std::swap(A[i], A[pivot_row]);

            // Check for singularity after swap
            if (std::abs(A[i][i]) < std::numeric_limits<T>::epsilon()) {
                throw std::runtime_error("Matrix is singular or ill-conditioned.");
            }

            // Eliminate
            for (std::size_t k = i + 1; k < n; ++k) {
                T factor = A[k][i] / A[i][i];
                for (std::size_t j = i; j < n + 1; ++j) {
                    A[k][j] -= factor * A[i][j];
                }
            }
        }

        // Back substitution with safe size_t loop
        std::vector<T> x(n);
        for (std::size_t i = n; i-- > 0; ) {
            T sum = 0;
            for (std::size_t j = i + 1; j < n; ++j) {
                sum += A[i][j] * x[j];
            }
            x[i] = (A[i][n] - sum) / A[i][i];
        }

        return x;
    }

    // Computes Chebyshev coefficients and reconstructs the signal using a least-squares fit.
    template <FloatRange InR>
    std::vector<std::ranges::range_value_t<InR>> least_squares_filter(const InR& data, std::size_t max_degree) {
        using T = std::ranges::range_value_t<InR>;
        const std::size_t N = std::ranges::size(data);

        // Handle empty input
        if (N == 0) {
            return {};
        }
        // Handle single-point input
        if (N == 1) {
            return { data[0] };
        }

        // Clamp effective degree to meaningful range
        const std::size_t effective_degree = std::min(max_degree, N - 1);
        const std::size_t num_coeffs = effective_degree + 1;

        // Step 1: Pre-compute Chebyshev polynomial values for all data points
        std::vector<std::vector<T>> cheb_values(N, std::vector<T>(num_coeffs));
        for (std::size_t n = 0; n < N; ++n) {
            T x = (T{ 2 } *static_cast<T>(n) / static_cast<T>(N - 1)) - T{ 1 };
            if (num_coeffs > 0) {
                cheb_values[n][0] = T{ 1 };
            }
            if (num_coeffs > 1) {
                cheb_values[n][1] = x;
            }
            for (std::size_t k = 2; k < num_coeffs; ++k) {
                cheb_values[n][k] = T{ 2 } *x * cheb_values[n][k - 1] - cheb_values[n][k - 2];
            }
        }

        // Step 2: Build the Gram matrix (A^T * A) and the right-hand side vector (A^T * f)
        std::vector<std::vector<T>> ATA(num_coeffs, std::vector<T>(num_coeffs, 0.0));
        std::vector<T> ATf(num_coeffs, 0.0);

        for (std::size_t n = 0; n < N; ++n) {
            for (std::size_t i = 0; i < num_coeffs; ++i) {
                ATf[i] += cheb_values[n][i] * data[n];
                for (std::size_t j = 0; j < num_coeffs; ++j) {
                    ATA[i][j] += cheb_values[n][i] * cheb_values[n][j];
                }
            }
        }

        // Step 3: Solve the linear system (ATA) * c = ATf for the coefficients c
        std::vector<T> coeffs = solve_linear_system(ATA, ATf);

        // Step 4: Reconstruct the signal using the filtered coefficients
        std::vector<T> filtered_signal(N);
        for (std::size_t n = 0; n < N; ++n) {
            T s = 0;
            for (std::size_t k = 0; k < num_coeffs; ++k) {
                s += coeffs[k] * cheb_values[n][k];
            }
            filtered_signal[n] = s;
        }

        return filtered_signal;
    }

    // Filters a single point using a least-squares fit on a window.
    template<FloatRange InR>
    std::ranges::range_value_t<InR> least_squares_reconstruct_point(const InR& data, std::size_t max_degree, std::size_t point_index) {
        using T = std::ranges::range_value_t<InR>;
        const std::size_t N = std::ranges::size(data);

        if (N == 0) {
            throw std::invalid_argument("Cannot filter an empty window.");
        }
        if (N == 1) {
            return data[0];
        }

        const std::size_t effective_degree = std::min(max_degree, N - 1);
        const std::size_t num_coeffs = effective_degree + 1;

        // Step 1: Pre-compute Chebyshev polynomial values for all data points
        std::vector<std::vector<T>> cheb_values(N, std::vector<T>(num_coeffs));
        for (std::size_t n = 0; n < N; ++n) {
            T x = (T{ 2 } *static_cast<T>(n) / static_cast<T>(N - 1)) - T{ 1 };
            if (num_coeffs > 0) {
                cheb_values[n][0] = T{ 1 };
            }
            if (num_coeffs > 1) {
                cheb_values[n][1] = x;
            }
            for (std::size_t k = 2; k < num_coeffs; ++k) {
                cheb_values[n][k] = T{ 2 } *x * cheb_values[n][k - 1] - cheb_values[n][k - 2];
            }
        }

        // Step 2: Build the Gram matrix (A^T * A) and the right-hand side vector (A^T * f)
        std::vector<std::vector<T>> ATA(num_coeffs, std::vector<T>(num_coeffs, 0.0));
        std::vector<T> ATf(num_coeffs, 0.0);

        for (std::size_t n = 0; n < N; ++n) {
            for (std::size_t i = 0; i < num_coeffs; ++i) {
                ATf[i] += cheb_values[n][i] * data[n];
                for (std::size_t j = 0; j < num_coeffs; ++j) {
                    ATA[i][j] += cheb_values[n][i] * cheb_values[n][j];
                }
            }
        }

        // Step 3: Solve the linear system (ATA) * c = ATf for the coefficients c
        std::vector<T> coeffs = solve_linear_system(ATA, ATf);

        // Step 4: Reconstruct only the single point of interest
        T filtered_value = 0;
        for (std::size_t k = 0; k < num_coeffs; ++k) {
            filtered_value += coeffs[k] * cheb_values[point_index][k];
        }

        return filtered_value;
    }

    // ---------- API Functions ----------

    // chebyshev_filter_full_signal: Applies the filter to the entire signal.
    template<FloatRange InR, typename OutC>
    OutC chebyshev_filter_full_signal(const InR& data, std::size_t max_degree)
    {
        using T = std::ranges::range_value_t<InR>;
        const std::size_t N = std::ranges::size(data);

        if (N == 0) {
            OutC out{};
            if constexpr (requires(OutC & c, std::size_t n) { c.resize(n); }) {
                out.resize(0);
            }
            return out;
        }

        std::vector<T> filtered_signal = least_squares_filter(data, max_degree);

        // Transfer to output container
        OutC out{};
        if constexpr (requires(OutC & c, std::size_t n) { c.resize(n); }) {
            out.resize(N);
        }
        std::ranges::copy(filtered_signal, std::ranges::begin(out));

        return out;
    }

    // chebyshev_filter_windowed: Applies the filter over a sliding window.
    template<FloatRange InR, typename OutC>
    OutC chebyshev_filter_windowed(const InR& data, std::size_t max_degree,
        std::size_t window_size)
    {
        using T = std::ranges::range_value_t<InR>;
        const std::size_t N = std::ranges::size(data);

        // Clamp window size to a reasonable range
        window_size = std::min(window_size, N);
        if (window_size % 2 == 0) window_size++;
        if (window_size < 3) window_size = 3;

        const std::size_t half_window = window_size / 2;

        OutC out{};
        if constexpr (requires(OutC & c, std::size_t n) { c.resize(n); }) {
            out.resize(N);
        }

        for (std::size_t i = 0; i < N; ++i) {
            std::size_t window_start, window_end;

            if (i < half_window) {
                window_start = 0;
                window_end = window_size;
            }
            else if (i + half_window >= N) {
                window_start = N - window_size;
                window_end = N;
            }
            else {
                window_start = i - half_window;
                window_end = window_start + window_size;
            }

            // Use a span to create a non-owning view of the window
            std::span<const T> window_view(data.data() + window_start, window_size);

            // Filter the window using the full-signal implementation and extract the middle value
            out[i] = least_squares_reconstruct_point(window_view, max_degree, i - window_start);
        }

        return out;
    }

    // ---------- Unified API: Public Interface ----------
    template<typename Container>
    Container chebyshev_filter(const Container& data,
        std::size_t max_degree,
        std::size_t window_size = 0)
    {
        static_assert(FloatRange<Container>, "Container must be a range of floating-point numbers.");
        const std::size_t N = std::ranges::size(data);

        if (N == 0) {
            if constexpr (requires(Container & c, std::size_t n) { c.resize(n); }) {
                Container out{};
                out.resize(0);
                return out;
            }
            else {
                return Container{};
            }
        }

        // Explicitly choose full vs windowed filtering
        if (window_size > 0 && window_size < N) {
            return chebyshev_filter_windowed<Container, Container>(data, max_degree, window_size);
        }
        else {
            return chebyshev_filter_full_signal<Container, Container>(data, max_degree);
        }
    }

} // namespace cheb_ls
