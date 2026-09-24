# General JSON Value and Hardened Parser Design

Status: revision 2 approved on 2026-09-24, including decisions 10–12. Revision 2.1 amendments A1 and A2 await operator review. AXE P1 has landed on AXE `master` (`09b0883`, then `4e93d14`). This branch is rebased onto Yadro `master` `d511885`.

Consumer: the AI orchestrator's prerequisite P2 (`AI_orchestrator/docs/superpowers/plans/2026-09-23-increment-1-engine-skeleton.md` §3). The orchestrator uses it for:

- Codex app-server JSON-RPC;
- Claude Code stream-json;
- named-pipe IPC (compact UTF-8 JSON, at most 16 MiB per message);
- journal payloads;
- typed decoders that report a JSON pointer (design §7.6 and §12).

## Revision history

- **Revision 1 (2026-09-23):** initial draft, written while AXE P1 was pending.
- **Revision 2 (2026-09-24):** addresses the operator review.
  - **Depth.** The limit now counts nested containers exactly, and the grammar shape is chosen from stack measurements. See "Stack measurements" and "Depth".
  - **Stream caps.** Every stream and file overload now has a stream-cap contract, including `read_json(std::istream&, const json_db_defaults&)`, together with the rule that governs the manifest pass.
  - **Writer exceptions.** The writer exception contract is made consistent.
  - **Double conversion.** The double conversion contract is specified and checked against MSVC.
  - **AXE defect.** It records an AXE defect that the measurements exposed.
- **Revision 2.1 (2026-09-24):** two amendments raised while revising the implementation plan. Both are for operator review.
  - **A1.** `read_json_file` no longer has a separate `std::filesystem::file_size` pre-check. `read_capped` alone enforces the cap and reports the offset, line, and column promised for `input_too_large`. A pre-check could report those coordinates only by reading the same prefix, and the spec does not allow a coordinate-free error.
  - **A2.** The 640 KiB stack gate applies to the **measured peak stack displacement** of whole parses on a fresh 1 MiB thread. That covers fixed overhead, handler frames, number and string decoding at maximum depth, and exception dispatch on the error paths, instead of a per-level slope multiplied by 256. The slope is still logged, but only for information.

## Scope

In scope:

- A general JSON value type (DOM) with checked access, JSON pointer lookup, structural equality, and a writer.
- A single hardened AXE-based JSON front end that drives a SAX-style handler. The new DOM builder and the existing `json_db_builder` are both handlers.
- Moving `read_json` (json_db) onto the shared front end, which gives json_db the same hardening.
- A shared string escaper for both writers.
- Tests and project-file updates.
- Integration against current Yadro `master`.

Out of scope:

- The json_db value model. It does not change.
- json_db's double formatting.
- Incremental or streaming parsing. The orchestrator frames messages by newline and bounds their size.
- JSON Schema validation.
- Extensions such as comments, trailing commas, NaN, or single quotes.

## Current state (confirmed in the source)

1. **json_db cannot hold general JSON.** `json_db_builder` rejects the following:
   - boolean arrays;
   - nested arrays and objects inside arrays, unless table inference is on;
   - objects inside table rows;
   - mixed arrays.

   json_db remains the right tool for tree and tabular data.
2. **`detail::axe_json_reader` in `container/gbdb_json.h` has input-safety defects:**
   - a. It accepts raw bytes 0x00–0x1F inside strings.
   - b. It encodes each `\u` escape on its own. A surrogate pair therefore becomes CESU-8, and lone surrogates are accepted.
   - c. It does not validate UTF-8 inside strings.
   - d. Nesting depth is unlimited.
3. **The json_db writer has two related defects.** With `ascii_only`, `json_writer::write_string` emits one `\u00XX` per byte, which corrupts all non-ASCII text. Without `ascii_only`, it copies invalid UTF-8 through unchanged.
4. **The root must be an object or an array.** The grammar requires this, although RFC 8259 permits any value at the root. json_db depends on the restriction.
5. **Stream and file reads buffer everything first.**
   - `detail::read_stream`, `read_json_file`, and `read_json(std::istream&, const json_db_defaults&)` each buffer the entire input before parsing it.
   - The defaults overload then parses the buffer twice: once for the manifest and once for the data.
6. **Worktree builds cannot find AXE.** `vs\yadro_test.vcxproj` finds AXE at `$(ProjectDir)..\..\axe\include`. From a `.claude\worktrees\<name>` checkout, that path does not exist.
7. **Existing behaviour on double underflow.** On MSVC, `from_chars` reports `result_out_of_range` for `1e-400`, so today's reader rejects it with "JSON number is outside the double range". This was measured; see "Numbers".

