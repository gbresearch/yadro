# General JSON Value and Hardened Parser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal.** Add a general JSON value type with a writer, and a single hardened AXE-based JSON front end. The new DOM builder and json_db's existing builder both use that front end. The result is orchestrator prerequisite P2, and `read_json` gets the same input safety.

**Architecture.**

- `container/json_parser.h` holds:
  - the error types and options;
  - the SAX handler concept;
  - the capped stream reader;
  - a UTF-8 encode and decode helper for writers;
  - the shared string escaper;
  - `parse_json_events<H>()`.
- The front end has two parts:
  - **AXE token rules**, used by `parse_json_events`: whitespace, literals, numbers, and string characters through `axe::r_utf8()`.
  - **One hand-written structural rule** for objects and arrays, which a byte dispatcher invokes through a directly held `axe::r_depth_limit_t`. The wrapper is never nested inside an AXE composite.
- `container/json.h` holds the DOM (`json_value`), JSON pointer, equality, the writer, `json_value_builder`, and the parse entry points.
- `container/gbdb_json.h` drives `json_db_builder` through the same front end.

**Tech stack:**

- MSVC VS 18 with `/std:c++latest`.
- Header-only Yadro containers.
- AXE `master` at or after `4e93d14`, specifically `r_depth_limit` and `r_utf8`.
- `std::expected`, `std::from_chars` and `std::to_chars`.
- `GB_TEST`.
- MSBuild for x64 and Win32, Debug and Release.

**Spec:** `docs/superpowers/specs/2026-09-23-general-json-value-and-hardened-parser-design.md` (revision 2, approved 2026-09-24, including decisions 10–12).

## Global Constraints

**Dependencies and code origin**

- No third-party code or data. The conformance cases are written in-house; nothing is copied from JSONTestSuite.
- Do not modify AXE. The `r_binary_fn_t::get()` fix is being made separately in AXE worktree `nifty-benz-d1ce97` (branch `fix/composite-get-by-ref`). This design must not depend on it.

**Parser rules**

- The UTF-8 validity of parser input is decided only by `axe::r_utf8()`. Yadro's own UTF-8 decoder (`detail::utf8_decode`) serves only the writers and the escaper, which need no AXE. A cross-check test (Task 1) pins the two to the same byte-level verdicts.
- The depth wrapper (`axe::r_depth_limit_t<structural_rule>`) is a member of the per-call parser object. Only the value dispatcher invokes it, and only when the next byte is `{` or `[`.
  - Never place it inside `|`, `&`, `%`, `*`, or any other AXE composite.
  - Never recurse through an `r_rule`.
- The parser iterator type is `const char*`, not `std::string_view::const_iterator`. This keeps stack frames independent of the consumer's `_ITERATOR_DEBUG_LEVEL`. The orchestrator's Debug builds use the default level, 2.
- The front end throws `json_parse_error` for its own errors. It translates `axe::depth_limit_exceeded<const char*>` and `json_handler_error`, and it lets every other exception propagate unchanged. That includes json_db's `std::logic_error` and `std::bad_alloc`.

**Compatibility**

- json_db's value model, integer event classification, table behaviour, duplicate-key behaviour, and manifest handling are unchanged, except for the behaviour changes the spec lists.
- `json_parse_error`, `read_json`, `write_json`, and every existing json_db API stay source-compatible.
- No behaviour or name changes outside the files in the File Map.

**Build and tests**

- The code compiles without warnings at the test project's `/W3 /WX`. The new headers are also checked at `/W4` (Task 10).
- The non-AXE configuration (`GB_YADRO_ENABLE_AXE_JSON` undefined) must compile. Its parse entry points throw `std::logic_error`, as `read_json` does today.
- Use TDD: write the tests, confirm RED (compile failure or failing assertion), implement, confirm GREEN with the complete suite, then commit. Make one commit per task.

## Build commands

These use the pre-allowed forms from `CLAUDE.md`. From a worktree, AXE is not at `..\..\axe`, so every build passes `AxeIncludeDir` once Task 0 adds that property. Before Task 0 adds it, a worktree build cannot find AXE.

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" vs\yadro.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /p:AxeIncludeDir=C:\Projects\GitHub\axe\include /m /nologo /v:minimal 2>&1 | Select-Object -Last 40
```

- **Configurations.** Substitute `Release` for `Debug`, and `Win32` for `x64`, as each step requires.
- **Running the tests.** The test project's post-build event runs `yadro_test.exe`, so a GREEN build runs the complete suite. Rerun it directly with:

  ```powershell
  & '.\exe\x64\Debug\yadro_test.exe' 2>&1 | Select-Object -Last 40
  ```

- **Compile-only RED steps.** Append `/p:PostBuildEventUseInBuild=false`. The exe has no per-test filter.
- **Expected summary line:** `tests passed: N, failed: 0, disabled: D`. Record N and D for each configuration at the Task 0 baseline. Later tasks must show `failed: 0`, with N increased only by the new tests.

---

## File Map

- **Create `container/json_parser.h`:**
  - `json_parse_errc`, and `json_parse_error` (moved from `gbdb_json.h`, with `code` added);
  - `json_handler_error`, `json_parse_options`, `json_big_integer_policy`, and `json_duplicate_keys`;
  - the `json_handler` concept;
  - `json_invalid_utf8`, `json_escape_result`, and `append_json_string`;
  - in `detail`: UTF-8 helpers, `position_of`, `throw_parse_error`, and `read_capped`;
  - `parse_json_events`, with the grammar and structural rule.
- **Create `container/json.h`:**
  - `json_kind`, `json_value`, `json_array`, `json_member`, and `json_object`;
  - `json_access_error`, the `as_*` accessors, and `get_if`;
  - `json_pointer_error`, `at_pointer`, and the pointer builders;
  - `operator==`;
  - `json_format`, `json_write_error`, `format_json`, and `append_json`;
  - `json_value_builder`, `parse_json_value`, and `try_parse_json_value`.
- **Modify `container/gbdb_json.h`:**
  - include `json_parser.h` and remove the local `json_parse_error`;
  - add the three new `json_read_options` fields;
  - persist the new options in `write_json_defaults` and `apply_defaults_from_db`;
  - add `to_string` and `parse` for `json_big_integer_policy`;
  - `read_json` uses `parse_json_events`;
  - route all stream and file reads through `detail::read_capped`;
  - `json_writer::write_string` uses `append_json_string`;
  - delete `detail::axe_json_reader` and `detail::read_stream`.
- **Modify `container/gbcontainer.h`:** include `json_parser.h` and `json.h`.
- **Create `test/json_test.cpp`** (suite `json`) with all new tests.
- **Modify the project files:**
  - `vs/yadro.vcxproj` and `vs/yadro.vcxproj.filters`: add the two headers under filter `container`.
  - `vs/yadro_test.vcxproj` and `vs/yadro_test.vcxproj.filters`: add `test\json_test.cpp`, and add the `AxeIncludeDir` property.
- **Modify `README.md`:** a `json.h` and `json_parser.h` entry in the container table and the gbdb section, plus the stricter-input note.
- **Modify the plan and spec** only for the execution handoff and any recorded deviations.

Existing tests in `test/container_test.cpp` must pass unmodified. If a test depends on newly rejected input, stop and report it. Do not edit it silently.

---

### Task 0: Integrate with current master, make worktree builds find AXE, and record the baseline

**Files:**
- Modify: `vs/yadro_test.vcxproj` (4 `AdditionalIncludeDirectories` entries plus a property group).

- [ ] **Step 1: Rebase onto the current master and check AXE**

  ```powershell
  git rebase master
  git log --oneline -1 master
  git -C C:\Projects\GitHub\axe log --oneline -3
  ```

  Expected:
  - the rebase is clean, because the branch holds docs-only commits;
  - AXE `master` is at or after `4e93d14`, and `include\axe_depth.h` and `include\axe_utf8.h` exist.

  Record both hashes in the handoff.
- [ ] **Step 2: Add `AxeIncludeDir`**
  1. Add a global property group near the top of `vs/yadro_test.vcxproj`:

     ```xml
     <PropertyGroup Label="UserMacros">
       <AxeIncludeDir Condition="'$(AxeIncludeDir)'==''">$(ProjectDir)..\..\axe\include</AxeIncludeDir>
     </PropertyGroup>
     ```

  2. Replace each of the four `$(ProjectDir)..\..\axe\include` occurrences with `$(AxeIncludeDir)`. In the two configurations that currently omit it, keep `;%(AdditionalIncludeDirectories)` unchanged.
- [ ] **Step 3: Baseline, before any JSON change**

  Build x64 Debug, x64 Release, Win32 Debug, and Win32 Release with the command above.
  - Record `tests passed/failed/disabled` for each configuration.
  - If Win32 fails to build or has failing tests at baseline, record the exact error. Win32 then drops out of the acceptance criteria, as the spec's "if it builds today" allows. Do not fix unrelated Win32 issues.
  - From the main checkout (`C:\Projects\GitHub\yadro`), confirm that a build without `/p:AxeIncludeDir` still resolves AXE. Use a compile-only build of `vs\yadro_test.vcxproj`.
- [ ] **Step 4: Commit**

  ```powershell
  git add -- vs/yadro_test.vcxproj
  git commit -m "build: make the AXE include directory overridable for worktree builds"
  ```

---

### Task 1: `json_parser.h` foundation: errors, options, UTF-8 helpers, escaper, and capped reader

**Files:**
- Create: `container/json_parser.h`
- Create: `test/json_test.cpp`
- Modify: `container/gbdb_json.h` (remove the `json_parse_error` definition at `:61-70`, and include `json_parser.h`)
- Modify: `vs/yadro.vcxproj`, `vs/yadro.vcxproj.filters`, `vs/yadro_test.vcxproj`, and `vs/yadro_test.vcxproj.filters`

**Interfaces produced** (all in `gb::yadro::container`):

```cpp
enum class json_parse_errc { syntax, unexpected_end, control_character, invalid_escape,
    lone_surrogate, invalid_utf8, depth_exceeded, input_too_large, number_out_of_range,
    duplicate_key, handler_rejected };
