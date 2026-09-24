# General JSON Value and Hardened Parser Design

Status: draft for operator review. Implementation is blocked on AXE prerequisite P1. On 2026-09-23, AXE `master` and `claude/exciting-kalam-fb8525` were both at `08ad339` with clean trees, so P1 had not landed.

Consumer: the AI orchestrator's prerequisite P2 (`AI_orchestrator/docs/superpowers/plans/2026-09-23-increment-1-engine-skeleton.md` §3). The orchestrator uses it for Codex app-server JSON-RPC, Claude Code stream-json, named-pipe IPC (compact UTF-8 JSON, at most 16 MiB per message), journal payloads, and typed decoders that report a JSON pointer (design §7.6 and §12).

## Scope

In scope:

- A general JSON value type (DOM) with checked access, JSON pointer lookup, structural equality, and a writer.
- A single hardened AXE-based JSON front end that drives a SAX-style handler. The new DOM builder and the existing `json_db_builder` are both handlers.
- Moving `read_json` (json_db) onto the shared front end, which gives json_db the same hardening.
- A shared string escaper for both writers.
- Tests and project-file updates.

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
2. **`detail::axe_json_reader` (`container/gbdb_json.h:1964`) has input-safety defects:**
   - a. `json_char = _ - '"' - '\\' | ...` accepts raw bytes 0x00–0x1F.
   - b. `decode_string` encodes each `\u` escape on its own (`:2046`). A surrogate pair therefore becomes two 3-byte sequences (CESU-8), and lone surrogates are accepted.
   - c. Raw UTF-8 is not validated inside strings, and nesting depth is unlimited. The recursive `r_rule` overflows the stack on deep input.
3. **The json_db writer has two related defects.** With `ascii_only`, `write_string` (`:1759`) emits one `\u00XX` per byte, which corrupts all non-ASCII text. Without `ascii_only`, it copies invalid UTF-8 through unchanged.
4. **The root must be an object or an array.** The grammar requires this, although RFC 8259 permits any value at the root. json_db depends on the restriction.
5. **Worktree builds cannot find AXE.** `vs\yadro_test.vcxproj` finds AXE at `$(ProjectDir)..\..\axe\include`. From a `.claude\worktrees\<name>` checkout, that path does not exist, so a worktree build hits the `#error` in `gbdb_json.h`.

## Required AXE P1 facilities (assumed contract)

The front end uses AXE's facilities and does not duplicate them. It relies on the following.

- **`r_rule` depth limit:**
  - The limit is set per rule instance at run time.
  - It counts the active nested invocations of that rule.
  - When the limit is exceeded, the parse fails in a way that is distinguishable from an ordinary mismatch, such as a dedicated exception or callback, and the iterator position is available.
  - The counter is restored when an exception unwinds.
- **UTF-8 rule:**
  - It matches exactly one well-formed UTF-8 scalar-value sequence, as defined by Unicode Table 3-7.
  - It rejects overlong forms, encoded surrogates (U+D800–U+DFFF), values above U+10FFFF, 0xC0, 0xC1, bytes 0xF5–0xFF, stray continuation bytes, and truncated sequences.
  - It composes with AXE difference and alternation operators.

If the AXE API that lands differs, the front end will adapt to it. If AXE cannot meet one of these requirements (for example, a depth failure cannot be told apart from a mismatch), I will stop and report it rather than reimplement the facility in Yadro.

## Architecture

```
container/json_parser.h   json_parse_error (moved here), json_parse_errc, json_parse_options,
                          json_handler concept, json_handler_error, parse_json_events<H>(),
                          detail UTF-8 helpers, append_json_string() escaper
container/json.h          json_value, json_array, json_object, json_kind, accessors,
                          JSON pointer, operator==, format_json(), json_value_builder,
                          parse_json_value(), try_parse_json_value()
container/gbdb_json.h     includes json_parser.h; read_json drives json_db_builder through
                          parse_json_events(); json_writer::write_string uses append_json_string()
```

All code lives in namespace `gb::yadro::container`, next to the existing `json_parse_error`, `read_json`, and `write_json`. `json_parse_error` keeps its name, namespace, and members, so moving it is source-compatible. Both new headers are added to `gbcontainer.h`.

The parse entry points exist only when `GB_YADRO_ENABLE_AXE_JSON` is defined, the same opt-in as today. Without it, they throw `std::logic_error`, as `read_json` does now. The value type, JSON pointer, equality, and writer need no AXE.

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