## AXE facilities used, and an AXE defect found

These are used as landed at `4e93d14`:

- **`axe::r_depth_limit(rule, max_depth)`** (`axe_depth.h`). Each invocation of the wrapper counts one level, including an invocation that fails to match. When the limit is exceeded, it throws `axe::depth_limit_exceeded<I>` before invoking the wrapped rule. The exception carries the position and the limit, and the depth is restored on unwind. The depth is keyed on the wrapper object's address.
- **`axe::r_utf8()`** (`axe_utf8.h`) matches one well-formed UTF-8 scalar value, as defined by Unicode Table 3-7.

**AXE defect (not fixed here; reported for an AXE session).** In `axe_composite.h:60`, `r_binary_fn_t::get() const &` is declared as:

```cpp
decltype(auto) get() const & { return rs_; }
```

The member name is not parenthesized, so the function returns `std::tuple<R, Rs...>` **by value**. Every invocation of `&`, `|`, or a similar composite therefore copies its whole sub-rule tree onto the stack. There are two consequences:

- **The depth limit silently stops working.** An `r_depth_limit` nested inside a composite is a fresh copy, with a new address, on every call. Its depth never exceeds 1, so the limit never fires. In a scratch probe, `value = scalar | r_depth_limit(object | array, 3)` accepted 4 levels, and 1,000,000 `[` overflowed even an 8 MiB stack. The AXE JSON sample works only because its wrapper is the outermost rule held by the `r_rule`.
- **It costs stack and time on every rule invocation.**

The one-line fix is `return (rs_);`. With that fix applied to a scratch copy of AXE, the nested limit fired at exactly the right depth.

The Yadro design below does **not** depend on the fix. Its depth wrapper is invoked directly, never through a composite. Its boundary tests would detect a regression.

## Stack measurements (scratch probe, 2026-09-24)

**Method:**

- The probe was compiled with the flags recorded for `yadro_test` Debug: `/ZI /JMC /RTC1 /sdl /GS /Od /MTd`, plus `_ITERATOR_DEBUG_LEVEL=0`.
- For Release, it used `/O2 /GL /MT`.
- The recorded figure is the stack pointer at each container, over 64 nested levels.
- AXE had the `get()` fix applied, which favours shape A. Without the fix, shape A x64 Debug was 20.8–22.4 KB per level.

Bytes per nesting level (KiB needed for 256 levels):

| Grammar shape | x64 Debug | Win32 Debug | x64 Release | Win32 Release |
|---|---:|---:|---:|---:|
| A: all AXE combinators, recursion via `r_rule` + `std::ref` | 19,648–21,104 (4,912–5,276) | 15,940–16,892 (3,985–4,223) | 640–672 (160–168) | 432–464 (108–116) |
| C: AXE token rules, hand-written structural rule | 2,160 (540) | 1,348 (337) | 288 (72) | 224 (56) |

With shape A, 256 levels cannot fit a 1 MiB stack in Debug. With shape C, 256 levels fit in every configuration. The worst case, x64 Debug, uses 53% of a 1 MiB stack.

Both shapes rejected 1,000,000 `[` at offset 256 without a crash. Both accepted exactly N containers at limit N, for arrays, for objects, and with a scalar leaf.

## Architecture

```
container/json_parser.h   json_parse_error (moved here), json_parse_errc, json_parse_options,
                          json_handler concept, json_handler_error, parse_json_events<H>(),
                          detail::read_capped(), UTF-8 helpers, append_json_string() escaper
container/json.h          json_value, json_array, json_object, json_kind, accessors,
                          JSON pointer, operator==, format_json(), json_value_builder,
                          parse_json_value(), try_parse_json_value()
container/gbdb_json.h     includes json_parser.h; read_json drives json_db_builder through
                          parse_json_events(); json_writer::write_string uses append_json_string()
```

All code lives in namespace `gb::yadro::container`, next to the existing `json_parse_error`, `read_json`, and `write_json`.

- `json_parse_error` keeps its name, namespace, and members, so moving it is source-compatible.
- Both new headers are added to `gbcontainer.h`.
- The parse entry points exist only when `GB_YADRO_ENABLE_AXE_JSON` is defined, the same opt-in as today. Without it, they throw `std::logic_error`, as `read_json` does now.
- The value type, JSON pointer, equality, and writer need no AXE.

