# STITCH++

Write small typed stubs defining your API. STITCH++ automatically fuzzes valid combinations of
endpoints, generates arguments, and finds crashes, exceptions, and failed checks.

> [!NOTE]
> **Are you an agent?** Read this instead:
> https://hgarrereyn.github.io/STITCHplusplus/agents/overview.md

**[Documentation](https://hgarrereyn.github.io/STITCHplusplus/)** ·
[Tutorial](https://hgarrereyn.github.io/STITCHplusplus/guide/tutorial/library/) ·
[CLI reference](https://hgarrereyn.github.io/STITCHplusplus/reference/cli/) ·
[Example projects](projects/README.md) ·
[Paper](https://doi.org/10.1145/3830454.3846663)

## Install

```sh
pip install stitch-fuzz
stitch setup --lang python
```

Requires Docker. The library, the harness and the fuzzer run in containers.

## Languages

[C/C++](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/cpp/) ·
[Python](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/python/) ·
[JavaScript](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/javascript/) ·
[Java](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/java/) ·
[Go](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/go/) ·
[Rust](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/rust/) ·
[Ruby](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/ruby/) ·
[Swift](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/swift/) ·
[Objective-C](https://hgarrereyn.github.io/STITCHplusplus/reference/languages/objc/)

<details>
<summary>How each language is instrumented</summary>

| Language | Coverage | Comparisons (cmplog) | Bugs detected |
|---|---|---|---|
| C/C++ | SanitizerCoverage (clang) | `trace-cmp`, `memcmp`/`strcmp` hooks | ASan, UBSan, uncaught exceptions |
| Python | `sys.monitoring` line and branch events | AST rewrite of `==`, `!=`, `in`, `<` | undeclared exceptions |
| JavaScript | Babel plugin (CommonJS hook and ESM loader) | Babel rewrite of `===` and `switch`; `String`/`Set`/`Map` hooks | undeclared exceptions, prototype pollution, unhandled rejections |
| Java | Java agent, counters per basic block | `if_icmp`, `lcmp`, `switch`, `String.equals`/`startsWith`, `Map.get` | undeclared `Throwable`s |
| Go | `-d=libfuzzer` 8-bit counters | `libfuzzerTraceCmp*`, `libfuzzerHookStrCmp` | panics, fatal errors, data races (`-race`, optional) |
| Rust | SanitizerCoverage | `trace-compares`, ASan `memcmp` hooks | panics (including overflow checks), ASan |
| Ruby | Prism source rewriting | rewritten `==`, `<`, `eql?`, `start_with?`, `include?`, `case`/`when` | undeclared exceptions |
| Swift | SanitizerCoverage | `trace-cmp` | traps (bounds, overflow, nil unwrap), ASan |
| Objective-C | SanitizerCoverage (clang, GNUstep) | `trace-cmp`, `memcmp`/`strcmp` hooks, `NSString` comparison wrappers | Objective-C exceptions, ASan, UBSan |

In every language, failed `check`s in your stubs and timeouts are reported too. Details:
[`languages/README.md`](languages/README.md).

</details>

## Citation

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
