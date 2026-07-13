# Threadpool Eventcount Wakeup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `gb::yadro::async::threadpool`'s timed polling and `tasks_in_system_` wait predicate with a provably lossless atomic eventcount protocol while preserving eager worker creation and all existing task, idle, continuation, and shutdown contracts.

**Architecture:** Every initial publication and re-publication increments one cache-line-separated `std::atomic<std::uint64_t>` generation and notifies waiters. A worker snapshots the generation, performs a conclusive all-source search, and then calls `atomic::wait(snapshot)`; batch-steal and inbox-migration paths re-publish after making their in-hand batches visible. `tasks_in_system_` remains exclusively responsible for drain and `on_idle` semantics.

**Tech Stack:** Microsoft Visual Studio 2026 C++ compiler, C++23 via `/std:c++latest`, `std::atomic::wait/notify`, Chase-Lev work-stealing deque, existing `GB_TEST` harness, Release x64 MSBuild.

## Global Constraints

- Work only in the Yadro repository; do not modify downstream consumers.
- Keep worker creation eager and preserve the observable `thread_count()` result.
- Keep `tasks_in_system_` accounting unchanged for queued plus executing work, graceful drain, and `on_idle`.
- Start with sequentially consistent operations on `work_generation_`; do not relax ordering in this implementation.
- Treat every inbox-to-deque and batch-steal re-push as a publication.
- Remove `WorkerCtl::cv`; retain `WorkerCtl::mutex` as the inbox guard.
- Do not add a timed fallback.
- Use test-first red/green cycles for every behavior change.
- Preserve the unrelated untracked `.claude/` directory.
- `.git` metadata is read-only in the current workspace. Run the listed commit commands only if that permission changes; otherwise report the exact blocker and leave commits to the user.

---

## File map

- Modify `async/threadpool.h`: eventcount state, publication helpers, conclusive search, parking, shutdown wakeup, private parking observations, and updated design commentary.
- Modify `test/async_test.cpp`: test accessor plus deterministic and stress regressions for parking, publication, continuations, nested waits, idle, and shutdown.
- No change to `async/chase_lev_deque.h` is expected; its deque algorithm and memory ordering stay untouched.
- No project-file or downstream-consumer change is expected.

### Task 1: Add an observable failing regression for periodic wakeups

**Files:**
- Modify: `test/async_test.cpp:39-43,214-222,435-460`
- Modify: `async/threadpool.h:171-172,545-557,1116-1142`

**Interfaces:**
- Produces: internal `gb::yadro::async::detail::threadpool_test_access` with `parked_workers(const threadpool&)` and `park_returns(const threadpool&)`.
- Produces: private `threadpool::parked_workers_` and `threadpool::park_returns_` counters. They are not a public production API.

- [x] **Step 1: Write the failing empty-pool regression against the wished-for internal accessor**

Register this test after the existing `wait_for` helper. Do not add the accessor yet:

First, define the test-only accessor switch before any includes in `test/async_test.cpp`:

```cpp
#define GB_YADRO_THREADPOOL_TESTING 1
```

```cpp
GB_TEST(async, eventcount_empty_pool_parks_without_polling) {
    threadpool pool{4};
    wait_for([&] {
        return gb::yadro::async::detail::threadpool_test_access::parked_workers(pool) == pool.thread_count();
    });

    const auto returns = gb::yadro::async::detail::threadpool_test_access::park_returns(pool);
    std::this_thread::sleep_for(100ms);

    gbassert(gb::yadro::async::detail::threadpool_test_access::parked_workers(pool) == pool.thread_count());
    gbassert(gb::yadro::async::detail::threadpool_test_access::park_returns(pool) == returns
        && "parked workers returned without a publication");
}
```

- [x] **Step 2: Run the Release x64 build and verify the accessor is missing**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Build /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Expected: compile failure because `detail::threadpool_test_access` does not exist. This is the first RED result.

- [x] **Step 3: Add only the narrow parking instrumentation around the current timed wait**

Declare the internal accessor beside the existing forward declaration:

```cpp
class threadpool;
namespace detail { struct threadpool_test_access; }
```

Inside `threadpool`, add the friend and counters without changing the wait predicate:

```cpp
friend struct detail::threadpool_test_access;

alignas(kCacheLineSize) std::atomic<std::size_t> parked_workers_{0};
alignas(kCacheLineSize) std::atomic<std::uint64_t> park_returns_{0};
```

Wrap the current `wait_for` call as follows:

```cpp
parked_workers_.fetch_add(1);
my_ctl.cv.wait_for(lk, std::chrono::microseconds{200}, [&] {
    return stop_.load(std::memory_order_relaxed)
        || !my_ctl.inbox.empty()
        || tasks_in_system_.load(std::memory_order_acquire) > 0;
});
parked_workers_.fetch_sub(1);
park_returns_.fetch_add(1);
```

Define the internal accessor after the class definition only for the test translation unit:

```cpp
#ifdef GB_YADRO_THREADPOOL_TESTING
namespace detail {
    struct threadpool_test_access {
        [[nodiscard]] static std::size_t parked_workers(
            const threadpool& pool) noexcept {
            return pool.parked_workers_.load();
        }

        [[nodiscard]] static std::uint64_t park_returns(
            const threadpool& pool) noexcept {
            return pool.park_returns_.load();
        }
    };
}
#endif
```

- [x] **Step 4: Rebuild and verify the behavioral test now fails for the intended reason**

Run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Build /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Expected: the test executable runs, and `async.eventcount_empty_pool_parks_without_polling` fails because `park_returns` increases during the 100 ms observation window. No production wakeup behavior has been fixed yet.

- [ ] **Step 5: Commit the test and instrumentation if Git metadata is writable**

Blocked: this workspace permits reading `.git` but not creating `.git/index.lock`.

```powershell
git add -- async/threadpool.h test/async_test.cpp
git commit -m "test: expose threadpool parking regressions"
```

Expected in the current workspace: `index.lock: Permission denied`. Do not work around that by changing repository permissions.

### Task 2: Implement the atomic eventcount and conclusive pre-park search

**Files:**
- Modify: `async/threadpool.h:151-169,561-635,767-821,860-985,1116-1142`
- Test: `test/async_test.cpp`

**Interfaces:**
- Produces: private `void signal_work(std::size_t visible_tasks = 1) noexcept`.
- Produces: private `void signal_all_workers() noexcept`.
- Produces: private `InboxProbe migrate_inbox(WorkerCtl&, WorkStealingDeque<TaskBase*>&, bool) noexcept`.
- Produces: private `StealResult steal_batch_and_run(WorkStealingDeque<TaskBase*>&, WorkStealingDeque<TaskBase*>&) noexcept`.
- Consumes: parking counters and test accessor from Task 1.

- [x] **Step 1: Add failing parked-wakeup, local-wait, and blocking-task tests**

Add `#include <utility>` directly to the include block in `test/async_test.cpp`. Add this test-only guard beside the existing `wait_for` helper:

```cpp
template <typename F>
struct test_scope_exit {
    F cleanup;
    bool active{true};
    ~test_scope_exit() { if (active) cleanup(); }
    void release() noexcept { active = false; }
};
template <typename F> test_scope_exit(F) -> test_scope_exit<F>;
```

Add these tests after the empty-pool test:

```cpp
GB_TEST(async, eventcount_external_submission_wakes_parked_worker) {
    threadpool pool{4};
    wait_for([&] {
        return gb::yadro::async::detail::threadpool_test_access::parked_workers(pool) == pool.thread_count();
    });

    auto task = pool.submit([] { return 42; });
    std::future<int> result = task;
    gbassert(result.wait_for(5s) == std::future_status::ready);
    gbassert(result.get() == 42);
}

GB_TEST(async, eventcount_local_publication_survives_nested_wait) {
    threadpool pool{2};
    wait_for([&] {
        return gb::yadro::async::detail::threadpool_test_access::parked_workers(pool) == pool.thread_count();
    });

    auto outer = pool.submit([&] {
        auto inner = pool.submit([] { return 42; });
        return inner.get();
    });
    std::future<int> result = outer;
    gbassert(result.wait_for(5s) == std::future_status::ready);
    gbassert(result.get() == 42);
}

GB_TEST(async, eventcount_blocked_task_does_not_wake_other_workers) {
    threadpool pool{4};
    std::atomic started{false};
    std::atomic release{false};

    auto blocker = pool.submit([&] {
        started.store(true);
        started.notify_one();
        release.wait(false);
    });
    test_scope_exit release_guard{[&] {
        release.store(true);
        release.notify_one();
    }};
    started.wait(false);

    wait_for([&] {
        return gb::yadro::async::detail::threadpool_test_access::parked_workers(pool) == 3;
    });
    const auto returns = gb::yadro::async::detail::threadpool_test_access::park_returns(pool);
    std::this_thread::sleep_for(100ms);
    gbassert(gb::yadro::async::detail::threadpool_test_access::parked_workers(pool) == 3);
    gbassert(gb::yadro::async::detail::threadpool_test_access::park_returns(pool) == returns);

    release.store(true);
    release.notify_one();
    blocker.get();
    release_guard.release();
}
```

