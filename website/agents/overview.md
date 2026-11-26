# STITCH++ agent guide

STITCH++ fuzzes a library through its API, in C/C++, Objective-C, Python, JavaScript, Java, Go,
Rust, Ruby or Swift. You write **stubs**: small typed functions that each make one API call. The
fuzzer chains stubs into **programs**, passes objects from one call to the next, generates the
arguments, and reports crashes, undeclared exceptions and failed checks. Everything builds and runs
in Docker.

This file is the entry point. The other files of the guide are listed at the end; read your
language's file before writing stubs.

## How it works

A stub's signature tells the fuzzer what the call needs and what it produces:

- **Object**: a value one stub produces and later stubs use (a parsed document, a handle). A stub
  can **borrow** an object (use it; it stays available), **take** it (consume it), or return a
  **new** one.
- **Fuzzed parameter**: every other parameter. Numbers, ranges, choices from a list, strings and
  byte buffers. The fuzzer picks the values.

The fuzzer builds a program by choosing a stub whose object inputs are available, and wiring each
input to an output of an earlier call of the right type. It mutates programs at two levels: the
arguments of each call (bytes, numbers, decoder choices) and the program itself (insert, delete,
rewire and splice calls). It keeps programs that reach new code, and learns which producers each
consumer needs.

`stitch exec --trace` prints a program:

```
[stitch] #0 load(text=Str[5,decoded]"tmp=\n")
[stitch]     -> ok produced o0.0
[stitch] #1 remove(st=o0.0, key=Str[3]"tmp")
[stitch]     -> ok
[stitch] #2 roundtrip(st=o0.0)
```

`o0.0` is output 0 of call 0.

| Term | Meaning |
|---|---|
| bail | an expected failure (invalid input, unmet precondition). Not a bug. The call's outputs are not created; calls that need them are skipped |
| bug | a crash, sanitizer report, undeclared exception, failed `check`, or a program slower than the timeout |
| check | `check(cond, "what")`: a property that must hold. False is a bug |
| data type | a tag on a string or bytes parameter that adds seeds, a dictionary or a decoder |
| decoder | harness code that builds a structured value from fuzzer choices, so mutations keep it well formed |
| attribute | state the harness stores on an object, for facts the library cannot report |
| campaign | one fuzzing run (`probe`, `main`, or `--name`). Crash ids (`c1`, ...) are per campaign |

## Setup

Requires Docker with buildx and Python 3.9 or newer.

```sh
pip install stitch-fuzz
stitch setup --lang <lang>    # cpp, objc, python, javascript, java, go, rust, ruby, swift
```

### Versions: use the one in stitch.toml

Every project pins the STITCH++ release it is written for:

```toml
[project]
stitch_version = "0.2.0"
```

A release is one version of the `stitch` CLI together with its runtime and Docker images, so the
pin decides everything that builds and runs. The CLI refuses a project pinned to another release
and prints the command to install the right one.

- **Working on an existing project** (a `stitch/` folder checked into a library's repository,
  say): read `stitch_version` first and install exactly that release:
  `pip install stitch-fuzz==0.2.0`, or run it without installing with
  `uvx --from stitch-fuzz==0.2.0 stitch ...`. Never edit the pin to silence the error.
- **New project:** `stitch init` pins the installed release.
- **Moving a project to a newer release** (only when asked to): install the newer CLI, read
  `stitch changelog` (the releases after the pinned one, and what each requires of a harness
  under **Upgrading**), change the harness accordingly, run `stitch upgrade` (rewrites the pin),
  then `stitch build`, `stitch check` and `stitch probe`. Start fresh campaigns afterwards.

Reproducers from `stitch repro` and `stitch report` are frozen: they run with Docker alone, on the
images of the release that made them, whatever release is installed later
([reproducers.md](reproducers.md)). Every key of `stitch.toml`: [stitch-toml.md](stitch-toml.md).

