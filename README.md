# STITCH v2

API fuzzing for C/C++ libraries, built for coding agents. You (or an agent) write small typed stubs for
the library's API; the LibAFL engine composes them into call sequences, learns which ones work, and finds
bugs. Everything builds and runs in Docker. Crashes come out as minimized, self-contained Docker
reproducers, and the library's git history can be bisected automatically.

```sh
bin/stitch init cjson --git https://github.com/DaveGamble/cJSON --rev v1.7.18
cd cjson && $EDITOR Dockerfile harness/      # see `stitch guide`
stitch build && stitch check
stitch probe --secs 60                       # what bails, what never succeeds, and why
stitch fuzz --jobs 4 && stitch status
stitch crashes                               # unique crashes c1, c2, ...
stitch exec c1 --trace                       # the calls, the arguments, the ASan report
stitch repro c1                              # self-contained dockerized reproducer (verified)
stitch write c1                              # the crash as one standalone C++ file (stubs pasted in)
stitch try .stitch/standalone/c1/min.c       # agent-minimized version: still the same crash?
stitch bisect c1                             # first bad commit of the library
```

Docs: [`docs/agent-guide.md`](docs/agent-guide.md) (workflow, printed by `stitch guide`) and
[`docs/spec.md`](docs/spec.md) (harness reference, `stitch guide --spec`).

## Prototype projects

Five libraries from the v1 benchmark, harnessed from scratch with this toolchain
(`projects/*/notes.md` has the details):

| Project | Endpoints | Library findings |
|---|---|---|
| cJSON | 65 | 3 bugs in cJSON_Utils, all present at upstream HEAD: `sort_list` breaks the `child->prev` tail invariant (later appends drop members, or write to freed memory after `ApplyPatches`); `decode_pointer_inplace` mis-decodes `~0`/`~1`; `overwrite_item` frees borrowed (`StringIsConst`) keys |
| quickjs-ng | 52 | `JS_NewSymbol` aborts on numeric descriptions (at HEAD); parser heap overread in `js_parse_for_in_of` under a memory limit (at HEAD); `JS_NewTypedArray` reads past `argv` (fixed upstream, `bisect --fixed` → 3f57bfb); several out-of-memory error-path use-after-frees and the BigInt raw-context SEGV (not reproducing at HEAD); `JS_DetectModule` reads past `input_len` (questionable) |
| libyaml | 33 | `yaml_parser_load` asserts when called again after an error (questionable) |
| zlib | 45 | none within the documented contract (`inflatePrime` misuse leads to an input overread) |
| Lua 5.5-dev | 38 | none |

Every harness-side crash met during triage (API misuse, oracle false positives) is listed in the
notes with its fix; the recurring lessons are in the agent guide ("Harness design rules").

`tests/smoke.sh` runs the whole pipeline on `examples/demolib` (two planted bugs: found, minimized,
reproduced from scratch in Docker, and bisected to the right commits).

## Layout

| Path | What |
|---|---|
| `runtime/` | C++ runtime: `stitch.h` (harness API, signature reflection), `runtime.cpp` (program execution, object pool, attributes, `Source` decoding, plan mode, SanitizerCoverage edge + cmplog hooks), `stitch_abi.h` (C ABI to the engine), `standalone/stitch_standalone.h` (harness API without the fuzzer, for generated reproducers) |
| `engine/` | Rust engine on LibAFL 0.16: program input, producer-aware generation, graph + byte mutators (havoc, tokens, cmplog I2S per parameter buffer), fork executor, statistics, triage, minimization |
| `docker/` | `base.Dockerfile` (toolchain + runtime + engine), compiler wrappers, `stitch-build` (in-container harness builder) |
| `cli/` | `stitch` CLI (Python, stdlib only): projects, images, campaigns, reports, reproducers, bisection |
| `examples/` | `demolib` (toy library with a planted bug history) |
| `projects/` | prototype harnesses for real libraries |

Install the CLI with `pip install -e cli` or run `bin/stitch` directly. Requirements: Docker with buildx,
git, Python ≥ 3.9.

## How it works

- **Harness = typed stubs.** `New<T> parse(Str<4096, JsonText> text)`: the signature is the spec. A
  registration macro reflects the function type into a manifest (objects, inputs, outputs, fuzz parameters
  with sizes, ranges and data types).
- **Programs.** The engine's input is a list of calls; object inputs reference outputs of earlier calls,
  parameters are byte buffers. Programs are saved by endpoint *name*, so corpora survive harness edits.
- **Execution.** A persistent forked worker runs programs handed to it through shared memory
  (re-forked after a crash/timeout and every 200 programs; ASan makes `fork()` cost ~1-2 ms, so
  forking per program caps throughput at a few hundred execs/s; persistent workers are ~13x faster
  on cJSON). Per-call status (ok / bail with reason / skipped), produced outputs, missing-attribute
  reads and the crash pc go to shared memory. Triage re-runs every crash standalone.
- **Learning.** Endpoints that never succeed are down-weighted; for every (call, input, previous call on
  that object) the engine tracks the success rate and prefers producers that work.
- **Mutations.** Graph edits (append/insert/delete calls, rewire inputs, splice from other programs,
  duplicate calls) and parameter edits through LibAFL's own byte mutators applied to one buffer at a time:
  havoc, tokens, and cmplog input-to-state replacement, with comparisons attributed to the call that made
  them. Decoders turn structured choices into values; their encoding is identity-preserving, so I2S works
  on choice buffers too, and decoded values can be lowered to raw bytes to break their structure.
- **Standalone reproducers.** `stitch write` runs the program once in *plan mode* (the runtime
  records every call's concrete arguments as the stub saw them, `Src` draws, outcomes, consumed
  objects and finalizers). Then it pastes each call's stub body into `main()` as a lambda, with
  parameters declared from those values. It also pastes the harness helpers the calls use. The
  file builds against the library plus `stitch_standalone.h`, a single header that implements the
  harness API without the fuzzer (object cells, attributes, consumption, `Source` replay, bail =
  divergence). `stitch try` builds and runs any edited version in the project image and compares
  the crash (kind + innermost library frame), so an agent can shrink it to plain library calls.
  `tests/standalone_fidelity.py` checks the conversion on random corpus programs: every call must
  have the same outcome as in the fuzzer.
- **Docker.** `stitch2/base` = clang 18 + sanitizers + runtime + engine. A project's Dockerfile builds the
  library on top with instrumenting `CC`/`CXX`; the source comes in as a named build context (git
  revision exported from a cached mirror, or a local directory). One image per revision, so bisection
  and `--rev` builds reuse Docker's layer cache.
