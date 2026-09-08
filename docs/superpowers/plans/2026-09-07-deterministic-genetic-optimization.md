# Deterministic Genetic Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in, budget-driven genetic-optimization mode whose completed results are reproducible for the same seed, starting state, configuration, and logical thread count.

**Architecture:** Keep every existing duration-based overload on its legacy RNG and execution path. Deterministic overloads use domain-separated logical RNG streams, stable population ordering, a controller-owned evaluation plan, and generation-atomic commits; worker tasks compute target values but never mutate the memo table. Extend the value-returning memo tables with lookup-only and ready-value insertion operations so budget admission and memo occupancy are deterministic.

**Tech Stack:** Microsoft Visual Studio C++23 with `/std:c++latest`, header-only Yadro algorithms and containers, `std::mt19937_64`, `std::seed_seq`, `std::atomic::wait/notify`, existing `gb::yadro::async::threadpool`, `GB_TEST`, Debug and Release x64 MSBuild.

**Spec:** `docs/superpowers/specs/2026-09-06-deterministic-genetic-optimization-design.md`

## Global Constraints

- Determinism is opt-in; existing `optimize()` signatures, defaults, overload resolution, serialized field order, and successful nonexception behavior remain unchanged.
- The completed-result guarantee requires equivalent initial in-memory state, seed, deterministic options, GA configuration, fitness results, and logical thread count; `optimization_stats::elapsed` is excluded.
- Successful deterministic completion is selected only by generation/evaluation budgets and deterministic stopping criteria. Wall time is a failure ceiling that throws `genetic_optimization_timeout`.
- Physical worker IDs, task start order, task completion order, and prior work on a reused pool must not influence deterministic random decisions.
- Deterministic worker tasks must never call `evaluate_chromosome()` or mutate the memo table.
- Memo-enabled deterministic evaluation groups by the normalized 128-bit memo key and inherits the memo's existing collision semantics. `memo_capacity == 0` evaluates every admitted entry separately.
- The GA's concrete memo type is `sharded_lockfree_memo_table<xxhash128, memo_fn_t, target_t, 128>`; the plain-table operations and the sharded forwarding layer are both required.
- The value-returning memo exception path resets `state`, resets `h_lo`, then notifies. The `void` specialization is unchanged and outside scope.
- Equivalent-fitness population ranking is stable; history continues to use its existing `lower_bound` insertion polarity.
- New `stop_reason` values are appended. No existing enum value or serialized field moves.
- Use TDD and commit after every independently reviewable task.
- Do not add dependencies, project files, a public population accessor, or persisted deterministic-run metadata.

---

## File Map

- Modify `container/lockfree_memo_table.h`: value-table exception reset ordering, lookup-only APIs, ready-value insertion APIs, and sharded forwarding.
- Modify `algorithm/genetic_optimization.h`: public deterministic contracts, stream derivation, stable ranking/breeding, deterministic evaluation planning, direct/parallel/adaptive entry points, budget commits, recovery, reporting, and header documentation.
- Modify `test/container_test.cpp`: value-table lost-wakeup regression and plain/sharded lookup/insertion coverage.
- Modify `test/algorithm_test.cpp`: API, archive, stream, tie, budget, memo, scheduling, recovery, timeout, and adaptive reproducibility tests.
- Do not modify `vs/yadro.vcxproj`, `vs/yadro_test.vcxproj`, or downstream repositories.

The test executable has no per-test filter and is invoked by the Visual Studio project's post-build event. Use `/p:PostBuildEventUseInBuild=false` only for compile-only RED steps; every GREEN build runs the complete Yadro test executable.

### Task 1: Add deterministic memo-table primitives and fix value-table exception wakeup

**Files:**
- Modify: `container/lockfree_memo_table.h:28-39,90-241,444-579`
- Test: `test/container_test.cpp:390-705`

**Interfaces:**
- Produces: `lockfree_memo_table::try_get(Args&&...) -> std::optional<Value>`.
- Produces: `lockfree_memo_table::try_get_with_hash(std::uint64_t, std::uint64_t) -> std::optional<Value>`.
- Produces: `lockfree_memo_table::insert_ready_with_hash(std::uint64_t, std::uint64_t, Value) -> Value`.
- Produces: identical forwarding operations on `sharded_lockfree_memo_table<Hasher, Function, Value, NumShards>`.
- Preserves: all `void`-specialization code and behavior.

- [x] **Step 1: Add the value-table exception waiter regression**

Add `<future>` to `test/container_test.cpp`, then add this test beside `lockfree_memo_test`:

```cpp
GB_TEST(container, lockfree_memo_exception_reset_wakes_waiter)
{
    using namespace std::chrono_literals;
    using hasher = gb::yadro::util::xxhash128;

    std::atomic<int> calls{ 0 };
    std::atomic<bool> leader_entered{ false };
    std::atomic<bool> release_leader{ false };

    lockfree_memo_table table(16,
        [&](int value) -> int {
            const int call = calls.fetch_add(1, std::memory_order_relaxed);
            if (call == 0) {
                leader_entered.store(true, std::memory_order_release);
                leader_entered.notify_all();
                release_leader.wait(false, std::memory_order_acquire);
                throw std::runtime_error("leader failure");
            }
            return value * 2;
        }, hasher{}, 8);

    auto leader = std::async(std::launch::async, [&] {
        return table.get_or_compute(21);
    });
    leader_entered.wait(false, std::memory_order_acquire);

    auto waiter = std::async(std::launch::async, [&] {
        return table.get_or_compute(21);
    });
    std::this_thread::sleep_for(20ms);
    release_leader.store(true, std::memory_order_release);
    release_leader.notify_all();

    must_throw<std::runtime_error>([&] { (void)leader.get(); });
    gbassert(waiter.wait_for(1s) == std::future_status::ready);
    gbassert(waiter.get() == 42);
    gbassert(calls.load(std::memory_order_relaxed) == 2);
}
```

- [x] **Step 2: Build without the post-build event, then run the old code under a process ceiling to confirm RED**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Build /p:Configuration=Debug /p:Platform=x64 `
  /p:PostBuildEventUseInBuild=false /v:minimal
$testProcess = Start-Process -FilePath '.\exe\x64\Debug\yadro_test.exe' `
  -PassThru -NoNewWindow
