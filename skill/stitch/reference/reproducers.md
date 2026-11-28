# Reproducers

Three kinds, for three purposes:

| Command | Output | Use it for |
|---|---|---|
| `stitch repro c1` | a folder that rebuilds everything with Docker and runs the crashing program | proving the crash reproduces from scratch, harness included |
| `stitch write c1` | one source file in the harness language | shrinking the crash to a few lines of plain library code |
| `stitch report c1` | `Dockerfile` + that file + `run.sh`, and `ISSUE.md` | reporting the bug to the library's maintainers |

The usual order: `stitch write`, shrink the file to plain library code with `stitch try`, then
`stitch report`.

## Frozen reproducers

The folders `stitch repro` and `stitch report` write are **frozen**: every input is pinned, and
they run with Docker alone by default (or Podman with `STITCH_RUNTIME=podman`). They do not need
STITCH++ installed, and they do not change when the
project moves to a newer STITCH++ release (`stitch upgrade`) or the installed CLI changes. Keep
them as they are: they are the record of the bug.

| Input | Pinned by |
|---|---|
| STITCH++ toolchain (compilers, runtime) | the published image of the project's release, by digest (`FROM ...@sha256:...`) |
| The library | its git commit; a local checkout that is not pushed is included as `source.tar.gz` |
| How the library is built | the project's `Dockerfile`, copied in |
| apt, pip and npm packages | the time the library image was built (below) |
| The program | `crash.stp` (`stitch repro`) or the reproducer file (`stitch report`) |

To make one for a crash:

```sh
stitch write c1                          # .stitch/standalone/c1/repro.<ext>
cp .stitch/standalone/c1/repro.c .stitch/standalone/c1/min.c
# shrink min.c to plain library calls, checking each edit:
stitch try .stitch/standalone/c1/min.c   # SAME CRASH ... plain library code
stitch report c1                         # .stitch/reports/c1/: Dockerfile, min file, run.sh, ISSUE.md
stitch repro c1                          # .stitch/repros/<project>-c1/: the same with the harness
```

Both commands build and run the result from scratch and say whether it crashes the same way. A
reproducer is fully frozen when:

- the project's STITCH++ release is published. Made with an unreleased CLI (a checkout of
  STITCH++), the Dockerfile names a local image and works only on that machine: `stitch report`
  says so.
- the library source can be fetched: a `[source] git` URL, or a `[source] path` checkout (such as
  the repository a `stitch/` folder lives in) whose commit is pushed and has no changes outside the
  STITCH++ project folder. Otherwise the source is bundled as `source.tar.gz`, which must travel
  with the folder.

To keep them with a harness that is checked into a repository, copy them out of `.stitch/` (which
is not committed), for example to `stitch/bugs/<name>/`. Run `./run.sh` there to check one later.

## stitch repro

```sh
stitch repro c1
```

```
[stitch] verified: same crash
reproducer: .stitch/repros/cjson-c1
```

```sh
cd .stitch/repros/cjson-c1
./run.sh                # rebuilds the image and runs the crashing program
./run.sh --standalone   # builds and runs the minimized file instead
```

The folder (`.stitch/repros/<project>-<id>`) holds the pinned library source, the Dockerfile, the
harness, the crashing program and its trace. It needs only Docker or Podman. `run.sh` pulls the STITCH++
toolchain image the reproducer was made with, pinned by digest, so upgrading STITCH++ does not
change it. If that image cannot be pulled, or `STITCH_REPRO_BUILD=1` is set, `run.sh` builds the
toolchain from the copy in `stitch/` instead. `stitch repro` runs it once
from scratch to verify it. Run `stitch repro c1` again after you write a minimized file (below):
it copies the file into `standalone/`.

## stitch write

```sh
stitch write c1
```

```
wrote .stitch/standalone/c1/repro.cpp (150 lines, 12 calls, crash in call #11)
  expected: heap-use-after-free WRITE in suffix_object
SAME CRASH: .stitch/standalone/c1/repro.cpp -> heap-use-after-free WRITE in suffix_object
```

