# Languages

Each folder holds one harness language: its runtime, its Docker image and its build script.

| Folder | Harness | Coverage while fuzzing | Comparisons (cmplog) | Bugs |
|---|---|---|---|---|
| `cpp/` | C/C++ stubs, `STITCH_ENDPOINT` | SanitizerCoverage (clang) | `trace-cmp`, `memcmp`/`strcmp` hooks | ASan, UBSan, `stitch::check`, uncaught exceptions |
| `python/` | functions with annotations | `sys.monitoring` LINE + BRANCH | AST rewrite of `==`, `!=`, `in`, `<` | undeclared exceptions, `stitch.check` |
| `javascript/` | `stitch.endpoint(name, args, returns, fn)` | Babel plugin (CommonJS hook + ESM loader) | Babel rewrite of `===`, `switch`; `String`/`Set`/`Map` hooks | undeclared exceptions, prototype pollution, unhandled rejections |
| `java/` | `@Endpoint` static methods | Java agent, counters per basic block (JDK ASM) | `if_icmp`, `lcmp`, `switch`, `String.equals`/`startsWith`, `Map.get` | undeclared `Throwable`s (incl. `StackOverflowError`) |
| `go/` | `stitch.Endpoint(name, fn, specs...)` | `-d=libfuzzer` 8-bit counters (as `go test -fuzz`) | `runtime.libfuzzerTraceCmp*`, `libfuzzerHookStrCmp` | panics, fatal errors, `-race` (optional) |
| `rust/` | `endpoint! { fn ... }` | SanitizerCoverage (`-Cpasses=sancov-module`) | `trace-compares`, ASan `memcmp` hooks | panics (incl. overflow checks), ASan |
| `ruby/` | `Stitch.endpoint :name, {specs}, outputs do ... end` | Prism source rewriting (byte counters per statement list and `&&`/`\|\|`) | rewritten `==`, `<`, `eql?`, `start_with?`, `include?`, `case`/`when` | undeclared exceptions, `Stitch.check` |
| `swift/` | `let x = endpoint("name") { (a: Str<64, Plain>) -> New<T> in }` | SanitizerCoverage (`-sanitize-coverage=edge,trace-pc-guard,trace-cmp`) | `trace-cmp` | traps (bounds, overflow, nil unwrap), ASan, `check` |

## How a language runs

- **In-process** (C/C++, Rust, Swift): the harness binary contains the runtime and the engine
  (C/C++ and Swift link `libstitch_engine.a`; the Rust worker binary loads `libstitch_engine.so`
  and exports the `stitch_*` ABI). A persistent fork server runs programs in a forked child with
  results, edges and comparisons in shared memory. A runtime that survives a finding (a Rust
  panic, a failed check) returns `EXEC_FINDING` from `stitch_exec` and the child keeps running.
  Swift stubs run on the C/C++ runtime through `swift/bridge/bridge.cpp` (C API in
  `swift/runtime/Sources/CStitch/include/cstitch.h`).
- **Worker** (Python, JavaScript, Java, Go, Ruby): the language's own process talks to the
  `stitch-engine` binary over the worker protocol below. The engine numbers the edges a worker
  reports densely (`engine/src/worker.rs`), so its per-run work grows with the library's coverage,
  not with the worker's map size.

`STITCH_PROFILE=<file>` makes the engine append, every 10 s, how long a run takes and how much of
that is the worker (or forked child) versus the engine.

## What a language folder contains

```
languages/<lang>/
  Dockerfile        targets `toolchain` (language + runtime) and `base` (+ the engine)
  runtime/          the harness API and the code that runs programs
  bin/stitch-build  builds the harness inside the project image
  standalone/       stand-in for the runtime in `stitch write` reproducers (if needed)
```

`stitch-build` (run in the project image, project at `/project`) writes:

| File | Purpose |
|---|---|
| `.stitch/build/fuzzer` | the engine with this language's worker: `fuzzer fuzz ...`, `fuzzer exec FILE`, `fuzzer triage ...` |
| `.stitch/build/repro` | runs programs without the engine: `repro [--trace] [--plan F] FILE`, `repro --manifest`, `repro --each [--timeout S] [--files-from L] [--coverage DIR]` |
| `.stitch/build/manifest.json` | stub signatures (same schema as `repro --manifest`) |
| `.stitch/build/app/` | interpreted languages: a snapshot of the harness that the scripts run from (campaigns copy it) |