if ($testProcess.WaitForExit(15000)) {
    throw "RED was not reproduced; test exited with code $($testProcess.ExitCode). The race may not have materialized on this run."
}
Stop-Process -Id $testProcess.Id -Force
Write-Host 'RED confirmed: value-table waiter remained blocked after exception reset'
```

Expected RED: the test process remains blocked until the 15-second outer ceiling kills it. A normal exit, including exit code zero, does not confirm this defect; it means the lost-wakeup race did not materialize on that run (or a different failure occurred and must be inspected). Retry the bounded run or strengthen the waiter synchronization rather than treating a pass as evidence that the bug is absent. The test must not be allowed to hang the implementation session indefinitely.

- [x] **Step 3: Fix value-returning exception reset without breaking probe chains**

Add a tombstone state to the value-returning table and replace its
notify-before-reset sequence with:

```cpp
catch (...) {
    e.state.store(State::tombstone, std::memory_order_release);
    e.h_lo.store(0, std::memory_order_release);
    e.state.notify_all();
    throw;
}
```

`State::empty` remains reserved for a virgin slot so a failed computation does
not create a hole in a collision chain. Do not wait on either `State::empty` or
`State::tombstone`, because the next changed word may be `h_lo` while state
remains unchanged. Do not make the analogous edit in
`lockfree_memo_table<Hasher, Function, void>`.

- [x] **Step 4: Run the Debug build and confirm the waiter regression is GREEN**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Build /p:Configuration=Debug /p:Platform=x64 /v:minimal
```

Expected: exit code 0, the new exception-reset test passes, and the full Debug x64 suite reports zero failures.

- [x] **Step 5: Add failing plain and sharded lookup/insertion tests**

Add tests that exercise miss-without-compute, hit, ready insertion, normalized zero key, collision semantics, and sharded forwarding:

```cpp
GB_TEST(container, lockfree_memo_lookup_and_ready_insert)
{
    using hasher = gb::yadro::util::xxhash128;
    std::atomic<int> calls{ 0 };
    lockfree_memo_table table(16,
        [&](int value) { ++calls; return value * 10; }, hasher{}, 8);

    gbassert(!table.try_get(4));
    gbassert(calls.load() == 0);

    auto [h_lo, h_hi] = hasher{}(4);
    gbassert(table.insert_ready_with_hash(h_lo, h_hi, 41) == 41);
    gbassert(table.try_get(4) == 41);
    gbassert(table.get_or_compute(4) == 41);
    gbassert(calls.load() == 0);

    gbassert(table.insert_ready_with_hash(0, 0, 7) == 7);
    gbassert(table.try_get_with_hash(0, 0) == 7);
}

GB_TEST(container, sharded_lockfree_memo_lookup_and_ready_insert)
{
    using hasher = gb::yadro::util::xxhash128;
    auto fn = [](int value) { return value * 2; };
    sharded_lockfree_memo_table<hasher, decltype(fn), int, 8>
        table(64, fn, hasher{}, 8);

    gbassert(!table.try_get(9));
    auto [h_lo, h_hi] = table.get_hasher()(9);
    gbassert(table.insert_ready_with_hash(h_lo, h_hi, 99) == 99);
    gbassert(table.try_get(9) == 99);
}
```

Also extend the blocked-computation setup from Step 1 so `try_get(21)` waits and returns the second computation after reset. Add a forced-key test using `try_get_with_hash(1, 1)` and `insert_ready_with_hash(1, 2, value)` to pin linear probing and probe exhaustion.

- [x] **Step 6: Compile and confirm the new APIs are missing**

Run the Debug build command from Step 4.

Expected: compile errors naming missing `try_get`, `try_get_with_hash`, and `insert_ready_with_hash` members.

- [x] **Step 7: Implement the value-returning plain-table APIs**

Add `<optional>`. Reuse the existing zero-key normalization and probe sequence.
Implement `try_get(Args&&...)` by hashing once and forwarding. A virgin empty
slot is a conclusive miss; a tombstone is not, so lookup continues probing.
For a matching hash in `State::empty` or `State::tombstone`, recheck `h_lo` and
yield instead of using `atomic::wait`. Wait only on `State::computing`.

Implement `insert_ready_with_hash` with the same state machine. Record the first
tombstone while continuing the exact-key search. Only after reaching a virgin
empty slot or exhausting the search may insertion try to claim the recorded
tombstone; this prevents duplicate keys when an existing key lies later in the
chain. On a successful claim, store `h_hi`, move the value into `e.value`,
publish `State::ready` with release ordering, notify waiters, and return
`e.value`. A matching ready key returns its existing value; a matching computing
key waits or resumes after reset. After a failed CAS, restart the search rather
than blindly advancing.

- [x] **Step 8: Implement sharded forwarding and run Debug and Release**

The sharded methods compute or accept the same `(h_lo, h_hi)`, select:

```cpp
const size_t shard_idx = mix64(h_hi) & ShardMask;
```

and forward to the selected plain value table. Do not add methods to the `void` specialization.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Build /p:Configuration=Debug /p:Platform=x64 /v:minimal
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Build /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Expected: both commands exit 0; all container tests and the complete suites pass.

- [x] **Step 9: Commit Task 1**

```powershell
git add -- container/lockfree_memo_table.h test/container_test.cpp
git commit -m "feat: add deterministic memo table operations"
```

### Task 2: Add deterministic public contracts without changing serialized layout

**Files:**
- Modify: `algorithm/genetic_optimization.h:28-42,686-701,1135-1174,1331-1375,1639-1693,1847-1874`
- Test: `test/algorithm_test.cpp:35-205`

**Interfaces:**
- Produces: `deterministic_ga_options(seed, generation_budget, evaluation_budget, failure_timeout)` with no default constructor.
- Produces: `genetic_optimization_timeout : std::runtime_error` with `timeout()` and `elapsed()` accessors.
- Produces: `detail::DeterministicThreadPool`.
- Produces: appended `stop_reason::generation_budget` and `stop_reason::evaluation_budget`.

- [x] **Step 1: Add compile-time and runtime API tests**

Add these assertions near the existing GA value-type test:

```cpp
using namespace gb::yadro::algorithm::conv;
using namespace std::chrono_literals;
namespace cdetail = gb::yadro::algorithm::conv::detail;

static_assert(!std::default_initializable<deterministic_ga_options>);
static_assert(cdetail::DeterministicThreadPool<gb::yadro::async::threadpool>);
static_assert(!cdetail::DeterministicThreadPool<std::size_t>);
static_assert(static_cast<std::uint8_t>(stop_reason::elite_perturbation) == 8);
static_assert(static_cast<std::uint8_t>(stop_reason::generation_budget) == 9);
static_assert(static_cast<std::uint8_t>(stop_reason::evaluation_budget) == 10);

const deterministic_ga_options options{ 17, 4, 100, 2s };
gbassert(options.seed == 17);
gbassert(options.generation_budget == 4);
gbassert(options.evaluation_budget == 100);
gbassert(options.failure_timeout == 2s);

const genetic_optimization_timeout timeout{ 1s, 1500ms };
gbassert(timeout.timeout() == 1s);
gbassert(timeout.elapsed() == 1500ms);
gbassert(std::string_view{ timeout.what() }.contains("deterministic genetic optimization"));
```

- [x] **Step 2: Build Debug and confirm the public contracts are absent**

Run the Debug build command from Task 1.

Expected: compile errors for the missing options, exception, concept, and stop reasons.

- [x] **Step 3: Add the public types, concept, and stop reasons**

