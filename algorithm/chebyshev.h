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

// NOTE: the former `cheb1` (non-functional placeholder IIR filter) and `cheb_ls`
// (an unused duplicate of `cheb`) namespaces were removed — see CODE_REVIEW.md
// findings 5.A / 5.B. `cheb` is the single supported Chebyshev least-squares
// smoothing implementation.

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