- [x] **Step 2: Run the suite and record the current RED behavior**

Run the Release x64 MSBuild command from Task 1.

Expected: the empty and blocked-task parking tests fail under the timed predicate loop. The local nested-wait test should pass on the current polling implementation and serves as a preserved behavior contract.

- [x] **Step 3: Replace `WorkerCtl::cv` and add the eventcount state**

Ensure `<algorithm>` is included. Replace `WorkerCtl` with:

```cpp
struct WorkerCtl {
    std::mutex             mutex;
    std::vector<TaskBase*> inbox;
};
```

Task 1 already added `parked_workers_` and `park_returns_`. Add only `work_generation_` next to those existing cache-line-separated counters:

```cpp
alignas(kCacheLineSize) std::atomic<std::uint64_t> work_generation_{0};
```

All calls below intentionally use the default sequentially consistent ordering.

- [x] **Step 4: Add the exact publication helpers**

Add:

```cpp
void signal_work(std::size_t visible_tasks = 1) noexcept {
    if (visible_tasks == 0)
        return;

    work_generation_.fetch_add(1);
    const auto wake_count = std::min(visible_tasks, deques_.size());
    for (std::size_t i = 0; i < wake_count; ++i)
        work_generation_.notify_one();
}

void signal_all_workers() noexcept {
    work_generation_.fetch_add(1);
    work_generation_.notify_all();
}

void publish_batch(
    WorkStealingDeque<TaskBase*>& destination,
    std::vector<TaskBase*>& batch) noexcept {
    const auto count = batch.size();
    for (auto* task : batch)
        destination.push_bottom(task);
    batch.clear();
    signal_work(count);
}
```

Do not increment or decrement `tasks_in_system_` in any of these helpers.

- [x] **Step 5: Signal both initial enqueue paths after visibility**

Replace the worker-local notification with:

```cpp
tasks_in_system_.fetch_add(1, std::memory_order_acq_rel);
try {
    own->push_bottom(node);
}
catch (...) {
    tasks_in_system_.fetch_sub(1, std::memory_order_acq_rel);
    throw;
}
signal_work();
```

Keep the external path's existing `try`/`catch` rollback unchanged and replace only `ctl->cv.notify_one()` with `signal_work()`. The complete external branch must be:

```cpp
else {
    const std::size_t target = ext_home_raw_ % ctls_.size();
    {
        std::lock_guard idle_lk{idle_.mutex};
        tasks_in_system_.fetch_add(1, std::memory_order_acq_rel);
    }
    try {
        std::lock_guard inbox_lk{ctls_[target]->mutex};
        ctls_[target]->inbox.push_back(node);
    }
    catch (...) {
        tasks_in_system_.fetch_sub(1, std::memory_order_acq_rel);
        throw;
    }
    signal_work();
}
```

The signal must remain after the inbox lock is released and after the task is visible. If `vector::push_back` throws, the counter rollback must execute and no generation signal is sent.

- [x] **Step 6: Add inbox migration and batch-steal helpers with re-publication**

Add:

```cpp
enum class InboxProbe { Empty, Contended, Published };

InboxProbe migrate_inbox(
    WorkerCtl& source,
    WorkStealingDeque<TaskBase*>& destination,
    bool conclusive) noexcept {
    std::unique_lock lk{source.mutex, std::defer_lock};
    if (conclusive) {
        lk.lock();
    }
    else if (!lk.try_lock()) {
        return InboxProbe::Contended;
    }

    if (source.inbox.empty())
        return InboxProbe::Empty;

    std::vector<TaskBase*> batch;
    std::swap(batch, source.inbox);
    lk.unlock();
    publish_batch(destination, batch);
    return InboxProbe::Published;
}

StealResult steal_batch_and_run(
    WorkStealingDeque<TaskBase*>& victim,
    WorkStealingDeque<TaskBase*>& destination) noexcept {
    auto [status, first] = victim.steal();
    if (status != StealResult::Success)
        return status;

    std::vector<TaskBase*> extra;
    extra.reserve(kStealBatchSize - 1);
    for (int i = 1; i < kStealBatchSize; ++i) {
        auto [extra_status, task] = victim.steal();
        if (extra_status == StealResult::Success)
            extra.push_back(task);
        else
            break;
    }

    publish_batch(destination, extra);
    run_task(first);
    return StealResult::Success;
}
```