Implement `deterministic_ga_options` exactly as the spec, with a four-argument `constexpr noexcept` constructor. Add `<format>`, then implement `genetic_optimization_timeout` in the same header with two `std::chrono::nanoseconds` members and this base message:

```cpp
std::runtime_error(std::format(
    "deterministic genetic optimization exceeded failure timeout "
    "(limit={}ns, elapsed={}ns)", timeout.count(), elapsed.count()))
```

Define the exposition-only constraint before the optimizer template:

```cpp
namespace detail {
    template<typename ThreadPool>
    concept DeterministicThreadPool = requires(ThreadPool& tp) {
        { tp.thread_count() } -> std::convertible_to<std::size_t>;
    };
}
```

Append the two enum values after `elite_perturbation`; add explicit cases to `stop_reason_name` and `stop_reason_description`. Do not change `is_terminal`, whose exclusion-based implementation already makes the appended values terminal.

- [x] **Step 4: Add an archive-layout regression using the legacy field sequence**

Construct an empty optimizer and a memory archive manually from the current positional fields, then load it through `genetic_optimization_t::serialize`:

```cpp
auto opt = genetic_optimization_t(
    [](int value) { return value * value; },
    std::less<int>{},
    discrete_value_range<int>({ -1, 0, 1 }));

ga_config config;
adaptive_phase_config adaptive;
stopping_criteria stopping;
optimization_stats stats;
using chromosome_t = std::tuple<int>;
using history_t = optimization_history<chromosome_t, int, std::less<int>>;
history_t history{ std::numeric_limits<std::size_t>::max(), std::less<int>{} };
std::vector<std::pair<chromosome_t, std::optional<int>>> population;
std::optional<int> target;
std::optional<int> previous_best;
std::size_t function_calls = 0;
std::size_t requests = 0;

gb::yadro::archive::omem_archive<> legacy;
legacy(config, adaptive, stopping, stats, history, population, target,
    previous_best, function_calls, requests);
gb::yadro::archive::imem_archive<> input{ legacy };
input(opt);
gbassert(opt.config == config);
gbassert(opt.stats() == stats);
gbassert(opt.pop_size() == 0);
```

This test fails if deterministic policy fields are inserted into the positional optimizer archive.

Set `stats.last_stop_reason` to each appended budget value in separate archive round trips and assert the integer-backed field restores exactly. This pins append-only enum serialization without adding a field.

- [x] **Step 5: Run Debug and Release, then commit Task 2**

Run both build commands from Task 1 Step 8.

Expected: both suites pass and the archive regression consumes exactly the legacy field list.

```powershell
git add -- algorithm/genetic_optimization.h test/algorithm_test.cpp
git commit -m "feat: define deterministic GA contracts"
```

### Task 3: Implement domain-separated logical RNG streams and stable breeding order

**Files:**
- Modify: `algorithm/genetic_optimization.h:1467-1552,2975-2991,3047-3057,3091-3210`
- Test: `test/algorithm_test.cpp`

**Interfaces:**
- Produces: `detail::deterministic_rng_domain` with fixed underlying values.
- Produces: `detail::deterministic_seed_material(...) -> std::array<std::uint32_t, 16>`.
- Produces: `detail::make_deterministic_rng(...) -> std::mt19937_64`.
- Produces: `detail::logical_chunk_count(...)` and `detail::logical_chunk_bounds(...)`.
- Produces: private deterministic initialization, stable ranking, and serial/parallel breeding helpers.

- [x] **Step 1: Add seed-material and partition tests**

Start with behavioral RED tests that do not assume a golden vector from code that has not been implemented. Create two engines for every domain and assert their first eight outputs agree for identical keys. Assert that changing each key component independently—seed, phase, generation, domain, and logical stream—changes the observed sequence. Capture a later-key normal-breeding sequence, exhaust a cataclysm and an elite-perturbation engine, then recreate the later-key normal engine and assert its sequence is unchanged; recovery draws must not shift normal-breeding streams.

Pin the independently derivable partition shape:

```cpp
static_assert(detail::logical_chunk_count(0, 8) == 0);
static_assert(detail::logical_chunk_count(7, 4) == 4);
static_assert(detail::logical_chunk_count(7, 16) == 7);
static_assert(detail::logical_chunk_bounds(7, 4, 0) == std::pair{ 0uz, 2uz });
static_assert(detail::logical_chunk_bounds(7, 4, 3) == std::pair{ 6uz, 7uz });
```

- [x] **Step 2: Build Debug and confirm RED on missing deterministic stream helpers**

Run the Debug build command.

Expected: compile errors for the missing domain, seed-material, and partition functions.

- [x] **Step 3: Implement the exact stream derivation**

Use these fixed domain values:

```cpp
enum class deterministic_rng_domain : std::uint64_t {
    initial_population = 0x494e495400000001ULL,
    normal_breeding = 0x4e4f524d414c0001ULL,
    cataclysm = 0x4341544100000001ULL,
    elite_perturbation = 0x454c495400000001ULL,
};
```

Use canonical SplitMix64:

```cpp
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
```

Build the 16-word material by first applying `splitmix64(seed)`, then for each of `phase`, `generation`, `std::to_underlying(domain)`, and `logical_stream` assigning `state = splitmix64(state ^ splitmix64(component))`. For word pairs zero through seven, assign `state = splitmix64(state + pair_index)`, then emit low and high 32-bit halves. Construct `std::seed_seq` from the material and seed `std::mt19937_64` from that sequence.

- [x] **Step 4: Compute, inspect, and pin the implemented seed material**

After implementing Step 3, add a temporary nonasserting diagnostic that calls `detail::deterministic_seed_material` for `(seed=0x0123456789abcdef, phase=2, generation=7, domain=normal_breeding, stream=3)` and prints all 16 words in fixed-width hexadecimal. Build Debug with `/p:PostBuildEventUseInBuild=false`, then run `& '.\exe\x64\Debug\yadro_test.exe'`. Inspect the complete output and independently walk the Step 3 SplitMix64 state transitions to confirm the value source. Do not obtain the vector by copying a compiler assertion failure. Remove the diagnostic, then add this regression lock using the deliberately recorded output:

```cpp
constexpr auto material = detail::deterministic_seed_material(
    0x0123456789abcdefULL,
    2,
    7,
    detail::deterministic_rng_domain::normal_breeding,
    3);
static_assert(material == std::array<std::uint32_t, 16>{
    0x739cd088U, 0xb7d9ab74U, 0xf5c06ebaU, 0x114cefd2U,
    0x6d7c2359U, 0x1a2a3647U, 0x27d18387U, 0xe681427bU,
    0x93521029U, 0x0cee4d77U, 0x69e28142U, 0x1379bc46U,
    0x23b71f49U, 0x0402ca23U, 0xd202bfc0U, 0x08eb8139U });
```

- [x] **Step 5: Implement and test stable ranking**

