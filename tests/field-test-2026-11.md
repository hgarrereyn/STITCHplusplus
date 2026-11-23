# Field test, November 2026: every language, new libraries

How STITCH++ held up when used from scratch on libraries it had never seen, and what was fixed.

## What was run

- **Images**: `stitch setup` for all eight languages from source (no prebuilt images).
- **Demo pipelines**: `tests/languages.py` (build, fuzz, triage, minimize, exec, write/try, a Val
  program, repro, cov) for the seven worker/in-process languages, and `tests/smoke.sh` for C/C++.
- **Regression sweep**: `stitch check` and a 30 s `stitch probe` of all 41 existing example projects.
- **New harnesses**: one agent per language wrote a harness for a new library using only
  `stitch guide`, then fuzzed it (up to three 10-minute campaigns, one worker, a loaded 4-core
  machine), triaged every bucket, and ran minimize, repro, write/try, cov and bisect. Each agent
  kept a log of everything about STITCH++ that was broken or confusing.

| Language | Library | Stubs | Findings (status at the latest release) |
|---|---|---|---|
| C++ | tinyxml2 7.0.0 | 75 | null write in `InsertAfterChild`, float precision, out-of-range ints accepted, ... (11.0.0) |
| Python | sqlparse 0.4.3 | 35 | non-idempotent `format` (0.6.1); `IndexError` in `strip_whitespace` (fixed) |
| JavaScript | marked 4.0.9 | 21 | `Parser.parse` mutates tokens, a `lexer` `TypeError`, superlinear emphasis (fixed) |
| Java | SnakeYAML 1.30 | 54 | unloadable `dump` output, NEL/BOM round trips (2.6); stack overflows (fixed) |
| Go | BurntSushi/toml 1.0.0 | 24 | mixed arrays lose scalars (1.5.0); lexer and encoder panics (fixed) |
| Rust | pulldown-cmark 0.8.0 | 14 | slice panic, inverted offset ranges (fixed) |
| Ruby | kramdown 2.3.0 | 22 | stack overflows, hangs, `NoMethodError`s, `RegexpError` (2.6.1) |
| Swift | Ink 0.5.0 | 14 | trap on unterminated front matter, a hang on `<p` (0.6.0), exponential `[` parsing |

Details are in each project's `notes.md`.

Results: all demo pipelines pass; every existing project builds and probes (one failed `stitch
check` because of a lint bug, below); every agent got from `stitch init` to verified reproducers.

## Broken (fixed)

| Problem | Seen in | Fix |
|---|---|---|
| `stitch try --rev X` said SAME CRASH when the file died loading the library (`ERR_REQUIRE_ESM`), and `write` said SAME CRASH for a reproducer whose stubs were missing | JavaScript | "own check" matching only for plain library code, and never after a load failure |
| `stitch try` said DIFFERENT CRASH for the same Python method (`Class.method` vs `method`) | Python | qualified and plain names of a frame compare equal |
| `--rev`, `bisect` and `cov` build failures printed an empty reason | Rust | the compiler output from `stitch-build`'s JSON is shown |
| ESM-only packages could not be loaded (Node 22.11), `.mjs` harness files could not import `stitch` or the library, and `write` left `.mjs` files out | JavaScript | Node 22.21 (`require(esm)`), a resolve hook for `NODE_PATH`, `.mjs` pasted as CommonJS |
| `minimize` saved a `.min.stp` for a program that does not crash; crash ids then ran it | C++, Swift | it fails instead |
| `minimize` ran the campaign's old harness while `exec` ran the current one | C++, Java | both use the current build |
| triage counted `minimize` outputs as crashes and picked them as examples, then wrote `.min.min.stp` | Rust | `.min.stp` files are not crashes; re-minimizing works in place |
| the fuzzer's `.<name>.stp` metadata files were counted as crash and corpus programs | all | filtered |
| a resumed campaign kept buckets from an older harness | Go, Python, Swift, Ruby | the triage cache is dropped when the harness changed |
| a stack overflow was split into one bucket per place the stack ran out (11 for one bug) | Java | bucketed by kind and stub (engine and CLI) |
| `bisect` counted a revision where the harness fails to load as the crash | JavaScript | such revisions are skipped, with the reason |
| `exec` of a hang ran 60 s with `timeout_ms = 2000`; a 4.8 s run said "no crash" | Swift, JavaScript | kill timeout from `timeout_ms`; slow runs are called out |
| concurrent commands rebuilt `.stitch/build` at the same time ("harness build failed") | C++ | one build per project at a time |
| `--json` output started with a "cloning ..." line | sweep | progress lines go to stderr |
| `stitch check` failed on `ruby/mail`: `#` in a Ruby string was taken for a comment | sweep | string-aware comment stripping; run-time keys skipped |
| `curl \| sh` in the engine and Rust Dockerfiles hid a failed download (`cargo: not found` later) | setup | download, then run |
| piping any command into `head` printed a `BrokenPipeError` traceback | Python, C++, Java, Ruby | handled |

