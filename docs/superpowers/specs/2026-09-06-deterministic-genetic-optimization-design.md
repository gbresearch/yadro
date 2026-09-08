# Deterministic Genetic Optimization Design

## Scope

This change adds an opt-in deterministic execution mode to
`gb::yadro::algorithm::conv::genetic_optimization_t` and its single-threaded,
thread-pool, and adaptive multi-phase optimization paths. It also adds the
lookup-only and ready-value insertion operations required by deterministic
evaluation planning to `lockfree_memo_table` and
`sharded_lockfree_memo_table`. Existing `optimize()` overloads retain their
current time-driven, nondeterministically seeded behavior.

Deterministic mode guarantees identical completed optimization results for the
same:

- initial in-memory optimizer state;
- deterministic seed and deterministic run options;
- GA, adaptive-phase, stopping, wrapper, target, and comparison configuration;
- population and history limits;
- fitness-function results; and
- logical thread count.

The guarantee covers the final population, ordered history, stop reason,
generation and evaluation counters, diversity and recovery counters, and all
other non-time statistics. `optimization_stats::elapsed` is observational data
and is excluded.

The guarantee applies to repeated executions of the same build on the same
supported platform and floating-point environment. Cross-toolchain,
cross-standard-library, cross-architecture, and altered floating-point-mode
reproducibility are not promised because standard random distributions and
floating-point target functions may differ across those boundaries.

The first implementation does not promise continuity across serialization.
The optimizer's memo table is intentionally not serialized today, so a loaded
optimizer is not equivalent to the original in-memory state. No serialized
field is added or reordered as part of this work.

## Goals

- Make deterministic execution explicit and opt-in.
- Remove physical worker identity and task scheduling from random-number
  selection.
- Drive successful completion with discrete generation and evaluation budgets.
- Treat wall time exclusively as an operational failure ceiling.
- Define total, deterministic handling for equivalent fitness values.
- Make parallel evaluation, memo reuse, and exception propagation independent
  of task completion order.
- Preserve existing source and archive compatibility for nondeterministic
  consumers.

## Non-goals

- Changing the behavior or defaults of existing `optimize()` overloads.
- Making arbitrary stateful, nondeterministic, or data-racing fitness functions
  reproducible.
- Bit-identical results across different toolchains, library versions,
  architectures, or floating-point environments.
- Interrupting a fitness invocation that blocks beyond the wall-time ceiling.
- Persisting deterministic continuation state in existing binary archives.
- Replacing `std::mt19937_64` or rewriting every wrapper's sampling algorithms.
- Guaranteeing identical results when the logical thread count changes.
- Correcting the existing memo table's 128-bit key-collision semantics.

## Confirmed Baseline

The current optimizer obtains randomness from `detail::thread_rng()`. That
engine is `thread_local` and is seeded from `std::random_device` and the current
physical `std::thread::id`. Single-threaded initialization, stopping recovery,
and breeding consume the caller thread's engine. Parallel breeding divides the
offspring into worker-count-dependent chunks and consumes whichever worker
engine executes each task. Task stealing and prior work on a reused worker
therefore change the generated population.

The existing public API accepts a duration and an optional `max_tries`. The
duration is a normal successful stop condition. `max_tries` is compared with
the cumulative number of actual fitness calls, but evaluation occurs a complete
population at a time and can exceed the checked value. Parallel memo lookup can
also allow equal chromosomes to race to computation, making call and cache-hit
counts sensitive to scheduling.

Population ranking currently uses `nth_element` followed by `sort` with only
the fitness comparator. Equivalent-fitness individuals therefore have no
specified survivor order. History insertion also ranks only by fitness. These
behaviors are acceptable in legacy mode but cannot form a reproducibility
contract.

The optimizer and `ga_config` use positional serialization. A recent
compatibility repair deliberately removed newly added serialized fields.
Deterministic run policy must therefore remain outside the existing serialized
configuration layout.

## Alternatives

### 1. Explicit deterministic run options and overloads — selected

Add a deterministic options type and overloads that select a deterministic
execution policy. Existing overload resolution and configuration serialization
remain unchanged. The policy shares the GA's selection, crossover, mutation,
stopping, reporting, and adaptive logic while substituting deterministic random,
ranking, evaluation, and budget services.