This explicitly re-publishes every extra task before `run_task(first)`, so blocking inside the first task cannot hide the extras. An `Abort` while collecting extras ends the batch; successfully removed extras are still published.

- [x] **Step 7: Replace `worker_loop` search and parking with snapshot-search-wait**

Keep the owner-pop fast path. Use `migrate_inbox(..., false)` for cheap own/victim inbox attempts and `steal_batch_and_run` for the cheap randomized victim attempt. Replace the old step-4 block with this conclusive phase:

```cpp
const auto observed_generation = work_generation_.load();
const auto start = static_cast<std::size_t>(rng() % n);
bool retry_search = false;
bool work_found = false;

for (std::size_t offset = 0; offset < n; ++offset) {
    const auto victim = (start + offset) % n;

    if (victim != id) {
        const auto steal_status = steal_batch_and_run(
            *deques_[victim], my_deque);
        if (steal_status == StealResult::Success) {
            work_found = true;
            break;
        }
        if (steal_status == StealResult::Abort) {
            retry_search = true;
            break;
        }
    }

    if (migrate_inbox(*ctls_[victim], my_deque, true)
        == InboxProbe::Published) {
        work_found = true;
        break;
    }
}

if (work_found || retry_search)
    continue;
if (stop_.load(std::memory_order_relaxed))
    break;

parked_workers_.fetch_add(1);
work_generation_.wait(observed_generation);
parked_workers_.fetch_sub(1);
park_returns_.fetch_add(1);
```

The generation snapshot must occur before the conclusive scan. Do not add `tasks_in_system_` to this parking decision.

- [x] **Step 8: Wake all atomic waiters during shutdown**

Replace the loop over `ctl->cv.notify_all()` with:

```cpp
stop_.store(true, std::memory_order_relaxed);
signal_all_workers();
```

Keep this after graceful drain reaches zero or immediately after the stopped state is selected. Do not change pending-task abandonment or joining.

- [x] **Step 9: Run the tests and verify GREEN**

Run the Release x64 MSBuild command.

Expected:

- `eventcount_empty_pool_parks_without_polling`: PASS.
- `eventcount_external_submission_wakes_parked_worker`: PASS.
- `eventcount_local_publication_survives_nested_wait`: PASS.
- `eventcount_blocked_task_does_not_wake_other_workers`: PASS.
- All pre-existing async tests: PASS.

- [ ] **Step 10: Commit the eventcount implementation if permitted**

Blocked: this workspace permits reading `.git` but not creating `.git/index.lock`.

```powershell
git add -- async/threadpool.h test/async_test.cpp
git commit -m "fix: park threadpool workers on work eventcount"
```

### Task 3: Add re-publication and concurrent nested-wait stress coverage

**Files:**
- Modify: `test/async_test.cpp`
- Verify: `async/threadpool.h`

**Interfaces:**
- Consumes: `signal_work`, `publish_batch`, `migrate_inbox`, and `steal_batch_and_run` from Task 2.
- Produces: regression evidence that temporarily in-hand batches are always made discoverable again.

- [x] **Step 1: Write a batch-steal blocking-first-task regression**

Add:

```cpp
GB_TEST(async, eventcount_batch_steal_republishes_extras_before_blocking) {
    for (int round = 0; round < 100; ++round) {
        threadpool pool{4};
        std::atomic release_first{false};
        std::atomic<int> completed{0};
        test_scope_exit release_guard{[&] {
            release_first.store(true);
            release_first.notify_all();
        }};

        auto root = pool.submit([&] {
            std::vector<Task<void>> children;
            children.reserve(64);
            children.push_back(pool.submit([&] {
                release_first.wait(false);
                completed.fetch_add(1);
            }));
            for (int i = 1; i < 64; ++i) {
                children.push_back(pool.submit([&] {
                    completed.fetch_add(1);
                }));
            }
            for (const auto& child : children)
                child.wait();
        });

        wait_for([&] { return completed.load() >= 63; });
        release_first.store(true);
        release_first.notify_all();
        root.get();
        gbassert(completed.load() == 64);
        release_guard.release();
    }
}
```

This repeatedly forces local publication plus nested waiting while a stolen oldest task may block. The fixed timeout in `wait_for` converts any stranded extra batch into a test failure instead of a hung test process.