## Front end (`json_parser.h`)

### Handler interface

This is the interface `json_db_builder` already implements, unchanged:

```cpp
template<class H>
concept json_handler = requires(H h, std::string_view s, bool b, std::int64_t i, std::uint64_t u, double d) {
    h.begin_object(); h.end_object(); h.key(s);
    h.begin_array();  h.end_array();
    h.null_value(); h.bool_value(b); h.int_value(i); h.uint_value(u); h.double_value(d);
    h.string_value(s);
};

template<json_handler H>
void parse_json_events(std::string_view text, H& handler, const json_parse_options& options = {});
```

- A string or key `string_view` is valid only for the duration of the call. When a string contains no escapes, it points into the input. Otherwise it points into a scratch buffer that the front end reuses.
- Integer events keep today's classification:
  - a negative integer calls `int_value`;
  - a non-negative integer calls `uint_value`;
  - a token with `.`, `e`, or `E` calls `double_value`.
- **Exception policy:**
  - A handler may throw `json_handler_error{code, message}`. The front end catches it at the dispatch site and rethrows it as `json_parse_error`, with the offset of the token that triggered the event.
  - Every other exception propagates unchanged, including json_db's existing `std::logic_error` and `std::bad_alloc`.
  - After a throw, the handler's state is unspecified.

### Options

```cpp
enum class json_big_integer_policy { error, to_double };
enum class json_duplicate_keys { reject, keep_first, keep_last };  // interpreted by handlers

struct json_parse_options {
    std::size_t max_depth = 256;        // maximum number of simultaneously open containers
    std::size_t max_input_bytes = 0;    // 0 = unlimited
    json_big_integer_policy big_integers = json_big_integer_policy::error;
    bool allow_scalar_root = true;      // RFC 8259; json_db passes false
    json_duplicate_keys duplicate_keys = json_duplicate_keys::reject; // DOM builder only
};
```

### Grammar shape

The front end uses shape C, because shape A cannot meet the default depth within a 1 MiB Debug stack.

- **Token rules are AXE combinators.** These cover whitespace, `true`, `false`, `null`, the number grammar, and the string grammar.
- **The string grammar:**
  - An unescaped character is `axe::r_utf8() - '"' - '\\' - r_any('\x00', '\x1f')`.
  - An escape is `'\\' & ('"' | '\\' | '/' | 'b' | 'f' | 'n' | 'r' | 't' | 'u' & 4 hex)`.
  - Surrogate pairing is enforced when the string is decoded, where the offset of the `\u` is known.
- **Structure is one hand-written, AXE-compatible rule.** It is a function object with `result<I> operator()(I, I) const`.
  - It consumes the opening bracket and loops over members or elements with explicit whitespace, `,`, and `:` handling.
  - It calls the AXE token rules for keys and scalars.
  - It fires the handler events.
  - Objects and arrays share this one rule.
- **The value dispatcher** is a plain function that looks at the next byte:
  - `{` or `[` invokes the depth-limited structural rule;
  - `"` invokes the string rule;
  - `-` or a digit invokes the number rule;
  - `t`, `f`, or `n` invokes the literal rules;
  - anything else is a `syntax` error, or `unexpected_end` at the end of the input.
- **Depth wrapper.** One `axe::r_depth_limit_t<structural_rule>` is held as a member of the parser object. It is always invoked directly by the dispatcher, never embedded in an AXE composite, so the AXE defect above cannot affect it.

### Depth

- **Definition.** Depth is the number of simultaneously open containers. `1`, `"x"`, and a root scalar have depth 0. `[]`, `{}`, and `[1]` have depth 1. `{"a":[{}]}` has depth 3.
- **Counting.** The dispatcher invokes the depth-limited rule only when the next byte is `{` or `[`. Every invocation therefore opens exactly one container, and a scalar leaf or malformed input never counts. The wrapper's limit is exactly `max_depth`.
- **Limit exceeded.** Opening container number `max_depth + 1` throws `json_parse_error{depth_exceeded}`, translated from `axe::depth_limit_exceeded`. Its offset is that container's opening bracket.
- **`max_depth = 0`** means that only scalar roots are accepted.
- **Stack budget.** The default of 256 is kept only because shape C measured 540 KiB in x64 Debug. The implementation must meet the following acceptance criteria, checked by tests:
  - The real front end, with handler dispatch, parses 256 levels on a thread created with an explicit 1 MiB stack reservation, in all four configurations.
    - The DOM builder parses nested arrays, nested objects, and alternating levels.
    - The json_db builder parses nested objects. json_db's value model rejects nested arrays with `std::logic_error` before the depth limit applies.
  - **Peak gate (amendment A2).** Each workload runs on a fresh thread, and its peak stack displacement is measured from the lowest committed page of that thread's stack. The workloads are the successful 256-level parses, parses with number and string decoding at depth 256, and error paths at the limit. In x64 Debug, the peak must be at most 640 KiB (62.5% of 1 MiB). If the implementation exceeds this, I will stop and report rather than lower the default on my own. The bytes-per-level slope is logged for information only.