It prints `{"ok": true, "endpoints": N, ...}` with `--json`, or `{"ok": false, "errors": [{"file", "output"}]}`.

The CLI needs nothing else: build, fuzz, probe, triage, minimize, bisect, `repro` folders, `sample` and
`check` work for every language. `stitch write` needs a writer in `cli/stitch_fuzz/writers/<lang>.py`,
and `stitch cov` needs `repro --each --coverage DIR` to leave `DIR/lcov.info` (plus `html/`,
`report.txt`).

## Programs

A `.stp` program is the same for every language (little endian):

```
"STCH" u32 n_names { u16 len, bytes }*        stub name table
u32 n_nodes, per node:
  u32 name_index
  u8 n_inputs   { u32 src_node, u8 src_output }*
  u8 n_params   { u8 form, u32 len, bytes }*  form 0 = value bytes, 1 = decoder choices
```

A runtime runs the nodes in order and records, per node: status (1 ok, 2 bail, 3 skipped: input
missing, 4 skipped: unknown stub or signature mismatch), outputs produced, borrowed inputs consumed,
the bail reason and the last missing attribute key (both as FNV-1a hashes of strings it also
reports). Parameter bytes decode as in `cpp/runtime/include/stitch.h`: integers little endian,
`Range`/`OneOf`/`Flags` 8 bytes that wrap into the allowed values, `Bool` the low bit, buffers
truncated to their maximum. Decoder choices go through a `Source` with the encoding in
`cpp/runtime/src/runtime.cpp` (the engine mutates inside the strings it finds there).

## Worker protocol

The `stitch-engine` binary starts the worker from `STITCH_WORKER` (a shell command) with fd 3
(engine to worker) and fd 4 (worker to engine), stdout redirected to stderr, and
`STITCH_FUZZING=1` while fuzzing (instrument only then). Messages in both directions are
`u8 tag, u32 length, payload`.

| Engine sends | Payload | Worker replies |
|---|---|---|
| (start) | | `H`: u32 coverage map size, manifest JSON |
| `X` run | u32 flags (1 trace, 2 cmplog), program | `O` ran, or `C` found a bug (and keeps running): results |
| `D` decode | u32 datatype, u32 cap, choices | `R`: value bytes |
| `G` generate | u32 datatype, u64 seed, u32 cap, prefix | `R`: choice bytes |
| `S` spans | u32 datatype, choices | `R`: u32 triples (offset, prefix width, length) per string draw |
| `T` dictionary token | u32 datatype, token | (none) |
| `P` plan file | path, empty to stop | (none) |
| `Q` quit | | (exits) |

Results: `u32 n_nodes, i32 current, u64 crash_pc`, per node `u8 status, u8 n_out, u16 out_mask,
u32 reason, u32 miss_key, u32 consumed`, `u32 len` + string table (`{u32 hash, u16 len, bytes}*`),
`u32 n` + edges (`{u32 index, u8 count}*`), `u32 n` + comparison records (72 bytes each:
`u16 node, u8 kind, u8 size, u8 len_a, u8 len_b, u8 a_const, u8 pad, a[32], b[32]`).

A worker that dies is a crash (a clean exit 0 is not); one that does not answer within the
timeout is killed. `crash_pc` groups findings: `1<<63 | stub index << 32 | hash(kind, location)`.

Bug reports, printed to stderr, are parsed by `engine/src/triage.rs` and `cli/stitch_fuzz/reports.py`:

```
[stitch] EXCEPTION in <stub>: <kind>: <message>
    #0 0x0 in <function> <file>:<line>          innermost first; harness files under /harness/
[stitch] CHECK FAILED: <what> (node N)
```

## Adding a language

1. `runtime/`: the API, program execution (port `cpp/runtime/src/runtime.cpp`; `python/runtime/stitch/_runtime.py`
   is the shortest port), the `Source`, coverage and comparison hooks, and the worker loop.
2. `Dockerfile` with `toolchain` and `base` targets, and `bin/stitch-build`.
3. Add the name to `LANGUAGES` in `cli/stitch_fuzz/docker.py`, a template in `cli/stitch_fuzz/templates.py`,
   a writer in `cli/stitch_fuzz/writers/`, and (if its runtime prints its own crash format) a parser in
   `cli/stitch_fuzz/reports.py`.
4. An example in `examples/<lang>-demo` with planted bugs, and a line in `tests/languages.py`.