This makes the compatibility boundary visible at the call site and keeps
deterministic-only requirements out of legacy execution.

### 2. Add a seed and mode flag to `ga_config` — rejected

This is superficially convenient but either changes the positional archive
format or creates public configuration fields that silently disappear during
serialization. It also permits invalid combinations such as deterministic mode
without discrete budgets.

### 3. Add a separate deterministic optimizer type — rejected

A second optimizer could have a clean API but would duplicate the GA's large
selection, stopping, recovery, adaptive, statistics, and reporting surface.
Behavior and bug fixes would predictably diverge.

### 4. Pre-generate all random choices on the calling thread — rejected

This would be deterministic and simple, but it would serialize the existing
parallel breeding path and allocate an additional generation-wide decision
buffer. Logical streams retain parallel breeding without tying results to
physical workers.

## Public API

Add a public run-policy type adjacent to `ga_config`:

```cpp
struct deterministic_ga_options {
    std::uint64_t seed;
    std::size_t generation_budget;
    std::size_t evaluation_budget;
    std::chrono::nanoseconds failure_timeout;

    constexpr deterministic_ga_options(
        std::uint64_t seed,
        std::size_t generation_budget,
        std::size_t evaluation_budget,
        std::chrono::nanoseconds failure_timeout) noexcept;

    auto operator<=>(const deterministic_ga_options&) const = default;
};

class genetic_optimization_timeout : public std::runtime_error {
public:
    genetic_optimization_timeout(
        std::chrono::nanoseconds timeout,
        std::chrono::nanoseconds elapsed);

    [[nodiscard]] std::chrono::nanoseconds timeout() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds elapsed() const noexcept;
};
```

`generation_budget` is the maximum number of fully evaluated generations that
may be committed during one public call. Initial-population completion is not a
generation. `evaluation_budget` is the maximum number of target-function
invocations started during that call; memo hits do not consume it.
`failure_timeout` is required, must be positive, and is not a search budget.
The options type intentionally has no default constructor or default member
initializers: selecting deterministic mode requires the caller to supply every
part of its execution contract. Semantic validation still occurs at optimize
call entry so invalid options cannot mutate an optimizer.

`genetic_optimization_timeout` is declared in
`algorithm/genetic_optimization.h`, derives publicly from
`std::runtime_error`, and carries the configured timeout and observed elapsed
duration. Its `what()` text identifies deterministic genetic optimization and
formats both durations. The payload lets callers handle the failure without
parsing text.

Add deterministic overloads corresponding to each existing public shape:

```cpp
auto optimize(const deterministic_ga_options& run,
    std::size_t population_size,
    std::size_t max_history = std::numeric_limits<std::size_t>::max());

template<detail::DeterministicThreadPool ThreadPool>
auto optimize(ThreadPool& tp,
    const deterministic_ga_options& run,
    std::size_t population_size,
    std::size_t max_history = std::numeric_limits<std::size_t>::max());

auto optimize(std::size_t num_phases,
    const deterministic_ga_options& run,
    std::size_t initial_population_size,
    std::size_t max_history = std::numeric_limits<std::size_t>::max());

template<detail::DeterministicThreadPool ThreadPool>
auto optimize(ThreadPool& tp,
    std::size_t num_phases,
    const deterministic_ga_options& run,
    std::size_t initial_population_size,
    std::size_t max_history = std::numeric_limits<std::size_t>::max());
```

The exact return type remains
`std::pair<optimization_stats, history_t>`. The deterministic overloads do not
accept the legacy duration or `max_tries`; accepting both policy families in one
call would create competing stop semantics.

The internal exposition-only `detail::DeterministicThreadPool` concept requires
that `tp.thread_count()` be convertible to `std::size_t`. Normal template
instantiation continues to verify that the pool accepts the evaluation and
breeding callables. The explicit count constraint both states the deterministic
contract and prevents an lvalue `std::size_t` from matching a deterministic
thread-pool overload where the phase-count overload is intended.

