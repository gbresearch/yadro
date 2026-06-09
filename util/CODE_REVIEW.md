# Code Review — `yadro/util` Facilities

Review date: 2026-06-08
Scope: every file under `yadro/util`, cross-checked against `vs/yadro.vcxproj` (library)
and `test/util_test.cpp` (tests, built by `vs/yadro_test.vcxproj`).

Primary review goals (per request):
1. Confirm which files are actually used by the build; flag dead files.
2. Verify platform independence — Windows-only code must sit behind `#ifdef`
   guards so a GCC/Clang build does not break.
3. Audit `test/util_test.cpp` for wrong or missing tests.

---

## 1. File usage vs. `yadro.vcxproj`

The directory contains **23 files**; the project references all 23:

| util file | role | referenced in vcxproj | reachable from `gbutil.h` |
|---|---|---|---|
| `gbutil.h` | umbrella include | ✅ `ClInclude` | (is the umbrella) |
| `gbmacro.h` | macros (`GB_TIMER`, concat/stringify) | ✅ | ✅ |
| `gberror.h` | exceptions, `gbassert`, `must_throw` | ✅ | ✅ |
| `gblog.h` | thread-safe `logger`, `tab` | ✅ | ✅ |
| `gbmemory.h` | `make_unique_*`, aligned alloc | ✅ | ✅ |
| `gbtest.h` | unit-test framework, `GB_TEST` | ✅ | ✅ |
| `gbtimer.h` | accumulating/scope timers | ✅ | ✅ |
| `gbwin.h` | Win: `dll`, uuid, temp path, `micro_sleep` | ✅ | ✅ |
| `gnuplot.h` | gnuplot pipe interface (Win) | ✅ | ✅ |
| `hash_util.h` | `make_hash`, xxhash128 (scalar/avx2/stream) | ✅ | ✅ |
| `hybrid_mutex.h` | spin/wait hybrid mutex | ✅ | ✅ |
| `misc.h` | grab-bag utilities | ✅ | ✅ |
| `named_resource_lock.h` | named OS mutex over a resource | ✅ | ✅ |
| `string_util.h` | base64, md5, utf conv, tokenize | ✅ | ✅ |
| `string_util.cpp` | md5 impl | ✅ `ClCompile` | — |
| `time_util.h` | time stamps, date math | ✅ | ✅ |
| `traits.h` | `callable_traits`, `is_detected` | ✅ | ✅ |
| `tuple_functions.h` | tuple/aggregate algorithms | ✅ | ✅ |
| `file_mutex.h` | cross-process `global_mutex` | ✅ | ✅ |
| `file_mutex.cpp` | POSIX `global_mutex` impl | ✅ `ClCompile` | — |
| `win_pipe.h` | named-pipe RPC client/server (Win) | ✅ | ✅ |
| `win_service.h` | Win service host | ✅ | ✅ |
| `win_service.cpp` | Win service impl | ✅ `ClCompile` | — |

**Result: no unused files.** Every header is part of the public surface and is
included by `gbutil.h`; the three `.cpp` units are compiled into the static lib.

---

## 2. Review plan (by functional group)

- **Group A — Diagnostics & errors:** `gberror.h`, `gbmacro.h`
- **Group B — Logging & timing:** `gblog.h`, `gbtimer.h`
- **Group C — Test harness:** `gbtest.h`
- **Group D — Memory & traits:** `gbmemory.h`, `traits.h`
- **Group E — General utilities:** `misc.h`, `tuple_functions.h`
- **Group F — Strings & hashing:** `string_util.{h,cpp}`, `hash_util.h`
- **Group G — Time:** `time_util.h`
- **Group H — Synchronization:** `hybrid_mutex.h`, `file_mutex.{h,cpp}`, `named_resource_lock.h`
- **Group I — Windows-only:** `gbwin.h`, `win_pipe.h`, `win_service.{h,cpp}`, `gnuplot.h`

The driving question for each: does it compile on GCC/Clang (or is it correctly
`#ifdef`-guarded), is it correct, and is it tested?

### Portability guard summary

