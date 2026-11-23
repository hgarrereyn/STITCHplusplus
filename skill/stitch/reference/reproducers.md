# Reproducers

Two kinds, for two purposes:

| Command | Output | Use it for |
|---|---|---|
| `stitch repro c1` | a folder that rebuilds everything with Docker and runs the crashing program | proving the crash reproduces from scratch; attaching to a report |
| `stitch write c1` | one source file in the harness language | shrinking the crash to a few lines of plain library code |

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
harness, the crashing program and its trace. It needs only Docker. `stitch repro` runs it once
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