The single-threaded overload has one logical breeding stream. Deterministic
thread-pool overloads require `tp.thread_count()` to produce a positive integral
value and capture it once at call entry. That value is the logical thread count
for the complete call. A pool without that observable contract remains usable
through the legacy overloads but is not accepted by deterministic parallel
overloads.

`deterministic_ga_options` is a transient execution policy. It is deliberately
not a member of `ga_config`, is not part of `genetic_optimization_t::serialize`,
and is not included in reports that describe persistent optimizer state. The
report prints the resulting budget stop reason through existing statistics but
does not retain or print the last deterministic seed, budgets, or timeout. The
caller owns that transient run policy.

## Validation

At call entry deterministic mode validates:

- `population_size > 0`;
- `generation_budget > 0`;
- `evaluation_budget > 0`;
- `failure_timeout > 0ns`;
- the existing GA and stopping configurations; and
- for parallel execution, `tp.thread_count() > 0`.

Generation and evaluation budgets are per public call, not cumulative absolute
limits. The implementation snapshots cumulative counters on entry and accounts
against call-local deltas. Returned statistics remain cumulative to preserve the
existing optimizer contract.

An existing warm population may contain unevaluated chromosomes, and a fresh
run must create and evaluate its initial population before any generation can
be committed. Deterministic mode first constructs that candidate initial state
and its stable evaluation plan without invoking the target. If the complete
plan exceeds the call's evaluation budget, it throws `std::invalid_argument`
before invoking the target function or committing the candidate population.
This avoids a successful result whose baseline population depends on partial
evaluation.

## Deterministic Random Streams

Physical thread IDs, task start order, task completion order, and prior random
work on a reused thread pool never participate in deterministic seeding.

Every engine is derived from a stable stream key:

```text
(seed, phase, generation, operation_domain, logical_stream)
```

The key components have fixed-width unsigned representations. A documented,
repository-owned mixing function expands the key into the complete
`std::mt19937_64` seed state. It must not use `std::hash`, because its mapping is
not a portable persistence contract. Domain constants are named fixed values;
adding a new domain must not renumber existing domains.

The initial-population, normal-breeding, cataclysm, and elite-perturbation
domains are distinct. Random draws added to one operation cannot shift another
operation's stream.

For normal parallel breeding, the active logical stream count is
`min(num_offspring, logical_thread_count)`. `logical_stream` ranges from zero to
that active count minus one. Offspring slots are assigned by the same fixed
ceiling-division contiguous partition shape used by the legacy breeder,
computed solely from offspring count and active logical stream count. Each
logical task owns one engine and writes only its assigned output indices. The
thread pool may execute any logical task on any physical worker. The mapping and
output are unchanged by stealing or scheduling. Thread counts at least as large
as `num_offspring` therefore produce the same stream partition.

Control-path operations use logical stream zero in their own operation domain.
The single-threaded path uses the same partition and stream derivation with a
logical thread count of one rather than consuming `detail::thread_rng()`.

The adaptive path uses zero-based phase indices in stream keys. Within a phase,
generation is the phase-local committed-generation index. A direct public
deterministic call uses phase zero. Each new public call starts its call-local
phase and generation numbering from zero; the equivalent initial optimizer
state is part of the reproducibility key.

Legacy paths continue to use `detail::thread_rng()` and retain their current
behavior.

## Deterministic Population Order and Ties

Before its first rank, a fresh deterministic population is in initial-generation
index order, including injected chromosomes followed by newly generated slots.
The existing initial `feed_history_from_population()` occurs once in that
unsorted canonical order. After deterministic ranking, the entire population is
fitness ordered, with ties stable in their pre-rank order.

Each post-breed deterministic population is constructed in this canonical
logical order:

1. retained elites in deterministic rank order;
2. offspring in ascending logical offspring index.

For deterministic ranking, fitness is compared first with `CompareFn`. Two
fitness values are equivalent when neither `compare(a, b)` nor `compare(b, a)`
is true. Equivalent individuals retain their prior canonical population order.
The implementation uses a stable full ordering operation; it does not use
`nth_element`, whose choice among equivalent elements is unspecified.

Tournament selection retains the first sampled candidate when fitness values
are equivalent. It never replaces an incumbent merely because the challenger
is equivalent.

