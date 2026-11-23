# Example projects

Harnesses for real libraries, grouped by language. Each project with findings has a `notes.md`:
what the harness covers, the findings (checked against the latest release with `stitch exec --rev`)
and the harness fixes made during triage. Verified reproducer folders are in `<lang>/<project>/bugs/`.

To run one: `cd projects/python/tomlkit && stitch build && stitch fuzz`.

## Projects

| Project | Stubs | Findings |
|---|---|---|
| **C/C++** | | |
| [cJSON](cpp/cjson/notes.md) | 65 | 3 bugs in cJSON_Utils, all present at HEAD: `sort_list` breaks the list tail pointer (appends lose members or write to freed memory); `decode_pointer_inplace` mis-decodes `~0`/`~1`; `overwrite_item` frees borrowed (`StringIsConst`) keys |
| [quickjs-ng](cpp/quickjs/notes.md) | 52 | `JS_NewSymbol` aborts on numeric descriptions (HEAD); heap overread in `js_parse_for_in_of` under a memory limit (HEAD); `JS_NewTypedArray` reads past `argv` (fixed upstream in 3f57bfb) |
| [libyaml](cpp/libyaml/notes.md) | 33 | `yaml_parser_load` asserts when called again after an error (questionable) |
| [zlib](cpp/zlib/notes.md) | 45 | none |
| [Lua 5.5-dev](cpp/lua/notes.md) | 38 | none |
| [tinyxml2 7.0.0](cpp/tinyxml2/notes.md) | 75 | `InsertAfterChild` with the next sibling as anchor writes through a null pointer (`bugs/insert-after-last-child/`); floats print with 8 digits and do not read back; `Query*Attribute` accepts out-of-range numbers; `Parse` accepts a lone end tag; `SetText("")` does not round trip (all in 11.0.0); fixed since: pretty-printing mixed content is not idempotent |
| **Python** | | |
| [tomlkit 0.13.2](python/tomlkit/notes.md) | 16 | unescaped control characters in comments; empty super-tables dropped; duplicate table headers after `add` (all in 0.15.2); fixed since: `OverflowError` on huge years and 6 printer bugs |
| [python-dateutil 2.9.0](python/dateutil/notes.md) | 17 | `tzstr("GMT")` `TypeError`; the parser accepts UTC offsets of 24 h or more; `isoparse` rejects `isoformat()` output with second offsets; `rrule` with `INTERVAL=0` divides by zero (latest release) |
| [intervaltree 3.1.0](python/intervaltree/notes.md) | 16 | none (a `slice` check was a harness bug) |
| [sqlparse 0.4.3](python/sqlparse/notes.md) | 35 | `format(reindent=True)` and `format(strip_whitespace=True)` are not idempotent (0.6.1); fixed since: `IndexError` in `format(strip_whitespace=True)` on `"(AS)"` (fixing commit found with `bisect --fixed`) |
| **JavaScript** | | |
| [lodash 4.17.15](javascript/lodash/notes.md) | 13 | `set({}, "toString.a", v)` and similar paths write into the shared `Object.prototype` methods (4.18.0); prototype pollution through `set`, `setWith`, `update`, `zipObjectDeep` (CVE-2020-8203 family, fixed in 4.17.21); `unset(o, "__proto__.constructor")` deletes `Object.prototype.constructor` (fixed in 4.17.23); `cloneDeep` throws on arrays with an own `constructor` |
| [semver 7.5.1](javascript/semver/notes.md) | 11 | ReDoS in `Range` (CVE-2022-25883), found as a timeout (fixed in 7.5.2); `minVersion` returns `null` for a satisfiable range; `simplifyRange` returns `*` when no listed version matches (both in 7.7.3) |
| [yaml 2.6.1](javascript/yaml/notes.md) | 12 | `stringify` writes block scalars that lose a leading space or don't parse (non-default `indent`); anchors with control characters parse but don't print; 2 more print round-trip bugs (all in 2.9.1) |
| [marked 4.0.9](javascript/marked/notes.md) | 21 | fixed since: `Parser.parse` mutates its tokens (parsing them twice duplicates task-list checkboxes), `lexer` `TypeError` on a table with a control character, superlinear emphasis parsing |
| **Java** | | |
| [jsoup 1.15.2](java/jsoup/notes.md) | 11 | XSS in `Cleaner` with `preserveRelativeLinks` (CVE-2022-36033); `select` throws `StringIndexOutOfBoundsException` on a malformed attribute selector, `wrap` throws `IndexOutOfBoundsException`, control-character attribute keys break iteration (all in 1.21.1); fixed since: IOOBE after a parent's `empty()`, `clean()` not idempotent |
| [commons-lang 3.12.0](java/commons-lang/notes.md) | 14 | `createNumber` and `isCreatable` disagree (3.17.0); `createNumber` `StringIndexOutOfBoundsException`, `StrBuilder.contains("")` (fixed) |
| [org.json 20230227](java/org-json/notes.md) | 12 | `JSONML.toJSONArray` throws `ClassCastException` and `NumberFormatException` (20250517); `XML.toString` output that is not XML |
| [SnakeYAML 1.30](java/snakeyaml/notes.md) | 54 | `dump` with a large indent/indicator indent writes YAML that does not load; NEL and BOM strings do not round trip; non-`YAMLException`s from the constructor (all in 2.6); fixed since: stack overflows on deeply nested input (commit found with `bisect --fixed`) |
| **Go** | | |
| [go-yaml v3 (496545a)](go/yaml/notes.md) | 10 | nil dereference in the parser (CVE-2022-28948) and "attempted to parse unknown event" panics (fixed in v3.0.0); `Marshal` writes keys that don't round-trip: a leading tab makes unparseable output, a key of newlines loses one (v3.0.1) |
| [gjson 1.6.4](go/gjson/notes.md) | 7 | `Valid` accepts a lone `-`; `Parse` drops the closing quote of escaped strings; `Get`/`Parse().Get` disagree (all fixed by 1.20.5) |
| [x/net/html 0.12.0](go/xnet-html/notes.md) | 9 | text in svg/math rendered unescaped (CVE-2023-3978) |
| [BurntSushi/toml 1.0.0](go/toml/notes.md) | 24 | `Decode` drops the scalars of an array that mixes scalars and tables (1.5.0); fixed since: a lexer slice panic on invalid UTF-8 after a multiline string (fixing commit found with `bisect --fixed`), an encoder panic on mixed arrays |
| **Rust** | | |
| [smallvec 1.6.0](rust/smallvec/notes.md) | 11 | heap buffer overflow in `insert_many` (RUSTSEC-2021-0003) |
| [url 2.2.0](rust/url/notes.md) | 7 | assertion and slicing panics in `set_host`/`set_username`/`set_password`, and URLs that don't parse back after setters (2.5.9); 11 more panics and round-trip bugs fixed since |
| [toml_edit 0.19.15](rust/toml_edit/notes.md) | 6 | `'''\r\n'''` prints differently from its input (fixed by 0.23) |
| [pulldown-cmark 0.8.0](rust/pulldown-cmark/notes.md) | 14 | fixed since: slice panic in `scan_inline_html` on `"><a\r"`, `into_offset_iter` ranges with start > end in tables |
| **Ruby** | | |
| [rexml 3.2.5](ruby/rexml/notes.md) | 20 | a recursive entity overflows the stack when text is read; out-of-range character references raise `RangeError`; `deep_clone` fails on text added through the API; a malformed DOCTYPE crashes the stream parser; malformed XPath raises internal errors (all in 3.4.4); fixed since: 27 parser crashes, including stack overflows from self-referencing entities (fixed in CVE-2024-41946's release) |
| [addressable 2.7.0](ruby/addressable/notes.md) | 16 | template literal text reaches a `Regexp` unescaped (`RegexpError`); `normalize` raises on invalid UTF-8 escapes in a host and is not idempotent (all in 2.9.2); fixed since: `display_uri` IDNA crashes |
| [rack 2.2.3](ruby/rack/notes.md) | 13 | `MediaType.params(";")`, `best_q_match` and multipart parts with an empty `Content-Type` raise `NoMethodError` (all in 3.2.9); unknown multipart charsets and empty forwarded hosts (fixed in 3.x) |
| [kramdown 2.3.0](ruby/kramdown/notes.md) | 22 | self-referencing IALs and footnotes overflow the stack (or hang `to_kramdown`); `&#9999999;` breaks `to_html`; a NUL in a nested list raises `NoMethodError`; `convert_th` is missing in three converters; small `line_width` raises `RegexpError` (all in 2.6.1); fixed since: `NoMethodError` on `<table markdown>` |
| **Swift** | | |
| [SwiftSoup 2.3.2](swift/swiftsoup/notes.md) | 19 | `wrap` force-unwraps nil; `select` and `cssSelector` range traps; `remove`/`unwrap` index out of range; `parse` hangs (all fixed by 2.14.4; the same classes as jsoup's) |
| [Yams 4.0.6](swift/yams/notes.md) | 10 | `Yams.load("{!!: [int, x]}")` force-unwraps nil; Codable and serialize/compose round trips that change the value (6.2.2) |
| [CryptoSwift 1.6.0](swift/cryptoswift/notes.md) | 10 | AES-GCM and AES-CCM decryption of a ciphertext shorter than the tag traps; invalid KDF and ChaCha20 parameters trap instead of throwing (1.10.0) |
| [Ink 0.5.0](swift/ink/notes.md) | 14 | unterminated front matter traps in `Metadata.read`; `html(from: "<p")` never returns (both in 0.6.0); parse time grows exponentially with nested `[` |

## Large projects

One larger library per language, harnessed from scratch and fuzzed for 30 minutes with 2 workers.
Line coverage is `stitch cov` on the campaign's corpus; findings were rerun at the latest release.

| Project | Stubs | Line coverage | Findings |
|---|---|---|---|
| [libxml2 v2.11.0 (C)](cpp/libxml2/notes.md) | 27 (now 253) | 34% (53% in the 30 files reached; schemas, RelaxNG, catalogs are not targeted), measured with the first 27 stubs; `harness/ext/` (reader, writer, XPath axes, ...) came later | URI save/parse round trips (v2.15.4); fixed since: push parser vs one-shot verdicts, `xmlGetUTF8Char` accepting continuation bytes, regexp compile blowup, serializer output that does not reparse |
| [Jinja2 3.0.0 (Python)](python/jinja2/notes.md) | 9 | 82% | generated Python that does not compile (`break` outside a loop, repeated keyword arguments, duplicate macro parameters), optimizer `TypeError`, sync/async rendering disagreeing (all in 3.1.6); fixed since: a parser `AttributeError`, an idtracking assertion |
| [handlebars 4.7.6 (JavaScript)](javascript/handlebars/notes.md) | 6 | 90% | `compile()` vs `precompile()`+`template()` differ, `TypeError`s instead of Handlebars errors (4.7.10); fixed since: template text breaking out of the generated JavaScript (CVE-2021-23369's root cause) |
| [Commons Compress 1.20 (Java)](java/commons-compress/notes.md) | 24 | 66% | 59 buckets, 46 fixed by 1.28.0 (NPEs, OOMs, infinite loop: the 1.21 CVE batch); still: `BlockLZ4CompressorOutputStream` on empty input, cpio `TRAILER!!!` entries, cpio/Z `IllegalArgumentException`s, an `ArithmeticException` new in 1.28.0 |
| [expr v1.15.0 (Go)](go/expr/notes.md) | 6 | 62% | member access through a nil struct pointer panics in reflect (v1.17.8); fixed since: a compiler nil dereference, checker typing `$env` wrongly, printer bugs |
| [regex 1.5.4 (Rust)](rust/regex/notes.md) | 7 | 81% | all fixed by 1.13.2: compile blowup (CVE-2022-24713), engine disagreements, empty matches splitting UTF-8 characters |
| [mail 2.7.1 (Ruby)](ruby/mail/notes.md) | 9 | 76% | `ArgumentError`/`RangeError` from bad Date headers, `Encoding::CompatibilityError` in `encoded`, subjects with CR/LF decoding to `=0D=0A` (2.9.1); fixed since: a `NameError` in a generated parser |
| [swift-syntax 509.0.0 (Swift)](swift/swift-syntax/notes.md) | 6 | 50% | the parser drops text (a tree that does not print back to its source), a range trap in the parser, a Fix-It precondition (604.0.0); fixed since: a force-unwrap trap, incremental reparse mismatches |

## Harnesses without write-ups

These build and fuzz, but have no campaign notes yet: [gonum](go/gonum),
[guava](java/guava), [mathjs](javascript/mathjs), [networkx](python/networkx),
[activesupport](ruby/activesupport), [chrono](rust/chrono),
[swift-collections](swift/swift-collections).