- **Documentation.** The header will state the measured per-level cost and that `max_depth` must fit the parsing thread's stack.

### String decoding

- **Surrogates.**
  - A `\uD800`–`\uDBFF` escape must be followed immediately by a `\uDC00`–`\uDFFF` escape. The pair is combined and emitted as one 4-byte UTF-8 sequence.
  - A lone high or low surrogate throws `lone_surrogate` at the offset of its `\u`. There is no option to allow lone surrogates.
- **`\u0000`** yields a NUL byte inside the string.
- **Failure classification.** When the string loop stops before a closing `"`, the byte at that position is classified as one of:
  - `control_character`: 0x00–0x1F;
  - `invalid_escape`: a backslash not followed by a valid escape;
  - `invalid_utf8`: a byte of 0x80 or above that `r_utf8` rejected;
  - `unexpected_end`: end of input.

  The error offset is that byte.

### Numbers

- **Integers.** An integer token that fits int64 (when negative) or uint64 (when non-negative) becomes an integer event. Out-of-range integers follow `big_integers`:
  - `error` throws `number_out_of_range`;
  - `to_double` applies the double conversion below.

  Values never wrap. `-0` is integer 0.
- **Double conversion.** A token with a fraction or an exponent, or an out-of-range integer under `to_double`, is converted with `std::from_chars` (general format). The contract:
  - The result is the IEEE-754 binary64 value nearest to the exact decimal value, with ties to even. Subnormal results are returned as such.
  - If the correctly rounded result is ±∞, the parse throws `number_out_of_range` at the token.
  - If a non-zero token rounds to zero, the result is a zero with the token's sign.
- **Telling underflow from overflow.** `from_chars` reports `result_out_of_range` for both. The standard does not say what it stores in that case, so the front end ignores the stored value and classifies from the token:
  - Let `e` be the decimal exponent of the token's first non-zero significant digit: the explicit exponent, adjusted by that digit's position relative to the decimal point, with saturating arithmetic.
  - Any finite value in [1, DBL_MAX] is representable, so `e < 0` means underflow (result ±0.0) and `e >= 0` means overflow (error).
  - A token whose digits are all zero never reaches this path, because `from_chars` returns `0` or `-0` without error.
- **MSVC behaviour verified on 2026-09-24 (VS 18, `/std:c++latest`):**

  | Input | `from_chars` result |
  |---|---|
  | `4.9406564584124654e-324` (denorm_min) | `ok`, the same value |
  | `2.4703282292062328e-324` (just above half of denorm_min) | `ok`, denorm_min |
  | `2.4703282292062327e-324` (just below half) | `result_out_of_range` |
  | `1e-400`, `123e-10000000`, `1e-99999999999999999999` | `result_out_of_range` |
  | `1e-320` | `ok`, subnormal |
  | `1.7976931348623158e308` | `ok`, DBL_MAX |
  | `1.7976931348623159e308`, `1e309` | `result_out_of_range` |
  | `0e-400`, `0.0000e99999` | `ok`, 0 |

  Every one of these inputs becomes a pinned test, parsed through both the DOM and the SAX path. If a future STL changes its behaviour, the tests fail rather than the behaviour changing silently.

### Input size and streams

- **Buffers.** `max_input_bytes` is checked before any parsing. A `string_view` input longer than the cap throws `json_parse_error{input_too_large}` at offset `max_input_bytes`. Line and column are those of that offset, computed over the capped prefix.
- **Streams.** Every stream and file entry point reads through `detail::read_capped(std::istream&, std::size_t cap)`.
  - It reads in chunks and stops as soon as it holds `cap + 1` bytes, so it never buffers more than `cap + 1` bytes.
  - In that case it throws `input_too_large`.
  - With `cap == 0`, it reads the whole stream, as today.
