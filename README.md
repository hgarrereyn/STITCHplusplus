# STITCH++

API fuzzing for libraries in C/C++, Python, JavaScript, Java, Go, Rust, Ruby and Swift, built for
coding agents. You write small typed stubs that call the library. A LibAFL-based fuzzer chains them into
call sequences, learns which ones work, and finds crashes, exceptions and failed checks. Builds,
fuzzing and reproducers all run in Docker.

## Install

```sh
pip install stitch-fuzz          # or: pip install git+https://github.com/hgarrereyn/STITCHplusplus
stitch setup                     # check Docker, get the C/C++ image
stitch setup --lang python       # other languages: python, javascript, java, go, rust, ruby, swift, or all
stitch skill                     # optional: install the agent skill into ./.claude/skills
```

Needs Docker with buildx, git, and Python 3.9 or newer. `stitch setup` pulls prebuilt images when
they are published for your version, and otherwise builds them once (a few minutes per language).

## Use

```sh
stitch init cjson --git https://github.com/DaveGamble/cJSON --rev v1.7.18
stitch init tomlkit --lang python --git https://github.com/python-poetry/tomlkit --rev 0.13.2
cd tomlkit                       # edit Dockerfile and harness/ (see `stitch guide`)
stitch build
stitch probe --secs 60           # what fails, and why
stitch fuzz --jobs 4
stitch status
stitch cov                       # coverage of the corpus: lcov.info, HTML, never-called functions
stitch crashes                   # unique crashes: c1, c2, ...
stitch exec c1 --trace           # the calls, their arguments, the report
stitch exec c1 --rev HEAD        # does it still happen upstream?
stitch repro c1                  # Docker reproducer folder, verified from scratch
stitch bisect c1                 # commit that introduced the bug
stitch write c1                  # the crash as one source file, to shrink with `stitch try`
```

The CLI is the same for every language. The guide is an agent skill:
[`skill/stitch/SKILL.md`](skill/stitch/SKILL.md) covers the workflow, and
[`skill/stitch/reference/`](skill/stitch/reference/) has one page per language. `stitch guide`
prints the workflow; `stitch guide python` (or `cpp`, `javascript`, `java`, `go`, `rust`, `ruby`,
`swift`) prints a language's page.

A stub has the same shape in every language: typed parameters are fuzzed, objects come from
earlier calls, and results become objects for later calls.

```cpp
New<Json> parse(Str<4096, JsonText> text) {                  // C++
    cJSON* j = cJSON_Parse(text.c_str());
    stitch::require(j, "parse error");
    return j;
}
STITCH_ENDPOINT(parse);
```

```python
@stitch.endpoint(bails_on=(TOMLKitError,))                    # Python
def parse(text: Str[2048, TomlText]) -> New[Doc]:
    return tomlkit.parse(text)
```

```js
stitch.endpoint("parse", { text: Str(2048, YamlText) }, New(Doc), ({ text }) => YAML.parseDocument(text));
```

```java
@Endpoint
public static Document parse(@Str(max = 4096, type = "Html") String html) { return Jsoup.parse(html); }
```

```go
stitch.Endpoint("parse", parse, "text:max=2048,type=YamlText")   // func parse(text string) (*yaml.Node, error)
```

```rust
endpoint! {
    fn parse(text: Str<2048, TomlText>) -> Result<New<Doc>, toml_edit::TomlError> {
        Ok(New::new(text.parse()?))
    }
}
```

```ruby
Stitch.endpoint :parse, { text: Str[2048, XmlText] }, New[Doc], bails_on: [REXML::ParseException] do |text|
  REXML::Document.new(text)
end
```

```swift
let parse = endpoint("parse") { (html: Str<4096, Html>) throws -> New<Doc> in New(try SwiftSoup.parse(html.value)) }
```

## Languages