[[nodiscard]] constexpr std::string_view to_string(json_parse_errc) noexcept;

struct json_parse_error : std::runtime_error {
    std::size_t offset = 0; std::uint32_t line = 0; std::uint32_t column = 0;
    json_parse_errc code = json_parse_errc::syntax;
    json_parse_error(std::string message, std::size_t offset, std::uint32_t line,
                     std::uint32_t column, json_parse_errc code = json_parse_errc::syntax);
};
struct json_handler_error : std::runtime_error {
    json_parse_errc code;
    json_handler_error(json_parse_errc code, std::string message);
};

enum class json_big_integer_policy { error, to_double };
enum class json_duplicate_keys { reject, keep_first, keep_last };
struct json_parse_options { std::size_t max_depth = 256; std::size_t max_input_bytes = 0;
    json_big_integer_policy big_integers = json_big_integer_policy::error;
    bool allow_scalar_root = true; json_duplicate_keys duplicate_keys = json_duplicate_keys::reject; };

template<class H> concept json_handler = /* spec § Handler interface */;

enum class json_invalid_utf8 { error, replace };
struct json_escape_result { bool ok = true; std::size_t invalid_offset = 0; };
[[nodiscard]] json_escape_result append_json_string(std::string& out, std::string_view s,
    bool ascii_only, json_invalid_utf8 policy);