Add a deterministic-only sorter:

```cpp
void sort_population_deterministic()
{
    std::stable_sort(population_.begin(), population_.end(),
        [this](const individual_t& lhs, const individual_t& rhs) {
            if (lhs.second && rhs.second) return compare_(*lhs.second, *rhs.second);
            return lhs.second.has_value() && !rhs.second.has_value();
        });
}
```

Do not change legacy `sort_population(size_t)`. Add a constant-fitness test with injected chromosomes `0..7`; after the initial unsorted history feed, assert the existing `lower_bound` polarity yields history chromosomes `7..0`.

Pin tournament tie behavior by creating two identical deterministic engines. Use the first engine and `std::uniform_int_distribution<size_t>{0, fitnesses.size() - 1}` to capture the first sampled index. Pass the second engine, an all-equal fitness vector, and `k > 1` to `detail::tournament_select`; assert it returns that first sampled index.

- [x] **Step 6: Implement deterministic initialization and breeding helpers**

Initialization copies the current population into a candidate, stably ranks before truncating, and fills missing indices from the `initial_population` stream keyed by `(seed, phase, 0, domain, 0)`. It does not mutate `population_` until Task 4 admits and evaluates the candidate.

For breeding, use:

```cpp
const size_t num_offspring = pop_size - elite_n;
const size_t stream_count = detail::logical_chunk_count(
    num_offspring, logical_thread_count);
```

Copy elites in rank order. Each logical stream owns the exact contiguous range from `logical_chunk_bounds`, creates one `normal_breeding` engine keyed by phase, phase-local generation, and logical stream, and fills `next[elite_n + offspring_index]`. The serial helper calls the same implementation with logical thread count one. The parallel helper submits one task per active logical stream and drains futures in logical-stream order. Neither helper reads `detail::thread_rng()`.

- [x] **Step 7: Run stream and breeding reproducibility tests**

Add a test that calls deterministic breeding twice with the same fixed evaluated population and compares every chromosome. Submit unrelated tasks to a four-thread pool between calls and confirm the result stays identical. For seven offspring, compare the results from eight- and sixteen-thread pools and confirm they match because both use seven active logical streams.

Run Debug and Release builds. Expected: all tests pass; legacy GA tests remain unchanged.

- [x] **Step 8: Commit Task 3**

```powershell
git add -- algorithm/genetic_optimization.h test/algorithm_test.cpp
git commit -m "feat: add logical GA random streams"
```

### Task 4: Add deterministic evaluation planning and the serial budget-driven path

**Files:**
- Modify: `algorithm/genetic_optimization.h:1876-1936,2647-2677,2914-3064`
- Test: `test/algorithm_test.cpp`

**Interfaces:**
- Produces: private `deterministic_run_state` and `deterministic_evaluation_group`.
- Produces: private `deterministic_phase_outcome` and `run_deterministic_phase`.
- Produces: `detail::group_deterministic_memo_keys` for canonical key grouping and collision tests.
- Produces: private `build_deterministic_evaluation_plan` and `evaluate_deterministic_serial`.
- Produces: public serial `optimize(const deterministic_ga_options&, size_t, size_t)`.
- Consumes: memo APIs from Task 1, public contracts from Task 2, and RNG/ranking/breeding from Task 3.

- [x] **Step 1: Add failing serial budget and memo-accounting tests**

Use a helper that suppresses every non-budget stop:

```cpp
auto make_budget_optimizer = [] {
    auto optimizer = genetic_optimization_t(
        [](int value) { return value * value; },
        std::less<int>{},
        discrete_value_range<int>({ -20, -10, -5, 5, 10, 20 }));
    optimizer.config.memo_capacity = 0;
    optimizer.stop_criteria.stagnation_absolute_floor =
        std::numeric_limits<std::size_t>::max();
    optimizer.stop_criteria.diversity_threshold = 0.0;
    optimizer.target_fitness = -1;
    return optimizer;
};
```

Test these exact contracts:

```cpp
{
    auto optimizer = make_budget_optimizer();
    auto [stats, history] = optimizer.optimize(
        deterministic_ga_options{ 101, 2, 16, 5s }, 6, 20);
    gbassert(stats.generations == 2);
    gbassert(stats.total_evaluations == 16);
    gbassert(stats.cache_hits == 0);
    gbassert(stats.last_stop_reason == stop_reason::generation_budget);
    gbassert(!history.empty());
}
{
    auto optimizer = make_budget_optimizer();
    auto [stats, history] = optimizer.optimize(
        deterministic_ga_options{ 101, 2, 10, 5s }, 6, 20);
    gbassert(stats.generations == 0);
    gbassert(stats.total_evaluations == 6);
    gbassert(stats.last_stop_reason == stop_reason::evaluation_budget);
}
{
    auto optimizer = make_budget_optimizer();
    must_throw<std::invalid_argument>([&] {
        optimizer.optimize(deterministic_ga_options{ 101, 2, 5, 5s }, 6, 20);
    });
    gbassert(optimizer.stats().total_evaluations == 0);
    gbassert(optimizer.pop_size() == 0);
}
```

Add invalid-call tests for zero population, generation budget, evaluation budget, and timeout.

Add three initial-population accounting cases that stop at a reachable target before breeding:

```cpp
auto make_duplicate_optimizer = [] {
    auto optimizer = genetic_optimization_t(
        [](int value) { return value * value; },
        std::less<int>{},
        discrete_value_range<int>({ 3 }));
    optimizer.target_fitness = 9;
    return optimizer;
};

// Memo enabled: four equal keys become one call plus three cache hits.
auto memoized = make_duplicate_optimizer();
for (int i = 0; i < 4; ++i) memoized.inject_chromosome(std::tuple{ 3 });
auto [memo_stats, memo_history] = memoized.optimize(
    deterministic_ga_options{ 202, 1, 4, 5s }, 4, 10);
gbassert(memo_stats.total_evaluations == 1);
gbassert(memo_stats.cache_hits == 3);

// Memo disabled: the same four chromosomes are four target calls.
auto uncached = make_duplicate_optimizer();
for (int i = 0; i < 4; ++i) uncached.inject_chromosome(std::tuple{ 3 });
uncached.config.memo_capacity = 0;
auto [uncached_stats, uncached_history] = uncached.optimize(
    deterministic_ga_options{ 202, 1, 4, 5s }, 4, 10);
gbassert(uncached_stats.total_evaluations == 4);
gbassert(uncached_stats.cache_hits == 0);
```

For a pre-existing memo hit, optimize a one-value `{0}` space, call `soft_reset()` to preserve its memo, capture the cumulative counters, repeat the same deterministic call, and assert the second call's deltas are zero target evaluations and one cache hit. Add a `detail::group_deterministic_memo_keys` unit test with two different logical chromosomes assigned the same synthetic `hash128_t`; assert the lower population index is the sole representative. This provides collision coverage without attempting to discover a real xxHash128 collision.