- [x] **Step 2: Write concurrent local-submission plus graceful-shutdown stress**

Add:

```cpp
GB_TEST(async, eventcount_local_nested_wait_and_shutdown_stress) {
    for (int round = 0; round < 100; ++round) {
        threadpool pool{4};
        std::latch published{1};
        std::atomic<int> completed{0};

        auto root = pool.submit([&] {
            std::vector<Task<void>> children;
            children.reserve(32);
            for (int i = 0; i < 32; ++i) {
                children.push_back(pool.submit([&] {
                    completed.fetch_add(1);
                }));
            }
            published.count_down();
            for (const auto& child : children)
                child.wait();
        });

        published.wait();
        std::jthread shutdown_thread([&] { pool.shutdown(true); });
        shutdown_thread.join();
        root.get();
        gbassert(completed.load() == 32);
    }
}
```

Add `#include <latch>` directly to the include block in `test/async_test.cpp`.

- [x] **Step 3: Run the suite and verify both stress tests pass repeatedly**

Run the Release x64 MSBuild command three consecutive times.

Expected: all three runs pass; neither stress test reaches the five-second `wait_for` deadline or deadlocks shutdown.

- [x] **Step 4: Audit every `push_bottom` in `threadpool.h`**

Run:

```powershell
rg -n "push_bottom" async/threadpool.h
```

Expected classifications:

- Initial local enqueue: followed by `signal_work()`.
- Inbox migration: performed through `publish_batch()`.
- Batch-steal extra tasks: performed through `publish_batch()` before the first task runs.
- Shutdown draining: no publication because tasks are being abandoned after workers have stopped.

No runnable re-push may bypass `signal_work` or `publish_batch`.

- [ ] **Step 5: Commit the stress coverage if permitted**

Blocked: this workspace permits reading `.git` but not creating `.git/index.lock`.

```powershell
git add -- test/async_test.cpp async/threadpool.h
git commit -m "test: stress threadpool eventcount republication"
```

### Task 4: Complete shutdown, idle, continuation, and lifecycle regression coverage

**Files:**
- Modify: `test/async_test.cpp`
- Verify: `async/threadpool.h`

**Interfaces:**
- Consumes: existing `Task`, `then`, `shutdown`, `tasks_in_system`, and test parking accessor.
- Produces: deterministic coverage for all preserved public contracts named in the approved design.

- [x] **Step 1: Add a fully parked shutdown regression**

```cpp
GB_TEST(async, eventcount_shutdown_wakes_fully_parked_workers) {
    for (int round = 0; round < 100; ++round) {
        threadpool pool{8};
        wait_for([&] {
            return gb::yadro::async::detail::threadpool_test_access::parked_workers(pool) == pool.thread_count();
        });
        pool.shutdown(true);
        gbassert(pool.state() == threadpool::PoolState::Stopped);
    }
}
```

- [x] **Step 2: Add graceful-drain coverage**

```cpp
GB_TEST(async, eventcount_graceful_shutdown_drains_accepted_work) {
    threadpool pool{4};
    std::atomic<int> completed{0};
    std::vector<Task<void>> tasks;
    for (int i = 0; i < 1000; ++i) {
        tasks.push_back(pool.submit([&] {
            completed.fetch_add(1);
        }));
    }

    pool.shutdown(true);
    gbassert(completed.load() == 1000);
    for (const auto& task : tasks)
        task.get();
}
```

- [x] **Step 3: Add immediate-shutdown abandonment coverage**

```cpp
GB_TEST(async, eventcount_immediate_shutdown_abandons_pending_work) {
    threadpool pool{1};
    std::atomic started{false};
    std::atomic release{false};

    auto running = pool.submit([&] {
        started.store(true);
        started.notify_one();
        release.wait(false);
    });
    started.wait(false);
    auto pending = pool.submit([] { return 42; });

    std::jthread shutdown_thread([&] { pool.shutdown(false); });
    std::this_thread::sleep_for(10ms);
    release.store(true);
    release.notify_one();
    shutdown_thread.join();
    running.get();

    try {
        (void)pending.get();
        gbassert(false && "pending task should be abandoned");
    }
    catch (const std::future_error& error) {
        gbassert(error.code() == std::make_error_code(std::future_errc::broken_promise));
    }
}
```

- [x] **Step 4: Add concurrent external-submit exactly-once coverage**