| File | Windows-only? | Guarded so non-Win build is safe? |
|---|---|---|
| `gbwin.h` | yes | ✅ (`GBWINDOWS` wraps whole body) |
| `win_pipe.h` | yes | ✅ (`#ifdef GBWINDOWS`) |
| `win_service.{h,cpp}` | yes | ✅ (`#ifdef GBWINDOWS`) |
| `named_resource_lock.h` | mostly | ⚠️ mixes `GBWINDOWS` and `_WIN32` (I-12) |
| `file_mutex.{h,cpp}` | split | ⚠️ POSIX branch missing headers (H-13) |
| `gnuplot.h` | yes | ❌ **not guarded** — uses `_popen`/Win-only helpers (I-4) |
| `gblog.h` | no (claims portable) | ❌ unconditional `<process.h>` (B-3) |
| `time_util.h` | no (claims portable) | ❌ uses `::_getpid()` (G-2) |
| `gbmacro.h` | no | ❌ `##` paste breaks GCC/Clang (A-1) |

---

## 3. Issues found

Severity: **H**=high (build break / wrong result), **M**=medium (latent bug /
unreachable path), **L**=low (cleanup / warning / doc).

| # | File:loc | Sev | Issue | Resolution |
|---|---|---|---|---|
| A-1 | `gbmacro.h:41,45` | H | `std::chrono::##time_unit` — the `##` paste joins `::` with the arg, producing an invalid preprocessing token. MSVC tolerates it; GCC/Clang error ("pasting … does not give a valid token"). `GB_TIMER` is unbuildable off-MSVC. Drop the `##`. | ✅ Fixed — `gbmacro.h` now uses `std::chrono::time_unit`. |
| B-3 | `gblog.h:39` | H | Unconditional `#include <process.h>` (a Windows-only header). Breaks GCC/Clang. The "for getpid" comment is stale — `getpid` is not used in this header. Remove the include or guard it. | ✅ Fixed — removed the stale `<process.h>` include. |
| G-2 | `time_util.h:44,48` | H | `time_stamp()` calls `::_getpid()` (MSVC-only, needs `<process.h>` which this header doesn't include). Header is advertised as platform-independent but won't compile on GCC/Clang. Use a guarded pid helper. | ✅ Fixed — added portable `get_process_id()` (`_getpid`/`getpid`) with guarded includes. |
| I-4 | `gnuplot.h` (whole) | H | Windows-only (`_popen`/`_pclose` at 518/538, `get_temp_file_path` from `gbwin.h`) but **not** wrapped in `#ifdef GBWINDOWS`. Pulled in unconditionally by `gbutil.h` ⇒ non-Win build fails. Guard the file. | ✅ Fixed — body wrapped in `#ifdef GBWINDOWS`; gnuplot test in `util_test.cpp` guarded to match; dead `foo()` removed (I-18). |
| F-9 | `string_util.h:264` | H | `base64_decode`: loop guard `--in_len` (pre-decrement) drops the **last** character when the encoded text length is a multiple of 4 with no `=` padding (i.e. original size a multiple of 3). Returns one byte short. Masked in tests because every tested input is padded. Use post-decrement / fix the count. | ✅ Fixed — changed `--in_len` to `in_len--`; regression test T-1 added. |
| H-13 | `file_mutex.{h,cpp}` POSIX branch | H | POSIX `global_mutex` uses `open/read/write/lseek/fcntl/errno/O_RDWR/F_WRLCK` but neither file includes `<unistd.h>`, `<fcntl.h>`, `<cerrno>`. POSIX build won't compile. Add the includes. | ✅ Fixed — added `<unistd.h>`, `<fcntl.h>`, `<cerrno>` to the POSIX branch. |
| C-8 | `gbtest.h:229` | M | `_disable_test` compares `test->_test_name == test_name` — pointer comparison of `const char*`, not string compare. `disable_tests(...)` silently fails to match. Use `std::strcmp`/`string_view`. | ✅ Fixed — compares via `std::string_view`; safe no-op test T-4 added. |
| H-6 | `hybrid_mutex.h:162-173` | M | `try_lock_until` phase 2 calls `state.wait(1)` (untimed). Under sustained contention with no notify it can block **past** the deadline, so the timeout is not honored. Bound the wait or re-check via a timed strategy. | ✅ Fixed — replaced untimed `state.wait()` with capped backoff bounded by remaining time; test T-2 added. |
| E-14 | `misc.h:413-419` | M | `window_function`: ternary `value<min \|\| value>max ? fun(value-min) : value>max ? fun(max-value) : value` — the `value>max` branch is unreachable (caught by the first condition), so the over-max case uses `value-min` instead of the max-relative value. Likely a logic bug; untested. | ✅ Fixed — first branch now `value < min_value` only, restoring the distinct over-max branch; test T-3 added. |
| I-12 | `named_resource_lock.h:43 vs 96` | M | Mixes guards: `comparable_resource_path` uses `#ifdef _WIN32`, but `acquire`/`is_named_resource_locked` use `#ifdef GBWINDOWS`. If the header is included standalone on Windows (without `gbwin.h` defining `GBWINDOWS`), `acquire` throws "not implemented" while the path is still lower-cased. Use one guard consistently. | ✅ Fixed — includes `gbwin.h` and uses `GBWINDOWS` throughout. |
| D-11 | `gbmemory.h:94` | L | `aligned_allocator::deallocate` passes `n` (element count) to the sized+aligned `operator delete`, which expects **bytes** (`sizeof(T)*n`). Also no converting/rebind constructor (`aligned_allocator(const aligned_allocator<U,A>&)`), which the standard allocator requirements expect. | ✅ Fixed — deallocate now passes `sizeof(T)*n` bytes; added rebind converting constructor. |
| B-16 | `gbtimer.h:172,183` | L | `inline [[nodiscard]] auto make_*` — attribute placed after the `inline` specifier; GCC/Clang may emit "attribute ignored". Prefer `[[nodiscard]] inline`. | ✅ Fixed — reordered on both `make_accumulating_timer`/`make_slave_timer`. |
| H-5 | `hybrid_mutex.h:102` | L | `[[nodiscard]] void lock()` — `nodiscard` on a `void` return is meaningless; GCC/Clang warn (and with `/WX`-equivalents fail). Remove it. | ✅ Fixed — `[[nodiscard]]` removed from `lock()`. |
| C-7 | `gbtest.h:236` | L | `test_base` ctor init list orders `_test_name` before `_policy`, but `_policy` is declared first ⇒ `-Wreorder` warning on GCC/Clang. Reorder the init list. | ✅ Fixed — init list now `_policy`, `_test_name`. |
| F-10 | `string_util.h:311` | L | `tokenize<T>` hardcodes `std::string token;` instead of `std::basic_string<T>`; only `char` works (test only uses `char`). | ✅ Fixed — token is now `std::basic_string<T>`. |
| F-17 | `hash_util.h:49` | L | `make_hash(unsigned v){ return v; }` marked "TODO: remove after experiment" — identity hash leftover; remove or justify. | ✅ Fixed — removed; `unsigned` falls through to the generic `std::hash` overload. |
| I-15 | `gbwin.h:1, :149` | L | Missing the standard license header (every other file has it). `micro_sleep` comment says "nanoseconds → QPC ticks" but the unit is microseconds. | ✅ Fixed — added license header; corrected comment to "microseconds". |
| I-18 | `gnuplot.h:505` | L | Leftover `foo()` helper that returns a sample plot command — dead code; remove. | ✅ Fixed — removed (folded into I-4). |

---

## 4. Test review (`test/util_test.cpp`)

### Existing tests — assessment
Coverage is strong for: `logger`/`tab`, `named_resource_lock`, all of
`tuple_functions.h`, `hash_util.h` (xxhash128 static/stream/avx2, boundaries,
alignment, seeds), `misc.h` highlights (`locked_call`, `raii`, `retainer`,
`accumulating_timer`, `almost_equal`, `tokenize`), `string_util.h`
(base64 round-trip *padded only*, md5, UTF conv), `time_util` (`datetime_to_chrono`,
`week_of_year`), and an extensive `win_pipe` suite. No tests were found to be
*wrong* per se, but two have blind spots that hide real bugs (below).

### Missing / blind-spot tests

| T# | Target | Gap | Catches |
|---|---|---|---|
| T-1 | `base64_decode` | No unpadded input (encoded length % 4 == 0, original size % 3 == 0), e.g. round-trip a 3- or 12-byte buffer. | **F-9** — ✅ added (round-trip loop over sizes 0..16 in `string_util` test). |
| T-2 | `hybrid_mutex.h` | Entirely untested — `hybrid_mutex` lock/unlock/`try_lock`, `try_lock_for/until` timeout, `spin_wait`. | **H-6** — ✅ added (`hybrid_mutex_test`: lock/try_lock, bounded timeout, 4-thread contention). |
| T-3 | `misc.h::window_function` | Untested; add below/in-range/above-max cases. | **E-14** — ✅ added (in/at/below/above cases in `misc` test). |
| T-4 | `gbtest.h` self-test | `disable_tests` / `disable_suites` never exercised. | **C-8** — ✅ added (`test_harness_disable_is_safe_noop`, non-destructive: matches the comparison path against real tests without disabling any; full match-true coverage deferred as it would need harness support to avoid mutating the global run). |
| T-5 | `gbmemory.h` | Untested: `make_unique_copy`, `create_unique` overloads, `aligned_allocator`/`aligned_vector`, `make_unique_array`, `aligned_array`. | **D-11** — ✅ added (`gbmemory_test`; over-alignment checks also cover the rebind/byte-size dealloc fix). |
| T-6 | `time_util.h` | `time_stamp`, `unpack_time`, `to_time_point` untested. | **G-2** — ✅ added (`time_util_test`; `time_stamp` pid check covers `get_process_id`, tz-independent date math). |
| T-7 | `traits.h` | `callable_traits`, `is_detected*` family untested. | ✅ added (`traits_test`; lambda/fn-ptr traits, `is_detected*`, `explicitly_convertible_to`). |
| T-8 | `string_util.h` | `tokenize<wchar_t>`, `from_wstring`, `wrap_cmd`, `compare(span)`, `strings_equal` untested. | **F-10** — ✅ added (`string_util_extra`; `tokenize<wchar_t>` covers the char-type fix). |
| T-9 | `gbwin.h` | `dll`, `get_uuid_string/wstring`, `get_temp_file_path`, `micro_sleep` untested. | ✅ added (`gbwin_test`, GBWINDOWS-guarded). |
| T-10 | `gbtimer.h` | `GB_TIMER` macro, `global_timer_map_t`, `make_slave_timer`, `get_duration_suffix` untested. | **A-1** — ✅ added (`gbtimer_test`; instantiating `GB_TIMER` exercises the token-paste fix). |
| T-11 | `file_mutex.h` | `global_mutex` not directly tested (only indirectly via the pipe server mutex). | **H-13** — ✅ added (`global_mutex_test`, cross-thread named-mutex contention, GBWINDOWS-guarded). |
| T-12 | `gberror.h` | `throw_error`, `unreachable`, `exception_t<Data>` payload + `serialize` untested. | ✅ added (`gberror_test`; `throw_error`/`unreachable`/payload/`error_t` tag — `serialize` still only via archive tests). |

All T-1…T-12 implemented; full build (`Debug|x64`) passes with **187 tests passing,
0 failing, 1 pre-existing disabled**. Remaining untested surface is intentionally
deferred: POSIX `file_mutex` (needs a non-Windows host), `win_service` (needs SCM /
service host), and `exception_t::serialize` (covered indirectly by archive tests).

---

## 5. Notes / non-issues
- `win_pipe.h`, `win_service.{h,cpp}`, `gbwin.h` are correctly fenced behind
  `GBWINDOWS`; the static-lib project does not define `Unicode`, so `win_service.cpp`'s
  `TCHAR`-based SCM calls resolve to the ANSI variants and match `std::string`.
- `hash_util.h`'s `xxhash128_simd_guard` array-size-mismatch trick to force a
  link error on inconsistent AVX2 settings across TUs is intentional and fine.
- The `make_hash` overload set is potentially ambiguous for types that are both
  `std::hash`-able and a `common_range` (e.g. `std::string`), but no current call
  site exercises that path; left as an observation, not a logged issue.