- [x] **Step 2: Build Debug and confirm no deterministic optimize overload exists**

Expected: compile errors selecting `optimize(deterministic_ga_options, ...)`.

- [x] **Step 3: Add deterministic run-state and evaluation-plan types**

Use call-local state:

```cpp
using deterministic_clock = std::chrono::steady_clock;

struct deterministic_run_state {
    deterministic_ga_options options;
    deterministic_clock::time_point started;
    deterministic_clock::time_point deadline;
    std::size_t remaining_evaluations;
    std::size_t committed_generations{};
};

struct deterministic_evaluation_group {
    hash128_t key{};
    std::size_t representative{};
    std::vector<std::size_t> members;
    std::optional<target_t> value;
    bool uses_memo{};
};

struct deterministic_phase_outcome {
    stop_reason reason{ stop_reason::none };
    size_t committed_generations{};
    size_t unused_generations{};
};
```

Scan candidate indices in ascending order and append each first-seen group directly to the plan vector. Use `std::map<hash128_t, size_t>` only as a key-to-plan-position lookup side table; never iterate the map to construct or reorder the plan. This preserves ascending representative-index order for lookup, submission, future draining, memo insertion, and lowest-index exception selection. Normalize raw key `(0, 0)` to `(1, 0)` before grouping, matching the memo table. If memoization is disabled, append one nonmemo group per unevaluated index.

- [x] **Step 4: Implement counter-neutral planning and admission**

For memo-enabled runs, call `ensure_memo_table()`, hash through the concrete `memo_table_t` hasher, group normalized keys, and call `try_get_with_hash` in representative order. Count missing representatives without incrementing either atomic counter. If missing count exceeds `remaining_evaluations`, return `stop_reason::evaluation_budget` without changing the candidate, counters, or memo.

After admission, increment `total_eval_requests_` by the total number of unevaluated candidate entries. For each missing representative, increment `fn_call_count_` immediately before invoking `target_fn_` directly through `std::apply`. Do not call `evaluate_chromosome()`.

```cpp
auto plan = build_deterministic_evaluation_plan(candidate);
const size_t required = std::ranges::count_if(plan,
    [](const deterministic_evaluation_group& group) {
        return !group.value.has_value();
    });
if (required > run.remaining_evaluations)
    return stop_reason::evaluation_budget;

const size_t requests = std::ranges::count_if(candidate,
    [](const individual_t& individual) { return !individual.second; });
total_eval_requests_.fetch_add(requests, std::memory_order_relaxed);
```

- [x] **Step 5: Implement serial evaluation, deterministic memo commit, and exception selection**

Evaluate representatives in ascending index. Capture each result or exception. After every admitted computation finishes, insert successful memoized results with `insert_ready_with_hash` in representative order and use the returned memo value. Copy that value to every group member. Rethrow the exception for the lowest representative index after deterministic memo commits and `sync_stats()`.

On success, subtract the actual started target calls from `remaining_evaluations`. A memo hit consumes no evaluation budget. A candidate is assigned to `population_` only after every member has a value and the timeout check passes.

```cpp
std::vector<std::pair<size_t, std::exception_ptr>> failures;
for (auto& group : plan) {
    if (!group.value) {
        fn_call_count_.fetch_add(1, std::memory_order_relaxed);
        --run.remaining_evaluations;
        try {
            group.value = std::apply([this](const auto&... args) {
                return target_fn_(args...);
            }, candidate[group.representative].first);
        }
        catch (...) {
            failures.emplace_back(group.representative, std::current_exception());
        }
    }
    if (!group.value) continue;
    if (group.uses_memo) {
        group.value = memo_.table->insert_ready_with_hash(
            group.key.low, group.key.high, *group.value);
    }
    for (const size_t index : group.members)
        candidate[index].second = *group.value;
}
sync_stats();
if (!failures.empty()) std::rethrow_exception(failures.front().second);
```

The plan is already representative-index ordered, so `failures.front()` is the required lowest-index exception.

- [x] **Step 6: Implement the serial deterministic phase loop**

Add private `run_deterministic_phase`, receiving `(run_state&, phase_index, generation_allocation, population_size, max_history, evaluator)`. It:

1. builds and evaluates a candidate initial population atomically;
2. feeds initial history before the first rank;
3. stably ranks the full population;
4. checks deterministic stop criteria;
5. stops with `generation_budget` after the allocated count;
6. breeds and evaluates a complete candidate;
7. commits it and increments cumulative and call-local generations.

For this task, tests suppress recovery as shown in Step 1; Task 6 supplies transactional cataclysm and elite perturbation. Compute the stagnation limit from the call's `evaluation_budget`, not cumulative lifetime calls.

```cpp
while (true) {
    sort_population_deterministic();
    feed_history_from_population();
    auto decision_rng = detail::make_deterministic_rng(
        run.options.seed, phase_index, phase_generations,
        detail::deterministic_rng_domain::cataclysm, 0);
    const stop_reason reason = should_stop_or_cataclysm(
        compute_stagnation_limit(run.options.evaluation_budget),
        elite_count(population_size), decision_rng);
    if (reason == stop_reason::cataclysm
        || reason == stop_reason::elite_perturbation)
        throw std::logic_error(
            "deterministic recovery requires Task 6 transactional handling");
    if (is_terminal(reason))
        return deterministic_phase_outcome{
            reason, phase_generations,
            generation_allocation - phase_generations };
    if (phase_generations == generation_allocation)
        return deterministic_phase_outcome{
            stop_reason::generation_budget, phase_generations, 0 };

    auto candidate = breed_next_generation_deterministic(
        population_size, elite_count(population_size), run.options.seed,
        phase_index, phase_generations, 1);
    const stop_reason evaluation = evaluate_deterministic_serial(candidate, run);
    if (evaluation == stop_reason::evaluation_budget)
        return deterministic_phase_outcome{
            evaluation, phase_generations,
            generation_allocation - phase_generations };
    population_ = std::move(candidate);
    ++phase_generations;
    ++run.committed_generations;
    std::lock_guard lock(stats_mutex_);
    ++stats_.generations;
}
```

The temporary `logic_error` prevents the Task 4 checkpoint from returning a nominally deterministic result after the legacy helper has taken an in-place recovery path. Task 6 removes this guard when it replaces the legacy call with decision-only, domain-separated, transactional recovery.

- [x] **Step 7: Add the public serial wrapper and timeout checks**

Validate before changing history, population, memo, or counters:

```cpp
void validate_deterministic_options(
    const deterministic_ga_options& run,
    std::size_t population_size) const
{
    if (population_size == 0)
        throw std::invalid_argument("deterministic optimize: population_size must be > 0");
    if (run.generation_budget == 0)
        throw std::invalid_argument("deterministic optimize: generation_budget must be > 0");
    if (run.evaluation_budget == 0)
        throw std::invalid_argument("deterministic optimize: evaluation_budget must be > 0");
    if (run.failure_timeout <= std::chrono::nanoseconds::zero())
        throw std::invalid_argument("deterministic optimize: failure_timeout must be > 0");
    config.validate();
    stop_criteria.validate();
}
```