- **The entry points:**

  | Entry point | Cap applied |
  |---|---|
  | `read_json(std::istream&, const json_read_options&)` | `options.max_input_bytes` |
  | `read_json(std::istream&, const json_db_defaults&)` | `defaults.read.max_input_bytes`, applied while buffering, before the manifest pass |
  | `read_json_file(path, options)`, and `insert_json_file` through it | `options.max_input_bytes`, enforced by `read_capped` (at most cap + 1 bytes read, with coordinates computed over the prefix it holds). There is no separate `file_size` pre-check (amendment A1). |
  | `read_json_defaults(std::istream&)`, `json_db_defaults(std::istream&)` | `json_read_options{}` defaults: unlimited size, depth 256. These read trusted configuration files, as today. |
  | `parse_json_value(std::istream&, options)`, `try_parse_json_value(std::istream&, options)` | `options.max_input_bytes` |

- **Manifest rule for the defaults overload.** The caller's `defaults.read` supplies `max_input_bytes`, `max_depth`, and `big_integers` for the buffering step, the manifest pass, and the data pass.
  - A file manifest can never change these three options, under any `json_defaults_conflict_policy`.
  - This holds today by construction: `extract_manifest_defaults` starts from the caller's defaults and overrides only `write.table_format`, `write.blob_write_mode`, and `schema_version`. The spec makes the rule explicit, and a test pins it.
  - `validate_defaults_conflict` does not compare the limits.

### Errors

```cpp
enum class json_parse_errc { syntax, unexpected_end, control_character, invalid_escape,
    lone_surrogate, invalid_utf8, depth_exceeded, input_too_large, number_out_of_range,
    duplicate_key, handler_rejected };
```

`json_parse_error` gains a `code` member. The new constructor parameter has a default, so existing call sites still compile.

- `offset` is a 0-based byte offset.
- `line` and `column` are 1-based.
- `column` counts bytes, because the offending byte may not be valid UTF-8.
- Only LF advances `line`.
- Line and column are computed only when an error is thrown: one linear scan.

## JSON value (`json.h`)

### Types and invariants

```cpp
enum class json_kind { null, boolean, int64, uint64, number, string, array, object };
using json_array = std::vector<json_value>;
class json_object;   // ordered members { key, value }; keys unique by construction
class json_value;    // variant<nullptr_t, bool, int64_t, uint64_t, double, std::string, json_array, json_object>
```

- **Integer canonicalization.** A `uint64` value always exceeds `INT64_MAX`. Every integer that fits in int64, whether parsed or constructed from any integral type, is stored as `int64`.
- **Construction.** A value can be constructed from `nullptr`, `bool`, any integral type, `float` or `double`, `std::string`, `std::string_view`, `const char*`, `json_array`, or `json_object`. Strings are not validated at construction; the writer validates them.
- **`json_object` keeps member order and enforces unique keys:**
  - `find(key)` returns a pointer, or `nullptr` if the key is absent.
  - `insert(key, value)` fails and returns `false` if the key exists.
  - `insert_or_assign` keeps the member's position.
  - `erase(key)` preserves order.
  - Iteration exposes keys read-only and values mutably.
  - Lookup is linear, and that cost is documented.

  An initializer list containing duplicate keys throws `std::invalid_argument`.

### Checked accessors

- `kind()` and the `is_*()` predicates.
- `get_if<T>()` returns a pointer on an exact kind match.
- The `as_*` accessors return `std::expected<..., json_access_error>`. `json_access_error` holds `{ code: type_mismatch | out_of_range, actual: json_kind }`.
  - `as_bool()`.
  - `as_int64()` accepts only an `int64` kind. A `uint64` kind is `out_of_range`. A double is a `type_mismatch`, even when it is integral.
  - `as_uint64()` accepts `uint64` and non-negative `int64`.
  - `as_double()` accepts any number. Integers convert to the nearest double.
  - `as_string()` returns a `std::string_view`.
  - `as_array()` and `as_object()` return a pointer, with const and mutable overloads.

### JSON pointer (RFC 6901)

- `at_pointer(std::string_view)` returns `std::expected<const json_value*, json_pointer_error>`, with a mutable overload.
- `json_pointer_error` holds a reason (`syntax`, `not_found`, `not_a_container`, `invalid_index`) and the index of the failing reference token.
- `""` means the whole document.
- `~0` and `~1` are unescaped; any other `~x` is a `syntax` error.
- An array index must be `0` or match `[1-9][0-9]*`, and must be within range.
- `-` is `not_found` for lookup.
- Helpers: `append_json_pointer_token(std::string&, std::string_view)` and `append_json_pointer_index(std::string&, std::size_t)`.