History observation processes the current canonical population from lowest to
highest logical index and reuses `optimization_history::try_insert` unchanged.
That method uses `lower_bound`, so each newly accepted observation is inserted
before all existing entries with equivalent fitness. Within one population
scan, later observed tied entries consequently precede earlier observed tied
entries. If history is already at capacity, its existing reject-before-insert
rule still applies. Existing chromosome-key duplicate rejection also remains in
force. This polarity is deterministic given the fixed observation order and
preserves legacy history behavior and serialized vector meaning. Tests pin the
exact resulting order.

These tie rules require only the existing fitness comparator; chromosome value
types do not gain a new ordering requirement.

The full deterministic rank also defines cataclysm survivors. A cataclysm keeps
the fitness-ranked prefix `[0, survivor_count)`, including positions beyond
`elite_n` when `cataclysm_survival_fraction` exceeds `elitism_fraction`. This is
an intentional deterministic-mode difference from the legacy partial
`nth_element` ordering.

## Memo-Table Prerequisites

Deterministic budget admission needs to distinguish a ready memo hit from a
missing key without invoking the stored function or claiming a slot. Add
lookup-only operations to the value-returning plain and sharded memo tables:

```cpp
std::optional<Value> try_get(Args&&... args) const;
std::optional<Value> try_get_with_hash(
    std::uint64_t h_lo, std::uint64_t h_hi) const;
```

The public sharded `try_get` computes the key once and chooses
`mix64(h_hi) & ShardMask`, exactly like `get_or_compute`, before delegating to
the selected plain table. The all-zero-key normalization and linear probe
sequence are also identical to `get_or_compute_with_hash`.

The lookup never claims an empty slot and never invokes `func_`. A matching
`State::ready` returns a copy of the stored value. A matching
`State::computing` waits for that state to become ready or for the slot to be
reset; it then returns the ready value or resumes the probe respectively. A
matching `h_lo` whose state is still `State::empty` is treated as the existing
key-publication window: lookup rechecks `h_lo` while waiting for `h_hi` and
state publication or observes a reset rather than reporting a false miss. It
must not use `atomic::wait` on `State::empty`, because the next changed word can
be `h_lo`. A different key continues probing. A virgin empty slot returns
`std::nullopt`, and exhausting `max_probe_` throws the table's existing
probe-limit error unless a tombstone already proves the searched chain can
contain a missing key. Although a valid optimizer boundary has no outstanding
memo computations, defining the concurrent case keeps the container API
correct on its own.

Failed value computations publish `State::tombstone`, reset `h_lo` to zero
with release ordering, and only then call `state.notify_all()`. Waiters never
wait on `State::empty` or `State::tombstone`: both are publication/reset states
where `h_lo` may be the next changed word. Tombstones preserve open-addressing
probe chains. Lookup continues past them. Insertion records the first tombstone
while it continues the exact-key search. A virgin empty terminator is always
claimed directly, so concurrent same-key inserters converge on the same CAS
rather than choosing different tombstone/virgin slots from stale observations.
The first tombstone is reused only after a full probe that observed no unstable
different-key entry. This includes a computing entry with the requested low hash
but a different high hash. For a different low hash, insertion accepts a ready
state as stable only after reloading the low hash and confirming that the state
and hash observations still describe the same entry. If a full probe saw an
entry still being published or computed, insertion yields and retries because
that entry may itself become the first tombstone. This prevents false misses and
duplicate insertion behind a failed slot without blocking on an unrelated
colliding computation.

Parallel target evaluation must not make memo occupancy depend on completion
order. Add corresponding `insert_ready_with_hash(h_lo, h_hi, value)` operations
to the value-returning plain and sharded tables. They insert a precomputed value
without invoking `func_`; the sharded form uses the identical shard mapping. A
matching ready key preserves and returns the table's existing value. A matching
computing key waits under the same rule as `try_get`. New distinct keys claim
slots using the same stable-scan rule, including direct virgin-slot claims and
deferred tombstone reuse after an exact-key search with no unstable entry.

```cpp
Value insert_ready_with_hash(
    std::uint64_t h_lo, std::uint64_t h_hi, Value value) const;
```