The wrapper then creates one steady-clock deadline, sets logical thread count one, resizes history, and invokes phase zero. Check timeout before initialization, before evaluation, after evaluation drain, before breeding, and before commit. Throw `genetic_optimization_timeout{options.failure_timeout, now - started}`; never return `stop_reason::deadline`.

Use one public-boundary scope guard to add `now - started` to `stats_.elapsed` exactly once on normal return or exception. Successful return sets `last_stop_reason` to the deterministic criterion or budget reason selected by the phase loop. A timeout records elapsed time but leaves `last_stop_reason` distinct from legacy `deadline`.

- [x] **Step 8: Run Debug and Release and commit Task 4**

Expected: the exact 6/16 budget accounting passes, rejected candidates are counter-neutral, and all legacy tests remain green.

```powershell
git add -- algorithm/genetic_optimization.h test/algorithm_test.cpp
git commit -m "feat: add serial deterministic GA execution"
```

### Task 5: Add deterministic parallel evaluation and scheduling-independent results

**Files:**
- Modify: `algorithm/genetic_optimization.h:1938-2012,3003-3045`
- Test: `test/algorithm_test.cpp`

**Interfaces:**
- Produces: constrained public `optimize(ThreadPool&, const deterministic_ga_options&, size_t, size_t)`.
- Produces: private `evaluate_deterministic_parallel` and parallel phase runner.
- Consumes: Task 4 evaluation plans; worker tasks return values/exceptions only.

- [x] **Step 1: Add repeated parallel reproducibility tests with scheduling perturbation**

Create two fresh optimizers per iteration with a pure target that sleeps according to the chromosome value. Run twenty iterations with four-thread pools and compare:

```cpp
auto normalize_stats = [](optimization_stats stats) {
    stats.elapsed = {};
    return stats;
};

gbassert(normalize_stats(lhs_stats) == normalize_stats(rhs_stats));
gbassert(lhs_history.all() == rhs_history.all());
```

Before the second run, submit and drain unrelated tasks on its pool. Use the same seed, options, config, population size, and logical thread count. Add the eight-versus-sixteen-thread test for a population with seven offspring.

Run the same two-call warm-restart sequence on two equivalent optimizers and assert equality after both calls. Add a different-seed sanity case over a large discrete space and assert the serialized generated populations differ; treat this only as coverage that the seed participates in stream identity, not as a universal collision guarantee.

- [x] **Step 2: Add a deterministic final-population comparison helper in the test only**

Do not add a production accessor. After collecting result assertions, normalize each optimizer for serialization:

```cpp
template<typename Optimizer>
auto serialized_population_state(Optimizer& optimizer)
{
    using reset_flag = typename Optimizer::reset_flag;
    optimizer.soft_reset(reset_flag::keep_population);
    gb::yadro::archive::omem_archive<> archive;
    archive(optimizer);
    return archive.get_stream().buffer();
}
```

Compare the returned buffers. `soft_reset(keep_population)` clears statistics/history and makes fitness optional values empty while retaining chromosome order; the memo is not serialized.

- [x] **Step 3: Build Debug and confirm the constrained parallel overload is absent**

Expected: compile failure for the thread-pool/options overload while the lvalue-`size_t` static assertion from Task 2 continues to compile.

- [x] **Step 4: Implement parallel representative evaluation without worker memo writes**

Constrain the public overload with `template<detail::DeterministicThreadPool ThreadPool>`. Capture `static_cast<size_t>(tp.thread_count())` exactly once at call entry, reject zero, and retain that logical count in the call-local state.

Submit one task for each admitted missing representative. Each task increments `fn_call_count_`, invokes `target_fn_` directly for the representative chromosome, and returns `target_t`. Store `(representative_index, future)` in ascending representative order. Drain all futures; retain the first exception by representative order, not completion order. Only the controller calls `insert_ready_with_hash`, applies group values, updates request counts, and commits candidate state.

```cpp
for (auto& group : plan) {
    if (group.value) continue;
    const size_t index = group.representative;
    futures.emplace_back(index, tp([this, &candidate, index] {
        fn_call_count_.fetch_add(1, std::memory_order_relaxed);
        return std::apply([this](const auto&... args) {
            return target_fn_(args...);
        }, candidate[index].first);
    }));
}
```

Add a code comment at the submission site stating that `evaluate_chromosome()` is forbidden because it writes through `get_or_compute`.

- [x] **Step 5: Add lowest-index multi-exception coverage**

Inject a known population whose target throws `std::runtime_error("index-0")` and `std::runtime_error("index-2")` for two representative chromosomes. Delay the lower-index throw so it completes last. Assert that all futures drain and the public exception text is `index-0`, proving selection follows representative order.

- [x] **Step 6: Add timeout failure tests**

Use a target that blocks for longer than a 1ms failure timeout but eventually returns. Assert:

```cpp
catch (const genetic_optimization_timeout& error) {
    gbassert(error.timeout() == 1ms);
    gbassert(error.elapsed() >= error.timeout());
    gbassert(optimizer.stats().last_stop_reason != stop_reason::deadline);
}
```

Also run the same optimization with a generous timeout and verify its normalized result matches a repeated run. Confirm timeout drains submitted futures and retains only the last fully committed population.

- [x] **Step 7: Run Debug and Release, then commit Task 5**

Expected: repeated runs match despite varied worker history and completion order; no deterministic worker writes the memo.

```powershell
git add -- algorithm/genetic_optimization.h test/algorithm_test.cpp
git commit -m "feat: parallelize deterministic GA evaluation"
```

### Task 6: Make stopping recovery and tie behavior generation-atomic

**Files:**
- Modify: `algorithm/genetic_optimization.h:2683-2880,3047-3130`
- Test: `test/algorithm_test.cpp`

**Interfaces:**
- Produces: private `deterministic_stop_decision` and `check_deterministic_stop` separated from recovery mutation.
- Produces: private `make_deterministic_cataclysm_candidate` and `make_deterministic_elite_perturbation_candidate` using their own RNG domains.
- Produces: private `evaluate_and_commit_deterministic_recovery` shared by serial and parallel phase loops.
- Produces: private `commit_recovery_statistics(stop_reason)` called only after a recovered population is fully evaluated.
- Preserves: legacy `should_stop_or_cataclysm` and legacy recovery RNG path.

- [x] **Step 1: Add recovery-budget atomicity tests**

Force cataclysm with `diversity_threshold = 1.0`, `cataclysm_enabled = true`, and a finite recovery cap. Set the evaluation budget one call below the complete recovery plan and assert population serialization, `cataclysm_count`, request counters, and generation count remain at their pre-recovery committed values. Repeat with exactly enough budget and assert recovery commits once.