### Equality

`operator==` is structural:

- **Arrays** compare element by element, in order.
- **Objects** compare as sets of members: same keys, equal values, with member order ignored. Comparing large objects sorts member pointers, so it does not become quadratic.
- **Numbers** compare by mathematical value, exactly, across `int64`, `uint64`, and `double`:
  - `1 == 1.0` is true;
  - `-0.0 == 0` is true;
  - `9007199254740993` (int) `!= 9007199254740992.0`, because an integer and a double are never compared through a lossy conversion;
  - NaN, which can only be constructed and never parsed, is unequal to everything.
- **Other kinds** compare equal only to the same kind.

Tests check kind preservation separately.

### Writer

```cpp
enum class json_invalid_utf8 { error, replace };  // replace = U+FFFD per maximal subpart
struct json_format { bool pretty = false; std::uint32_t indent = 2; bool ascii_only = false;
                     json_invalid_utf8 invalid_utf8 = json_invalid_utf8::error; };
std::string format_json(const json_value&, const json_format& = {});
void append_json(std::string& out, const json_value&, const json_format& = {});
void format_json(std::ostream&, const json_value&, const json_format& = {});
```

- **Layout.** Compact output contains no whitespace. Pretty output puts one member or element per line, writes `"key": value`, writes empty containers as `[]` and `{}`, and adds no trailing newline.
- **Escapes.** The writer escapes `"`, `\`, `\b`, `\f`, `\n`, `\r`, and `\t`. Other control characters become `\u00xx` with lowercase hex. `/` and DEL are not escaped.
- **`ascii_only`.** Every code point above 0x7F becomes `\uxxxx`. A non-BMP code point becomes a surrogate pair.
- **Doubles.** Doubles use `std::to_chars` shortest round-trip form. If that output has no `.`, `e`, or `E`, the writer appends `.0`.
- **Recursion.** The writer, equality, and the destructor are recursive. Parsed values are bounded by `max_depth`. Callers who build values programmatically own the depth.

### Writer exception contract

- **The shared escaper does not throw on bad input.** It throws only `std::bad_alloc`:

  ```cpp
  struct json_escape_result { bool ok = true; std::size_t invalid_offset = 0; };
  [[nodiscard]] json_escape_result append_json_string(std::string& out, std::string_view s,
                                                      bool ascii_only, json_invalid_utf8 policy);
  ```

  - With `error`, it stops at the first ill-formed UTF-8 sequence and reports that byte's offset within `s`.
  - With `replace`, it writes U+FFFD for each maximal ill-formed subpart and always succeeds.
- **The DOM writer** (`format_json`, `append_json`) throws `json_write_error : std::runtime_error` in two cases:
  - a non-finite double;
  - an escape result that is not `ok`. The message names the string's JSON pointer and the byte offset.
- **The json_db writer** (`write_json` and the other functions that go through `detail::json_writer`) keeps its own exception type:
  - It calls the escaper with `json_invalid_utf8::error`.
  - It translates a failed result into `std::logic_error("JSON writer cannot represent invalid UTF-8 ...")`.
  - This matches its existing `std::logic_error` for NaN and infinity.
  - `json_write_error` never escapes a json_db API, and json_db gets no `invalid_utf8` option.

### Parsing into the DOM

- `parse_json_value(text, options)` throws `json_parse_error`.
- `try_parse_json_value(text, options)` returns `std::expected<json_value, json_parse_error>`. It catches only `json_parse_error`.
- Both have `std::string_view` and `std::istream&` overloads.
- **`json_value_builder`** is public and implements the handler interface:
  - it keeps an explicit frame stack, so the builder itself does not recurse;
  - it canonicalizes integers;
  - it applies `duplicate_keys`, throwing `json_handler_error{duplicate_key}` so that the error reports the key's offset.
- **Duplicate detection** scans linearly while an object has up to 8 members. Above that, it switches to a transient hash set of member indices.

## json_db integration

- **Parser.** `read_json(std::string_view, json_read_options)` now calls `parse_json_events(text, builder, options)`, with `allow_scalar_root = false`. `detail::axe_json_reader` is deleted.
- **New read options.** `json_read_options` gains `max_depth = 256`, `max_input_bytes = 0`, and `big_integers = error`.
  - `write_json_defaults` and `read_json_defaults` persist the new options.
  - A missing field reads as its default, so `schema_version` stays 1.
- **Streams.** `detail::read_stream` is replaced by `detail::read_capped`, following the stream-cap table above.
- **Writer.** `json_writer::write_string` switches to `append_json_string()`, following the writer exception contract above.

## Behaviour changes for existing gbdb_json users

`read_json`, `read_json_file`, and `insert_json_file` now reject input they used to accept:

1. Raw control characters U+0000–U+001F inside strings or keys (`control_character`).
2. Invalid UTF-8 inside strings or keys (`invalid_utf8`):
   - overlong encodings;
   - encoded surrogates (CESU-8, e.g. `ED A0 80`);
   - code points above U+10FFFF;
   - bytes C0, C1, and F5–FF;
   - stray continuation bytes;
   - truncated sequences.
3. Lone surrogate escapes (`lone_surrogate`).
4. More than `max_depth` (default 256) simultaneously open containers (`depth_exceeded`). Previously, very deep input crashed the process.
5. Input larger than `max_input_bytes`, when the caller sets it. The default is unlimited, so nothing changes for existing callers.

Other changes:

- **Stored values.** A surrogate-pair escape is now stored as one 4-byte UTF-8 sequence instead of two 3-byte sequences.
- **Relaxation.** A non-zero double that rounds to zero (for example `1e-400`) now reads as a signed zero. It used to be rejected with "outside the double range" (confirmed on MSVC). Overflow is still rejected.
- **Diagnostics.**
  - Messages are more specific.
  - `json_parse_error::code` is new.
  - Offsets point at the offending byte.
  - `json_db_builder` value-model errors are still `std::logic_error`.
- **Writer.**
  - With `ascii_only`, `write_json` emits correct code-point escapes and surrogate pairs.
  - It throws `std::logic_error` for strings that are not valid UTF-8, in both modes.

These are unchanged:

- the root must be an object or an array;
- a BOM is rejected;
- integer classification is the same;
- duplicate-key and table behaviour are the same;
- a file manifest cannot change read options.

## Tests (`test/json_test.cpp`, suite `json`)

1. **Conformance corpus.** These cases are written in-house in the `y_`/`n_`/`i_` scheme; no JSONTestSuite files or code are copied. They are embedded as byte-exact literals, roughly 60 `y_`, 90 `n_`, and 25 `i_` cases.
   - Each case runs through the DOM and through a no-op SAX handler.
   - `n_` cases assert the expected `json_parse_errc`.
   - Each `i_` case asserts the documented decision.
   - Object- and array-rooted cases also run through `read_json`.
2. **Round-trip property tests.**
   - A fixed-seed `mt19937_64` generates about 2,000 random values.
   - Each value is formatted in compact, pretty, and ascii-only modes.
   - `parse(format(v)) == v`, with kinds identical at every node.
   - `format(parse(format(v))) == format(v)`.
3. **Depth tests:**
   - **Boundary.** With `max_depth` = 1, 2, 3, and 256, exactly N containers are accepted and N+1 are rejected. This holds for:
     - arrays and objects;
     - alternating object and array levels;
     - the innermost container being empty (`[]`, `{}`) or holding a scalar leaf (`1`, `"s"`, `true`, `null`);
     - containers with several sibling members before the deep one.

     The error offset equals the offending bracket.
   - **Scalar leaves don't count.** `[[1]]` passes with `max_depth = 2`, and `max_depth = 0` accepts only a scalar root.
   - **Hostile input.** 1,000,000 `[`, 1,000,000 `{"a":`, and 1,000,000 `[` followed by garbage are rejected without a crash.
   - **Stack budget.** 256 levels parse on a 1 MiB-reservation thread, through the DOM builder (arrays and objects) and the json_db builder (objects). The measured peak stack displacement, including error paths, is checked against 640 KiB in x64 Debug (amendment A2). Bytes per level are logged for information.
4. **Numbers:**
   - every row of the MSVC boundary table;
   - INT64_MIN±1, INT64_MAX±1, UINT64_MAX, and UINT64_MAX+1, under both `big_integers` policies;
   - `-0`, `-0.0`, and a 400-digit integer under `to_double` (overflow error).
5. **Streams and caps:**
   - For `string_view`, `std::istream`, the `json_db_defaults` overload, `read_json_file`, and the DOM stream overloads: input exactly at the cap is accepted, and cap+1 is rejected with `input_too_large` at offset cap.
   - A stream-reading test proves that no more than cap+1 bytes are consumed: a custom `streambuf` counts the bytes it hands out.
   - Under every conflict policy, a file whose manifest carries `read.max_depth`, `read.max_input_bytes`, or `read.big_integers` cannot loosen the caller's limits.
6. **Strings:**
   - **Surrogates:** a valid pair, with upper- and lower-case hex; a lone high surrogate at the end; a high followed by a high; a high followed by `A`; a lone low surrogate; and the ascii writer's output of pairs.
   - **UTF-8:** each invalid class is rejected, and the boundaries U+0080, U+07FF, U+0800, U+FFFF, U+10000, and U+10FFFF are accepted.
   - **Errors:** control characters and escape errors, each with its expected offset.
7. **Other:**
   - duplicate keys: all three policies, and a duplicate beyond the hash threshold;
   - JSON pointer features and each error reason;
   - accessors and equality semantics;
   - golden writer output;
   - **Writer exceptions:** the DOM writer throws `json_write_error`, and json_db throws `std::logic_error`, for both NaN and invalid UTF-8. `replace` mode output is also checked.
   - hand-written Claude stream-json and review-finding shapes, navigated by pointer.
8. **json_db hardening:**
   - behaviour-change items 1–5 are rejected;
   - surrogate pairs are stored as 4-byte UTF-8;
   - the new options are honoured and persisted;
   - the ascii writer is fixed.
9. **Performance smoke test.**
   - **Shapes:** an array of records; one long string with escapes; an object with about 500,000 keys.
   - **Sizes:** Release parses 50 MiB and 12.5 MiB. Debug uses one eighth of those sizes.
   - **Pass criteria:** the DOM and SAX parses complete, and `t(4x) / min-of-2 t(1x) < 8`.
   - Throughput is written to the test log.

**Acceptance:** all `yadro_test` suites pass in x64 Debug and x64 Release, and in Win32 Debug and Release if Win32 builds at baseline. There are no new warnings. The new headers also compile cleanly at `/W4`; this is checked locally and not committed.

## Build, project, and integration

- Add `container\json_parser.h` and `container\json.h` to `vs\yadro.vcxproj` and its `.filters` file.
- Add `test\json_test.cpp` to `vs\yadro_test.vcxproj` and its `.filters` file.
- Replace the four hard-coded `$(ProjectDir)..\..\axe\include` entries with an `AxeIncludeDir` property. It keeps that path as the default and can be overridden with `/p:AxeIncludeDir=...`.
- The implementation plan starts by rebasing on the then-current Yadro `master`, so the branch includes `durable_file`, `sha256`, and the `win_pipe` security changes. Before any JSON change, the plan re-runs the full suite as a baseline on x64 and Win32, Debug and Release. It rebases again, and re-runs the suite, before the final commit.

## Decisions for review

The operator accepted decisions 1–9 in principle, subject to the fixes in revision 2.

1. **Names.** The new headers are `container/json_parser.h` and `container/json.h`, in `gb::yadro::container`, with the type named `json_value`.
2. **Integer canonicalization.** A `uint64` kind holds only values above `INT64_MAX`.
3. **Big integers.** Out-of-range integers are an error by default; `to_double` is opt-in.
4. **Object equality.** Member order is ignored when comparing objects. Arrays remain ordered.
5. **No leniency options.** Lone surrogates and a BOM are always rejected.
6. **Scalar roots.** The DOM accepts a scalar root; json_db still rejects one.
7. **Shared escaper for json_db.** json_db's writer uses the shared escaper, and keeps `std::logic_error` as its exception type.
8. **Column unit.** Error columns count bytes.
9. **AXE path.** Add the `AxeIncludeDir` property.
10. **Double underflow.** Specified and verified above. A non-zero value that rounds to zero is accepted as a signed zero. Subnormal values are correctly rounded, and overflow is an error.
11. **New in revision 2: grammar shape C.** Shape C (AXE token rules plus a hand-written structural rule, wrapped in `r_depth_limit`) replaces the all-combinator grammar. The reason is the measured Debug stack cost.
12. **New in revision 2: the AXE `get()` defect.** It goes back to an AXE session. It is not worked around beyond never nesting `r_depth_limit` inside a composite.

## Follow-ups not in this change

- **AXE `r_binary_fn_t::get()`:** return `(rs_)`. Add an AXE test with `r_depth_limit` nested inside `|` and `&`.
- **json_db integral doubles:** json_db writes an integral double such as `1.0` as `1`, which reads back as `uint64`.
- **json_db error positions:** duplicate-key and value-model errors could carry a parse position if `json_db_builder` threw `json_handler_error`. That would change the exception type.