- A string or key `string_view` is valid only for the duration of the call. When a string contains no escapes, it points into the input (no copy). Otherwise it points into a scratch buffer that the front end reuses, so there is no per-string allocation.
- Integer events keep today's classification, which json_db depends on:
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
    std::size_t max_depth = 256;        // nested containers; [] is depth 1, a root scalar is depth 0
    std::size_t max_input_bytes = 0;    // 0 = unlimited; checked before parsing
    json_big_integer_policy big_integers = json_big_integer_policy::error;
    bool allow_scalar_root = true;      // RFC 8259; json_db passes false
    json_duplicate_keys duplicate_keys = json_duplicate_keys::reject; // DOM builder only
};
```

The front end ignores `duplicate_keys`. The option lives in this struct so that callers configure the DOM with one options object. json_db keeps its own `reject_duplicate_keys` option.

### Grammar and decoding changes

- **String characters.** A raw string character is one of:
  - printable ASCII (0x20–0x7F) other than `"` and `\`;
  - one AXE UTF-8 multi-byte sequence;
  - an escape.

  Raw 0x00–0x1F are rejected. DEL (0x7F) and the noncharacters are valid JSON and are accepted.
- **Diagnostics.** Each string is written as `'"' & *json_char & ('"' | r_fail(classify))`. When the loop stops, the failure handler classifies the byte at that point as one of: `control_character`, `invalid_utf8`, `invalid_escape`, or `unexpected_end`. The error offset therefore points at the offending byte, not at the start of the document.
- **Surrogates.**
  - A `\uD800`–`\uDBFF` escape must be followed immediately by a `\uDC00`–`\uDFFF` escape. The pair is combined and emitted as one 4-byte UTF-8 sequence.
  - A lone high or low surrogate is rejected (`lone_surrogate`), with the offset of its `\u`.
  - Surrogates are always rejected; there is no option to allow them.
  - `\u0000` is accepted and yields a NUL byte inside the string.
- **Depth.**
  - The `json_value` `r_rule` gets the AXE depth limit, set to `max_depth + 1` because the root value's own invocation counts once.
  - Exceeding it throws `depth_exceeded` at the opening bracket.
  - Tests pin exactly N (accepted) and N+1 (rejected).
- **Input size.** If `text.size() > max_input_bytes`, the front end throws `input_too_large` at offset 0.
  - The `std::istream` overloads read at most `max_input_bytes + 1` bytes before this check, so an oversized stream is never fully buffered.
- **Numbers.** The grammar is unchanged.
  - Integer tokens outside the int64 or uint64 range are handled by `big_integers`: an error (`number_out_of_range`), or conversion to the correctly rounded double. Values never wrap.
  - `-0` is integer 0.
  - A double that overflows (`1e400`) is `number_out_of_range`.
  - A double that underflows (`1e-400`) yields the correctly rounded subnormal or a signed zero. I will check MSVC's `from_chars` `result_out_of_range` behaviour and handle underflow explicitly.
- **Root and whitespace.**
  - A scalar root is accepted unless `allow_scalar_root` is false.
  - Whitespace is exactly space, tab, LF, and CR.
  - A UTF-8 BOM is rejected as `syntax`; it was already rejected.

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

- **Integer canonicalization.** A `uint64` value always exceeds `INT64_MAX`. Every integer that fits in int64, whether parsed or constructed from any integral type, is stored as `int64`. As a result, round trips preserve the kind exactly.
- **Construction.** A value can be constructed from `nullptr`, `bool`, any integral type, `float` or `double`, `std::string`, `std::string_view`, `const char*`, `json_array`, or `json_object`. Strings are not validated at construction; the writer validates them.
- **`json_object` keeps member order and enforces unique keys:**
  - `find(key)` returns a pointer, or `nullptr` if the key is absent.
  - `insert(key, value)` fails and returns `false` if the key exists.
  - `insert_or_assign` keeps the member's position.
  - `erase(key)` preserves order.
  - Iteration exposes keys read-only and values mutably.
  - Lookup is linear. The orchestrator's objects are small, and the linear cost is documented.

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
- `-` means one past the end of an array, so it is `not_found` for lookup.
- Helpers for building the pointers used in decode errors: `append_json_pointer_token(std::string&, std::string_view)` and `append_json_pointer_index(std::string&, std::size_t)`.

### Equality

`operator==` is structural:

- **Arrays** compare element by element, in order.
- **Objects** compare as sets of members: same keys, equal values, with member order ignored.
  - This is JSON object semantics, and it is what the orchestrator's "re-encodes to an equivalent value" fixture test needs.
  - Comparing large objects sorts member pointers, so it does not become quadratic.
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
- **`ascii_only`.** Every code point above 0x7F becomes `\uxxxx`. A non-BMP code point becomes a surrogate pair (`\ud83d\ude00`). This escaper is `append_json_string()` in `json_parser.h` and is shared with json_db.
- **Doubles.** Doubles use `std::to_chars` shortest round-trip form. If that output has no `.`, `e`, or `E`, the writer appends `.0`, so a double re-reads as a double (`1.0`, `-0.0`).
- **Errors.** Non-finite doubles, and invalid UTF-8 when `invalid_utf8` is `error`, throw `json_write_error : std::runtime_error`.
- **Recursion.** The writer, equality, and the destructor are recursive. Parsed values are bounded by `max_depth`. The header documents that callers who build values programmatically own the depth.