`.stitch/standalone/c1/` then holds:

- `repro.<ext>`: `main()` makes every call of the crashing program with its arguments filled in.
  In C/C++ each stub's body is pasted inline. In the other languages the file contains (Python,
  JavaScript, Ruby, Objective-C) or builds against (Java, Go, Rust, Swift) the harness, and
  `main()` calls the stubs.
- a stand-in for the STITCH++ runtime (`stitch_standalone.h`, `stitch.py`, ...), so `repro` runs
  without the fuzzer.
- `expected.json`: the crash to match (kind and innermost library function).
- `README.md`: how to build it by hand. `trace.txt`: the original run's trace.

One call in `repro.cpp`:

```cpp
    // #1 parse(text)  [harness/parse/parse.cpp:4]
    Obj<Json> json1 = stitch::call(1, "parse", [&]() -> New<Json> {
        // text: 4 bytes from the JsonText decoder
        Str<4096, JsonText> text("null");
        cJSON* j = cJSON_Parse(text.c_str());
        stitch::require(j, "parse error");
        return j;
    });
```

A `Src` parameter shows what each draw returned, so you can replace it with the value:

```cpp
        // draws: s.coin(0.8) -> true; s.choice(2) -> 1
        Src<16> s("\001\001\000\000\000\000\000\000\000", 9);
```

## stitch try

```sh
cp .stitch/standalone/c1/repro.cpp .stitch/standalone/c1/min.c    # .c for a C library
stitch try .stitch/standalone/c1/min.c
```

`stitch try` builds the file in the project image, runs it, and compares the result with
`expected.json`:

| Result | Meaning |
|---|---|
| SAME CRASH | same kind of crash in the same innermost library function (exit code 0) |
| DIFFERENT CRASH | it crashes somewhere else |
| NO CRASH | it runs to completion |
| DIVERGED | a pasted `require` failed where the original run passed |
| COMPILE ERROR | the file does not compile; the errors are printed (exit code 2). In Python, Ruby and JavaScript a syntax error shows as DIFFERENT CRASH |

Options:

- `stitch try FILE c3`: compare with crash `c3` instead of `expected.json` (for a file you wrote
  yourself).
- `stitch try FILE` outside `.stitch/standalone/`, with no crash id: run the file and report what
  happens.
- `--rev REV`: build against another library version. `--show`: print the program's output.

A failed `check` ("check failed: X") has no library frame to match. Reproduce it with an assertion
in your own code that carries the same message: `assert(cond && "X")` in C,
`raise AssertionError("X")` in Python, `throw new Error("X")` in JavaScript, `panic("X")` in Go.
`stitch try` reports SAME CRASH for any failure raised in the file's own code, so run it with
`--show` once and confirm the output is your assertion, not an unrelated error.

## Shrinking

Run `stitch try` after every step. Undo a step when the result is no longer SAME CRASH.

1. Delete calls the crash does not need.
2. Shrink arguments: cut long strings to the bytes that matter. Replace `Src` values with the key
   or index the `// draws:` comment shows.
3. Inline the stubs: replace each stub call with the library calls the stub makes (open your
   harness file next to it), using plain variables. Drop `require` lines.
4. Remove the runtime stand-in import and the pasted helpers.
5. Add a comment at the line that triggers the bug.

Before (one of 12 pasted calls):

```cpp
    stitch::call(2, "add_item", [&] {
        Borrow<Json> obj = json0;
        Str<64> key("2/l");
        Take<Json> item = json1;
        if (!cJSON_IsObject(obj) || !cJSON_AddItemToObject(obj, key.c_str(), item)) {
            cJSON_Delete(item);
            stitch::bail("add failed");
        }
    });
```

After (the whole file):