Deterministic GA code invokes target functions without writing the memo from
worker tasks, drains their results, and calls `insert_ready_with_hash` on the
controller in ascending representative index. Memo occupancy and probe-limit
success or failure are therefore deterministic. The deterministic path must not
call `evaluate_chromosome()`: that helper routes through `get_or_compute`, which
would let worker completion order determine memo insertion order.

The value-returning table's tombstone reset protocol is an intentional fix to
pre-existing `get_or_compute` lost-wakeup and probe-chain defects and therefore
changes legacy exception concurrency behavior from potentially hanging or
missing a colliding entry to making progress while preserving the chain. Its
existing success, value, collision, and exception contracts remain unchanged.
The `void` specialization has the same notification-before-reset defect, but it
is not used by the GA and is explicitly outside this change; repairing it
requires separate coverage for its configurable fire-and-forget and
wait-for-completion behavior.

The concrete GA memo type that consumes the new sharded forwarding operations
is `sharded_lockfree_memo_table<xxhash128, memo_fn_t, target_t,
/*NumShards=*/128>` in `algorithm/genetic_optimization.h`. Both the plain
value-returning table and this sharded wrapper must expose the required API.

Tests for these container APIs belong in `test/container_test.cpp` and cover
ready hit, miss, normalized all-zero key, collision probing, matching computing
wait/reset, sharded routing, stable ready insertion, probe exhaustion, and a
full probe whose unstable collision later becomes the reusable tombstone,
including collisions that share a low hash but differ in their high hash.

## Deterministic Evaluation

Before invoking the fitness function, deterministic mode builds an evaluation
plan in ascending canonical population index. When memoization is enabled, it
computes the memo table's normalized 128-bit key for every unevaluated
chromosome and groups equal keys. Each group has a representative at its lowest
population index. This deliberately inherits the memo table's existing
collision semantics: distinct chromosomes with the same 128-bit key share one
cached value, with the lowest population index selecting the deterministic
representative.

When `config.memo_capacity == 0`, memoization is disabled and the plan does not
group entries. Every unevaluated population entry is its own representative and
consumes one target invocation if admitted.

The plan then resolves groups already present in the memo and identifies the
remaining target invocations. Budget admission occurs against that stable list
before any invocation for the population begins. Admitted representatives may
be evaluated in parallel, but results are drained and applied in ascending
representative index. Every duplicate receives its representative's result.

For memo-enabled runs, the planner calls `try_get_with_hash` for each key group
in representative-index order. Ready groups use the cached value. Missing group
representatives are the stable list used for budget admission and parallel
target invocation. Results are drained in representative order, inserted into
the memo in that same order, and then copied to every member of the group. The
planner uses the value returned by `insert_ready_with_hash`, which is the
already-cached value if a matching ready key appeared concurrently.

The deterministic planning layer prevents equal memo keys from racing to invoke
the target independently. Consequently `total_evaluations` and `cache_hits` do
not depend on worker scheduling. Cache hits retain the existing meaning of
evaluation requests that do not call the target. The planner increments
`total_eval_requests_` once for every previously unevaluated population entry
and `fn_call_count_` once for each target invocation it starts. In a
memo-enabled run, every request other than an admitted uncached representative
is a cache hit; in a memo-disabled run every request is a target invocation.
Plan construction and lookup are counter-neutral. Request counters advance only
after the complete plan passes budget admission, so a discarded candidate does
not affect cumulative statistics.

If multiple tasks throw, all submitted tasks are drained and deterministic mode
rethrows the exception belonging to the lowest representative population index.
Higher-index completion order cannot select the visible exception. The current
population remains at the last fully committed boundary.

Deterministic mode assumes the target function:

- returns the same value for the same chromosome;
- does not base results on time, worker identity, invocation order, or mutable
  shared state; and
- is safe for the selected parallel execution.

Violating these preconditions is outside the reproducibility guarantee.

## Budget and Commit Semantics

Successful progress is controlled only by discrete counters and deterministic
stopping criteria. The wall clock never chooses a successful final population.

Initial-population completion is an atomic prerequisite. Each subsequent
generation follows this sequence:

1. Check deterministic stopping criteria against the last committed population.
2. If `generation_budget` is exhausted, stop with `generation_budget`.
3. Generate the entire candidate population with logical RNG streams.
4. Build its stable evaluation plan without invoking the target.
5. If the plan needs more target invocations than remain in
   `evaluation_budget`, discard the candidate and stop with
   `evaluation_budget`.
6. Evaluate the admitted plan and apply results in canonical index order.
7. Commit the fully evaluated population and increment the generation count.

No partially evaluated generation becomes a successful result. Random draws
used for a discarded candidate do not matter because the public call terminates
at that boundary.

Add terminal `stop_reason` values `generation_budget` and
`evaluation_budget`. They are appended to the enum so existing numeric values
remain unchanged. Existing `deadline` and `max_tries` meanings remain unchanged
for legacy calls. The new values participate in reporting and serialization
through the existing integer stop-reason field without changing field layout.
`stop_reason_name`, `stop_reason_description`, and every exhaustive report switch
gain explicit cases for both values.

Target, stagnation, diversity, and elite-convergence criteria remain active in
deterministic mode because their decisions derive from deterministic committed
state. Cataclysm and elite perturbation remain nonterminal and use their own
logical RNG domains. If recovery introduces unevaluated chromosomes, its
evaluation is admitted atomically against the remaining evaluation budget
before recovery is committed.

## Wall-Time Failure Ceiling

`failure_timeout` protects operations rather than controlling search quality.
The implementation checks a single steady-clock deadline:

- before initialization work;
- before submitting each evaluation batch;
- after draining each evaluation batch;
- before breeding or recovery; and
- before committing a completed generation.

If the ceiling is reached, deterministic mode throws a dedicated
`genetic_optimization_timeout` exception. It does not return a normal result and
does not record `stop_reason::deadline`, because a timeout run is not a
reproducible successful optimization outcome.

Already running target invocations are drained before throwing, matching the
existing lifetime-safety rule for futures. The ceiling therefore cannot forcibly
interrupt or bound one blocking target call. After a timeout, the optimizer is
left at its last fully committed population boundary, cumulative evaluation
counters reflect invocations that actually occurred, and elapsed time reflects
observed work. The caller may inspect or reset that failed state, but it is not
covered by the identical-result guarantee.

## Adaptive Multi-Phase Mode

Adaptive deterministic mode replaces duration fractions with integer generation
allocation. At call entry it distributes the generation budget across phases
using the existing triangular phase weights. Allocation uses integer arithmetic
and a largest-remainder rule with lower phase index breaking equal remainders.
The allocations sum exactly to the global generation budget.

The evaluation budget remains one global call-local ceiling rather than being
partitioned. This prevents an arbitrary phase boundary from rejecting a full
generation that fits the remaining call budget. Each phase observes the same
decreasing remaining-evaluation counter.

Each phase receives its fixed generation share, while unused generations from
an early deterministic stop roll forward to the next phase. A target-reached or
evaluation-budget result ends the full call. Adaptive configuration changes
continue to derive from per-phase statistics and are therefore deterministic.

A phase with a zero generation share may still complete or reuse the current
population and apply its deterministic stopping checks, but it cannot commit a
new generation.

The single shared failure deadline covers the complete adaptive call; it is not
partitioned by phase.

The deterministic adaptive overload does not delegate each phase back through a
public deterministic overload. Instead, direct and adaptive entry points are
thin wrappers over a private phase-aware routine that receives the actual
zero-based `phase_index`, that phase's generation allocation, the shared
remaining evaluation budget, and the shared steady-clock deadline. This keeps
the phase component of every stream key intact and prevents per-phase budget or
timeout resets.

The existing `num_phases == 1` short-circuit is preserved semantically and
bit-for-bit: it calls the same private routine as direct deterministic optimize
with phase zero, the full generation and evaluation budgets, and the same
deadline. A one-phase adaptive call and a direct deterministic call from
equivalent state therefore produce identical completed results.

## State, Reset, and Warm Restart