| Language | Coverage while fuzzing | Comparisons (cmplog) | Bugs |
|---|---|---|---|
| C/C++ | SanitizerCoverage (clang 18) | `trace-cmp`, `memcmp`/`strcmp` hooks | ASan, UBSan, uncaught exceptions, `check` |
| Python 3.12 | `sys.monitoring` lines and branches | `==`, `in`, `<`, ... rewritten on import | undeclared exceptions, `check` |
| JavaScript (Node 22.21) | Babel plugin (CommonJS and ESM) | `===`, `switch`, `String`/`Set`/`Map` hooks | undeclared exceptions, prototype pollution, unhandled rejections, `check` |
| Java 21 | Java agent, counters per basic block | `if_icmp`, `lcmp`, `switch`, `String.equals`, `Map.get` | undeclared `Throwable`s, `check` |
| Go 1.24 | `-d=libfuzzer` (as `go test -fuzz`) | `libfuzzerTraceCmp*`, string compares | panics, fatal errors, races (optional), `check` |
| Rust (nightly) | SanitizerCoverage, in-process | `trace-compares` | panics (with overflow checks), ASan, `check` |
| Ruby 3.4 (YJIT) | Prism source rewriting (byte counters) | rewritten `==`, `<`, `start_with?`, `case`/`when`, ... | undeclared exceptions, `check` |
| Swift 6.2 | SanitizerCoverage, in-process (on the C/C++ runtime) | `trace-cmp` | traps (bounds, overflow, nil unwrap), ASan, `check` |

