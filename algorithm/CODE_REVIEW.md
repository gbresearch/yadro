# Code Review — `yadro/algorithm` facilities

Scope: all algorithm units referenced by `vs/yadro.vcxproj`.

Headers: `gbalgorithm.h` (umbrella), `statistics.h`, `regression_analysis.h`,
`genetic_optimization.h`, `discrete_transform.h`, `chebyshev.h`, `student.h`,
`mackinnon.h`, `adf_test.h`, `hmm.h`.
Sources: `mackinnon.cpp`, `adf_test.cpp`.

Reviewed in six functional groups. A global invariant confirmed up front:
**`util::gbassert` always throws — it is not compiled out in release builds**
(`util/gberror.h`), so every input guard in these algorithms is live in
production. `util::almost_equal` uses an *absolute* tolerance defaulting to
machine epsilon (~2.2e-16).

---

## Group 1 — descriptive stats & filter functions (`statistics.h`)

Math verified by boundary constraints: `sigmoid1090` (`f(x10)=0.1`,
`f(x90)=0.9`), `gauss_filter` (`f(center)=1`, 50%-points = 0.5; constant is
`ln2`), `sigmoid_filter` (constant is `ln3`), `mean_stddev` (textbook
single-pass Welford). All correct.

- **1.1 (Low)** `sigmoid1090` guards only `x10 != x90`, not ordering. Reversed
  inputs silently invert the 10/90 semantics. Other filters assert ordering.
- **1.2 (Very low)** `mean_stddev` could `sqrt` a tiny negative variance under
  pathological cancellation; a `std::max(variance, 0.0)` clamp would harden it.
- **1.3 (Info)** the `sigmoid1090` equality guard is effectively an exact-equality
  check (absolute machine-epsilon tolerance), not a "well separated" check.

## Group 2 — hypothesis testing & distributions (`statistics.h`, `student.h`)

`student.h` core numerics (continued-fraction `incomplete_beta` /
`incomplete_gamma`, `t_cdf`, `chi2_cdf`) are sound and reference-validated by
`student_test`.

- **2.A (Medium)** `shapiro_wilk_test` returns an inverted/invalid p-value
  (`p = 1 − exp(−1.2725·(W−1)²)` → near-normal `W≈1` yields `p≈0`). The method
  is actually **Shapiro–Francia** (coefficients `a=m/√Σm²` ignore the
  order-statistic covariance matrix). The test only pins the (wrong) output
  (`p≈0.0005` for a near-normal sample) rather than validating against R/scipy.
- **2.B (Low–Med)** `student_t` root-finding brackets too narrow: `t∈[0,10]`
  wrong for `df=1` (n=2, true ≈12.706); `χ²∈[0,1000]` wrong for `n≳1000`. Both
  silently clamp.
- **2.C (Low)** `student_t` uses the unstable `Σx²−n·mean²` variance formula,
  inconsistent with Welford in `mean_stddev`.
- **2.D (Low; Win32)** `kolmogorov_smirnov_test` computes `n1*n2` in `size_t`
  before the cast to double → overflow on the 32-bit build above ~65k samples.
- **2.E (Low)** KS test divides by `(n1+n2)` with no empty-input guard.

## Group 3 — unit-root tests (`mackinnon.cpp`, `adf_test.cpp`)

OLS/Cholesky pipeline and ADF regression alignment are correct.

- **3.A (Medium)** The "accurate" `mackinnon_pvalue` (+ `critical_value`,
  `norm_ppf`, `norm_cdf`) is dead relative to internal use (`adfuller` calls
  `mackinnon_pvalue_fast`); `norm_cdf` is never called at all; the `large[]`
  coefficient arrays are never referenced; p-dependence enters only via an ad-hoc
  `z*(1+0.5/T)` term that is not MacKinnon's response surface.
- **3.B (Low)** Header accuracy claim ("±0.02 of statsmodels/R for n>25") is
  unsubstantiated for the actually-used `_fast` path; the ADF test checks only
  p-value direction.
- **3.C (Low–Med)** `adfuller` minimum-length guard `>= 5 + max_lag_use` ignores
  `trend_cols`; for CONSTANT_TREND at minimum length + higher lags `n_eff−k ≤ 0`
  → `s2` divide-by-zero / `0/0`. Masked (no crash) but the guard is misleading.