```cpp
GB_TEST(async, eventcount_concurrent_submissions_are_not_stranded) {
    threadpool pool{8};
    constexpr int submitters = 16;
    constexpr int tasks_per_submitter = 250;
    std::atomic<int> executed{0};
    std::mutex tasks_mutex;
    std::vector<Task<void>> tasks;

    std::vector<std::jthread> threads;
    for (int i = 0; i < submitters; ++i) {
        threads.emplace_back([&] {
            std::vector<Task<void>> local;
            local.reserve(tasks_per_submitter);
            for (int j = 0; j < tasks_per_submitter; ++j) {
                local.push_back(pool.submit([&] {
                    executed.fetch_add(1);
                }));
            }
            std::lock_guard lock{tasks_mutex};
            for (auto& task : local)
                tasks.push_back(std::move(task));
        });
    }
    threads.clear();
    for (const auto& task : tasks)
        task.get();

    gbassert(executed.load() == submitters * tasks_per_submitter);
}
```

- [x] **Step 5: Add an explicit continuation exactly-once assertion**

```cpp
GB_TEST(async, eventcount_continuation_executes_exactly_once) {
    threadpool pool{4};
    std::atomic<int> calls{0};
    auto left = pool.submit([] { return 6; });
    auto right = pool.submit([] { return 7; });
    auto result = pool.then([&](int a, int b) {
        calls.fetch_add(1);
        return a * b;
    }, left, right);

    gbassert(result.get() == 42);
    gbassert(result.get() == 42);
    gbassert(calls.load() == 1);
}
```

The existing `test_idle_fires`, `test_idle_invariant`, `from_worker`, `then_chain`, `fan_out_fan_in`, and `on_idle` tests remain part of the required verification and must not be weakened.

- [x] **Step 6: Run the full regression set**

Run the Release x64 MSBuild command three times.

Expected: every new eventcount test and every existing async test passes on all runs. Confirm `tests failed: 0` in each output.

- [ ] **Step 7: Commit the contract tests if permitted**

Blocked: this workspace permits reading `.git` but not creating `.git/index.lock`.

```powershell
git add -- test/async_test.cpp
git commit -m "test: cover threadpool eventcount contracts"
```

### Task 5: Rewrite obsolete synchronization documentation and audit dead code

**Files:**
- Modify: `async/threadpool.h:28-146,773-821,860-985,1116-1142`
- Verify: `async/chase_lev_deque.h`

**Interfaces:**
- Consumes: the final eventcount implementation from Tasks 2-4.
- Produces: source documentation that describes only the implemented protocol.

- [x] **Step 1: Replace the file-header CV sections with the eventcount contract**

Remove the old sections titled `External submission: per-worker inboxes + per-worker CVs`, `Why idle_.mutex guards the external fetch_add` where it discusses CV behavior, and the obsolete CV lock-order entries. Add this content in their place:

```cpp
 * ── Work publication and worker parking ──────────────────────────────────
 * Every operation that makes queued work visible increments
 * work_generation_ and notifies one or more atomic waiters. This includes
 * initial inbox/deque insertion and re-publication after inbox migration or
 * batch stealing. tasks_in_system_ is not a runnable-work predicate: it
 * includes executing tasks and remains dedicated to drain/on_idle accounting.
 *
 * Before parking, a worker snapshots work_generation_ and conclusively scans
 * every deque and inbox. Chase-Lev Abort means contention and forces another
 * pass. Only after every source is observed empty does the worker call
 * work_generation_.wait(snapshot). Publication before the snapshot is found
 * by the scan; publication after it changes the value, so wait(snapshot)
 * cannot strand work.
 *
 * WorkerCtl::mutex protects only its inbox. Workers do not use per-worker
 * condition variables.
```

- [x] **Step 2: Rewrite enqueue, migration, parking, and shutdown comments**

Ensure comments state:

- local enqueue signals the shared generation after `push_bottom`;
- external enqueue signals after releasing the inbox mutex;
- inbox and batch-steal re-pushes call `publish_batch`;
- the conclusive scan is exhaustive and treats `Abort` as contention;
- parking is an untimed `work_generation_.wait(snapshot)`;
- shutdown advances the generation and notifies all waiters.

Delete the old step-4 predicate and `200us` spin-guard block entirely.

- [x] **Step 3: Run dead-documentation and dead-member scans**

Run:

```powershell
rg -n "wait_for|200.*s|tasks_in_system_ > 0|per-worker CV|work_cv_|\.cv|condition_variable" async/threadpool.h
```

Expected:

- No worker timed wait.
- No `tasks_in_system_ > 0` parking predicate.
- No `WorkerCtl::cv` member or use.
- `condition_variable` remains only where required by `SharedState` and `drain_cv_`.
- No claim that a shared notification is wrong with per-worker inboxes.

- [x] **Step 4: Verify the deque source was not behaviorally changed**

Run:

```powershell
git diff -- async/chase_lev_deque.h
```

Expected: no diff. If a direct threadpool-polling comment genuinely requires correction, change only that comment and rerun the deque tests; do not alter deque operations or memory ordering.

- [x] **Step 5: Build and run the full suite after the documentation cleanup**

Run the Release x64 MSBuild command.

Expected: build succeeds with warnings treated as errors; all tests pass.

- [ ] **Step 6: Commit the documentation update if permitted**

Blocked: this workspace permits reading `.git` but not creating `.git/index.lock`.

```powershell
git add -- async/threadpool.h
git commit -m "docs: describe threadpool eventcount wakeups"
```

### Task 6: Measure before/after CPU and complete verification

**Files:**
- Verify: `async/threadpool.h`
- Verify: `test/async_test.cpp`
- Verify: `docs/superpowers/specs/2026-07-12-threadpool-eventcount-wakeup-design.md`
- Verify: `docs/superpowers/plans/2026-07-12-threadpool-eventcount-wakeup.md`

**Interfaces:**
- Produces: exact Release x64 test results, thread counts, CPU-time deltas, and compatibility report.

- [x] **Step 1: Run focused async regressions through the existing test binary**

Build, then run:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Build /p:Configuration=Release /p:Platform=x64 /v:minimal
& '.\exe\x64\Release\yadro_test.exe'
```

The existing harness runs all registered tests. Record every `async.eventcount_*` result separately from the full-suite summary rather than claiming a filtered run.

- [x] **Step 2: Rebuild the same temporary 64-worker process probe used for the baseline**

Create `C:\tmp\yadro_threadpool_probe.cpp` with this exact source:

```cpp
#include "C:/Projects/GitHub/yadro/async/threadpool.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

using gb::yadro::async::threadpool;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "idle";

    if (mode == "idle") {
        threadpool pool{64};
        std::cout << "worker_threads=" << pool.thread_count() << '\n' << std::flush;
        std::this_thread::sleep_for(6s);
        return 0;
    }

    if (mode == "blocked") {
        threadpool pool{64};
        std::atomic started{false};
        std::atomic release{false};
        auto blocker = pool.submit([&] {
            started.store(true);
            started.notify_one();
            release.wait(false);
        });
        started.wait(false);
        std::cout << "worker_threads=" << pool.thread_count()
                  << " tasks_in_system=" << pool.tasks_in_system() << '\n'
                  << std::flush;
        std::this_thread::sleep_for(6s);
        release.store(true);
        release.notify_one();
        blocker.get();
        return 0;
    }

    if (mode == "local") {
        threadpool pool{2};
        auto outer = pool.submit([&] {
            auto inner = pool.submit([] { return 42; });
            return inner.get();
        });
        std::cout << "local_nested_result=" << outer.get() << '\n';
        return 0;
    }

    if (mode == "shutdown") {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) {
            threadpool pool{64};
            std::this_thread::sleep_for(1ms);
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        std::cout << "parked_shutdown_iterations=100 elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                  << '\n';
        return 0;
    }

    return 2;
}
```

Compile with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command `
  "& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -SkipAutomaticLocation; `
   cl /nologo /std:c++latest /O2 /EHsc /MD /utf-8 `
      /Fe:C:\Projects\GitHub\yadro\yadro_threadpool_probe.exe `
      C:\tmp\yadro_threadpool_probe.cpp"
```

Use the exact baseline semantics: 64 eager workers, six-second process lifetime, one `atomic::wait` blocker in blocked mode, local nested result 42, and 100 parked construction/destruction cycles.

- [x] **Step 3: Measure the empty pool over the same four-second interval**

Run:

```powershell
$out = 'C:\Projects\GitHub\yadro\probe-idle.txt'
$process = Start-Process '.\yadro_threadpool_probe.exe' `
    -ArgumentList 'idle' -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $out
Start-Sleep -Seconds 1
$process.Refresh()
$threads = $process.Threads.Count
$cpuStart = $process.TotalProcessorTime.TotalSeconds
$wall = [System.Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds 4
$wall.Stop()
$process.Refresh()
$cpuEnd = $process.TotalProcessorTime.TotalSeconds
$process.WaitForExit()
[pscustomobject]@{
    Mode = 'idle'
    OSThreads = $threads
    IntervalSeconds = $wall.Elapsed.TotalSeconds
    CPUSeconds = $cpuEnd - $cpuStart
    LogicalCoresUsed = ($cpuEnd - $cpuStart) / $wall.Elapsed.TotalSeconds
    ProbeOutput = (Get-Content -Raw $out).Trim()
} | Format-List
```