namespace detail {
    struct utf8_decoded { char32_t code_point; std::uint8_t length; bool valid; }; // invalid: length = maximal subpart (>= 1)
    [[nodiscard]] utf8_decoded utf8_decode(std::string_view s, std::size_t i) noexcept;
    void append_utf8(char32_t code_point, std::string& out);
    struct json_position { std::uint32_t line; std::uint32_t column; };
    [[nodiscard]] json_position position_of(std::string_view text, std::size_t offset) noexcept;
    [[noreturn]] void throw_parse_error(std::string_view text, std::size_t offset,
                                        json_parse_errc code, std::string_view message);
    [[nodiscard]] std::string read_capped(std::istream& in, std::size_t cap);
}
```

- [ ] **Step 1: Write the failing tests** in the new `test/json_test.cpp`, suite `json`

  1. Copy the license header and the `namespace { using namespace gb::yadro::container; using namespace gb::yadro::util; ...` scaffold from `test/container_test.cpp`.
  2. Add the file to `vs/yadro_test.vcxproj` and its `.filters` file (filter `Source Files`, like the other tests).
  3. Add these tests:
     - **`json_parse_error_compat_test`.** The old 4-argument constructor still compiles, and `code` defaults to `syntax`. `to_string` returns a distinct name for every enumerator.
     - **`json_escape_basic_test`.** Golden outputs:
       - `"a\"b\\c/\b\f\n\r\t\x01\x1f\x7f"` → `"a\\\"b\\\\c/\\b\\f\\n\\r\\t\\u0001\\u001f\x7f"`. `/` and DEL stay raw, and hex is lowercase.
       - `"é😀"` with `ascii_only=false` → unchanged bytes.
       - With `ascii_only=true` → `\u00e9\ud83d\ude00`.
     - **`json_escape_invalid_utf8_test`:**
       - With `error`, each invalid input returns `ok == false` and the exact `invalid_offset`. The inputs are `"ab\xC0\xAF"` (offset 2), `"x\xED\xA0\x80"` (1), `"\xF4\x90\x80\x80"` (0), `"ok\xE2\x82"` (2, truncated), and `"\x80"` (0).
       - With `replace`, each maximal subpart becomes one `U+FFFD`: `"\xE2\x82A"` → `"\xEF\xBF\xBDA"`, and `"\xC0\xAF"` → two replacement characters.
     - **`json_utf8_decoder_matches_axe_test`** (inside `if constexpr (gbdb_json_axe_enabled)`). Compare `detail::utf8_decode` with `axe::r_utf8()`, using `const char*` iterators, on validity and consumed length, over:
       - every single byte;
       - every 2-byte sequence;
       - every `E0`–`EF` lead with all continuation pairs;
       - every `F0`–`F4` lead with all 3-byte continuation tails drawn from {`80`, `8F`, `90`, `9F`, `A0`, `BF`, `C0`, `41`}.

       Both must agree on validity. For valid input, the lengths must match.
     - **`json_position_test`.** `position_of` over `"ab\ncd\r\nef"` gives:
       - offset 0 → line 1, column 1;
       - offset 3 → line 2, column 1;
       - offset 6 → line 2, column 4 (`\r` counts as a byte);
       - offset 7 → line 3, column 1.
     - **`json_read_capped_test`.** A `counting_streambuf` (a `std::streambuf` subclass serving a string through `underflow`, one 4 KiB chunk at a time, that counts the bytes handed out) is used as follows:
       - with cap 0, it reads everything;
       - input exactly at the cap is returned intact;
       - a cap of `size - 1` throws `json_parse_error` with `code == input_too_large` and `offset == cap`;
       - on a 1 MiB source with cap 10, the bytes consumed are at most `cap + 1 + 4096`, and the returned or buffered string never exceeds `cap + 1`;
       - a stream in fail state (not EOF) throws `std::runtime_error("Failed to read JSON stream")`.
- [ ] **Step 2: Confirm RED.** Run a compile-only Debug x64 build. Expected: errors naming `json_parse_errc`, `append_json_string`, and the `detail::` helpers.
- [ ] **Step 3: Implement `json_parser.h`**
  1. Add the Boost license header, `#pragma once`, and the standard headers.
  2. Add the AXE opt-in block (`GB_YADRO_ENABLE_AXE_JSON` → `<axe.h>`, with the same `#error` when AXE is missing). It defines `GB_YADRO_JSON_PARSER_HAS_AXE` and exports `inline constexpr bool json_parser_axe_enabled`. Undefine the macro at the end of the file, as `gbdb_json.h` does with its own `GB_YADRO_GBDB_JSON_HAS_AXE`.
  3. `gbdb_json.h` keeps its own block and its `gbdb_json_axe_enabled` constant, unchanged. `parse_json_events` is declared in both modes; in the non-AXE mode it throws. As a result, `json.h` needs no AXE macro of its own.

  Implementation rules:
  - **`utf8_decode`** follows Unicode Table 3-7:
    - an invalid lead byte, or a lone continuation byte, has length 1;
    - a truncated or ill-formed sequence has, as its length, the maximal subpart: the number of bytes that form a valid prefix, with a minimum of 1.
  - **`append_json_string`:**
    - copies runs of safe ASCII in bulk (`out.append(ptr, n)`);
    - escapes as the tests pin;
    - with `ascii_only`, decodes each multi-byte sequence and emits either `\uXXXX` or a pair `\uD8xx\uDCxx`, with lowercase hex;
    - otherwise copies valid multi-byte sequences unchanged;
    - validates UTF-8 in both modes.

    Its only exception is `std::bad_alloc`.
  - **`read_capped`:**
    - loops `in.read(buf, 64 KiB chunk)`, or `min(chunk, cap + 1 - size)` when a cap is set;
    - appends `gcount()` bytes;
    - stops at EOF;
    - throws `input_too_large` through `throw_parse_error(buffer, cap, ...)` as soon as `size > cap`, computing line and column over the buffer;
    - throws the existing `runtime_error` message when `!in && !in.eof()`.
  - **`throw_parse_error`** computes the line and column from `position_of`.
- [ ] **Step 4: Update `gbdb_json.h` and the project files**
  - Delete the local `json_parse_error` struct and add `#include "json_parser.h"`.
  - Add both new headers to `vs/yadro.vcxproj` and its `.filters` file under filter `container`. `json.h` is created empty with just its license header and `#pragma once` here, so that the project entries stay valid.
  - Add both headers to `gbcontainer.h` after `gbdb_json_path.h`.
- [ ] **Step 5: GREEN.** Build Debug x64 and Release x64. Expected: exit 0, `failed: 0`, and all existing container JSON tests pass, since only the error type moved.
- [ ] **Step 6: Commit**

  ```powershell
  git add -- container/json_parser.h container/json.h container/gbdb_json.h container/gbcontainer.h test/json_test.cpp vs/yadro.vcxproj vs/yadro.vcxproj.filters vs/yadro_test.vcxproj vs/yadro_test.vcxproj.filters
  git commit -m "feat: add shared JSON error, escaper, UTF-8 and capped-stream foundation"
  ```

---

### Task 2: The hardened front end, `parse_json_events`

**Files:**
- Modify: `container/json_parser.h`
- Test: `test/json_test.cpp`

**Interfaces produced:** `template<json_handler H> void parse_json_events(std::string_view text, H& handler, const json_parse_options& options = {});`

- **With AXE enabled:** as specified below.
- **Without AXE:** it throws `std::logic_error("JSON parsing requires opt-in AXE support: define GB_YADRO_ENABLE_AXE_JSON and add AXE include directory")`.

- [ ] **Step 1: Write the failing tests** (all inside `if constexpr (gbdb_json_axe_enabled)`)

  **Test helpers.** `event_recorder` implements the handler concept and appends compact tokens to a `std::string`:
  - `{`, `}`, `[`, `]`;
  - `K<key>`, `S<str>`, `I<n>`, `U<n>`, `D<shortest to_chars>`, `B1`, `B0`, `N`;
  - a separator `|` after each token.

  `expect_error(text, code, offset, options = {})` catches `json_parse_error`, asserts the code and offset, and fails if nothing was thrown.

  Tests:
  - **`json_events_basic_test`:**
    - `{"a":[1,-2,3.5,"x",true,false,null],"b":{}}` → `{|Ka|[|U1|I-2|D3.5|Sx|B1|B0|N|]|Kb|{|}|}|`;
    - `"s"` at the root → `Ss|`;
    - `12` at the root → `U12|`;
    - leading and trailing whitespace (only `\x20\t\n\r`) is accepted.
  - **`json_events_syntax_errors_test`** (code at offset). Inputs in these tables are C++ string literals, so `""` is the empty document.

    | Input | Code | Offset |
    |---|---|---|
    | `""` | `unexpected_end` | 0 |
    | `"   "` | `unexpected_end` | 3 |
    | `"[1,]"` | `syntax` | 3 |
    | `"{\"a\":1,}"` | `syntax` | 7 |
    | `"[1 2]"` | `syntax` | 3 |
    | `"{\"a\" 1}"` | `syntax` | 5 |
    | `"{1:2}"` | `syntax` | 1 |
    | `"01"` | `syntax` | 1 |
    | `"-"` | `syntax` | 1 |
    | `"1."` | `syntax` | 1 |
    | `"tru"` | `unexpected_end` | 0 |
    | `"trux"` | `syntax` | 0 |
    | `"[1]x"` | `syntax` | 3 |
    | `"\xEF\xBB\xBF{}"` | `syntax` | 0 |
    | `"[\v]"` | `syntax` | 1 |
    | `"'a'"` | `syntax` | 0 |
    | `"[NaN]"` | `syntax` | 1 |
    | `"[+1]"` | `syntax` | 1 |
    | `"[.5]"` | `syntax` | 1 |
    | `"[0x1]"` | `syntax` | 2 |
    | `"/*c*/{}"` | `syntax` | 0 |
    | `"["` | `unexpected_end` | 1 |
    | `"{\"a\":"` | `unexpected_end` | 5 |

    `allow_scalar_root=false` with `"1"` gives `syntax` at 0.
  - **`json_events_string_test`:**
    - All escapes decode correctly.
    - `"\u0000"` → a string of one NUL byte (check `size() == 1`).
    - A string without escapes is delivered as a view into the input: the recorder captures `data()` and asserts it lies within the input range.
  - **String errors:**

    | Input | Code | Offset |
    |---|---|---|
    | `"[\"a\x01\"]"` | `control_character` | 3 |
    | `"[\"\x1f\"]"` | `control_character` | 2 |
    | `"[\"\\x\"]"` | `invalid_escape` | 2 |
    | `"[\"\\u12G4\"]"` | `invalid_escape` | 2 |
    | `"[\"\\"` | `unexpected_end` | 3 (a quote, then a backslash at the end) |
    | `"[\"abc"` | `unexpected_end` | 5 |
    | `"[\"\xC0\xAF\"]"` | `invalid_utf8` | 2 |
    | `"[\"a\xED\xA0\x80\"]"` | `invalid_utf8` | 3 |
    | `"[\"\xF4\x90\x80\x80\"]"` | `invalid_utf8` | 2 |
    | `"[\"\xE2\x82\"]"` | `invalid_utf8` | 2 |
    | `"[\"\x80\"]"` | `invalid_utf8` | 2 |

    These are accepted: U+0080 (`C2 80`), U+07FF (`DF BF`), U+0800 (`E0 A0 80`), U+FFFF (`EF BF BF`), U+10000 (`F0 90 80 80`), and U+10FFFF (`F4 8F BF BF`).
  - **`json_events_surrogate_test`:**
    - `"\ud83d\ude00"` and `"\uD83D\uDE00"` → `F0 9F 98 80`.
    - `"\uDBFF\uDFFF"` → `F4 8F BF BF`.

    Lone surrogates, each `lone_surrogate` at the offset of the offending `\u`:
    - `["\ud800"]` → offset 2;
    - `["\ud800x"]` → 2;
    - `["\ud800\ud800"]` → 2;
    - `["\ud800\u0041"]` → 2;
    - `["\udc00"]` → 2;
    - `["a\ude00"]` → 3.
  - **`json_events_integer_test`** (each asserted with its event):
    - `-9223372036854775808` → `I`, `9223372036854775807` → `U`, `18446744073709551615` → `U`;
    - `-9223372036854775809` and `18446744073709551616` → `number_out_of_range` at offset 0 by default;
    - with `to_double`, those two → `D-9.223372036854776e+18` and `D1.8446744073709552e+19`;
    - `-0` → `I0`, and `-0.0` → `D-0`;
    - a 400-digit integer under `to_double` → `number_out_of_range`.
  - **`json_events_double_boundary_test`.** Every row of the spec's MSVC table, wrapped as `[x]` and compared bitwise with `std::bit_cast<std::uint64_t>`:
    - `4.9406564584124654e-324` → `0x1`;
    - `2.4703282292062328e-324` → `0x1`;
    - `2.4703282292062327e-324` → `+0`;
    - `-1e-400` → `0x8000000000000000`;
    - `1e-400`, `123e-10000000`, and `1e-99999999999999999999` → `+0`;
    - `1e-320` → the bit pattern `from_chars` gives, which is subnormal and non-zero;
    - `1.7976931348623158e308` → DBL_MAX;
    - `1.7976931348623159e308`, `1e309`, `-1e309`, and `1e99999999999999999999` → `number_out_of_range` at offset 1;
    - `0e-400` and `0.0000e99999` → `+0`;
    - `1` followed by 400 zeros, then `e-1` → `number_out_of_range` (a positive decimal exponent despite the negative explicit exponent);
    - `0.` followed by 400 zeros, then `1e400` → exactly `1e-1`, i.e. the double nearest 0.1. This checks saturating exponent arithmetic without spurious range errors.
  - **`json_events_depth_test`:**
    - For `max_depth` in {1, 2, 3, 256} and each of the following generators, depth N passes and depth N+1 fails with `depth_exceeded` at the offset of the (N+1)th opening bracket:
      - `arrays(n, leaf)`: `[` × n, the leaf, `]` × n;
      - `objects(n, leaf)`: `{"k":` × n, the leaf, `}` × n;
      - `alternating(n, leaf)`;
      - `with_siblings(n, leaf)`: every level is `{"a":1,"b":[true,null],"k":...}`.

      The leaves are `""` (the innermost container is empty), `1`, `"s"`, `true`, and `null`.
    - `max_depth=0`: `1` passes, and `[]` gives `depth_exceeded` at 0.
    - `[[1]]` passes with `max_depth=2`.
    - Hostile inputs are rejected with `depth_exceeded` at offset 256, or at the offset of the 257th `{`:
      - 1,000,000 `[`;
      - 1,000,000 × `{"a":`;
      - 1,000,000 `[` followed by `x`.
  - **`json_events_input_cap_test`:**
    - `max_input_bytes = size` passes;
    - `size - 1` gives `input_too_large` at offset `size - 1`, before the handler sees any event (the recorder is still empty).
  - **`json_events_handler_error_test`:**
    - A handler throwing `json_handler_error{duplicate_key, "dup"}` from `key()` on the second key of `{"a":1,"a":2}` → `json_parse_error{duplicate_key}` at offset 7, with line and column computed.
    - A handler throwing `std::logic_error` → it propagates as `std::logic_error`, not translated.
    - A handler throwing `json_handler_error{handler_rejected, ...}` from `begin_array` → offset of the `[`.
  - **`json_events_line_column_test`.** `"{
\"a\":1,
\"Ã©\" 2}"`. Line 3 is `"é" 2}`, where `é` takes two bytes, so the missing `:` is reported at the byte `2`: `code == syntax`, `line == 3`, `column == 6`.
- [ ] **Step 2: Confirm RED.** Run a compile-only build. Expected: `parse_json_events` is undeclared.
- [ ] **Step 3: Implement the front end** (`#if GB_YADRO_JSON_PARSER_HAS_AXE`, in `detail::json_front_end<H>`)

  **Parser object state:**
  - `text` (a `std::string_view`), `const char* begin`, `const char* end`, `H& handler`, and `options`;
  - `std::string scratch`, reused across strings;
  - `axe::r_depth_limit_t<structural_rule> limited{ structural_rule{ this }, options.max_depth }`;
  - `structural_rule` is `struct { json_front_end* self; axe::result<const char*> operator()(const char*, const char*) const; }`, forwarding to `self->container(i)`.

  **Token rules.** These are built once per parse, as members constructed in the constructor. The AXE expressions are:
  - `json_hex = r_many(r_hex(), 4)`;
  - `escape = '\\' & (r_any("\"\\/bfnrt") | 'u' & json_hex)`;
  - `plain = r_utf8() - '"' - '\\' - r_any('\x00', '\x1f')`;
  - `chars = *(plain | escape)`;
  - `number = ~r_lit('-') & (r_lit('0') | r_any("123456789") & *_d) & ~(r_lit('.') & +_d) & ~(r_any("eE") & ~r_any("+-") & +_d)`;
  - literals: `"true"_axe`, `"false"_axe`, and `"null"_axe`.

  Whitespace is skipped by a plain loop over the four JSON whitespace bytes, not by an AXE rule, so the hot loop needs no composite.

  **Behaviour:**
  1. **`parse()`:**
     - If `max_input_bytes != 0 && size > max_input_bytes`, throw `input_too_large` at `max_input_bytes`.
     - Skip whitespace.
     - At the end of the input → `unexpected_end`.
     - If `!allow_scalar_root` and the next byte is not `{` or `[` → `syntax`.
     - Call `value(i)`, skip whitespace, and require the end of the input; otherwise `syntax` at that byte.
     - Wrap the parse in `try { ... } catch (const axe::depth_limit_exceeded<const char*>& e) { throw_parse_error(text, e.position() - begin, depth_exceeded, ...); }`.
  2. **`value(i)`** switches on `*i`:
     - `{` or `[` → `limited(i, end)`;
     - `"` → `string(i)` then `emit(i, &H::string_value)`;
     - `-` or a digit → `number(i)`;
     - `t`, `f`, or `n` → `literal(i)`;
     - anything else → `syntax` at `i`.

     It returns the position after the value.
  3. **`container(i)`:**
     - `*i` is `{` or `[`. Emit `begin_*` at offset `i`.
     - For an object, loop:
       - skip whitespace; expect `"` (or `}` when the object is still empty);
       - `key = string(i)`, then `emit(key_offset, &H::key)`;
       - skip whitespace; expect `:`; skip whitespace;
       - `value`;
       - skip whitespace; then `,` continues the loop and `}` ends it.
     - An array loops over elements in the same way.
     - End of input anywhere → `unexpected_end` at `end - begin`. Any other unexpected byte → `syntax` at that byte.
     - Emit `end_*` at the offset of the closing bracket.
     - Return `axe::result<const char*>(true, i_after_close)`.
  4. **`string(i)`:**
     - Run `chars(i + 1, end)` to get the run end `p`.
     - `p == end` → `unexpected_end` at `end - begin`.
     - `*p == '"'` → success: decode `[i + 1, p)`.
     - Otherwise, classify `*p`:
       - below 0x20 → `control_character`;
       - `\` → `invalid_escape`, unless the escape is cut off by the end of input, which gives `unexpected_end` at `end - begin`;
       - 0x80 or above → `invalid_utf8`;
       - anything else would be a grammar bug, so fail an assert (`gbassert`, available through Yadro `util`).
     - **Decoding:** if `memchr(first, '\\', n) == nullptr`, return a view of the input. Otherwise clear `scratch`, copy raw runs, and decode escapes:
       - pair surrogates, throwing `lone_surrogate` at the offset of the offending `\`;
       - encode with `detail::append_utf8`;
       - return a view of `scratch`.
  5. **`number(i)`:**
     - Match `number`. On failure, the error offset is `i + 1` if `*i == '-'`, otherwise `i`. That can only be a digit, which always matches.
     - Classify the token: `.`, `e`, or `E` makes it a double; a leading `-` makes it `from_chars` into int64; otherwise uint64.
     - Integer `result_out_of_range`:
       - `error` → `number_out_of_range` at the token;
       - `to_double` → the double path.
     - Double path: `from_chars(first, last, d, std::chars_format::general)`.
       - `ec == errc{}` → emit `d`.
       - `result_out_of_range` → compute `lead_exponent(token)` with saturating arithmetic, clamped to ±(1 << 30):
         - if the first non-zero significant digit is in the integer part, it is `explicit_exp + (int_digits_after_leading_zeros - 1)`;
         - otherwise it is `explicit_exp - (1 + zeros_after_point_before_first_nonzero)`.

         A negative result → emit `std::copysign(0.0, token_is_negative ? -1.0 : 1.0)`. Otherwise throw `number_out_of_range` at the token.
     - Any other `ec` is a grammar bug; use `gbassert`.
  6. **`literal(i)`:**
     - Try the matching literal rule.
     - On failure, if the remaining input is a proper prefix of the literal → `unexpected_end` at `i`; otherwise `syntax` at `i`.
  7. **`emit(offset, callable)`** calls the handler inside `try`, and catches `json_handler_error` to rethrow `json_parse_error{e.code}` at `offset`, with the handler's message.

  **Final wiring:**
  - `parse_json_events` constructs `json_front_end<H>` on the stack and calls `parse()`.
  - Header comment: the grammar shape, the AXE-defect rule, the depth definition, and the documented stack cost. The measured figures are filled in during Task 9.
- [ ] **Step 4: GREEN** on Debug x64 and Release x64. If a string-error offset in the tests disagrees with the rule above, fix the implementation, not the tests. The spec defines the offsets.
- [ ] **Step 5: Commit** with `git commit -m "feat: add hardened AXE JSON front end with SAX handler interface"`, after adding `container/json_parser.h` and `test/json_test.cpp`.

---

### Task 3: Move json_db onto the front end: caps, persisted options, and writer escaper

**Files:**
- Modify: `container/gbdb_json.h`
- Test: `test/json_test.cpp`

**Interfaces produced:**
- `json_read_options` gains `max_depth = 256`, `max_input_bytes = 0`, and `big_integers = json_big_integer_policy::error`.
- In `detail`: `to_string(json_big_integer_policy)` returns `"error"` or `"to_double"`, and `parse_json_big_integer_policy(std::string_view)` parses it.

- [ ] **Step 1: Write the failing tests** (suite `json`; test names prefixed `gbdb_`)
  - **`gbdb_read_rejects_hardened_inputs_test`.** Each of the following gives `json_parse_error` with the expected code:
    - a raw control character in a value and in a key;
    - each invalid UTF-8 class from Task 2;
    - a lone high and a lone low surrogate;
    - depth 257 (arrays and objects), and depth 4 with `max_depth=3`;
    - text over `max_input_bytes`.

    `std::string(1'000'000, '[')` → `depth_exceeded`, with no crash.
  - **`gbdb_read_surrogate_pair_stored_as_utf8_test`.** `{"s":"\ud83d\ude00"}` stores `F0 9F 98 80`.
  - **`gbdb_read_underflow_and_overflow_test`.** `{"d":1e-400}` stores `+0.0`, and `{"d":-1e-400}` stores `-0.0`, compared by bit pattern. `{"d":1e309}` → `number_out_of_range`.
  - **`gbdb_read_unchanged_contracts_test`.** These keep their current exception types:
    - `"1"` → `json_parse_error{syntax}`;
    - a UTF-8 BOM → `syntax`;
    - `{"a":true,"a":false}` → `std::logic_error` (unchanged);
    - `{"a":[true]}` → `std::logic_error` (the value model, unchanged);
    - `{"a":[1,2]}` → still `uint_array_ref`.
  - **`gbdb_stream_caps_test`.** Using the Task 1 `counting_streambuf`, for:
    - `read_json(std::istream&, json_read_options)`;
    - `read_json(std::istream&, json_db_defaults)`;
    - `read_json_file`, via a temp file under `std::filesystem::temp_directory_path()` with a unique name, removed at the end;
    - `insert_json_file`

    input at the cap is accepted, cap+1 is rejected with `input_too_large`, and the bytes consumed stay within `cap + 1 + 4096`. `read_json_file` with a file larger than the cap throws before reading, and nothing is consumed; verify through a file larger than the cap.
  - **`gbdb_manifest_cannot_loosen_limits_test`.** For every `json_defaults_conflict_policy`:
    - a document with `{"$gbdb_manifest":{"read":{"max_depth":100000,"max_input_bytes":0,"big_integers":"to_double"},"table_format":"columns_data"}, "x": <depth 5 array>, "n": 18446744073709551616}`, read with `defaults.read.max_depth=4`, fails with `depth_exceeded`;
    - with depth 3 but the big integer present → `number_out_of_range`;
    - with `defaults.read.max_input_bytes` smaller than the document → `input_too_large` before the manifest pass;
    - `fail_on_conflict` does not throw a conflict error because of the limit fields.
  - **`gbdb_defaults_persist_new_read_options_test`:**
    - `write_json_defaults` followed by `read_json_defaults` round-trips `max_depth=17`, `max_input_bytes=12345`, and `big_integers=to_double`;
    - a defaults document without these fields reads them as the defaults 256, 0, and `error`.
  - **`gbdb_writer_uses_shared_escaper_test`:**
    - `ascii_only` writes `"é😀"` as `\u00e9\ud83d\ude00`, and the result reads back to the same bytes;
    - a string holding `"\xC0\xAF"` makes `write_json` throw `std::logic_error`, in both `ascii_only` modes;
    - NaN still throws `std::logic_error`.
- [ ] **Step 2: Confirm RED.** Run a compile-only build and the failing assertions. The hardening tests fail on the current reader, and the new option fields are missing.
- [ ] **Step 3: Implement**
  - **`read_json(std::string_view, options)`:**

    ```cpp
    json_parse_options parse_options{ .max_depth = options.max_depth,
        .max_input_bytes = options.max_input_bytes, .big_integers = options.big_integers,
        .allow_scalar_root = false };
    json_db_builder builder{ options };
    parse_json_events(text, builder, parse_options);
    return std::move(builder).finish();
    ```

    Keep the `#else` branch's existing message.
  - **Delete** `detail::axe_json_reader` and `detail::read_stream`.
  - **Stream and file entry points:**
    - `read_json(std::istream&, options)` → `read_json(detail::read_capped(in, options.max_input_bytes), options)`.
    - `read_json(std::istream&, const json_db_defaults&)` → `detail::read_capped(in, defaults.read.max_input_bytes)`. Leave the manifest logic untouched. `extract_manifest_defaults` already starts from the caller's defaults; add a comment that read limits are never taken from a manifest.
    - `read_json_file`:
      - if `options.max_input_bytes != 0`, and `std::filesystem::file_size(file, ec)` succeeds and exceeds the cap → throw `json_parse_error{input_too_large}` at offset cap, before any byte is read. Nothing has been read, so no position can be computed: set `line` and `column` to 0, and document 0 as "not computed". Record this in the handoff as the interpretation of the spec's "line and column of that offset".
      - Then open the file and use `read_capped`.
  - **Persist the new options:**
    - in `write_json_defaults`: `read.max_depth` and `read.max_input_bytes` as `uint64`, and `read.big_integers` as a string;
    - in `apply_defaults_from_db`: `read_uint_option` or `read_enum_option`;
    - check that `read_uint_option` rejects values outside `std::size_t` on Win32.
  - **`json_writer::write_string`:**
    - build into a local `std::string`;
    - `auto r = append_json_string(s, value, _options.ascii_only, json_invalid_utf8::error)`;
    - if `!r.ok`, throw `std::logic_error("JSON writer cannot represent invalid UTF-8 at byte " + std::to_string(r.invalid_offset) + " of a string")`;
    - otherwise `_out << s`.
- [ ] **Step 4: GREEN.** Build Debug and Release x64. Every existing `container` suite test must pass unmodified; list any that fail and stop if the cause is newly rejected input.
- [ ] **Step 5: Commit** with `git commit -m "feat: route json_db reading through the hardened front end with input caps"`, after adding `container/gbdb_json.h` and `test/json_test.cpp`.

---

### Task 4: The value type, `json_value` and `json_object`: construction, accessors, equality

**Files:**
- Modify: `container/json.h`
- Test: `test/json_test.cpp`

**Interfaces produced:**

```cpp
enum class json_kind { null, boolean, int64, uint64, number, string, array, object };
class json_value; using json_array = std::vector<json_value>;
class json_member { public: const std::string& key() const noexcept; json_value value; /* key_ private */ };
class json_object {
public:
    json_object() = default;
    json_object(std::initializer_list<std::pair<std::string, json_value>>); // duplicate -> std::invalid_argument
    std::size_t size() const noexcept; bool empty() const noexcept; void reserve(std::size_t);
    iterator/const_iterator begin()/end();     // over json_member, key read-only
    json_value* find(std::string_view) noexcept; const json_value* find(std::string_view) const noexcept;
    bool contains(std::string_view) const noexcept;
    std::pair<json_value*, bool> insert(std::string key, json_value value);   // no overwrite
    json_value& insert_or_assign(std::string key, json_value value);         // keeps position
    bool erase(std::string_view key);                                        // preserves order
};
struct json_access_error { enum class reason { type_mismatch, out_of_range } code; json_kind actual; };
class json_value {
public:
    json_value() noexcept;                  // null
    json_value(std::nullptr_t) noexcept; json_value(bool) noexcept;
    template<std::integral T> requires (!std::same_as<T, bool> && !std::same_as<T, char>) json_value(T) noexcept; // canonicalized
    json_value(double) noexcept; json_value(float) noexcept;
    json_value(std::string); json_value(std::string_view); json_value(const char*);
    json_value(json_array); json_value(json_object);
    json_kind kind() const noexcept; bool is_null() const noexcept; /* is_bool, is_number, is_integer, is_string, is_array, is_object */
    template<class T> T* get_if() noexcept; template<class T> const T* get_if() const noexcept;
    std::expected<bool, json_access_error> as_bool() const noexcept;
    std::expected<std::int64_t, json_access_error> as_int64() const noexcept;
    std::expected<std::uint64_t, json_access_error> as_uint64() const noexcept;
    std::expected<double, json_access_error> as_double() const noexcept;
    std::expected<std::string_view, json_access_error> as_string() const noexcept;
    std::expected<const json_array*, json_access_error> as_array() const noexcept;
    std::expected<json_array*, json_access_error> as_array() noexcept;
    std::expected<const json_object*, json_access_error> as_object() const noexcept;
    std::expected<json_object*, json_access_error> as_object() noexcept;
    friend bool operator==(const json_value&, const json_value&) noexcept;
};
```

`char` is excluded from the integral constructor so that `json_value('x')` does not silently become a number. `get_if<T>` accepts `bool`, `std::int64_t`, `std::uint64_t`, `double`, `std::string`, `json_array`, and `json_object`; a `static_assert` rejects any other type.

- [ ] **Step 1: Write the failing tests**
  - **`json_value_construction_test`:**
    - `json_value(std::uint64_t{5}).kind() == int64`;
    - `json_value(std::uint64_t{1} << 63).kind() == uint64`;
    - `json_value(-1).kind() == int64`;
    - `json_value(std::uint8_t{7})` → `int64`;
    - `json_value(1.5f)` → `number`;
    - `json_value("x")` → `string`;
    - `json_value(nullptr)` and `json_value()` → `null`.
  - **`json_object_order_and_uniqueness_test`:**
    - insertion order is kept through iteration;
    - `insert` of an existing key returns `false` and leaves the value unchanged;
    - `insert_or_assign` of an existing key keeps its position;
    - `erase` keeps the order of the remaining members;
    - an initializer list with duplicate keys throws `std::invalid_argument`.
  - **`json_value_accessors_test`:**
    - `as_int64` on `uint64` max → `out_of_range`;
    - on `1.0` → `type_mismatch`, with `actual == number`;
    - `as_uint64` on `-1` → `out_of_range`, and on `5` → 5;
    - `as_double` on `int64` 3 → 3.0;
    - `as_string` returns a view into the value;
    - `as_array` and `as_object`, const and mutable, return the right pointer or `type_mismatch`;
    - `get_if<double>` on an int → `nullptr`.
  - **`json_value_equality_test`:**
    - `1 == 1.0`, `-0.0 == 0`, and `json_value(std::uint64_t{1} << 63) == json_value(9223372036854775808.0)`;
    - `9007199254740993` (int) `!= 9007199254740992.0`;
    - an int64 of −1 is not equal to `uint64` max;
    - NaN is not equal to NaN;
    - `"1" != 1`, and `null != false`;
    - an array is order-sensitive;
    - an object is order-insensitive, including for 40-member objects built in reversed order, which exercises the sorted path;
    - objects with different key sets are not equal;
    - nesting works.
- [ ] **Step 2: Confirm RED.** Run a compile-only build.
- [ ] **Step 3: Implement**
  - **Storage.** Use `std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double, std::string, json_array, json_object>`. `json_array` is `std::vector<json_value>`, and `json_object` holds `std::vector<json_member>`.
    - Declare `json_value` first; define `json_member` and `json_object` after it.
    - If MSVC rejects the recursive variant, store `json_array` and `json_object` in the variant as the declared types and move `json_value`'s member functions out of line after `json_object`. Do not switch to heap indirection without recording the deviation.
  - **Numeric equality.** Compare mathematically and exactly:
    - int64 against uint64: a negative value is never equal; otherwise cast and compare.
    - an integer against a double: the double must be finite and integral, with `std::trunc(d) == d`, and within [−2^63, 2^64). Convert the double to the matching integer type and compare exactly. The double 2^63 is compared as a uint64.
    - double against double: `==`, so NaN is never equal and −0 equals 0.
  - **Object equality.** Sizes must match.
    - With up to 16 members, look up each key of the left object in the right one.
    - Otherwise, build two vectors of `const json_member*`, sort both by key, and compare them pairwise.
- [ ] **Step 4: GREEN.** Build Debug and Release x64.
- [ ] **Step 5: Commit** with `git commit -m "feat: add general JSON value type with checked accessors and structural equality"`.

---

### Task 5: JSON pointer (RFC 6901)

**Files:**
- Modify: `container/json.h`
- Test: `test/json_test.cpp`

**Interfaces produced:**

```cpp
struct json_pointer_error { enum class reason { syntax, not_found, not_a_container, invalid_index } code; std::size_t token_index; };
std::expected<const json_value*, json_pointer_error> json_value::at_pointer(std::string_view) const;
std::expected<json_value*, json_pointer_error> json_value::at_pointer(std::string_view);
void append_json_pointer_token(std::string& pointer, std::string_view key);   // "/" + escaped key
void append_json_pointer_index(std::string& pointer, std::size_t index);
```

- [ ] **Step 1: Write the failing tests.** Use an in-house document that exercises every RFC 6901 feature:

  ```json
  {"list":["x","y"],"":1,"s/l":2,"p%c":3,"c^f":4,"b|r":5,"b\\s":6,"q\"t":7," ":8,"t~n":9,"obj":{"a":{"b":10}}}
  ```

  - **Successful lookups:**
    - `""` → the whole document;
    - `"/list"` and `"/list/1"` → `"y"`;
    - `"/"` → 1;
    - `"/s~1l"` → 2;
    - `"/p%c"`, `"/c^f"`, `"/b|r"`, `"/b\\s"`, `"/q\"t"`, and `"/ "` → 3 through 8;
    - `"/t~0n"` → 9;
    - `"/obj/a/b"` → 10.
  - **Errors:**
    - `"list"` (no leading slash) → `syntax`, token 0;
    - `"/t~2n"` → `syntax`, token 0;
    - `"/t~"` → `syntax`;
    - `"/missing"` → `not_found`, token 0;
    - `"/list/2"` → `invalid_index`, token 1;
    - `"/list/01"`, `"/list/-1"`, and `"/list/+1"` → `invalid_index`;
    - `"/list/-"` → `not_found`;
    - `"/list/99999999999999999999999"` → `invalid_index` (overflow);
    - `"/obj/a/b/c"` → `not_a_container`, token 3.
  - **Mutable overload.** Assign through the mutable overload and observe the change.
  - **Builders.** `append_json_pointer_token(p, "a/b~c")` gives `/a~1b~0c`, and a pointer built this way round-trips through `at_pointer`.
- [ ] **Step 2: Confirm RED.**
- [ ] **Step 3: Implement.**
  - Parse the pointer token by token without allocating, except for unescaping into a small reused `std::string` when a token contains `~`.
  - Parse indices with `std::from_chars` into `std::size_t` and reject leading zeros.
- [ ] **Step 4: GREEN.** Build Debug and Release x64.
- [ ] **Step 5: Commit** with `git commit -m "feat: add RFC 6901 JSON pointer lookup to json_value"`.

---

### Task 6: The DOM writer

**Files:**
- Modify: `container/json.h`
- Test: `test/json_test.cpp`

**Interfaces produced:**
- `struct json_format { bool pretty = false; std::uint32_t indent = 2; bool ascii_only = false; json_invalid_utf8 invalid_utf8 = json_invalid_utf8::error; };`
- `struct json_write_error : std::runtime_error { using std::runtime_error::runtime_error; };`
- `std::string format_json(const json_value&, const json_format& = {});`
- `void append_json(std::string&, const json_value&, const json_format& = {});`
- `void format_json(std::ostream&, const json_value&, const json_format& = {});`

The stream form throws `std::runtime_error("Failed to write JSON stream")` on a stream failure, matching `write_stream`.

- [ ] **Step 1: Write the failing tests**
  - **Compact golden output:** `{"a":[1,-2,3.5,"x",true,false,null],"b":{},"c":[],"d":1.0,"e":-0.0,"f":1e+300,"g":18446744073709551615}` from a value built in code. Note `1.0` and `-0.0`.
  - **Pretty golden output** with `indent = 2`:

    ```
    {
      "a": [
        1,
        {}
      ],
      "b": "x"
    }
    ```

    There is no trailing newline, and empty containers are written inline.
  - **`indent = 4`** and **`indent = 0`**. With `pretty` and `indent = 0`, members still go on new lines without indentation. Pin that.
  - **Doubles:** `0.1` → `0.1`, `5e-324` → `5e-324`, `1e16` → `1e+16`, and `123456789012345680.0` → shortest form.
    - For a sample of 100,000 random finite bit patterns (fixed seed), `from_chars(format)` gives back the same bits.
  - **`ascii_only`:** non-BMP characters become a surrogate pair, and the output contains only bytes below 0x80.
  - **Errors:**
    - NaN, +inf, and −inf → `json_write_error`;
    - an invalid-UTF-8 string value, and an invalid-UTF-8 object key → `json_write_error`, whose message contains the JSON pointer (`/k` for a value; for a key, the pointer of the containing object plus " (key)") and the byte offset;
    - with `replace`, output succeeds with U+FFFD.
  - **`append_json`** appends to existing content, and does not clear it.
- [ ] **Step 2: Confirm RED.**
- [ ] **Step 3: Implement.**
  - Recurse with a `std::string& out`.
  - Track the current JSON pointer in a `std::string` only on the error path: rebuild it by passing a small path stack (a vector of keys or indices) down the recursion, and format it only when throwing.
  - Doubles: `std::to_chars(buf, buf + 32, d)`. If the result has none of `.`, `e`, `E`, `n`, or `i`, append `.0`.
  - Integers use `std::to_chars`.
- [ ] **Step 4: GREEN.** Build Debug and Release x64.
- [ ] **Step 5: Commit** with `git commit -m "feat: add compact, pretty and ASCII-only JSON writer for json_value"`.

---

### Task 7: The DOM builder and parse entry points

**Files:**
- Modify: `container/json.h`
- Test: `test/json_test.cpp`

**Interfaces produced:**
- **`class json_value_builder`:**
  - it implements `json_handler`;
  - `explicit json_value_builder(json_duplicate_keys = json_duplicate_keys::reject)`;
  - `json_value finish() &&`.
- **Parse entry points:**
  - `json_value parse_json_value(std::string_view, const json_parse_options& = {})`;
  - `json_value parse_json_value(std::istream&, const json_parse_options& = {})`;
  - `std::expected<json_value, json_parse_error> try_parse_json_value(std::string_view, const json_parse_options& = {})`;
  - `std::expected<json_value, json_parse_error> try_parse_json_value(std::istream&, const json_parse_options& = {})`.

  The `try_` forms catch only `json_parse_error`. Without AXE, all four throw `std::logic_error`.

- [ ] **Step 1: Write the failing tests**
  - **`json_parse_value_basic_test`.** Parse a mixed document and check it with `at_pointer` and the kinds:
    - an unsigned integer ≤ INT64_MAX becomes `int64`;
    - `18446744073709551615` becomes `uint64`;
    - `-0` becomes `int64` 0;
    - `1.0` becomes `number`;
    - a scalar root parses.
  - **`json_parse_value_duplicate_keys_test`:**
    - `reject` → `duplicate_key` at the offset of the second key;
    - `keep_first` and `keep_last` keep the right value at the first key's position;
    - the same key in different objects is fine;
    - a 20-member object with a duplicate as its last key → `reject` still reports it, through the hash path, and `keep_last` updates member 0.
  - **`json_try_parse_value_test`:**
    - on error, an `unexpected` with the code, offset, line, and column;
    - a valid document gives `has_value()`;
    - the rule that `try_` catches only `json_parse_error`, never `std::bad_alloc` or a handler's `std::logic_error`, is verified in review (Task 10, Step 5), because forcing `bad_alloc` in a test is impractical.
  - **`json_parse_value_stream_test`.** The stream overloads honour `max_input_bytes` through `read_capped`, using `counting_streambuf`.
  - **`json_realistic_shapes_test`.** Two hand-written documents:
    - a Claude stream-json `assistant` message, `{"type":"assistant","message":{"id":"msg_1","content":[{"type":"text","text":"hi"},{"type":"tool_use","id":"t1","name":"Bash","input":{"command":"ls","timeout":120000}}],"usage":{"input_tokens":12,"output_tokens":3}},"session_id":"s"}`;
    - a review-findings array, `[{"file":"a.cpp","location":{"line":10,"column":4},"severity":"high","summary":"x"},{"file":"b.h","location":{"line":1,"column":1},"severity":"low","summary":"y"}]`.

    Navigate them with `at_pointer` (`/message/content/1/input/timeout`, `/1/location/line`) and check round-trip equality through `format_json`.
- [ ] **Step 2: Confirm RED.**
- [ ] **Step 3: Implement `json_value_builder`**
  - **Frame stack.** Keep `std::vector<frame>`. A frame holds a `json_value` (array or object), an `std::optional<std::string> pending_key`, and `std::unique_ptr<index_set>`.
    - `index_set` is an `std::unordered_set<std::size_t, key_hash, key_eq>`.
    - Its hasher and comparator read `members[i].key()` through a pointer to the frame's object. It supports heterogeneous lookup by `std::string_view` through `is_transparent`.
    - The set is created when an object reaches 9 members, and it indexes all of them at that point.
  - **Adding a value.** Scalars and completed containers go to the parent frame, or to the root if there is none. For an object parent:
    - `reject`: throw `json_handler_error{duplicate_key, "duplicate JSON object key"}` from `key()`. The front end supplies the key's offset.
    - `keep_first`: remember a "discard next value" flag.
    - `keep_last`: overwrite in place.
  - **Canonicalization.** `uint_value(u)` with `u <= INT64_MAX` stores an int64.
  - **`finish()`** requires an empty stack and a set root.
  - **`parse_json_value`** builds the options, constructs the builder with `options.duplicate_keys`, calls `parse_json_events`, and then `finish()`.
- [ ] **Step 4: GREEN.** Build Debug and Release x64.
- [ ] **Step 5: Commit** with `git commit -m "feat: add json_value builder and parse entry points on the shared front end"`.

---

### Task 8: Conformance corpus and round-trip property tests

**Files:**
- Test: `test/json_test.cpp`. If the file grows past about 2,500 lines, move the corpus to a new `test/json_conformance_test.cpp`, added to both test project files.

- [ ] **Step 1: The conformance table**

  ```cpp
  enum class expect { accept, reject };
  struct conformance_case { std::string_view name; std::string_view input; expect outcome;
                            json_parse_errc code = json_parse_errc::syntax; };
  ```

  - **Encoding and origin.** Inputs are byte-exact literals, using `std::string_view{ "...", n }` where a case contains NUL. The cases are written in-house. A comment states that they follow the `y_`/`n_`/`i_` naming scheme of JSONTestSuite and that no cases were copied.
  - **Minimum mandatory set.** Add further cases until there are at least 60 `y_`, 90 `n_`, and 25 `i_` cases.
    - **`y_` structure:**
      - `[]`, `{}`, and `[[],{}]`;
      - `{"a":{"b":[]}}` and `[1,[2,[3]]]`;
      - whitespace everywhere: `\x20\t\n\r` around every token;
      - an empty key `{"":0}`;
      - the root scalars `1`, `"s"`, `true`, `false`, and `null`;
      - a 256-deep array.
    - **`y_` numbers:**
      - `0`, `-0`, `0.0`, `1e1`, `1E+1`, `1e-1`, `-1.5e-3`, and `123456789`;
      - INT64_MIN and UINT64_MAX;
      - `1e-400` (underflow, accepted).
    - **`y_` strings:**
      - every escape;
      - `\u0000`;
      - `\u001f` escaped;
      - DEL raw;
      - U+FFFF raw and U+FDD0 raw;
      - a valid surrogate pair;
      - 1-, 2-, 3-, and 4-byte raw UTF-8;
      - `\/`;
      - a 1 MiB string.
    - **`n_` structure (`syntax` unless stated):**
      - unclosed `[` and `{` → `unexpected_end`;
      - extra `]`;
      - `[1,]` and `[,1]`;
      - `{"a":1,}` and `{,}`;
      - `{"a"}`, `{"a":}`, and `{:1}`;
      - `{1:1}` and `{a:1}`;
      - `[1 2]`;
      - `[1]]` and `[][]`;
      - a comment;
      - `'single'`;
      - `NaN`, `Infinity`, and `-Infinity`;
      - `True`;
      - `nul` → `unexpected_end`;
      - `nulll`;
      - a BOM;
      - `\v`, `\f`, and NBSP (`C2 A0`) as whitespace;
      - the empty document → `unexpected_end`;
      - a lone `,`;
      - `[1]x`;
      - a NUL byte after the document.
    - **`n_` numbers:**
      - `01`, `-01`, and `00`;
      - `1.` and `.1`;
      - `1e` and `1e+`;
      - `+1` and `--1`;
      - `0x10`;
      - `1_000`;
      - `1.5e`;
      - `Inf`;
      - a full-width digit;
      - `1e309` → `number_out_of_range`;
      - UINT64_MAX+1 and INT64_MIN−1 → `number_out_of_range`.
    - **`n_` strings:**
      - a raw control character at each of 0x00, 0x01, 0x0A, 0x1F → `control_character`;
      - `\x` and `\U0041` → `invalid_escape`;
      - `\u12` followed by `"` → `invalid_escape`;
      - an unterminated string → `unexpected_end`;
      - each invalid UTF-8 class from Task 2 → `invalid_utf8`;
      - each lone-surrogate form → `lone_surrogate`;
      - a string key with a raw tab → `control_character`.
    - **`i_` cases (outcome pinned per the spec):**
      - `[123.456e-789]` → accept, as `+0`;
      - `[1e400]` → reject, `number_out_of_range`;
      - `[100000000000000000000]` → reject by default;
      - `[-123123123123123123123123123123]` → reject by default;
      - `[0.4e` + 400 × `9` + `]` → reject, `number_out_of_range`;
      - `[1e-` + 400 × `9` + `]` → accept, as `+0`;
      - a UTF-8 BOM → reject, `syntax`;
      - a UTF-16LE BOM with `[]` → reject, `syntax`;
      - `["\uDADA"]` and `["\uD888\u1234"]` → reject, `lone_surrogate`;
      - `["\xFF"]` and an overlong `["\xC0\xAF"]` → reject, `invalid_utf8`;
      - U+FFFE raw → accept;
      - 257 nested arrays → reject, `depth_exceeded`;
      - the root scalar `2` → accept (DOM);
      - a 1,000-member object → accept;
      - a document containing `\u0000` in a key → accept.
  - **Runner (`json_conformance_test`):**
    - For each case, parse through `parse_json_value` and through a no-op SAX handler. Both must agree with `outcome`, and for a rejected case, with `code`.
    - For object- or array-rooted cases, also run `read_json` with `table_mode = infer_tables`:
      - an `n_` case must throw `json_parse_error` with the same code;
      - a `y_` case either succeeds or throws `std::logic_error`, since the json_db value model is narrower.
    - On failure, print the case name to `std::cout` before calling `gbassert`.
- [ ] **Step 2: Round-trip property test (`json_round_trip_property_test`)**
  - **Generator.** `std::mt19937_64 rng{ 0x5eed'1234'abcd'0001 }` generates 2,000 values with depth ≤ 6 and width ≤ 6. Kinds are weighted evenly.
  - **Strings.** Random length ≤ 24, drawn from ASCII, controls, `"`, `\`, U+0080, U+07FF, U+0800, U+FFFD, U+FFFF, U+10000, U+10FFFF, and random BMP characters outside the surrogate range. Keys are drawn the same way and deduplicated.
  - **Integers.** Edge values (0, ±1, INT64_MIN, INT64_MAX, 2^53±1, and uint64 values above INT64_MAX) plus random values.
  - **Doubles.** Random finite bit patterns, plus `denorm_min`, DBL_MAX, −0.0, 0.1, and 1e16.
  - **Checks.** For each value and for each of compact, `pretty` with indent 2, and `ascii_only`:
    - `identical(parse_json_value(format_json(v, f)), v)`, where the test-local `identical` requires the same kind at every node, the same member order, and bitwise-equal doubles;
    - `format_json(parse_json_value(s), f) == s`, where `s = format_json(v, f)`.
- [ ] **Step 3: Fixed-point test.** For every accepted `y_` case: `format(parse(format(parse(x)))) == format(parse(x))`.
- [ ] **Step 4: Build.** Run the Debug and Release x64 builds, then fix any front-end or writer defects the corpus exposes. Give each fix its own focused regression test in the relevant earlier test group.
- [ ] **Step 5: Commit** with `git commit -m "test: add in-house JSON conformance corpus and round-trip property tests"`.

---

### Task 9: Stack budget gate and performance smoke test

**Files:**
- Test: `test/json_test.cpp`
- Modify: `container/json_parser.h` (header comment: the measured stack cost)

- [ ] **Step 1: The stack tests** (inside `#if defined(GBWINDOWS)` and `if constexpr (gbdb_json_axe_enabled)`)
  - **Test helper `run_on_thread_with_stack(std::size_t reserve, F f)`:**
    - creates the thread with `CreateThread(nullptr, reserve, thunk, &ctx, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)`;
    - waits with `WaitForSingleObject(INFINITE)` and closes the handle through `unique_win_handle`;
    - rethrows any captured `std::exception_ptr`;
    - fails if the thread's exit code is non-zero.

    A stack overflow on that thread terminates the test process, which the suite reports as a crash. That outcome is the failure signal.
  - **`json_depth_256_fits_1mib_stack_test`.** On a 1 MiB-reservation thread:
    - `parse_json_value` of `arrays(256, "1")`, `objects(256, "true")`, and `alternating(256, "\"s\"")` succeeds;
    - `read_json` of `objects(256, "1")` succeeds;
    - `std::string(1'000'000, '[')` gives `depth_exceeded`.
  - **`json_stack_per_level_measurement_test`:**
    - A handler records `reinterpret_cast<std::uintptr_t>(&local)`, where `local` is a `volatile char` in a `__declspec(noinline)` member called from `begin_array`, for every level of `arrays(200, "1")` and `objects(200, "1")`.
    - `per_level = (first - last) / 199`. Print `json stack: <config> arrays=<n> objects=<n> bytes/level, 256 levels=<KiB> KiB` to `std::cout`.
    - Assert `per_level * 256 <= 640 * 1024` in every configuration.

    The spec's gate concerns x64 Debug, and the other configurations measured lower. If only a non-Debug configuration fails, stop and report; do not loosen the gate.

    **Gate failure.** If the gate fails in x64 Debug, stop and report the numbers to the operator. Do not change `max_depth`'s default, and do not weaken the test.
- [ ] **Step 2: Performance smoke test (`json_performance_smoke_test`)**
  - **Sizes.** Use `#ifdef NDEBUG` to choose the scale: `S = 50 MiB` in Release and `S = 50 MiB / 8` in Debug.
  - **Generators** (deterministic, from a fixed seed):
    - `records(target_bytes)`: `[` then `{"id":<n>,"name":"<16 chars incl. \\n and é>","vals":[1.5,-2,3e10],"ok":true,"nested":{"k":null}}` repeated, then `]`.
    - `long_string(target_bytes)`: one string with an escape every 16 bytes and a 3-byte character every 32 bytes.
    - `wide_object(keys)`: `{"k0":0,...}`.
  - **Timing.** Use `std::chrono::steady_clock`. `t1` is the minimum of two runs at the 1x size, and `t4` is a single run at the 4x size.
  - **Parse sets:**
    - `records` at `S/4` and `S`, parsed by a no-op SAX handler and by `parse_json_value` (the DOM result is destroyed outside the timed region);
    - `long_string` at `S/16` and `S/4`, parsed by SAX;
    - `wide_object` with 125,000 and 500,000 keys (divided by 8 in Debug), parsed by DOM, which exercises the duplicate-detection hash path.
  - **Assertions and output.** For each set, assert `t4 / t1 < 8.0`. Print the MiB/s figures to `std::cout`, and record them in the handoff.
  - **Win32.** If the 50 MiB DOM parse fails with `std::bad_alloc` there, pass `S/2` for Win32 only (`#if !defined(_WIN64)`) and record the deviation. Do not skip the SAX runs.
- [ ] **Step 3: Build and run.** Run the Debug and Release builds on x64 and Win32. Copy the measurement and throughput lines into the handoff notes, and into the `json_parser.h` header comment ("measured bytes per level: ..."), with the date and toolset.
- [ ] **Step 4: Commit** with `git commit -m "test: gate JSON parser stack use and check linear parse time"`.

---

### Task 10: Documentation, warning and configuration checks, final integration, and verification

**Files:**
- Modify: `README.md`, `container/json.h`, `container/json_parser.h`, `container/gbdb_json.h` (header comments)
- Modify: this plan (execution handoff)

- [ ] **Step 1: Header documentation**
  - **`json_parser.h`:**
    - the front-end structure;
    - why the depth wrapper is invoked directly (the AXE `get()` by-value copy);
    - the depth definition;
    - the measured stack cost;
    - the string-view lifetime;
    - the exception policy;
    - the number-conversion contract.
  - **`json.h`:** the invariants (integer canonicalization, unique keys, linear lookup), the equality semantics, the writer rules, and the recursion ownership for values built programmatically.
  - **`gbdb_json.h`:** a short note on `read_json` hardening and the new options.
- [ ] **Step 2: `README.md`**
  - Add `json.h` and `json_parser.h` entries to the container table.
  - Add a gbdb bullet noting that `read_json` shares the hardened front end, with the stricter-input list and the new `json_read_options` fields.
  - Add a short `json_value` example: parse, `at_pointer`, and `format_json`.
- [ ] **Step 3: Check at `/W4` and without AXE** (scratch only, not committed)
  1. Write two scratch translation units (TUs) in the session scratchpad:
     - `w4.cpp` includes `container/json.h` and `container/gbdb_json.h` with `GB_YADRO_ENABLE_AXE_JSON`, and instantiates `parse_json_events` with `json_value_builder` and `json_db_builder`, plus `format_json`.
     - `noaxe.cpp` is the same without the macro. It calls `parse_json_value` and `read_json` and expects `std::logic_error`.
  2. Compile both with `cl /nologo /std:c++latest /EHsc /utf-8 /Zc:__cplusplus /permissive- /W4 /WX /c` through a `vcvars64.bat` wrapper (the pattern the spec's stack probe used). For `w4.cpp`, pass `/external:I C:\Projects\GitHub\axe\include /external:W0`, which is how the orchestrator consumes AXE.
  3. Expected: zero warnings, and both TUs compile. Link and run `noaxe.cpp` to confirm the `logic_error` messages.
- [ ] **Step 4: Final rebase and full verification**

  ```powershell
  git rebase master          # the then-current local master
  git diff --check
  ```

  - Rebuild all four configurations with `/t:Rebuild` and the `AxeIncludeDir` form. Every configuration that was green at the Task 0 baseline must report `failed: 0`. Record `passed`, `failed`, and `disabled` against the baseline.
  - Rerun the Release x64 exe twice more to check that the timing gates are stable.
  - `git status --short --branch` must be clean apart from the commit made next.
- [ ] **Step 5: Independent code review.** Review against every section of the spec. Pay particular attention to:
  - the depth-wrapper placement: no composite, no `r_rule`;
  - the error offsets;
  - the stream-cap byte bound;
  - the manifest limits;
  - the writer exception mapping (json_db → `std::logic_error`);
  - underflow classification;
  - integer canonicalization;
  - object equality on large objects.

  Apply technically valid findings with fresh RED/GREEN cycles.

  **Mutation check.** Temporarily wrap the structural rule inside an AXE composite, for example `limited | r_fail(...)`, and confirm that the depth tests fail on the current AXE `master` if the `get()` fix has not landed. Revert, and record the result.
- [ ] **Step 6: Write the Execution Handoff section** at the end of this plan. It records:
  - the base master and AXE hashes;
  - the baseline and final test counts per configuration;
  - the stack and throughput measurements;
  - every deviation from this plan;
  - the final commit hash;
  - the public API summary;
  - the exact list of newly rejected input, for gbdb_json users.

  Commit with `git commit -m "docs: document JSON value type and record execution handoff"`. Do not push.

---

## Execution Handoff

_To be completed after execution (Task 10, Step 6)._