- **3.D (Low)** `mackinnon_pvalue` positive-stat branch has a discontinuity
  (jumps to 0.5). Cosmetic (function unused).

## Group 4 — time-series modeling (`hmm.h`)

`fit_hmm_on_signs` is a correct scaled Baum–Welch; γ/ξ renormalization makes the
result independent of β-init scaling; early-exit invalidity guards are sound.

- **4.A (Low)** No unit-test coverage.
- **4.B (Low)** Convergence test uses an *absolute* total-log-likelihood
  tolerance (length-dependent); long series always run `max_iter`.

## Group 5 — transforms & filters (`discrete_transform.h`, `chebyshev.h`)

FFT / Bluestein / `decompose_signal` are correct and reference-validated.

- **5.A (Medium)** `cheb1::chebyshev_filter` is a non-functional placeholder
  (computes poles, discards them, applies identity `b={1,…}`, `a={1,…}`). Unused.
- **5.B (Low–Med)** Three Chebyshev implementations; only `cheb::` is used.
  `cheb_ls::` is a full duplicate (and diverges on error handling).
- **5.C (Low)** `cheb_ls::chebyshev_filter_windowed` calls `data.data()` but its
  concept only requires random-access, not contiguous storage.
- **5.D (Low)** `bluestein_dft({})` infinite-loops: `2*N−1` underflows `size_t`
  and `next_power_of_two` overflows to 0. No empty-input guard.

## Group 6 — optimization (`genetic_optimization.h`, `regression_analysis.h`)

Threading (per-thread RNG, disjoint-index parallel breeding, drain-before-rethrow,
dual-mutex snapshots), memo lazy-init double-checked locking, serialization
completeness (memo intentionally excluded), and integer-mutation overflow
handling are all sound.

- **6.A (Medium)** `container_range` two-level mutation gating is detected via a
  **private** member in a requires-expression (`requires { w.per_element_mut_prob; }`),
  which under standard access-checking is unsatisfied → the documented outer-gate
  bypass is silently disabled (effective rate becomes `mutation_rate ×
  per_element_mut_prob`). `describe_wrapper(container_range)` likewise reads three
  private members and only compiles because `container_range` is never
  instantiated in a real run. **Deeper still:** `container_range::value_type` and
  `element_type` were private too, so `container_range` never satisfied the
  `GeneticWrapper` concept — it could not have been passed to `make_optimizer` at
  all. Untested (only a `static_assert` on three-way comparability).
- **6.B (Low–Med)** `max_tries` is compared against `total_eval_requests_` which
  counts cache hits, but the docs say "cache misses only". Behaviour/doc mismatch
  (also skews the stagnation limit).
- **6.C (Info)** No deterministic-seed entry point (reproducibility).

---

## Findings & resolution tracker

The **Resolution** column is updated after every round of fixes.