Every language supports every command: probe, fuzz, status, crashes (including timeouts),
minimize, exec (`--rev`), bisect, repro folders, `write`/`try` standalone files, and `cov` (lcov
and HTML, from llvm-cov, coverage.py, c8, JaCoCo, `go tool cover` or Ruby's `Coverage`).

## Example projects

`projects/<language>/` has harnesses for 49 libraries. Those listed below have `notes.md`: what the harness covers,
the findings (checked against the latest release with `stitch exec --rev`) and the harness fixes
made during triage. Verified reproducer folders are in `projects/*/*/bugs/`.

| Project | Stubs | Findings |
|---|---|---|
| **C/C++** | | |
| cJSON | 65 | 3 bugs in cJSON_Utils, all present at HEAD: `sort_list` breaks the list tail pointer (appends lose members or write to freed memory); `decode_pointer_inplace` mis-decodes `~0`/`~1`; `overwrite_item` frees borrowed (`StringIsConst`) keys |
| quickjs-ng | 52 | `JS_NewSymbol` aborts on numeric descriptions (HEAD); heap overread in `js_parse_for_in_of` under a memory limit (HEAD); `JS_NewTypedArray` reads past `argv` (fixed upstream in 3f57bfb) |
| libyaml | 33 | `yaml_parser_load` asserts when called again after an error (questionable) |
| zlib | 45 | none |
| Lua 5.5-dev | 38 | none |
| tinyxml2 7.0.0 | 75 | `InsertAfterChild` with the next sibling as anchor writes through a null pointer (`bugs/insert-after-last-child/`); floats print with 8 digits and do not read back; `Query*Attribute` accepts out-of-range numbers; `Parse` accepts a lone end tag; `SetText("")` does not round trip (all in 11.0.0); fixed since: pretty-printing mixed content is not idempotent |
| **Python** | | |
| tomlkit 0.13.2 | 16 | unescaped control characters in comments; empty super-tables dropped; duplicate table headers after `add` (all in 0.15.2); fixed since: `OverflowError` on huge years and 6 printer bugs |
| python-dateutil 2.9.0 | 17 | `tzstr("GMT")` `TypeError`; the parser accepts UTC offsets of 24 h or more; `isoparse` rejects `isoformat()` output with second offsets; `rrule` with `INTERVAL=0` divides by zero (latest release) |
| intervaltree 3.1.0 | 16 | none (a `slice` check was a harness bug) |
| sqlparse 0.4.3 | 35 | `format(reindent=True)` and `format(strip_whitespace=True)` are not idempotent (0.6.1); fixed since: `IndexError` in `format(strip_whitespace=True)` on `"(AS)"` (fixing commit found with `bisect --fixed`) |
| **JavaScript** | | |
| lodash 4.17.15 | 13 | `set({}, "toString.a", v)` and similar paths write into the shared `Object.prototype` methods (4.18.0); prototype pollution through `set`, `setWith`, `update`, `zipObjectDeep` (CVE-2020-8203 family, fixed in 4.17.21); `unset(o, "__proto__.constructor")` deletes `Object.prototype.constructor` (fixed in 4.17.23); `cloneDeep` throws on arrays with an own `constructor` |
| semver 7.5.1 | 11 | ReDoS in `Range` (CVE-2022-25883), found as a timeout (fixed in 7.5.2); `minVersion` returns `null` for a satisfiable range; `simplifyRange` returns `*` when no listed version matches (both in 7.7.3) |
| yaml 2.6.1 | 12 | `stringify` writes block scalars that lose a leading space or don't parse (non-default `indent`); anchors with control characters parse but don't print; 2 more print round-trip bugs (all in 2.9.1) |
| marked 4.0.9 | 21 | fixed since: `Parser.parse` mutates its tokens (parsing them twice duplicates task-list checkboxes), `lexer` `TypeError` on a table with a control character, superlinear emphasis parsing |
| **Java** | | |
| jsoup 1.15.2 | 11 | XSS in `Cleaner` with `preserveRelativeLinks` (CVE-2022-36033); `select` throws `StringIndexOutOfBoundsException` on a malformed attribute selector, `wrap` throws `IndexOutOfBoundsException`, control-character attribute keys break iteration (all in 1.21.1); fixed since: IOOBE after a parent's `empty()`, `clean()` not idempotent |
| commons-lang 3.12.0 | 14 | `createNumber` and `isCreatable` disagree (3.17.0); `createNumber` `StringIndexOutOfBoundsException`, `StrBuilder.contains("")` (fixed) |
| org.json 20230227 | 12 | `JSONML.toJSONArray` throws `ClassCastException` and `NumberFormatException` (20250517); `XML.toString` output that is not XML |
| SnakeYAML 1.30 | 54 | `dump` with a large indent/indicator indent writes YAML that does not load; NEL and BOM strings do not round trip; non-`YAMLException`s from the constructor (all in 2.6); fixed since: stack overflows on deeply nested input (commit found with `bisect --fixed`) |
| **Go** | | |
| go-yaml v3 (496545a) | 10 | nil dereference in the parser (CVE-2022-28948) and "attempted to parse unknown event" panics (fixed in v3.0.0); `Marshal` writes keys that don't round-trip: a leading tab makes unparseable output, a key of newlines loses one (v3.0.1) |
| gjson 1.6.4 | 7 | `Valid` accepts a lone `-`; `Parse` drops the closing quote of escaped strings; `Get`/`Parse().Get` disagree (all fixed by 1.20.5) |
| x/net/html 0.12.0 | 9 | text in svg/math rendered unescaped (CVE-2023-3978) |
| BurntSushi/toml 1.0.0 | 24 | `Decode` drops the scalars of an array that mixes scalars and tables (1.5.0); fixed since: a lexer slice panic on invalid UTF-8 after a multiline string (fixing commit found with `bisect --fixed`), an encoder panic on mixed arrays |
| **Rust** | | |
| smallvec 1.6.0 | 11 | heap buffer overflow in `insert_many` (RUSTSEC-2021-0003) |
| url 2.2.0 | 7 | assertion and slicing panics in `set_host`/`set_username`/`set_password`, and URLs that don't parse back after setters (2.5.9); 11 more panics and round-trip bugs fixed since |
| toml_edit 0.19.15 | 6 | `'''\r\n'''` prints differently from its input (fixed by 0.23) |
| pulldown-cmark 0.8.0 | 14 | fixed since: slice panic in `scan_inline_html` on `"><a\r"`, `into_offset_iter` ranges with start > end in tables |
| **Ruby** | | |
| rexml 3.2.5 | 20 | a recursive entity overflows the stack when text is read; out-of-range character references raise `RangeError`; `deep_clone` fails on text added through the API; a malformed DOCTYPE crashes the stream parser; malformed XPath raises internal errors (all in 3.4.4); fixed since: 27 parser crashes, including stack overflows from self-referencing entities (fixed in CVE-2024-41946's release) |
| addressable 2.7.0 | 16 | template literal text reaches a `Regexp` unescaped (`RegexpError`); `normalize` raises on invalid UTF-8 escapes in a host and is not idempotent (all in 2.9.2); fixed since: `display_uri` IDNA crashes |
| rack 2.2.3 | 13 | `MediaType.params(";")`, `best_q_match` and multipart parts with an empty `Content-Type` raise `NoMethodError` (all in 3.2.9); unknown multipart charsets and empty forwarded hosts (fixed in 3.x) |
| kramdown 2.3.0 | 22 | self-referencing IALs and footnotes overflow the stack (or hang `to_kramdown`); `&#9999999;` breaks `to_html`; a NUL in a nested list raises `NoMethodError`; `convert_th` is missing in three converters; small `line_width` raises `RegexpError` (all in 2.6.1); fixed since: `NoMethodError` on `<table markdown>` |
| **Swift** | | |
| SwiftSoup 2.3.2 | 19 | `wrap` force-unwraps nil; `select` and `cssSelector` range traps; `remove`/`unwrap` index out of range; `parse` hangs (all fixed by 2.14.4; the same classes as jsoup's) |
| Yams 4.0.6 | 10 | `Yams.load("{!!: [int, x]}")` force-unwraps nil; Codable and serialize/compose round trips that change the value (6.2.2) |
| CryptoSwift 1.6.0 | 10 | AES-GCM and AES-CCM decryption of a ciphertext shorter than the tag traps; invalid KDF and ChaCha20 parameters trap instead of throwing (1.10.0) |
| Ink 0.5.0 | 14 | unterminated front matter traps in `Metadata.read`; `html(from: "<p")` never returns (both in 0.6.0); parse time grows exponentially with nested `[` |

### Large projects

One larger library per language, harnessed from scratch and fuzzed for 30 minutes with 2 workers.
Line coverage is `stitch cov` on the campaign's corpus; findings were rerun at the latest release.

| Project | Stubs | Line coverage | Findings |
|---|---|---|---|
| libxml2 v2.11.0 (C) | 27 (now 253) | 34% (53% in the 30 files reached; schemas, RelaxNG, catalogs are not targeted), measured with the first 27 stubs; `harness/ext/` (reader, writer, XPath axes, ...) came later | URI save/parse round trips (v2.15.4); fixed since: push parser vs one-shot verdicts, `xmlGetUTF8Char` accepting continuation bytes, regexp compile blowup, serializer output that does not reparse |
| Jinja2 3.0.0 (Python) | 9 | 82% | generated Python that does not compile (`break` outside a loop, repeated keyword arguments, duplicate macro parameters), optimizer `TypeError`, sync/async rendering disagreeing (all in 3.1.6); fixed since: a parser `AttributeError`, an idtracking assertion |
| handlebars 4.7.6 (JavaScript) | 6 | 90% | `compile()` vs `precompile()`+`template()` differ, `TypeError`s instead of Handlebars errors (4.7.10); fixed since: template text breaking out of the generated JavaScript (CVE-2021-23369's root cause) |
| Commons Compress 1.20 (Java) | 24 | 66% | 59 buckets, 46 fixed by 1.28.0 (NPEs, OOMs, infinite loop: the 1.21 CVE batch); still: `BlockLZ4CompressorOutputStream` on empty input, cpio `TRAILER!!!` entries, cpio/Z `IllegalArgumentException`s, an `ArithmeticException` new in 1.28.0 |
| expr v1.15.0 (Go) | 6 | 62% | member access through a nil struct pointer panics in reflect (v1.17.8); fixed since: a compiler nil dereference, checker typing `$env` wrongly, printer bugs |
| regex 1.5.4 (Rust) | 7 | 81% | all fixed by 1.13.2: compile blowup (CVE-2022-24713), engine disagreements, empty matches splitting UTF-8 characters |
| mail 2.7.1 (Ruby) | 9 | 76% | `ArgumentError`/`RangeError` from bad Date headers, `Encoding::CompatibilityError` in `encoded`, subjects with CR/LF decoding to `=0D=0A` (2.9.1); fixed since: a `NameError` in a generated parser |
| swift-syntax 509.0.0 (Swift) | 6 | 50% | the parser drops text (a tree that does not print back to its source), a range trap in the parser, a Fix-It precondition (604.0.0); fixed since: a force-unwrap trap, incremental reparse mismatches |

## How it works

- **Stubs.** The function signature is the spec: object inputs (`Borrow`/`Take`), object outputs
  (`New`), and typed fuzz parameters. Each language turns signatures into the same manifest.
- **Programs.** A program is a list of calls. Object inputs refer to outputs of earlier calls.
  Programs store stub names, so corpora survive harness changes. The format is the same for every
  language.
- **Execution.** C/C++, Rust and Swift stubs run in the fuzzer's process: a persistent fork
  server runs many programs in a forked child, with results and coverage in shared memory.
  Python, JavaScript, Java, Go and Ruby run a worker process in their own runtime that talks to
  the engine over a pipe ([protocol](languages/README.md#worker-protocol)) and reports each call's
  result, coverage and comparisons. A worker survives exceptions and is restarted after a timeout
  or a fatal error.
- **Learning.** Stubs that never succeed get picked less. For each input, the fuzzer tracks which
  producers lead to success and prefers them.
- **Mutations.** Program edits (insert, delete, rewire, splice calls) and byte mutations of single
  parameters: LibAFL havoc, dictionary tokens, and cmplog input-to-state replacement. Decoders turn
  fuzzer choices into structured values (JSON, URLs, source code), and the byte mutators still apply.
- **Triage.** Crashes are re-run and bucketed by kind and location. Programs that were saved as
  timeouts and still run past the campaign timeout become `timeout` buckets, named by the call that
  was running.
- **Docker.** Each language image has its toolchain, the runtime and the fuzzer. A project's
  Dockerfile builds the library on top. The library source comes in as a build context: a git
  revision or a local directory. One image per revision, so `--rev` and bisection reuse the cache.
- **Coverage.** `stitch cov` replays the corpus with the language's own coverage tool and exports
  lcov and HTML.
- **Standalone reproducers.** `stitch write` replays a crash once while recording every call's
  arguments, then writes a program that makes the same calls with the arguments filled in. `stitch
  try` runs an edited copy and checks that it still fails the same way.

## Repository

| Path | Contents |
|---|---|
| `cli/stitch_fuzz/` | the `stitch` CLI (Python, no dependencies); `writers/` has the standalone writers per language |
| `engine/` | the fuzzer, in Rust on LibAFL 0.16: in-process for C/C++, `stitch-engine` + worker protocol for the others |
| `languages/<lang>/` | per language: `Dockerfile`, `runtime/` (the harness API), `bin/stitch-build`, `standalone/` |
| `skill/stitch/` | agent skill: `SKILL.md` (workflow) and `reference/` (one page per language, reproducers) |
| `projects/<lang>/` | example harnesses, notes and bug reports |
| `examples/` | toy libraries with planted bugs: `demolib` (C, history in `examples/demolib-src.bundle`) and `<lang>-demo` |
| `tests/` | `smoke.sh` (whole C/C++ pipeline on demolib), `languages.py` (build, fuzz, triage, write/try, repro, cov per language), `standalone_fidelity.py` |

Development: `pip install -e .` or run `bin/stitch` from the checkout. Releases: push a `vX.Y.Z` tag;
`.github/workflows/release.yml` publishes the images to GHCR and the package to PyPI.
