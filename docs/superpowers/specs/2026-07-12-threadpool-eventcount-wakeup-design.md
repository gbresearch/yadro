# Threadpool Eventcount Wakeup Design

## Scope

This change is limited to `gb::yadro::async::threadpool` and its Yadro tests and measurement tooling. It does not modify any downstream consumer.

The change replaces periodic worker polling with an event-driven wakeup protocol. Eager worker creation, `thread_count()`, `tasks_in_system_`, continuation behavior, nested waits, `on_idle`, and shutdown contracts remain intact. Lazy worker creation is explicitly deferred.

## Confirmed baseline

The current constructor creates every configured worker immediately. A `threadpool{64}` reports 64 pool workers; the measurement process had 68 total Windows threads after construction.

The Release x64 baseline command was:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'vs\yadro.sln' /m /t:Build /p:Configuration=Release /p:Platform=x64 /v:minimal
```

It completed with 188 tests passed, 0 failed, and 1 disabled in 5.557 seconds.

A temporary process-level probe produced these measurements:

| State | Interval | CPU time | Equivalent logical cores |
|---|---:|---:|---:|
| Empty 64-worker pool | 4.008 s | 0.000 s | 0.000 |
| One blocked task in a 64-worker pool | 4.088 s | 123.969 s | 30.325 |

The empty-pool result does not make the timed polling acceptable: workers still request 200 microsecond timed waits, although their cost was below process CPU-time resolution on this machine. The blocked-task result reproduces the severe failure mode.

The probe also confirmed that a two-worker local submission followed by a nested `get()` completed, and that 100 construct/park/destroy cycles completed in 1.562 seconds.

## Root cause

`tasks_in_system_` is incremented before a task is published and decremented only after its execution completes. It therefore represents queued plus executing work, not runnable work.

The worker wait predicate includes `tasks_in_system_ > 0`. A predicate-based `condition_variable::wait_for` checks the predicate before sleeping. When a task is executing or blocked, the predicate remains true, so unrelated workers return immediately and repeatedly search for work. The documented claim that the overload enforces a 200 microsecond sleep while the predicate is true is incorrect.

The polling was introduced to cover a real correctness gap:

- A worker can push a child task onto its owner-only Chase-Lev deque and then block in a nested task wait.
- The push sends a transient notification to another worker.
- If that notification arrives before the target parks, or the target checks a different random victim, the child remains undiscovered.
- Repeated polling eventually retries the correct victim and masks the lost notification.

External inbox publication is less exposed because the target inbox and its condition predicate share a mutex. However, an inbox owner can itself be blocked, which is why other workers must be able to migrate victim inbox contents.

## Alternatives

### 1. Atomic eventcount generation — selected

Use `std::atomic<std::uint64_t>::wait`, `notify_one`, and `notify_all` with a monotonically increasing work generation. This closes the check-to-park race without a global publication mutex and maps to the platform's address-based wait facility.

### 2. Condition variable plus generation — rejected

A correct shared condition-variable protocol requires every publisher to participate in the same mutex used by parking workers. That would serialize all publications on a global lock and undermine the work-stealing design.

### 3. Runnable counter or counting semaphore — rejected

This requires exactly-once permit accounting across owner pops, steals, batch steals, inbox migration, abandonment, and shutdown. Accounting drift would create either spinning or stranded tasks, while offering no necessary benefit over an eventcount.

## Wakeup protocol

The pool gains a cache-line-separated atomic `work_generation_`. All generation operations initially use sequentially consistent ordering. We will consider weaker ordering only as a separately justified optimization after correctness and stress verification.

Workers park exclusively on `work_generation_`. `WorkerCtl::cv` is therefore removed rather than retained as a dead or misleading synchronization mechanism. `WorkerCtl::mutex` remains and continues to protect the worker inbox.

Every operation that makes runnable work visible calls a common publication helper after the queue operation completes:

1. Publish the task or batch into an inbox or owner deque.
2. Increment `work_generation_`.
3. Notify one or more waiters.

Single-task publication increments once and calls `notify_one`. Batch publication increments once after the complete batch is visible, then calls `notify_one` exactly `min(batch_size, worker_count)` times. One notification is sufficient for correctness because a woken worker can drain work; additional notifications prevent batch execution from collapsing onto one worker.

Publication includes **re-publication** of already-counted work. In particular, the implementation must signal after:

- external submission to an inbox;
- worker-local submission to an owner deque;
- draining an owner's inbox into its deque;
- migrating another worker's inbox into the thief's deque;
- pushing the extra tasks obtained during a batch steal into the thief's deque;
- any future path that temporarily removes runnable tasks from all shared queues and later makes them visible again.

This rule is independent of `tasks_in_system_`; moving an existing task does not alter that counter.

## Worker search and parking

Workers retain fast paths for their own deque and inbox and may retain a cheap randomized steal attempt. Before parking, however, a worker performs a conclusive search:

1. Snapshot `work_generation_`.
2. Search its own deque and inbox.
3. Starting from a randomized victim, visit every other worker's deque and inbox.
4. Treat a Chase-Lev `Abort` result or an unavailable inbox lock as contention, not emptiness. Restart the conclusive pass, or take the inbox lock one at a time, rather than parking on an inconclusive observation.
5. If all sources are conclusively empty and shutdown is not requested, call `work_generation_.wait(snapshot)`.

The exhaustive pass is paid only when a worker is about to park. Normal owner-pop and successful-steal paths remain bounded fast paths.

## Lost-wakeup proof

Consider a worker that snapshots generation `G`, performs the conclusive search, and calls `wait(G)`:

- If publication completes before the snapshot, the work is visible during the exhaustive search unless another worker has already claimed it.
- If publication occurs after the snapshot but before or during the search, the generation differs from `G`, so `wait(G)` revalidates the value and cannot block.
- If publication occurs between the final search observation and wait registration, the same value revalidation prevents blocking.
- If publication occurs after registration, `notify_one` wakes a waiter and the changed generation permits it to return.
- If work is temporarily invisible during migration or batch stealing, the required re-publication changes the generation after the batch is visible again.
- If the search encounters concurrent deque or inbox activity, it does not classify the source as empty; it retries before parking.

An executing or blocked task does not itself change the work generation. When no queued work exists, unrelated workers therefore remain parked even though `tasks_in_system_` is nonzero.

The 64-bit generation can wrap only after exactly 2^64 publications between a worker's snapshot and wait. This is outside the practical lifetime of the pool; no smaller wrapping type will be used.

## Source documentation update

The implementation change includes a complete rewrite of the affected design commentary in `async/threadpool.h`. The updated header will document the atomic eventcount, conclusive pre-park scan, re-publication rules, and lost-wakeup proof described here.

All commentary that describes the removed per-worker condition-variable protocol must be deleted or replaced, including:

- the file-header section titled "External submission: per-worker inboxes + per-worker CVs";
- the claim that a shared `notify_one` is inherently wrong with per-worker inboxes;
- the old lock-order references to condition-variable waiting;
- the worker-loop step-4 predicate and 200 microsecond spin-guard explanation;
- comments on enqueue paths that say they notify a specific `WorkerCtl::cv`.

No obsolete `WorkerCtl::cv`, timed-wait, or per-worker-CV rationale may remain after the change. The Chase-Lev deque documentation will be updated only where it directly describes the threadpool's removed polling behavior.

## Shutdown and idle behavior

`tasks_in_system_` remains the source of truth for graceful drain and `on_idle`. It continues to count queued and executing tasks and is removed only from the worker parking predicate.

Graceful shutdown waits for `tasks_in_system_ == 0`, then sets the stop state, increments `work_generation_`, and calls `notify_all` so every parked worker exits and joins.

Immediate shutdown sets the stopped state, increments the generation, and calls `notify_all`. Running tasks finish before their worker threads join; pending tasks are abandoned and their task states receive the existing broken-promise result.

## Worker creation decision

The implementation retains eager creation. The constructor parameter is an exact `num_threads`, `thread_count()` exposes the created count, and eager creation is longstanding observable behavior. Changing it would mix lifecycle, allocation-failure, scaling, and compatibility concerns into the wakeup fix.

Lazy creation and an explicit eager/lazy construction policy remain possible future work, but require a separate design and test matrix.

## Test strategy

Tests will use synchronization primitives and a narrow test-only parking observer rather than depending primarily on wall-clock thresholds. The observer will expose parked-worker and completed-wait counts only to the test build; it will not add a production-facing API.

Deterministic and stress coverage will include:

- all workers reach the parked state and do not repeatedly return without a generation change;
- external submission wakes a fully parked pool;
- a worker publishes locally and blocks in a nested `get()` while another worker discovers the child;
- owner-inbox and victim-inbox migration re-publication cannot strand a batch;
- batch-steal re-publication cannot strand extra tasks if the thief blocks in its first task;
- one long-running blocking task leaves unrelated workers parked;
- many concurrent external and local submissions execute exactly once;
- continuations execute exactly once, including chains and fan-in;
- nested waits do not deadlock;
- `on_idle` fires only when no queued or executing work remains;
- graceful shutdown drains all accepted work;
- immediate shutdown abandons pending work according to the existing broken-promise contract;
- shutdown wakes a fully parked pool;
- concurrent local submission, nested waits, migration, and shutdown are stressed together over repeated rounds;
- repeated construction and destruction remains stable.

Focused tests will be run after each red-green change. The full Release x64 suite will be run after the focused tests pass.

## Measurement strategy

A separate process-level smoke probe will measure:

- pool-reported and OS-observed thread count immediately after construction;
- process CPU time over the same fixed interval for an empty pool;
- process CPU time over the same fixed interval with one blocked task;
- parked shutdown completion.

The before and after probes will use the same compiler configuration, worker count, sampling interval, and host. The final report will include exact commands, raw CPU-time deltas, logical-core equivalents, test results, and any compatibility change.

## Compatibility and downstream note

No intended public behavior changes except eliminating periodic idle polling and hot spinning. Worker creation remains eager, `thread_count()` remains stable, and accepted task, continuation, idle, and shutdown contracts remain unchanged.

Downstream consumers may still choose to restructure indefinitely blocking work or evaluate a future lazy-worker policy, but no downstream modification is part of this change.