| ID | Facility | Sev | Issue | Resolution |
|----|----------|-----|-------|------------|
| 6.A | genetic_optimization | Med | container_range gating keyed on a private member (requires-expr); `value_type`/`element_type` also private so it never satisfied GeneticWrapper; latent private access in `describe_wrapper`; untested | **Fixed & verified (R1):** made `value_type`/`element_type` public (now satisfies GeneticWrapper); added public `container_wrapper_tag` + `get_element_wrapper()` / `get_per_element_mut_prob()`; `apply_mutation` detects via the tag; `describe_wrapper` uses getters; new test `container_range_mutation_gating` passes (Debug\|x64, 175/175 tests, 0 warnings) |
| 2.A | statistics (shapiro) | Med | inverted/invalid p-value; method is Shapiro–Francia not Shapiro–Wilk; test pins wrong output | **Fixed & verified (R1/R2):** renamed `shapiro_wilk_test` → `shapiro_francia_test` (R1); replaced inverted heuristic with Royston (1993) normalization → near-normal p≈0.99 (was ≈5e-4), skewed sample p≈1e-6 (R2). Test rewritten to assert correct direction + pinned regression values; build verified |
| 3.A | mackinnon | Med | dead `mackinnon_pvalue` chain; unused `large[]`; not the real surface | **Fixed (R1):** deleted `mackinnon_pvalue`, `critical_value`, `norm_ppf`, `norm_cdf`, `Coefficients`, `MackinnonConstants`, `MacKinnon` namespace, and the `mackinnon_pvalue` declaration |
| 5.A | chebyshev (cheb1) | Med | identity-passthrough placeholder filter, unused | **Fixed (R1):** deleted `cheb1` namespace |
| 5.B | chebyshev (cheb_ls) | Low–Med | duplicate unused implementation | **Fixed (R1):** deleted `cheb_ls` namespace; `cheb` is the single implementation |
| 6.B | genetic_optimization | Low–Med | `max_tries` counts cache hits vs docs "misses only" | **Fixed & verified (R2):** `total_attempts()` now returns `fn_call_count_` (actual evaluations); budget excludes cache hits per the contract; GA tests still pass |
| 2.B | student_t | Low–Med | root-finding brackets too narrow (df=1, n≳1000) | **Fixed & verified (R2):** `t_critical`/`chi2_critical` grow the upper bracket until it brackets the target; `student_test` passes |
| 3.C | adfuller | Low–Med | min-length guard ignores trend columns | **Fixed & verified (R2):** guard now requires `trend_cols + 2·max_lag + 3` observations; `adfuller_test` passes |
| 5.C | chebyshev | Low | `cheb_ls` assumed contiguity its concept didn't require | **Resolved (R1):** removed with `cheb_ls` (5.B) |
| 2.C | student_t | Low | unstable variance formula vs Welford | Open |
| 2.D | kolmogorov_smirnov | Low | `n1*n2` overflow on Win32 | **Fixed & verified (R2):** cast to double before multiplying |
| 5.D | bluestein_dft | Low | infinite loop on empty input | **Fixed & verified (R2):** early `if (N == 0) return {};` guard |
| 1.1 | sigmoid1090 | Low | no `x10 < x90` ordering guard | **Fixed & verified (R2):** asserts `x10 < x90`; covered by new test |
| 1.2 | mean_stddev | V.Low | sqrt of tiny negative variance possible | **Fixed & verified (R2):** `std::max(variance, 0.0)` clamp |
| 2.E | kolmogorov_smirnov | Low | empty-input division by zero | **Fixed & verified (R2):** returns {NaN, NaN} for empty input |
| 4.A | hmm | Low | no test coverage | **Fixed & verified (R2):** added `hmm_fit_on_signs_test` (regime-switching fixture + too-short guard) |
| 4.B | hmm | Low | length-dependent convergence tolerance | Open |
| 3.B | mackinnon | Low | overstated accuracy claim in header | Open |
| 3.D | mackinnon | Low | positive-stat discontinuity (in deleted fn) | **Resolved (R1):** removed with 3.A |
| 6.C | genetic_optimization | Info | no deterministic seed entry point | Open |
| 1.3 | sigmoid1090 | Info | equality guard is exact, not "well separated" | Open |

### Round 1 — summary
Fixed: 6.A, 3.A, 5.A, 5.B (and 5.C, 3.D resolved as side effects). Partial: 2.A
(renamed; p-value validity still open). Tests added: `container_range_mutation_gating`.
Verified: full solution builds Debug|x64 with `TreatWarningAsError`; 175/175 tests
pass (1 unrelated win32 registry test disabled).

### Round 2 — summary
Fixed: 2.A (p-value, completed), 6.B, 2.B, 3.C, 5.D, 2.D, 2.E, 1.1, 1.2.
Tests added: `descriptive_stats_and_filters` (Group 1: `mean_stddev` + the three
shaping filters, incl. invalid-input throws) and `hmm_fit_on_signs_test` (4.A).
Verified: builds Debug|x64 with `TreatWarningAsError`; **177/177 tests pass**
(1 unrelated win32 registry test disabled). The Shapiro p-value test was rewritten
to assert correct direction (near-normal p≈0.99, skewed p≈1e-6) with pinned values.

Remaining open: 2.C (student_t unstable variance formula), 3.B (overstated
accuracy claim in mackinnon header), 4.B (hmm length-dependent convergence
tolerance), 6.C (no deterministic seed — info), 1.3 (info).