```c
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    cJSON *from = cJSON_Parse("{\"a\":1,\"c\":2,\"b\":3}");
    cJSON *to = cJSON_Parse("{\"a\":1,\"c\":2,\"d\":4}");
    /* sorts `from`, but the head's prev (the tail pointer) still points to "b" */
    cJSON *patches = cJSONUtils_GeneratePatchesCaseSensitive(from, to);
    /* removing "b" frees it; adding "d" then writes to the freed tail */
    cJSONUtils_ApplyPatchesCaseSensitive(from, patches);
    return 0;
}
```

```
SAME CRASH: .stitch/standalone/c1/min.c -> heap-use-after-free WRITE in suffix_object
  9 code lines, plain library code
```

## stitch report

```sh
stitch report c1                              # the newest min.* in .stitch/standalone/c1/
stitch report .stitch/standalone/c1/min.c     # or name the file
```

```
report: .stitch/reports/c1  (Dockerfile, repro.c, run.sh; ISSUE.md is the issue text)
  verified: same crash
  library pinned to https://github.com/DaveGamble/cJSON @ c859b25da029...; packages as of 2026-11-26T16:28:50Z
```

A reproducer a maintainer can read and run without STITCH++, small enough to paste into an issue:

- `Dockerfile`: the STITCH++ toolchain image for the language (compilers and runtimes, no fuzzer,
  pinned by digest), the library at the pinned commit built by the project's own `Dockerfile`
  (pasted in unchanged), then the reproducer, built and run with plain commands (`clang`,
  `python3`, `node`, `ruby`, `go build`, `cargo build`, `swift build`, `javac`).
- `repro.<ext>`: the reproducer file.
- `run.sh`: container build and run (`./run.sh` with Docker, or `STITCH_RUNTIME=podman ./run.sh`).
- `ISSUE.md`: the issue text: where it reproduces, the code, the output, and the Dockerfile in a
  collapsed section. Fill in what the bug is and why it matters.

`stitch report` builds and runs the folder from scratch, and fails if it does not crash the same
way. The file must be plain library code: in Go, Rust, Swift, Java and Objective-C, the file
`stitch write` makes calls the harness, so shrink it first. In C/C++, Python, JavaScript and Ruby
an unshrunk file works too: the runtime stand-in (`stitch_standalone.h`, `stitch.py`, ...) is
included as a fourth file.

**Pinned versions.** The library is pinned by commit. A local checkout (`[source] path`) is
cloned from its `origin` remote when its commit is pushed and unchanged (a `stitch/` folder in the
library's own repository), and otherwise included as `source.tar.gz`, which the maintainer needs
too. Packages are pinned to the time the
project image was built, when the crash was found: `apt-get` installs from the Ubuntu snapshot
archive of that time, and `pip` and `npm` ignore releases published after it. The same pins apply
to `stitch repro` folders. Go modules resolve to fixed versions on their own; for Rust and Swift,
the library's `Cargo.lock` / `Package.resolved` is used when it has one. RubyGems has no date
option: pin gem versions in the project Dockerfile (or install from the library's `Gemfile.lock`)
if they matter.

## When the crash does not survive shrinking

- **Undefined behavior on constants** (C/C++): the compiler sees literal arguments, which it did
  not see as fuzzer inputs. If a crash appears or disappears for no visible reason, look for UB
  on a constant such as `(int64_t)1e308`, and hide the value: `volatile double d = 1e308;`.
- **Reads of garbage memory**: a bug that reads past a stack array or through a wild pointer
  depends on memory layout, which differs in the pasted code. It can turn into NO CRASH or
  DIFFERENT CRASH. Use the `stitch repro` folder for those.
- **Memory limits**: a crash that needs an allocation to fail at an exact point may not survive
  edits that change allocations. Keep the original call order and sizes until the end.
- **Old programs**: `stitch write` warns when the program no longer crashes with the current
  harness. Pick a crash that `stitch crashes` still lists.

Leaks are ignored unless the expected crash is a leak.