A deterministic call may start from a fresh optimizer, an injected population,
or a warm in-memory population. The complete starting population, history,
statistics, memo key/value contents and slot occupancy, recovery baselines, and
public configuration are part of the initial state for the reproducibility
contract. Two optimizers whose memo tables were populated in different legacy
concurrent orders are not equivalent merely because their visible populations
and histories match. Fresh optimizers and optimizers advanced through the same
deterministic call sequence acquire the same memo insertion order.

Calling deterministic `optimize()` twice with the same seed on two equivalent
optimizer instances is reproducible. Calling it twice successively on one
instance is a warm restart from different state and is not expected to repeat
the first call's result; two instances subjected to the same call sequence do
remain reproducible.

`soft_reset()` and `clear()` retain their existing semantics. Because
`soft_reset()` preserves the memo and `clear()` does not, states produced by the
two operations are not equivalent for deterministic accounting.

No deterministic RNG engine or stream cursor is stored between calls. Stream
keys are reconstructed from the explicit seed and call-local logical
coordinates. This avoids adding serialization state and makes each public call
self-contained relative to its starting optimizer state.

## Compatibility

- Existing `optimize()` signatures, overload resolution, defaults, and
  successful nonexception runtime behavior remain unchanged.
- The value-returning memo table intentionally adds a tombstone reset protocol
  for existing `get_or_compute` callers, fixing exception-reset wakeup and
  probe-chain behavior; the `void` specialization is unchanged and outside
  scope.
- `ga_config`, `adaptive_phase_config`, wrapper, and optimizer serialization
  field order remain unchanged.
- Existing stop-reason numeric values remain unchanged; new values are appended.
- Existing thread pools without `thread_count()` remain supported by legacy
  parallel optimization.
- Deterministic mode does not strengthen `GeneticWrapper` value-type ordering
  requirements.
- Legacy ranking and parallel memo behavior need not be rewritten unless a
  shared refactor can be proven behavior-preserving.

## Test Strategy

GA tests belong in `test/algorithm_test.cpp`; memo-table prerequisite tests
belong in `test/container_test.cpp`. GA coverage uses small discrete search
spaces and short, fixed budgets. Timing is used only in timeout-specific tests.

### API and compatibility

- Existing optimize calls compile unchanged and continue to accept durations
  and `max_tries`.
- `deterministic_ga_options` requires all four constructor arguments and has no
  value-initialized default state.
- Deterministic overloads reject zero population, budgets, timeout, or logical
  thread count.
- A pool lacking `thread_count()` is rejected only by deterministic parallel
  overload resolution.
- An lvalue `std::size_t` phase count cannot bind as a deterministic thread pool.
- Existing binary archive fixtures load unchanged.
- A deterministic run followed by archive save/load does not add archive fields.
- `genetic_optimization_timeout` derives from `std::runtime_error` and exposes
  its timeout and elapsed payload without parsing `what()`.

### Repeated-run reproducibility

- Two fresh single-threaded optimizers with the same seed, configuration, and
  budgets produce identical population-observable results, ordered history,
  stop reason, and non-time statistics.
- Repeated parallel runs with the same seed, configuration, and four-thread
  pools produce the same result across many iterations.
- Fitness tasks use deterministic gates and varied delays to force different
  physical scheduling and completion orders without changing results.
- Reusing a thread pool after unrelated tasks does not perturb deterministic
  output.
- Equivalent warm optimizer instances subjected to the same sequence of
  deterministic calls remain identical.
- Different seeds are shown to produce different generated populations in a
  search space where collision is negligible; this is a sanity check, not a
  universal mathematical requirement.

### Logical streams

- A test-only stream probe pins seed derivation for representative phase,
  generation, domain, and logical-stream keys.
- Parallel offspring slots are written by their fixed logical partitions even
  when logical tasks run on unexpected physical workers.
- Random draws in cataclysm or elite perturbation do not shift normal-breeding
  streams for a later key.
- Results for different logical thread counts are allowed to differ; each count
  is independently reproducible.
- When `logical_thread_count >= num_offspring`, increasing the count further
  leaves the active stream partition and result unchanged.

### Tie handling

- A constant-fitness target produces a pinned elite order across repeated runs.
- Tournament selection retains the first sampled candidate on equivalent
  fitness.