Offline, `stitch guide` prints a workflow guide and `stitch guide <lang>` the syntax for one
language. Pass `--json` to any command whose output you parse.

## Workflow

Copy this checklist and keep it updated:

```
- [ ] 1. Project created
- [ ] 2. Library image builds
- [ ] 3. Stubs written
- [ ] 4. Harness builds and `stitch check` is clean
- [ ] 5. Probe report is clean
- [ ] 6. Campaign running
- [ ] 7. Every crash triaged
- [ ] 8. Coverage reviewed
- [ ] 9. Report written
```

1. **Create the project.** `stitch init <dir> --lang <lang> --git <url> --rev <tag>` (or
   `--path <checkout>`). It writes `stitch.toml`, a `Dockerfile` and `harness/`. Pin `--rev` to a
   release tag.
2. **Build the library image.** Edit the `Dockerfile` until `stitch build --image-only` succeeds.
   Your language's file shows the expected `Dockerfile` and `stitch.toml`. `stitch shell` opens a
   shell in the image. For C, C++ and Objective-C, build a static library with `$CC`/`$CXX`.
3. **Write stubs.** Read the library's tests before its docs: tests show the real API contract.
   Write producers first, then operations on objects, then checks. Follow [stubs.md](stubs.md).
4. **Build and check.** `stitch build`, then `stitch check`. Done when both pass without warnings.
5. **Probe.** `stitch probe --secs 60`, then read the report with [probe.md](probe.md). Fix the
   stubs it names and probe again. Done when no stub that should work is under "never succeeds" or
   "often skipped", and calls bailing is below 50%.
6. **Fuzz.** `stitch fuzz --jobs 4` starts campaign `main` in the background (it resumes its
   corpus); `stitch status` shows progress and the same report. Leave it running while you triage.
7. **Triage.** `stitch crashes`, then classify every crash with [triage.md](triage.md). Done when
   each crash is fixed in the harness or confirmed as a library bug with a reproducer
   ([reproducers.md](reproducers.md)).
8. **Coverage.** `stitch cov`; add stubs for library functions that were never called, then go
   back to step 4. See [coverage.md](coverage.md).
9. **Report.** For each library bug:

   ```markdown
   ### <crash kind> in <function> (<campaign>:<id>)

   - **Location:** <file>:<line> (innermost library frame)
   - **Upstream:** crashes / fixed at HEAD (`stitch exec <id> --rev HEAD`)
   - **Introduced by:** <commit> (`stitch bisect <id>`), if known
   - **Reproducer:** <path to the minimized standalone file>

   <two or three sentences: what the library does wrong, and why the calls are valid use of the API>

   <the minimized reproducer>
   ```

   Also list harness bugs you fixed and stubs you left out, with the reason.

## Rules that always apply

- Do not report a library bug until [triage.md](triage.md) says it is one.
- Do not add guards the library does not need; each hides behavior from the fuzzer.
- Keep stubs compiling across library versions, so `--rev` and `bisect` work.

## Files in this guide

Topics:

- [stubs.md](stubs.md): rules for writing stubs, and data types
- [probe.md](probe.md): reading the probe report
- [triage.md](triage.md): harness bug or library bug, and the steps for each crash
- [reproducers.md](reproducers.md): turning a crash into a short standalone file, and frozen
  reproducers for maintainers
- [coverage.md](coverage.md): coverage reports and what to do with them
- [stitch-toml.md](stitch-toml.md): every key of `stitch.toml`; the pinned STITCH++ version
- [cli.md](cli.md): every command

Languages (project setup, harness syntax, reproducers):

- [cpp.md](cpp.md): C/C++
- [objc.md](objc.md): Objective-C
- [python.md](python.md): Python
- [javascript.md](javascript.md): JavaScript
- [java.md](java.md): Java
- [go.md](go.md): Go
- [rust.md](rust.md): Rust
- [ruby.md](ruby.md): Ruby
- [swift.md](swift.md): Swift