Force elite perturbation with a constant numeric target, no target fitness, `elite_convergence_epsilon = 1.0`, and `max_elite_perturbation_count = 1`; apply the same insufficient/exact budget assertions.

Add paired repeated runs for terminal `target_reached`, `stagnation`, `diversity`, and `elite_converged` outcomes. For each pair, assert identical normalized statistics, ordered history, and population bytes, including the exact terminal reason and generations-without-improvement counter.

- [x] **Step 2: Add full-sort survivor and tied-history tests**

Create two optimizers with the same distinct-fitness chromosomes injected in ascending and descending order. Give them the same seed and force cataclysm with `cataclysm_survival_fraction > elitism_fraction`. After deterministic stable full ranking and recovery, normalize through `serialized_population_state` and assert both populations match. This proves survivors come from the full fitness-ranked prefix rather than the legacy unspecified post-elite partition.

Add a separate all-equal-fitness recovery case with survivor count above the elite count. Across repeated runs from the same injected order, assert that stable ranking preserves that order and the exact survivor prefix is retained, so deterministic recovery never depends on `nth_element` tie partitioning.

For constant fitness and injected `0..7`, stop at the target check after the initial unsorted feed and assert history chromosome order is exactly `7,6,5,4,3,2,1,0`, preserving `try_insert`'s lower-bound polarity.

- [x] **Step 3: Split deterministic stopping decisions from mutation**

Keep target, diversity, elite convergence, and stagnation precedence identical. Add a deterministic decision helper that updates deterministic observations but returns `cataclysm` or `elite_perturbation` without mutating `population_` or incrementing recovery counters. Replace the Task 4 legacy-helper call and remove its temporary recovery `logic_error`; legacy code continues to call `should_stop_or_cataclysm` unchanged.

```cpp
struct deterministic_stop_decision {
    stop_reason reason{ stop_reason::none };
    size_t elite_n{};
    size_t survivor_count{};
};

[[nodiscard]] deterministic_stop_decision check_deterministic_stop(
    size_t stagnation_limit, size_t elite_n);
```

`check_deterministic_stop` performs target, stagnation-observation, diversity, elite-convergence, and stagnation checks in the same order as the legacy function. It returns recovery metadata but never calls either legacy trigger.

- [x] **Step 4: Build and evaluate recovery candidates transactionally**

For cataclysm, copy the committed, fully ranked population; preserve `[0, survivor_count)` and replace later indices with `random_chromosome` using domain `cataclysm`, phase, phase-local generation, and logical stream zero. For elite perturbation, preserve index zero and replace `[1, elite_n)` with domain `elite_perturbation`.

Build and admit the complete evaluation plan before mutating public state or recovery counters. On success, commit the recovered population, clear `prev_best_`, reset generations-without-improvement, and increment the applicable recovery count. On insufficient budget, discard the candidate and return `evaluation_budget`.

```cpp
auto candidate = decision.reason == stop_reason::cataclysm
    ? make_deterministic_cataclysm_candidate(
        decision.survivor_count, run.options.seed, phase_index, phase_generation)
    : make_deterministic_elite_perturbation_candidate(
        decision.elite_n, run.options.seed, phase_index, phase_generation);
const stop_reason evaluation = std::invoke(evaluate, candidate, run);
if (evaluation == stop_reason::evaluation_budget) return evaluation;
population_ = std::move(candidate);
commit_recovery_statistics(decision.reason);
```

- [x] **Step 5: Resume breeding after committed recovery**

After a recovery commit, breed from the fully evaluated recovered population within the same generation iteration. If the offspring evaluation plan cannot fit, retain the recovered population as the last committed boundary, do not increment generations, and stop with `evaluation_budget`.

```cpp
if (decision.reason == stop_reason::cataclysm
    || decision.reason == stop_reason::elite_perturbation) {
    const stop_reason recovery = evaluate_and_commit_deterministic_recovery(
        decision, run, phase_index, phase_generation);
    if (recovery == stop_reason::evaluation_budget) return recovery;
}
auto offspring = breed_next_generation_deterministic(
    population_size, elite_n, run.options.seed,
    phase_index, phase_generation, logical_thread_count);
```

- [x] **Step 6: Run Debug and Release and commit Task 6**

Expected: recovery tests pass at both sides of the budget boundary, tie order is pinned, and legacy recovery tests remain green.

```powershell
git add -- algorithm/genetic_optimization.h test/algorithm_test.cpp
git commit -m "feat: make deterministic GA recovery atomic"
```

### Task 7: Add deterministic adaptive multi-phase optimization

**Files:**
- Modify: `algorithm/genetic_optimization.h:2025-2270`
- Test: `test/algorithm_test.cpp:270-345`

**Interfaces:**
- Produces: `detail::allocate_deterministic_generations(total, phases) -> std::vector<size_t>`.
- Produces: private `apply_deterministic_phase_baseline(const ga_config&, size_t phase, size_t num_phases)` containing the existing exploration-to-refinement interpolation.
- Produces: private `apply_deterministic_adaptive_rules(const optimization_stats& current, optimization_stats& previous, size_t phase, size_t num_phases, size_t initial_population_size, size_t& current_population_size)` containing the existing phase-delta adaptation rules.
- Produces: serial and constrained parallel adaptive deterministic overloads.
- Consumes: one shared `deterministic_run_state` across every phase.

- [x] **Step 1: Add exact allocation and one-phase equivalence tests**

Pin triangular largest-remainder allocation:

```cpp
gbassert(detail::allocate_deterministic_generations(10, 4)
    == std::vector<std::size_t>{ 4, 3, 2, 1 });
gbassert(detail::allocate_deterministic_generations(7, 4)
    == std::vector<std::size_t>{ 3, 2, 1, 1 });
```

Run a direct deterministic call and a one-phase adaptive deterministic call from equivalent fresh state. Compare normalized statistics, ordered history, and `serialized_population_state` byte-for-byte.

- [x] **Step 2: Build Debug and confirm adaptive overloads and allocator are absent**

Expected: compile errors naming the missing allocator and deterministic adaptive overloads.

- [x] **Step 3: Implement integer triangular allocation**

For phase `i`, use weight `num_phases - i` and denominator `num_phases * (num_phases + 1) / 2`. Compute base shares and remainders with checked integer arithmetic. Distribute the leftover one generation at a time by descending remainder and ascending phase index. Reject `num_phases == 0` before allocation.

```cpp
struct phase_share {
    size_t index;
    size_t generations;
    size_t remainder;
};

const size_t denominator = num_phases * (num_phases + 1) / 2;
for (size_t phase = 0; phase < num_phases; ++phase) {
    const size_t weight = num_phases - phase;
    const size_t numerator = total_generations * weight;
    shares.push_back({ phase, numerator / denominator, numerator % denominator });
}
```