- The initial unsorted history feed and later ranked feeds produce the pinned
  lower-bound insertion polarity for tied fitness across scheduling variations.
- An all-ties population never depends on `nth_element` partition choices.
- Cataclysm retains the deterministic full-sort survivor prefix when its
  survivor count exceeds the elite count.

### Evaluation and memoization

- Duplicate uncached memo keys produce exactly one target invocation per key
  group in both single-threaded and parallel deterministic modes.
- Forced 128-bit key collisions choose the lowest-index representative and
  inherit the existing memo collision value semantics deterministically.
- With `memo_capacity == 0`, equal chromosomes remain separate requests and
  each admitted entry invokes the target.
- Pre-existing memo entries consume no evaluation budget.
- Evaluation and cache-hit counters match across varied completion orders.
- Ready lookup and controller-ordered insertion keep memo occupancy and
  probe-limit outcomes independent of worker completion order.
- A value-returning `get_or_compute` waiter wakes and retries after the computing
  thread throws; the test pins reset-state, reset-key, then notify ordering.
- Deterministic evaluation does not call `evaluate_chromosome()` and worker
  tasks do not mutate the memo table.
- Results are applied by representative population index rather than completion
  order.
- Multiple evaluation failures rethrow the lowest-index exception after all
  futures are drained.

### Budgets and stopping

- Initial population admission fails before target invocation when it cannot fit
  the evaluation budget.
- Exactly the configured number of generations is committed when no
  deterministic stopping criterion or evaluation ceiling ends the run earlier.
- A candidate generation that cannot fit the remaining evaluation budget is
  discarded without partial population state, request counters, or a generation
  increment.
- Target, stagnation, diversity, cataclysm, and elite-perturbation paths remain
  reproducible at their budget boundaries.
- Generation and evaluation budgets reset per public call while returned
  counters remain cumulative.
- Adaptive generation allocations sum exactly to the global generation budget,
  tie remainders by phase index, and roll unused shares forward
  deterministically; evaluation admission uses the global remaining budget.
- A one-phase adaptive deterministic call is bit-identical to the corresponding
  direct deterministic call.

### Failure ceiling

- A generous timeout never changes a budget-completed result.
- A deliberately blocked or slow evaluation causes
  `genetic_optimization_timeout` after submitted work drains.
- Timeout does not return `stop_reason::deadline` or a normal partial result.
- State after timeout is the last fully committed population boundary, with
  actual invocation counters retained.

### Verification

Focused deterministic GA tests run first in Debug and Release x64. The complete
Yadro test executable then runs in both configurations. Reproducibility stress
tests run repeatedly in Release with forced scheduling variation. Final review
compares the public header documentation, overload set, report strings, enum
serialization, and tests against every contract in this specification.

## Documentation

The large design commentary in `algorithm/genetic_optimization.h` must be
updated with:

- the opt-in deterministic overload examples;
- the distinction between search budgets and the failure timeout;
- the logical-stream key and partition rule;
- deterministic tie, evaluation, and commit semantics;
- fitness-function and platform preconditions; and
- the serialization and cross-thread-count exclusions.

Comments that describe physical per-thread RNGs remain correct only for legacy
mode and must be labeled accordingly. No documentation may imply that supplying
a seed to a legacy overload makes it deterministic.

## Acceptance Criteria

The change is complete when:

- legacy callers retain their current API, defaults, archive layout, and
  nondeterministic behavior;
- deterministic callers provide an explicit seed, positive generation and
  evaluation budgets, and a positive failure timeout;
- no deterministic random decision depends on physical thread identity or task
  scheduling;
- deterministic worker tasks never call `evaluate_chromosome()` or mutate the
  memo table;
- value-returning memo exception reset clears state and key before notifying
  waiters, while the `void` specialization remains unchanged;
- successful deterministic calls use only discrete budgets and deterministic
  stop criteria to select their final state;
- equivalent-fitness ranking and history order are specified and tested;
- same-seed runs with identical configuration and logical thread count produce
  identical completed results under deliberately varied scheduling;
- timeout is reported as failure rather than normal partial success; and
- focused and full Debug and Release x64 verification passes.
