# STITCH++

API fuzzing for libraries in C/C++, Python, JavaScript, Java, Go, Rust, Ruby and Swift, built for
coding agents.

You write small typed stubs that call the library. A LibAFL-based fuzzer stitches them into call
sequences, learns which ones work, and finds crashes, exceptions and failed checks. Builds, fuzzing
and reproducers all run in Docker.

## Install

```sh
pip install stitch-fuzz
stitch setup                     # check Docker, get the C/C++ image
stitch setup --lang python       # or javascript, java, go, rust, ruby, swift, all
stitch skill                     # optional: install the agent skill into ./.claude/skills
```

Requires Docker with buildx, git, and Python 3.9+.

## Usage

```sh
stitch init tomlkit --lang python --git https://github.com/python-poetry/tomlkit --rev 0.13.2
cd tomlkit                       # write the Dockerfile and harness/ stubs
stitch build
stitch probe --secs 60           # which stubs fail, and why
stitch fuzz --jobs 4
stitch crashes                   # unique crashes: c1, c2, ...
stitch exec c1 --trace           # replay a crash
stitch repro c1                  # standalone Docker reproducer
```

Other commands: `status`, `cov`, `bisect`, `write`, `try`. The CLI is the same for every language.

A stub is a typed function. Fuzzed parameters come from the fuzzer, objects come from earlier calls,
and results become objects for later calls:

```python
@stitch.endpoint(bails_on=(TOMLKitError,))
def parse(text: Str[2048, TomlText]) -> New[Doc]:
    return tomlkit.parse(text)
```

## Documentation

- `stitch guide` (or [`skill/stitch/SKILL.md`](skill/stitch/SKILL.md)): the workflow
- `stitch guide <lang>` (or [`skill/stitch/reference/`](skill/stitch/reference/)): the harness API
  for each language
- [`languages/README.md`](languages/README.md): how each language is instrumented and run
- [`projects/`](projects/README.md): harnesses for 40+ real libraries, with the bugs they found
- [`examples/`](examples/): small demo libraries with planted bugs

## Development

Run `bin/stitch` from the checkout, or `pip install -e .`. Tests are in [`tests/`](tests/). Pushing a
`vX.Y.Z` tag publishes the images to GHCR and the package to PyPI.

## Citation

```bibtex
@inproceedings{green2026stitching,
  title     = {Automatic, Expressive, and Scalable Fuzzing with Stitching},
  author    = {Green, Harrison and Brown, Fraser and Le Goues, Claire},
  booktitle = {Proceedings of the ACM SIGSAC Conference on Computer and Communications Security (CCS)},
  year      = {2026}
}
```

## License

[MIT](LICENSE)
