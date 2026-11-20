# STITCH

API fuzzing for C and C++ libraries, built for coding agents. You write small typed C++ stubs that
call the library. A LibAFL-based fuzzer chains them into call sequences, learns which ones work, and
finds crashes. Builds, fuzzing and reproducers all run in Docker.

## Install

```sh
pip install stitch-fuzz          # or: pip install git+https://github.com/hgarrereyn/STITCH
stitch setup                     # check Docker, get the base image
stitch skill                     # optional: install the agent skill into ./.claude/skills
```

Needs Docker with buildx, git, and Python 3.9 or newer. `stitch setup` pulls a prebuilt base image
when one is published for your version, and otherwise builds it once (about 10 minutes).

## Use

```sh
stitch init cjson --git https://github.com/DaveGamble/cJSON --rev v1.7.18
cd cjson                         # edit Dockerfile and harness/ (see `stitch guide`)
stitch build
stitch probe --secs 60           # what fails, and why
stitch fuzz --jobs 4
stitch status
stitch crashes                   # unique crashes: c1, c2, ...
stitch exec c1 --trace           # the calls, their arguments, the sanitizer report
stitch repro c1                  # Docker reproducer folder, verified from scratch
stitch bisect c1                 # commit that introduced the bug
stitch write c1                  # the crash as one C++ file, to shrink with `stitch try`
```

The guide is an agent skill: [`skill/stitch/SKILL.md`](skill/stitch/SKILL.md). `stitch guide` prints
it, `stitch guide api` prints the harness API, `stitch guide reproducers` the reproducer workflow.

A stub:

```cpp
New<Json> parse(Str<4096, JsonText> text) {
    cJSON* j = cJSON_Parse(text.c_str());
    stitch::require(j, "parse error");
    return j;
}
STITCH_ENDPOINT(parse);
```

## Example projects

`projects/` has harnesses for five libraries. Each has `notes.md` with the harness fixes made during
triage. Verified bug reports are in `projects/*/bugs/`.

| Project | Stubs | Findings |
|---|---|---|
| cJSON | 65 | 3 bugs in cJSON_Utils, all present at HEAD: `sort_list` breaks the list tail pointer (appends lose members or write to freed memory); `decode_pointer_inplace` mis-decodes `~0`/`~1`; `overwrite_item` frees borrowed (`StringIsConst`) keys |
| quickjs-ng | 52 | `JS_NewSymbol` aborts on numeric descriptions (HEAD); heap overread in `js_parse_for_in_of` under a memory limit (HEAD); `JS_NewTypedArray` reads past `argv` (fixed upstream in 3f57bfb) |
| libyaml | 33 | `yaml_parser_load` asserts when called again after an error (questionable) |
| zlib | 45 | none |
| Lua 5.5-dev | 38 | none |

## How it works

- **Stubs.** The function signature is the spec: `Borrow<T>`/`Take<T>` inputs, `New<T>` outputs, and
  typed fuzz parameters. A registration macro turns the signature into a manifest for the fuzzer.
- **Programs.** A program is a list of calls. Object inputs refer to outputs of earlier calls.
  Programs store stub names, so corpora survive harness changes.
- **Execution.** A forked worker runs many programs from shared memory and is replaced after a crash,
  a timeout, or every 200 programs. It reports each call's result (ok, or failed with a reason) and the
  crash location.
- **Learning.** Stubs that never succeed get picked less. For each input, the fuzzer tracks which
  producers lead to success and prefers them.
- **Mutations.** Program edits (insert, delete, rewire, splice calls) and byte mutations of single
  parameters: LibAFL havoc, dictionary tokens, and cmplog input-to-state replacement. Decoders turn
  fuzzer choices into structured values (JSON, source code), and the byte mutators still apply.
- **Docker.** The base image has clang 18, sanitizers, the runtime and the fuzzer. A project's
  Dockerfile builds the library on top. The library source comes in as a build context: a git
  revision or a local directory. One image per revision, so `--rev` and bisection reuse the cache.
- **Standalone reproducers.** `stitch write` replays a crash once while recording every call's
  arguments, then writes `main()` with each stub's code pasted in and the arguments filled in.
  `stitch_standalone.h` provides the harness API without the fuzzer. `stitch try` builds an edited
  copy and checks that it still crashes the same way.

## Repository

| Path | Contents |
|---|---|
| `cli/stitch_fuzz/` | the `stitch` CLI (Python, no dependencies) |
| `runtime/` | C++ runtime: `stitch.h` (harness API), `runtime.cpp`, `standalone/stitch_standalone.h` |
| `engine/` | Rust fuzzer on LibAFL 0.16 |
| `docker/` | base image Dockerfile, compiler wrappers, in-container harness builder |
| `skill/stitch/` | agent skill: `SKILL.md` and reference files |
| `projects/` | example harnesses and bug reports |
| `examples/demolib` | toy library with planted bugs (history in `examples/demolib-src.bundle`) |
| `tests/` | `smoke.sh` (whole pipeline on demolib), `standalone_fidelity.py` |

Development: `pip install -e .` or run `bin/stitch` from the checkout. Releases: push a `vX.Y.Z` tag;
`.github/workflows/release.yml` publishes the images to GHCR and the package to PyPI.