## Confusing (fixed)

- A bare crash id (`c1`) did not say which campaign it came from; now it does. The guide says ids
  are per campaign. `write` and `repro` folders of non-`main` campaigns are named `probe-c3`.
- `stitch fuzz --help` had no descriptions. Now every option has one, and `minimize --secs` has its default.
- No way to wait for a background campaign: `stitch status --wait`.
- `bisect` of a bug fixed later ended in "present in every revision": it now points at `--fixed`.
- The probe report's "never succeeds" gave no reason for stubs that crash on every call.
- `stitch crashes` and `minimize` were silent for minutes: they now say what they run.
- Crash ids disappeared silently after a harness change: `stitch crashes` names them.
- The resume message's corpus count did not match `status`; it now explains why.
- JavaScript status showed `660/65537 edges` (the map size); now `660 edges`.
- Compiler errors were printed twice; Docker's `InvalidDefaultArgInFrom` warning ended every failed build.
- `exec`/`try` of a failed check printed AddressSanitizer's report of the abort.
- `write` warned "AssertionError instead of AssertionError"; it now says where the crashes differ.
- `try` refused files outside the project; it now runs a copy.
- `crash (no sanitizer report)` for Ruby/Python worker deaths now reads `crash (no error report)`.
- Templates: `init --lang go` guesses `module`, Swift guesses the product name with the right
  case, JavaScript's `npm install` survives devDependency peer conflicts, Ruby runs from `/src/lib`
  and works without a gemspec, Rust's comment mentions workspaces and `default-features`.
- Guides: Rust workspaces whose layout changes across `--rev` and types that borrow their input;
  Java on JDK 21 with old poms and the stub return types; JavaScript ES modules; `token()` is often,
  not always, a dictionary token (all languages); stale `stitch guide api` references.
- README: the new projects, the libxml2 stub count, and the 7 projects without notes.

## Second pass (fixed)

| Problem | Fix |
|---|---|
| flaky crashes were not marked, and `minimize` gave up on them | triage reruns each bucket's example (3 runs, timeouts excepted) and `stitch crashes` marks `[flaky: crashed 1 of 3 runs]`; `minimize` retries a flaky program and gives candidates two tries; saved programs that no longer crash are counted |
| a Ruby/Python worker that died without a report left nothing in its bucket, and `exec` showed something else | `crash (exit N, no error report)` from triage, `exec` and reproducers alike (a signal is 128 + N), with the worker's last output; `exec` says when a run differs from its bucket |
| `stitch probe --secs N` took much longer than N; no `--jobs` | it says the wall time includes the build and start-up (measured: 29 s for `--secs 20` on an idle machine; the 3x seen earlier was load); `--jobs` added |
| `minimize` spent its whole budget without saying why it stopped | it says when the time budget stopped it (each candidate is a fresh worker process, about 1 s for Ruby) |
| a Java stub returning `String` failed the build with a stack trace | one line with a hint |
| the "depends on the previous call" lines were hard to act on | they say which stub made or last used the object, and when to act |
| guide gaps: C++ handles that die with their owner; one Java value under two object types | recipes in `stitch guide cpp` and `stitch guide java`, both run: without the C++ recipe the harness finds its own use-after-free within a minute |
| found while testing: `ENV PYTHONPATH=/src` in a project Dockerfile removed the stitch runtime ("No module named stitch") | the runtime is on the venv path through a `.pth` file |
| found while testing: `stitch crashes NAME` for a campaign that does not exist printed a traceback | an error listing the campaigns |

## Still open

- `minimize` starts a fresh worker for every candidate, which is slow for Ruby and the JVM; using
  the fuzzer's persistent worker would make it much faster.
- Prebuilt images published for earlier versions do not match this engine and runtime: the next
  release (`v*` tag) publishes new ones; until then `stitch setup` builds locally.