### Parsing into the DOM

- `parse_json_value(text, options)` throws `json_parse_error`.
- `try_parse_json_value(text, options)` returns `std::expected<json_value, json_parse_error>`. It catches only `json_parse_error`.
- **`json_value_builder`** is public, parallel to `json_db_builder`, and implements the handler interface:
  - it keeps an explicit frame stack, so the builder itself does not recurse;
  - it canonicalizes integers;
  - it applies `duplicate_keys`, throwing `json_handler_error{duplicate_key}` so that the error reports the key's offset.
- **Duplicate detection** scans linearly while an object has up to 8 members. Above that, it switches to a transient hash set of member indices, so parsing stays linear for very large objects.

## json_db integration

- **Parser.** `read_json(std::string_view, json_read_options)` now calls `parse_json_events(text, builder, options)`, with `allow_scalar_root = false`. `detail::axe_json_reader` is deleted, together with its private `decode_string`, `parse_hex4`, `append_utf8`, and `store_number`.
- **New read options.** `json_read_options` gains `max_depth = 256`, `max_input_bytes = 0`, and `big_integers = error`.
  - `write_json_defaults` and `read_json_defaults` persist the new options.
  - A missing field reads as its default, so `schema_version` stays 1.
- **Writer.** json_db's `json_writer::write_string` switches to `append_json_string()`. This is decision 7 below.

## Behaviour changes for existing gbdb_json users

`read_json` and `read_json_file` now reject input they used to accept:

1. Raw control characters U+0000–U+001F inside strings or keys (`control_character`).
2. Invalid UTF-8 inside strings or keys (`invalid_utf8`):
   - overlong encodings;
   - encoded surrogates (CESU-8, e.g. `ED A0 80`);
   - code points above U+10FFFF;
   - bytes C0, C1, and F5–FF;
   - stray continuation bytes;
   - truncated sequences.

   Outside strings, such bytes were already syntax errors.
3. Lone surrogate escapes: a `\uD800`–`\uDBFF` escape without a following low surrogate, or a lone `\uDC00`–`\uDFFF` (`lone_surrogate`).
4. Nesting deeper than `max_depth` (default 256) (`depth_exceeded`). Previously, very deep input crashed the process.
5. Input larger than `max_input_bytes`, when the caller sets it. The default is unlimited, so nothing changes for existing callers.

Other changes:

- **Stored values.** A surrogate-pair escape is now stored as one 4-byte UTF-8 sequence instead of two 3-byte sequences.
- **Relaxations.** A double that underflows (for example `1e-400`) now reads as a signed zero or subnormal. It previously failed if MSVC reports it as out of range; I will confirm that during implementation.
- **Diagnostics.**
  - Messages are more specific.
  - `json_parse_error::code` is new.
  - Offsets point at the offending byte.
  - Exceptions that `json_db_builder` throws for value-model limits are still `std::logic_error`.
- **Writer, if decision 7 is accepted.**
  - With `ascii_only`, `write_json` now emits correct code-point escapes and surrogate pairs.
  - It throws `std::logic_error` for strings that are not valid UTF-8, matching how it already treats NaN.

These are unchanged: the root must still be an object or an array, a BOM is still rejected, integer classification is the same, and the duplicate-key and table behaviour are the same.

## Tests (`test/json_test.cpp`, suite `json`)

1. **Conformance corpus.** These cases are written in-house. They follow JSONTestSuite's `y_`/`n_`/`i_` scheme, but no files or code are copied. They are embedded as byte-exact string literals, roughly 60 `y_`, 90 `n_`, and 25 `i_` cases.
   - **Coverage:** structure, whitespace, literals, numbers, strings, escapes, surrogates, UTF-8, BOM, root scalars, and trailing content.
   - **Handlers:** each case runs through the DOM and through a no-op SAX handler.
   - **Error codes:** `n_` cases assert the expected `json_parse_errc`.
   - **Implementation-defined cases:** each `i_` case asserts the documented decision, so the behaviour is pinned.
   - **json_db:** every object- or array-rooted case also runs through `read_json`. `n_` cases must be rejected there too.
2. **Round-trip property tests.** A fixed-seed `mt19937_64` generates about 2,000 random values. The generator bounds depth, and it covers:
   - strings with non-BMP characters, controls, quotes, and backslashes;
   - int64 and uint64 edge values;
   - doubles from random finite bit patterns, plus `denorm_min`, `max`, `-0.0`, and `2^53±1`.

   Each value is formatted in compact, pretty, and ascii-only modes. The tests check that:
   - `parse(format(v)) == v`, with kinds identical at every node;
   - `format(parse(format(v))) == format(v)`.
