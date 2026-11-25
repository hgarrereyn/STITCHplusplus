<img src="https://hgarrereyn.github.io/STITCHplusplus/readme/title-dark.png#gh-dark-mode-only" width="608" alt="Automatic, Expressive, and Scalable Fuzzing for C/C++, Python, JavaScript, Java, Go, Rust, Ruby, Swift and Objective-C">
<img src="https://hgarrereyn.github.io/STITCHplusplus/readme/title-light.png#gh-light-mode-only" width="608" alt="Automatic, Expressive, and Scalable Fuzzing for C/C++, Python, JavaScript, Java, Go, Rust, Ruby, Swift and Objective-C">

Write small typed stubs defining your API. STITCH++ automatically fuzzes valid combinations of
endpoints, generates arguments, and finds crashes, exceptions, and failed checks.

**[Documentation](https://hgarrereyn.github.io/STITCHplusplus/)** &nbsp;·&nbsp;
[Tutorial](https://hgarrereyn.github.io/STITCHplusplus/guide/tutorial/library/) &nbsp;·&nbsp;
[CLI reference](https://hgarrereyn.github.io/STITCHplusplus/reference/cli/) &nbsp;·&nbsp;
[Paper](https://doi.org/10.1145/3830454.3846663)

> [!NOTE]
> **Are you an agent?** Read [`agents/overview.md`](https://hgarrereyn.github.io/STITCHplusplus/agents/overview.md)
> instead, or run `stitch guide`.

<a href="languages/README.md">
<img src="https://hgarrereyn.github.io/STITCHplusplus/readme/languages-dark.png#gh-dark-mode-only" width="840" alt="C/C++, Python, JavaScript, Java, Go, Rust, Ruby, Swift, Objective-C">
<img src="https://hgarrereyn.github.io/STITCHplusplus/readme/languages-light.png#gh-light-mode-only" width="840" alt="C/C++, Python, JavaScript, Java, Go, Rust, Ruby, Swift, Objective-C">
</a>

## Install

```sh
pip install stitch-fuzz
stitch setup --lang python
```

Requires Docker. The library, the harness and the fuzzer run in containers.

## How it works

A spec is written in the target language. It declares the objects your API passes around, and
stubs that each make one call:

```python
Store = stitch.object("Store")
Snapshot = stitch.object("Snapshot")

@stitch.endpoint(bails_on=(ValueError,))
def load(text: Str[256]) -> New[Store]:
    return kvlib.load(text)

@stitch.endpoint
def remove(st: Borrow[Store], key: Str[16]):
    st.remove(key)

@stitch.endpoint(bails_on=(ValueError,))
def snapshot(st: Borrow[Store]) -> New[Snapshot]:
    return kvlib.load(st.dump())
```

The fuzzer stitches stubs into programs, generates their arguments, and keeps the programs that
reach new code. When one crashes, it minimizes it and writes a standalone reproducer.

<img src="https://hgarrereyn.github.io/STITCHplusplus/readme/example-dark.png#gh-dark-mode-only" width="1040" alt="Left: a program stitched from the stubs, as a graph and as code. Right: a three-call crash it found, KeyError: 'tmp'.">
<img src="https://hgarrereyn.github.io/STITCHplusplus/readme/example-light.png#gh-light-mode-only" width="1040" alt="Left: a program stitched from the stubs, as a graph and as code. Right: a three-call crash it found, KeyError: 'tmp'.">

The [tutorial](https://hgarrereyn.github.io/STITCHplusplus/guide/tutorial/library/) takes this
key-value store from a new project to a minimized reproducer, in the language you pick.

## In this repository

- [`projects/`](projects/README.md): harnesses for 49 real libraries and the bugs they found,
  including known CVEs in jsoup, semver, go-yaml, x/net/html, regex and lodash
- [`examples/`](examples/): the finished tutorial harness in each language
- [`languages/`](languages/README.md): each language's runtime, coverage, comparison hooks and bug
  detectors
- [`skill/stitch/`](skill/stitch/SKILL.md): the agent skill
- [`docs/cli.md`](docs/cli.md): every command
- [`website/`](website/README.md): the documentation site

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
