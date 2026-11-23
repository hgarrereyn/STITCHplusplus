<div align="center">

# STITCH++

**API fuzzing for libraries in eight languages, built for coding agents.**

<img src="docs/assets/icons/cplusplus.svg" height="36" title="C/C++" alt="C/C++">&nbsp;&nbsp;
<img src="docs/assets/icons/python.svg" height="36" title="Python" alt="Python">&nbsp;&nbsp;
<img src="docs/assets/icons/javascript.svg" height="36" title="JavaScript" alt="JavaScript">&nbsp;&nbsp;
<img src="docs/assets/icons/java.svg" height="36" title="Java" alt="Java">&nbsp;&nbsp;
<img src="docs/assets/icons/go.svg" height="36" title="Go" alt="Go">&nbsp;&nbsp;
<img src="docs/assets/icons/rust.svg" height="36" title="Rust" alt="Rust">&nbsp;&nbsp;
<img src="docs/assets/icons/ruby.svg" height="36" title="Ruby" alt="Ruby">&nbsp;&nbsp;
<img src="docs/assets/icons/swift.svg" height="36" title="Swift" alt="Swift">

<br><br>

<img src="docs/assets/stitching.gif" width="760" alt="Stubs are stitched into a program, fuzzed, re-stitched, and a crash is found">

</div>

> [!IMPORTANT]
> **Agent guide.** If you are an agent, read [`skill/stitch/SKILL.md`](skill/stitch/SKILL.md)
> first (or run `stitch guide`), then your language's page in
> [`skill/stitch/reference/`](skill/stitch/reference/) (`stitch guide <lang>`).
>
> - Install with `pip install stitch-fuzz` and `stitch setup --lang <lang>`. Everything else runs in Docker.
> - Iterate with `stitch probe` until the health report is clean, then start `stitch fuzz`.
> - Decide for every crash whether the harness or the library is at fault. Check
>   `stitch exec <id> --rev HEAD` before you report a library bug.
> - Every command takes `--json`. All commands: [`docs/cli.md`](docs/cli.md).

## How it works

You write **stubs**: small typed functions that each make one API call. STITCH++ **stitches** them
into programs, passing objects from one call to the next, and fuzzes the arguments. It learns which
call sequences work, keeps the ones that reach new code, and reports crashes, uncaught exceptions and
failed checks.

```python
@stitch.endpoint(bails_on=(TOMLKitError,))
def parse(text: Str[2048, TomlText]) -> New[Doc]:
    doc = tomlkit.parse(text)
    stitch.check(doc.as_string() == text, "parse/print preserves the text")
    return doc
```

Stubs have the same shape in every language. The library, harness and fuzzer run in Docker, so you
only need Docker and the `stitch` CLI.

## Features

- **Powered by [LibAFL](https://github.com/AFLplusplus/LibAFL).** Coverage-guided scheduling, havoc
  mutations and multi-core campaigns.
- **Program mutations.** Insert, delete, rewire and splice calls, alongside byte-level mutation of
  each argument.
- **Comparison feedback (cmplog).** Operands of comparisons in the library flow back into inputs,
  in every language.
- **Structured inputs.** Typed decoders build JSON, URLs or source code from fuzzer bytes, with
  dictionaries and seeds.
- **Bug detectors.** ASan, UBSan, panics, traps, undeclared exceptions, prototype pollution,
  timeouts, and your own `check`s.
- **Triage.** Crash bucketing, minimization, replay at any revision, and bisection.
- **Reproducers.** Self-contained Docker folders, or one standalone source file per crash.
- **Coverage reports.** lcov and HTML from each language's own coverage tool.

## Languages

| | Language | Coverage | Comparisons (cmplog) | Detects |
|---|---|---|---|---|
| <img src="docs/assets/icons/cplusplus.svg" height="20"> | C/C++ | SanitizerCoverage | `trace-cmp`, `memcmp`/`strcmp` hooks | ASan, UBSan, uncaught exceptions |
| <img src="docs/assets/icons/python.svg" height="20"> | Python | `sys.monitoring` | rewritten `==`, `in`, `<` | undeclared exceptions |
| <img src="docs/assets/icons/javascript.svg" height="20"> | JavaScript | Babel plugin | `===`, `switch`, `String`/`Map` hooks | exceptions, prototype pollution |
| <img src="docs/assets/icons/java.svg" height="20"> | Java | Java agent | `if_icmp`, `switch`, `String.equals` | undeclared `Throwable`s |
| <img src="docs/assets/icons/go.svg" height="20"> | Go | `-d=libfuzzer` | `libfuzzerTraceCmp*` | panics, fatal errors, data races |
| <img src="docs/assets/icons/rust.svg" height="20"> | Rust | SanitizerCoverage | `trace-compares` | panics, ASan |
| <img src="docs/assets/icons/ruby.svg" height="20"> | Ruby | Prism rewriting | rewritten `==`, `case`/`when` | undeclared exceptions |
| <img src="docs/assets/icons/swift.svg" height="20"> | Swift | SanitizerCoverage | `trace-cmp` | traps, ASan |

Details: [`languages/README.md`](languages/README.md).

## Quick start

```sh
pip install stitch-fuzz
stitch setup --lang python
stitch init tomlkit --lang python --git https://github.com/python-poetry/tomlkit --rev 0.13.2
```

Write stubs, then build and probe:

<img src="docs/assets/probe.gif" width="760" alt="stitch build and stitch probe on tomlkit">

Triage a crash and check whether it is fixed upstream:

<img src="docs/assets/triage.gif" width="760" alt="stitch crashes and stitch exec on tomlkit">

All commands are in [`docs/cli.md`](docs/cli.md).

## Results

Harnesses for 40+ real libraries are in [`projects/`](projects/README.md). They found bugs that are
still in the latest releases, and found known CVEs again in jsoup, semver, go-yaml, x/net/html, regex
and lodash.

## Citation

If you find STITCH++ useful, please cite our paper:

```bibtex
@inproceedings{green2026stitching,
  title     = {Automatic, Expressive, and Scalable Fuzzing with Stitching},
  author    = {Green, Harrison and Brown, Fraser and Le Goues, Claire},
  booktitle = {Proceedings of the 2026 ACM SIGSAC Conference on Computer and Communications Security},
  series    = {CCS '26},
  publisher = {ACM},
  address   = {The Hague, Netherlands},
  pages     = {1582--1596},
  year      = {2026},
  doi       = {10.1145/3830454.3846663}
}
```

## License

[MIT](LICENSE)