The calculation is:

```text
logical_cores_used = cpu_seconds_delta / wall_seconds_delta
```

Expected compatibility: `pool.thread_count() == 64`. Expected behavior: CPU time remains near zero and the deterministic parking test confirms there are no repeated timed returns.

- [x] **Step 4: Measure one blocked task over the same four-second interval**

Run:

```powershell
$out = 'C:\Projects\GitHub\yadro\probe-blocked.txt'
$process = Start-Process '.\yadro_threadpool_probe.exe' `
    -ArgumentList 'blocked' -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $out
Start-Sleep -Seconds 1
$process.Refresh()
$threads = $process.Threads.Count
$cpuStart = $process.TotalProcessorTime.TotalSeconds
$wall = [System.Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds 4
$wall.Stop()
$process.Refresh()
$cpuEnd = $process.TotalProcessorTime.TotalSeconds
$process.WaitForExit()
[pscustomobject]@{
    Mode = 'blocked'
    OSThreads = $threads
    IntervalSeconds = $wall.Elapsed.TotalSeconds
    CPUSeconds = $cpuEnd - $cpuStart
    LogicalCoresUsed = ($cpuEnd - $cpuStart) / $wall.Elapsed.TotalSeconds
    ProbeOutput = (Get-Content -Raw $out).Trim()
} | Format-List
```

Before baseline: 123.969 CPU-seconds over 4.088 seconds, or 30.325 logical cores.

Expected after result: near-zero CPU use by unrelated workers. Do not declare success solely from this smoke measurement; also require the deterministic blocked-task parking test and source scan showing no timed loop.

- [x] **Step 5: Run local and parked-shutdown probe modes**

Run:

```powershell
& '.\yadro_threadpool_probe.exe' local
& '.\yadro_threadpool_probe.exe' shutdown
```

Expected:

```text
local_nested_result=42
parked_shutdown_iterations=100
```

Record elapsed shutdown time without imposing a performance threshold; correctness is that all 100 iterations finish and join.

After recording all measurements, remove only the known temporary probe artifacts:

```powershell
[System.IO.File]::Delete('C:\Projects\GitHub\yadro\probe-idle.txt')
[System.IO.File]::Delete('C:\Projects\GitHub\yadro\probe-blocked.txt')
[System.IO.File]::Delete('C:\Projects\GitHub\yadro\yadro_threadpool_probe.exe')
[System.IO.File]::Delete('C:\Projects\GitHub\yadro\yadro_threadpool_probe.obj')
[System.IO.File]::Delete('C:\tmp\yadro_threadpool_probe.cpp')
```

- [x] **Step 6: Run final source and worktree audits**

```powershell
rg -n "wait_for|200.*s|tasks_in_system_ > 0|WorkerCtl::cv|\.cv" async/threadpool.h
rg -n "push_bottom" async/threadpool.h
git diff --check
git status --short
```

Expected:

- No periodic worker wait or `tasks_in_system_` runnable predicate.
- Every runnable `push_bottom` is an initial publication or goes through the re-publication helper.
- No whitespace errors.
- Only intended threadpool, test, design, and plan files are changed; `.claude/` remains untouched.

- [x] **Step 7: Run the final complete Release x64 suite from a clean build**

Completed: clean Release x64 rebuild passed 199 tests, failed 0, with the
pre-existing registry integration test disabled.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Expected: build exit code 0, warnings-as-errors clean, 199 tests passed, 0 failed, and only the pre-existing registry integration test disabled unless `--run-all` is requested.

- [x] **Step 8: Prepare the completion report**

Report:

- root cause and why predicate `wait_for` spun;
- the snapshot-search-wait proof;
- explicit re-publication coverage for inbox migration and batch stealing;
- exact test/build commands and counts;
- before/after thread count and CPU measurements;
- source scan proving the 200 microsecond polling loop is gone;
- eager-creation compatibility status;
- no downstream consumer edits, plus the deferred lazy-worker note;
- the `.git/index.lock` permission blocker if commits remain impossible.
