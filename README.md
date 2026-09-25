# yadro

**yadro** ("core" in Russian) is a C++ library of essential facilities: a work-stealing
thread pool, a serialization framework, compact containers (including an in-memory
hierarchical database with JSON I/O), numerical and statistical algorithms (among them a
genetic optimizer), a discrete-event simulator, and a set of everyday utilities such as
error handling, logging, timing, hashing and a small unit-test framework.

The library is written in modern C++ (C++23 and later: concepts, `std::source_location`,
`std::stacktrace`, `std::expected`, deducing `this`, coroutines) and is mostly header-only.
A handful of `.cpp` files are compiled into a small static library.

- License: [Boost Software License 1.0](LICENSE)
- Author: Gene Bushuyev

---

## Contents

- [Requirements](#requirements)
- [Repository layout](#repository-layout)
- [Getting started](#getting-started)
- [Modules](#modules)
  - [util: general utilities](#util-general-utilities)
    - [Named-pipe security](#named-pipe-security)
  - [async: thread pool and tasks](#async-thread-pool-and-tasks)
  - [archive: serialization](#archive-serialization)
  - [container: data structures and gbdb](#container-data-structures-and-gbdb)
  - [algorithm: numerical and statistical algorithms](#algorithm-numerical-and-statistical-algorithms)
  - [simulator: discrete-event simulation](#simulator-discrete-event-simulation)
- [Testing](#testing)
- [License](#license)

---

## Requirements

- A compiler with recent C++ support. The project is developed with Microsoft Visual C++
  using `/std:c++latest` (`stdcpplatest`) and `/Zc:__cplusplus`, with warnings treated as
  errors. The code relies on recent language and library features (deducing `this`,
  `std::expected`, `std::stacktrace`, `std::from_range`). Older toolchains such as GCC 13
  or Clang 18 with libstdc++ 13 cannot compile it.
- The Visual Studio projects compile with `/arch:AVX2`. Some code paths (for example the
  FFT and the xxHash128 implementation) have AVX2-optimized variants.
- Windows is the primary platform. Most facilities are portable. The Windows-only parts
  (named pipes, Windows services, the registry backend, fiber-based simulation, DLL
  helpers) are guarded by the `GBWINDOWS` macro, which `util/gbwin.h` defines
  automatically on Windows builds.

## Repository layout

```
yadro/
├── include/
│   └── yadro.h            umbrella header: pulls in every module
├── util/                  general utilities            (gb::yadro::util)
│   └── gbutil.h           module header
├── async/                 thread pools and tasks       (gb::yadro::async)
│   └── async.h            module header
├── archive/               serialization                (gb::yadro::archive)
│   └── archive.h
├── container/             containers and gbdb          (gb::yadro::container)
│   └── gbcontainer.h      module header
├── algorithm/             numerical algorithms         (gb::yadro::algorithm)
│   └── gbalgorithm.h      module header
├── simulator/             discrete-event simulator     (gb::sim)
│   └── simulator.h        module header
├── test/                  unit tests, one file per module, plus the test driver
├── vs/                    Visual Studio solution and projects (library + tests)
└── docs/                  design specs and implementation plans
```

Each module directory has one aggregate header that includes the whole module, so you can
include just the module you need or everything at once through `include/yadro.h`.
Some directories also contain a `CODE_REVIEW.md` with review notes for that module.

## Getting started

### Including

```cpp
#include "yadro/include/yadro.h"              // everything
// or just what you need:
#include "yadro/async/async.h"
#include "yadro/container/gbcontainer.h"
```

All library code lives in the `gb::yadro` namespace, split per module
(`gb::yadro::util`, `gb::yadro::async`, `gb::yadro::archive`, `gb::yadro::container`,
`gb::yadro::algorithm`). The simulator lives in `gb::sim`.

### Building

Most of yadro is header-only. The following sources must be compiled and linked (the
`vs/yadro.vcxproj` project builds them into the `yadro` static library, written to
`lib/<Platform>/<Configuration>/`):

| Source                                                  | Provides                                 |
|---------------------------------------------------------|------------------------------------------|
| `algorithm/adf_test.cpp`, `algorithm/mackinnon.cpp`     | Augmented Dickey-Fuller test, p-values   |
| `container/gbdb.cpp`, `container/tree.cpp`              | gbdb and tree support                    |
| `simulator/fiber.cpp`, `simulator/scheduler.cpp`        | simulator scheduler and fibers           |
| `util/file_mutex.cpp`, `util/string_util.cpp`           | inter-process mutex, string helpers      |
| `util/win_service.cpp`                                  | Windows service support                  |

To build with Visual Studio, open `vs/yadro.sln` and build the `yadro` (library) and
`yadro_test` (test executable) projects for x64 or Win32, Debug or Release.

---

## Modules

### util: general utilities

Namespace `gb::yadro::util`, aggregate header `util/gbutil.h`.

| Header                 | Facilities |
|------------------------|------------|
| `gberror.h`            | `gbassert(cond[, msg])`: assertion that throws `failed_assertion` with the source location, and allocates nothing when it passes. Also `throw_error<E>()`, `must_throw(fn)`, `error_t<N, Base>` numbered errors, `exception_t<Data>` exceptions with an optional payload, source location and stack trace, `stack_trace()`, and the variadic `to_string` / `to_wstring`. |
| `gbtest.h`             | A lightweight unit-test framework: `GB_TEST(suite, name[, launch_policy])` registers a test, and `tester` runs, filters (`disable_suites`, `disable_tests`) and logs tests, optionally in parallel on the thread pool. |
| `gblog.h`              | Thread-safe `logger` that writes to any number of streams or files, organized into categories, plus `tab` for column alignment. |
| `gbtimer.h`, `gbmacro.h` | `accumulating_timer` and scope timers for profiling, the `GB_TIMER(name, unit)` macro, and macro concatenation/stringification helpers. |
| `time_util.h`          | Time stamps, Delphi `TDateTime` conversion (`datetime_to_chrono`), `week_of_year`, `to_time_point`, process id. |
| `string_util.h`        | UTF-8/UTF-16 conversion, base64 encode/decode, `tokenize`, span/string comparison, and MD5 (`md5`, `md5string`, `md5digest`). |
| `hash_util.h`          | `make_hash` for values, tuples, ranges and aggregates, and 128-bit xxHash (`xxhash128_scalar`, `xxhash128_stream`, AVX2 variant). |
| `tuple_functions.h`    | Compile-time tuple algorithms: `tuple_foreach`, `tuple_transform`, `tuple_split`, `subtuple`, `tuple_to_variant`, `make_flat_tuple`, `tuple_min`/`tuple_max`, and `aggregate_to_tuple`/`class_to_tuple` for reflecting aggregates. |
| `traits.h`             | `callable_traits` (function signature introspection) and the detection idiom (`is_detected`, `detected_or`, and related traits). |
| `misc.h`               | Assorted helpers: `almost_equal`, `raii`/`retainer` scope guards, `locked_resource`/`locked_call`, `movable_atomic`, `mixin`, `fast_random`, `window_function`, `tmp_file_cleaner_t`. |
| `gbmemory.h`           | `aligned_allocator`, `aligned_array`, `make_unique_array`, `create_unique`, `make_unique_copy`. |
| `hybrid_mutex.h`       | `hybrid_mutex`: spins briefly, then falls back to atomic wait/notify under sustained contention; works with `std::lock_guard`/`std::unique_lock`. |
| `file_mutex.h`         | `global_mutex`: a named, inter-process mutex (exclusive, shared and timed locking), backed by a lock file on POSIX and a named mutex on Windows. |
| `named_resource_lock.h`| `named_resource_lock::acquire(scope, path)`: an exclusive, fail-fast inter-process lock identified by a scope name and a filesystem path. Currently implemented on Windows only. |
| `gnuplot.h`            | A `gnuplot` wrapper that builds plots (`plot_t`, `plotstyle`, multi-pane layouts) from C++ data. |
| `gbwin.h`              | Windows helpers (only when `GBWINDOWS` is defined): `dll` loader, UUID strings, temporary file paths, `micro_sleep`. |
| `win_pipe.h`           | Windows named-pipe RPC: `start_server(pipename, [options,] log, fn...)` exposes a list of functions, by index or as `std::tuple{ "name", fn }`, and `winpipe_client_t::request<R>(index or name, args...)` calls them, with arguments and results serialized through `archive`. Pipes are secure by default; see [Named-pipe security](#named-pipe-security). |
| `win_service.h`        | Run an executable as a Windows service (`win_service_main`, `win_service_install`). |

Example: assertions and logging

```cpp
using namespace gb::yadro::util;

gbassert(x > 0, "x must be positive");   // throws failed_assertion with file:line on failure
must_throw([] { throw_error("boom"); }); // asserts that the callable throws

logger log(std::cout, "app.log");         // log to the console and a file
log() << "pi=" << tab{ 10 } << 3.14;
```

#### Named-pipe security

Servers created by `start_server` and `winpipe_server_t` are locked down by default:

- **Access:** a protected DACL grants full access only to the account the server process runs as and to LocalSystem (`default_pipe_sddl()`). Windows' own default would also admit Administrators, and give Everyone and anonymous read access.
- **Local clients only:** the pipe is created with `PIPE_REJECT_REMOTE_CLIENTS`.
- **Name squatting:** the server's first instance uses `FILE_FLAG_FIRST_PIPE_INSTANCE`, so it refuses to start if another process already owns the pipe name. While the server runs, it always keeps an instance open, so the name can't be taken between connections.
- **Clients:** `winpipe_client_t` requests only the rights it uses (`pipe_client_access`), and connects at identification level, so a server can tell who connected but can't impersonate the client.

Clients running as the server's own account are unaffected. Everyone else is refused unless you opt in:

| To admit | Use |
|----------|-----|
| Another account or group, e.g. a service account or interactive users | `pipe_server_options{ .sddl = default_pipe_sddl() + pipe_client_ace(L"<SID or alias>") }` |
| Clients on other machines | `pipe_server_options{ .allow_remote_clients = true }`, plus a DACL entry for their accounts |
| Several independent server processes on one pipe name | `pipe_server_options{ .first_pipe_instance = false }` |
| A trusted server that impersonates its clients | `pipe_client_options{ .allow_impersonation = true }` on the client |

Grant other accounts access through `pipe_client_ace`, not `GW` or `GA`: on a pipe those include the right to create pipe instances, which would let that account run its own instance and receive other clients' connections. Processes running as the server's own account keep that right, because the server needs it to create its later instances. `pipe_server_options` also accepts caller-owned `SECURITY_ATTRIBUTES`. The comments in `util/win_pipe.h` give the details.

```cpp
using namespace gb::yadro::util;

// server (blocks until a client calls shutdown()): also admit interactive users as clients
start_server(LR"(\\.\pipe\myapp)",
    pipe_server_options{ .sddl = default_pipe_sddl() + pipe_client_ace(L"IU") },
    nullptr,
    std::tuple{ "add", [](int a, int b) { return a + b; } });

// client, in another process
winpipe_client_t client(LR"(\\.\pipe\myapp)", "my client", 10);
auto sum = client.request<int>("add", 2, 3);   // std::expected<int, std::string>
```

### async: thread pool and tasks

Namespace `gb::yadro::async`, aggregate header `async/async.h`.

- **`threadpool`** (`threadpool.h`): a work-stealing pool with a Chase-Lev deque per worker
  (`chase_lev_deque.h`), batch stealing, eventcount-based parking of idle workers, and an
  optional on-idle callback.
  - `submit(fn, args...)` schedules a callable and returns a `Task<T>`.
  - `then(fn, deps...)` schedules a continuation that runs when all its dependencies
    (tasks or futures) are ready. Their values become `fn`'s arguments, and exceptions
    propagate down the chain.
  - `Task<T>` is copyable and shareable. It supports `get()`/`wait()`/`is_ready()` and
    converts to `std::future<T>`.
  - `shutdown(true)` drains accepted work; `shutdown(false)` abandons pending work.
    The destructor drains.
  - Tasks may submit and wait on other tasks from inside the pool.
- **`legacy::threadpool`** (`threadpool_legacy.h`): the earlier `std::future`-based pool,
  kept for compatibility.
- **`task_container`** (`taskcontainer.h`): task queues built on `std::queue` and
  `std::priority_queue`.

```cpp
using namespace gb::yadro::async;

threadpool pool(4);
auto a = pool.submit([] { return 6; });
auto b = pool.submit([] { return 7; });
auto c = pool.then([](int x, int y) { return x * y; }, a, b);
int answer = c.get();                    // 42
```

### archive: serialization

Namespace `gb::yadro::archive`, header `archive/archive.h`.

The `archive` class template serializes values to and from any stream with a call like
`ar(a, b, c)`. The same call writes on an output archive and reads on an input archive.

- Formats: `bin_archive<Stream>` (binary), `text_archive<Stream>` (human-readable, for
  debugging), and in-memory `omem_archive<>` / `imem_archive<>`.
- Built-in support for fundamental types, strings, `std::array`, `std::span`, sequence
  containers, `std::queue`/`std::stack`/`std::priority_queue`, ordered and unordered
  associative containers, `std::tuple`, `std::optional`, `std::variant`,
  `std::expected`, atomics, and enums through `serialize_as<T>(value)`.
- User types opt in with a `serialize(Archive&)` member function or a free
  `serialize(archive, obj)` overload. On compilers that support variadic structured
  bindings, aggregates are serialized automatically.
- Helpers: `serialization_size(args...)` (the byte count needed),
  `serialization_md5(args...)` (a digest of the serialized form), `deserialize<Ts...>(ar)`,
  and `bin_serialize(stream, args...)`.

```cpp
using namespace gb::yadro::archive;

struct point {
    int x; double y;
    void serialize(this auto&& self, auto&& ar) { ar(self.x, self.y); }
};

omem_archive<> out;
out(42, std::string("hello"), std::vector{ 1, 2, 3 }, point{ 1, 2.5 });

imem_archive<> in(out);
int i; std::string s; std::vector<int> v; point p;
in(i, s, v, p);
```

### container: data structures and gbdb

Namespace `gb::yadro::container`, aggregate header `container/gbcontainer.h`.

| Header                     | Facilities |
|----------------------------|------------|
| `static_vector.h`, `static_string.h` | Fixed-capacity vector and string backed by inline `std::array` storage. |
| `tensor.h`                 | `tensor<T, Dims...>`: a statically sized N-dimensional array (`tensor<T>` is the dynamically sized version). |
| `matrix.h`, `matrix_functions.h` | `matrix` built on `tensor`, `minor_view`, and matrix functions: `determinant`, `invert`, `solve`, `transpose`, `identity_matrix`, `submatrix`, and element-wise operations. |
| `graph.h`                  | A compact `graph` stored as vectors of nodes and edges, with optional node and edge payloads. Its layout is the same on 32-bit and 64-bit builds. |
| `tree.h`                   | `indexed_tree<T>`: a compact tree whose links are stable indices, with child and sibling insertion, detach/attach, subtree deletion and traversal. |
| `datapool.h`               | `data_pool` (with `duplicate_data_pool` and `unique_data_pool`, which deduplicates) for pooled arrays, and `basic_string_pool` for interned strings addressed by compact ids. |
| `lockfree_memo_table.h`    | `lockfree_memo_table`: a lock-free, optionally sharded, memoization cache for expensive function results. |
| `bounded_priority_queue.h` | A concurrent top-K collector: each thread inserts into its own local heap, and the heaps are merged at the end. |
| `pareto_front.h`           | Multi-objective selection: `objective_t` (projection, direction, epsilon), `dominates_t`, `pareto_set`, `pareto_set_ordered`. |
| `gbdb.h` and `gbdb_*.h`    | **gbdb**, a compact in-memory hierarchical database (see below). |
| `json.h`                   | `json_value`: a general JSON value (null, bool, int64, uint64, double, UTF-8 string, array, and an object that keeps member order and unique keys) with checked `std::expected` accessors, RFC 6901 JSON pointer lookup (`at_pointer`), structural equality, a compact, pretty and ASCII-only writer (`format_json`), and parsing (`parse_json_value`, `try_parse_json_value`). See below. |
| `json_parser.h`            | The hardened JSON front end shared by `json_value` and `json_db`: `parse_json_events` drives a SAX-style handler; `json_parse_error` reports a code, byte offset, line and column. Requires AXE (see below). |

#### gbdb

`basic_gbdb` stores a tree of keyed nodes in an `indexed_tree`. It interns keys and strings
in a string pool and deduplicates arrays in data pools, which keeps nodes small.
`json_db` (`basic_gbdb<char>`) holds JSON-like values: null, bool, integers, doubles,
strings, and typed arrays. `concurrent_json_db` adds snapshot publishing so readers see a
consistent state while writers update it. The database is serializable through `archive`.
It also supports operation logging to JSONL files and integrity scans, including checks of
external blob files.

- `gbdb_json.h`: a JSON reader and writer (`write_json`, `write_json_file`,
  `write_json_subtree`, `insert_json`, `insert_json_file`) with merge policies,
  table layouts, external blob files, and read/write options. `read_json` uses the hardened
  front end in `json_parser.h` and rejects raw control characters in strings, invalid UTF-8,
  lone UTF-16 surrogate escapes, nesting deeper than `json_read_options::max_depth` (256),
  and, when `max_input_bytes` is set, oversized input (streams and files never read more than
  the cap plus one byte). Out-of-range integers are rejected unless `big_integers` is
  `to_double`. A file manifest never changes these limits.
- `gbdb_json_path.h`: path-addressed JSON I/O (`write_json_path`, `insert_json_at_path`).
- `gbdb_file.h`: atomic binary save (`save_json_db_file`).
- `gbdb_registry.h`: import a Windows registry tree into a `json_db` and write one back
  (`read_registry_tree`, `write_registry_tree`), with a mock backend for tests.

```cpp
using namespace gb::yadro::container;

json_db db;
db.set({ "market", "symbol" }, std::string_view{ "AAPL" });
db.set("market/price", 193.25);                       // slash-separated paths work too
std::array<std::int64_t, 3> hist{ 190, 191, 193 };
db.set_array({ "market", "history" }, std::span{ hist });

double price = std::get<double>(*db.get({ "market", "price" }));
auto json = write_json(db);                           // serialize to JSON text
```

#### json_value

`json_value` holds any JSON document, including the mixed and nested arrays that `json_db`'s
value model does not cover. Parsing requires AXE: define `GB_YADRO_ENABLE_AXE_JSON` and add
the AXE include directory; the value type and the writer do not need it.

```cpp
using namespace gb::yadro::container;

auto message = parse_json_value(R"({"id":7,"content":[{"type":"text","text":"hi"}]})");
auto text = message.at_pointer("/content/0/text");     // std::expected<const json_value*, json_pointer_error>
auto id = message.at_pointer("/id").value()->as_int64(); // std::expected<std::int64_t, json_access_error>

json_value reply = json_object{ { "id", 7 }, { "ok", true }, { "items", json_array{ 1, 2.5, "x" } } };
auto compact = format_json(reply);                      // {"id":7,"ok":true,"items":[1,2.5,"x"]}

std::string_view untrusted = R"({"a":[1,]})";              // e.g. a message read from a pipe
auto result = try_parse_json_value(untrusted, json_parse_options{ .max_input_bytes = 16 << 20 });
if (!result)                                            // syntax at line 1, column 9
    std::cerr << result.error().what() << '\n';
```

- Integers that fit in int64 are stored as int64; `uint64` holds only larger values, so round
  trips preserve the kind. Doubles are written in shortest round-trip form and always read back
  as doubles.
- Objects keep member order; duplicate keys are rejected by default (`json_parse_options::duplicate_keys`).
- Equality is structural: object member order is ignored and numbers compare by exact value
  across int64, uint64 and double.
- The parser validates UTF-8, rejects raw control characters and lone surrogates, combines
  surrogate pairs, and limits nesting to `max_depth` (256) simultaneously open containers.
  Destroying, copying and comparing values do not recurse; 256 levels of parsing take at most
  about 400 KiB of stack in an unoptimized x64 build and 130 KiB in an optimized one.

### algorithm: numerical and statistical algorithms

Namespace `gb::yadro::algorithm`, aggregate header `algorithm/gbalgorithm.h`.

| Header                   | Facilities |
|--------------------------|------------|
| `genetic_optimization.h` | A generational **genetic algorithm optimizer** (`conv::genetic_optimization_t`, `make_optimizer`) over mixed search spaces of integer, floating-point, categorical and container genes (`min_max_value_range`, `discrete_value_range`, `container_range`). It offers tournament selection, SBX crossover, elitism, memoized fitness evaluation, single-threaded or thread-pool evaluation, four stopping criteria (stagnation, diversity/cataclysm, target fitness, elite convergence), time and evaluation budgets, adaptive phases, a deterministic mode with reproducible results, and history and statistics reporting. The header comment documents the algorithm and its tuning in detail. |
| `regression_analysis.h`  | `residuals`, and `least_squares_optimizer` / `least_abs_optimizer`, which fit parameterized models using the genetic optimizer. |
| `statistics.h`           | `mean_stddev`, the Kolmogorov-Smirnov two-sample test, the Shapiro-Francia normality test, and the `sigmoid1090`, `gauss_filter` and `sigmoid_filter` shaping functions. |
| `student.h`              | `student_t`: confidence intervals for the mean and standard deviation using Student's t-distribution. |
| `discrete_transform.h`   | FFT/IFFT (recursive, and iterative with AVX2), the DFT, Bluestein's DFT for arbitrary lengths, and spectral decomposition. |
| `chebyshev.h`            | Chebyshev polynomial least-squares smoothing (`cheb::chebyshev_filter`, full-signal or windowed). |
| `adf_test.h`, `mackinnon.h` | The Augmented Dickey-Fuller stationarity test (`adfuller`) with MacKinnon p-values. |
| `hmm.h`                  | `hmm_2state_t`: a two-state hidden Markov model fitted with scaled forward-backward (Baum-Welch). |

```cpp
using namespace gb::yadro::algorithm::conv;
using namespace std::chrono_literals;

genetic_optimization_t opt(
    [](int a, double x) { return (x - 1.5) * (x - 1.5) + a * a; },  // fitness
    std::less<double>{},                                            // minimize
    min_max_value_range<int>{ -10, 10 },
    min_max_value_range<double>{ -10., 10. });

auto [stats, history] = opt.optimize(100ms, /*population*/ 100, /*history*/ 5);
auto& best = history.best();                        // best fitness and chromosome
```

### simulator: discrete-event simulation

Namespace `gb::sim`, aggregate header `simulator/simulator.h`.

This is an event-driven simulation kernel in the style of hardware description languages,
available in two flavors:

- `gb::sim::coroutines`: processes are C++20 coroutines (`sim_task`) that `co_await`
  events and signals.
- `gb::sim::fibers`: processes are fibers that call wait functions (Windows fibers).

Building blocks:

- `scheduler_t`: owns simulation time, schedules events and runs until idle or a time
  limit (`run(max_time)`).
- `event`, plus the combinators `any_of`/`all_of` and `empty_event`.
- `signal<T>`: a value with delayed assignment (`sig(delay) = value`) and change
  notification, plus `wire`, `const_signal`, `conditional_event`, edge detectors
  (`pos_edge`, `neg_edge`), and `always(fn, signals...)` for sensitivity-list style
  processes.

```cpp
using namespace gb::sim;
using namespace gb::sim::coroutines;

scheduler_t sch;
signal clk(false, sch);
auto clock_gen = [](auto& clk, int period) -> sim_task {
    for (;;) { co_await clk; clk(period / 2) = !clk; }
};
clock_gen(clk, 2);
clk(0) = true;
sch.run(10);                                   // simulate 10 time units
```

---

## Testing

Tests live in `test/`, one file per module, and use the library's own `GB_TEST` framework:

```cpp
GB_TEST(container, my_test)                        // deferred (sequential) by default
{
    gbassert(1 + 1 == 2);
}
GB_TEST(algorithm, my_parallel_test, std::launch::async)  // may run on the thread pool
{
    ...
}
```

`test/yadro_test.cpp` is the driver. It turns on verbose logging to the console and
`yadro-test.log`, disables the platform-specific tests that don't apply, and runs all
suites. Build and run the `yadro_test` project. A few slow or environment-dependent tests
(for example `bounded_priority_queue_test` and the live Windows registry integration test)
are skipped unless you pass `--run-all`. The process exits with 0 when all enabled tests
pass and -1 otherwise.

## License

Distributed under the [Boost Software License, Version 1.0](LICENSE).
Copyright (C) 2011-2026, Gene Bushuyev.