3. **Targeted tests:**
   - **Depth:**
     - 256 is accepted and 257 is rejected;
     - a custom limit is honoured;
     - 1,000,000 `[` characters are rejected without a crash, in both Debug and Release.
   - **Surrogates:**
     - the pair case, with upper- and lower-case hex;
     - a lone high surrogate at the end, a high followed by a high, a high followed by `\u0041`, and a lone low surrogate;
     - the ascii writer's output of pairs.
   - **UTF-8:**
     - each invalid class is rejected;
     - the boundaries U+0080, U+07FF, U+0800, U+FFFF, U+10000, and U+10FFFF are accepted.
   - **Big integers:** INT64_MIN±1, INT64_MAX±1, UINT64_MAX, and UINT64_MAX+1, under both policies.
   - **Duplicate keys:**
     - all three policies;
     - the same key in different objects;
     - a duplicate beyond the hash threshold.
   - **Other:**
     - `max_input_bytes` at the limit and at limit + 1;
     - line and column on multi-line input;
     - JSON pointer (an in-house document exercising every RFC 6901 feature, and each error reason);
     - accessors, the equality semantics above, golden writer output, and writer errors.
   - **Realistic shapes:** a hand-written Claude stream-json message with a mixed `content` array, and review findings with a nested `location` object. Both are parsed and navigated by pointer.
4. **json_db hardening:**
   - `read_json` rejects items 1–5 of the behaviour-change list;
   - surrogate pairs are stored as 4-byte UTF-8;
   - the new options are honoured and persisted;
   - the ascii writer is fixed.
5. **Performance smoke test.**
   - **Synthetic documents:** deterministic, with three shapes:
     - an array of records;
     - one long string with escapes;
     - an object with about 500,000 keys, which exercises the duplicate-detection hash path.
   - **Sizes:** Release parses 50 MiB and 12.5 MiB. Debug uses one eighth of those sizes, because AXE is slow unoptimized.
   - **Assertions:**
     - both the DOM and the no-op SAX handler complete;
     - `t(4x) / min-of-2 t(1x) < 8` (a linear parser gives about 4, a quadratic one about 16);
     - throughput is written to the test log.

**Acceptance:** all `yadro_test` suites pass in x64 Debug and x64 Release, and in Win32 if Win32 builds at baseline. There are no new warnings.

**Warning checks:** the orchestrator compiles at `/W4`, so the new headers are also compiled once at `/W4` locally. That project-setting change is not committed.

**Stack check:** I will measure stack use per nesting level in Debug, for x64 and Win32. If 256 levels use more than 25% of the default 1 MiB stack, I will report it before choosing a different default.

## Build and project changes

- Add `container\json_parser.h` and `container\json.h` to `vs\yadro.vcxproj` and its `.filters` file (filter `container`).
- Add `test\json_test.cpp` to `vs\yadro_test.vcxproj` and its `.filters` file.
- Replace the four hard-coded `$(ProjectDir)..\..\axe\include` entries with a property, `AxeIncludeDir`. It keeps that path as the default. Worktree builds pass `/p:AxeIncludeDir=C:\Projects\GitHub\axe\include`, which still fits the pre-allowed MSBuild command shape.

## Decisions for review (recommendation given)

1. **Names.** The new headers are `container/json_parser.h` and `container/json.h`, in `gb::yadro::container`, with the type named `json_value`. That is the name the orchestrator plan already uses.
2. **Integer canonicalization.** A `uint64` kind holds only values above `INT64_MAX`.
3. **Big integers.** Out-of-range integers are an error by default; `to_double` is opt-in. This keeps json_db behaviour unchanged.
4. **Object equality.** Member order is ignored when comparing objects. Arrays remain ordered.
5. **No leniency options.** Lone surrogates and a BOM are always rejected. There are no options to allow them.
6. **Scalar roots.** The DOM accepts a scalar root; json_db still rejects one.
7. **Shared escaper for json_db.** json_db's writer uses the shared escaper. This fixes `ascii_only`, and writing invalid UTF-8 now throws. The alternative is to leave the json_db writer untouched.
8. **Column unit.** Error columns count bytes, not code points.
9. **AXE path.** Add the `AxeIncludeDir` property so that worktree builds can find AXE.
10. **Double underflow.** A double that underflows is accepted as a signed zero or subnormal instead of being rejected.

## Follow-ups not in this change

- json_db writes an integral double such as `1.0` as `1`, which reads back as `uint64`. The value is preserved but the type is not. This is left alone because it is outside this change's scope.
- json_db duplicate-key and value-model errors could carry a parse position if `json_db_builder` threw `json_handler_error`. That would change the exception type from `std::logic_error`, so it is deferred.