Before either multiplication, validate that it cannot overflow `size_t`; reject an unrepresentable allocation with `std::invalid_argument("deterministic optimize: phase allocation overflow")`. Sort a copy of indices by `(remainder descending, phase ascending)` for leftover distribution, then return generations in phase-index order.

- [x] **Step 4: Implement the private phase-aware driver**

Create one `deterministic_run_state` at the public call boundary. Pass the actual zero-based phase index, phase generation allocation, shared `remaining_evaluations`, and shared deadline into the existing private deterministic phase routine. Never call a public deterministic overload from inside the adaptive loop.

```cpp
for (size_t phase = 0; phase < num_phases; ++phase) {
    apply_deterministic_phase_baseline(saved_config, phase, num_phases);
    allocations[phase] += carried_generations;
    const auto outcome = run_deterministic_phase(
        run_state, phase, allocations[phase], current_population_size,
        max_history, evaluate);
    carried_generations = outcome.unused_generations;
    if (outcome.reason == stop_reason::target_reached
        || outcome.reason == stop_reason::evaluation_budget)
        break;
    apply_deterministic_adaptive_rules(
        make_result_snapshot().first, previous_stats, phase, num_phases,
        initial_population_size, current_population_size);
}
```

For multi-phase calls, `apply_deterministic_phase_baseline` runs before each phase and reproduces the current `optimize_imp` formulas from `saved_config`; the one-phase short-circuit bypasses it. Unused generations roll into the next phase. `target_reached` and `evaluation_budget` terminate the whole call; deterministic stagnation/diversity/elite-convergence outcomes feed the existing adaptive rules before the next phase. Restore `config` and `stop_criteria` on normal return and exception with an RAII guard local to deterministic adaptive execution.

`apply_deterministic_adaptive_rules` receives the cumulative current snapshot and `previous_stats` by reference, computes the same evaluation/cache deltas and configuration changes as legacy `optimize_imp`, then replaces `previous_stats` with the current snapshot.

- [x] **Step 5: Preserve one-phase identity and add parallel adaptive stress**

Special-case `num_phases == 1` by invoking the same private routine as direct optimize with phase zero, the full budgets, and the same deadline. Do not apply baseline adaptive scaling in that branch.

```cpp
if (num_phases == 1) {
    return run_deterministic_phase(run_state, 0,
        run.generation_budget, initial_population_size, max_history, evaluate);
}
```

For four phases, run repeated four-thread optimizations with forced completion-order variation. Assert identical normalized stats, history, phase allocation, final population bytes, and adaptive stop reason. Add a small-budget case proving evaluation budget remains global rather than partitioned.

Add an early-phase deterministic stagnation case whose unused generation share is nonzero. Assert the next phase receives that exact carry and the sum of committed plus finally unused generations equals the original global generation budget.

- [x] **Step 6: Run Debug and Release and commit Task 7**

```powershell
git add -- algorithm/genetic_optimization.h test/algorithm_test.cpp
git commit -m "feat: add deterministic adaptive GA execution"
```

Expected: direct/one-phase identity and repeated adaptive parallel runs pass.

### Task 8: Complete documentation, compatibility checks, and full verification

**Files:**
- Modify: `algorithm/genetic_optimization.h:45-625,2350-2639`
- Test: `test/algorithm_test.cpp`

**Interfaces:**
- Documents all public deterministic overloads, budgets, stream identity, tie policy, memo behavior, timeout failure, and reproducibility exclusions.
- Produces no new runtime interface beyond Tasks 1-7.

- [x] **Step 1: Update the header's public design commentary**

Add complete serial, parallel, and adaptive deterministic examples using:

```cpp
auto [stats, history] = optimizer.optimize(
    thread_pool,
    deterministic_ga_options{
        0x123456789abcdef0ULL,
        250,
        100'000,
        std::chrono::minutes{ 10 } },
    200,
    50);
```

State that legacy overloads use physical thread-local RNGs; deterministic overloads use logical streams. Document per-call budget semantics, full-generation admission, memo-key collision inheritance, stable tie ordering, controller memo insertion, target purity, elapsed exclusion, same-thread-count guarantee, cross-toolchain exclusion, and serialization exclusion.

- [x] **Step 2: Update report text and exhaustiveness checks**

Ensure both budget stop reasons have names and descriptions, timeout never records `deadline`, and the report does not retain or print transient seed/options. Compile with warnings-as-errors so missing enum switch cases or unreachable overloads fail the build.

- [x] **Step 3: Run archive and API compatibility checks**

Run the complete Debug suite and confirm the manual legacy archive test passes. Inspect the serialized call in `genetic_optimization_t::serialize` and verify its argument order is unchanged:

```text
config, adaptive_config, stop_criteria, stats_, history_, population_,
target_fitness, prev_best_, fn_call_count_, total_eval_requests_
```

Use `git diff 1b37273 -- algorithm/genetic_optimization.h` to verify no deterministic options, RNG cursor, deadline, or memo contents entered serialization.

- [x] **Step 4: Run full Debug and Release verification**

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /v:minimal
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Expected: both rebuilds exit 0; the complete test executable reports zero failures in both configurations.

- [x] **Step 5: Run Release reproducibility stress three times**

```powershell
1..3 | ForEach-Object {
    & '.\exe\x64\Release\yadro_test.exe'
    if ($LASTEXITCODE -ne 0) { throw "Release reproducibility run $_ failed" }
}
```

Expected: all three complete suites exit 0 with identical deterministic-GA assertions despite scheduling variation.

- [x] **Step 6: Perform final source and repository checks**

```powershell
git diff --check
git status --short --branch
git diff --stat 1b37273..HEAD
rg -n "thread_rng\(\)|evaluate_chromosome\(|get_or_compute\(" algorithm/genetic_optimization.h
rg -n "generation_budget|evaluation_budget|genetic_optimization_timeout|try_get|insert_ready_with_hash" `
  algorithm/genetic_optimization.h container/lockfree_memo_table.h test/algorithm_test.cpp test/container_test.cpp
```

Inspect every deterministic call site reported by the first search: deterministic breeding must use `make_deterministic_rng`, and deterministic evaluation must invoke `target_fn_` directly. Confirm the `void` memo specialization diff contains no behavior change.

- [ ] **Step 7: Request independent code review and address findings**

Review against every acceptance criterion in the spec, with special attention to archive layout, exception reset ordering, memo insertion order, discarded-candidate counters, tie polarity, one-phase identity, and scheduling stress. Apply technically valid findings with fresh red/green cycles before the final commit.

- [x] **Step 8: Commit the final documentation and verification adjustments**

```powershell
git add -- algorithm/genetic_optimization.h container/lockfree_memo_table.h `
  test/algorithm_test.cpp test/container_test.cpp
git commit -m "docs: document deterministic genetic optimization"
```

If Step 1-7 leave no uncommitted source or documentation changes, do not create an empty commit. Record exact Debug/Release results, stress-run results, final commit hash, and any environment-only blocker in the execution handoff.
