# Reproducers

Two kinds:

- `stitch repro c1`: a folder that rebuilds everything from scratch with Docker (pinned library source,
  Dockerfile, harness, the crashing program). `./run.sh` reproduces the crash. `stitch repro` verifies it.
- `stitch write c1`: one C++ file. Shrink it to a few lines of plain library code for the bug report.
  `stitch repro` copies the result into the folder, and `./run.sh --standalone` runs it.

## stitch write

```sh
stitch write c1
```

Output in `.stitch/standalone/c1/`:

- `repro.cpp`: every call of the crashing program in `main()`, with the stub code pasted in and the
  arguments filled in. Harness helpers it needs are pasted above `main()`.
- `stitch_standalone.h`: the harness API without the fuzzer.
- `expected.json`: the crash to match (sanitizer kind and innermost library function).

One call in `repro.cpp`:

```cpp
    // #2 get_object_item(root=json0, fresh, s, case_sensitive)  [harness/query/query.cpp:39]
    Obj<Item> item2 = stitch::call(2, "get_object_item", [&]() -> New<Item> {
        Borrow<Json> root = json0;
        Str<64, JsonKey> fresh("k");
        // draws: s.coin(0.8) -> true; s.choice(2) -> 1
        Src<16> s("\001\001\000\000\000\000\000\000\000", 9);
        Bool case_sensitive(true);
        std::string key = cj::pick_key(root, *s, fresh.c_str());
        ...
    });
```

## stitch try

```sh
cp .stitch/standalone/c1/repro.cpp .stitch/standalone/c1/min.c   # .c for C libraries
stitch try .stitch/standalone/c1/min.c
```

`stitch try` compiles the file in the project image with the `cflags` and `ldflags` from
`stitch.toml`, runs it, and compares the result with `expected.json`:

| Result | Meaning |
|---|---|
| SAME CRASH | same sanitizer kind and innermost library function (exit code 0) |
| DIFFERENT CRASH | it crashes somewhere else |
| NO CRASH | it runs to completion |
| DIVERGED | a pasted `stitch::require` failed where the original run passed |
| COMPILE ERROR | the compiler errors are printed (exit code 2) |

A failed `check` ("check failed: X") is matched by a crash whose output contains X, or by a
failure raised in the file's own code (`abort()`, `assert`, an exception with no library frame):
plain code detects the wrong behavior itself.

`stitch try FILE c3` compares with crash `c3` instead of `expected.json` (for a file you wrote
yourself). With neither, `stitch try FILE` runs the file and reports what it does. `--rev REV`
builds against another library version. `--show` prints the program output.

`stitch write` warns when the program it starts from no longer crashes (it was saved by an older
harness); `stitch crashes` lists the buckets that still reproduce.

## Shrinking

Run `stitch try` after every step. Undo a step when the result is no longer SAME CRASH.

1. Delete calls the crash does not need.
2. Shrink arguments: cut long strings and documents down to the bytes that matter. Replace `Src`
   values with the key or index the `// draws:` comment shows.
3. Inline the stubs: replace `Borrow<T> x = obj;` and harness helpers with plain variables and library
   calls. Drop `stitch::require` lines.
4. Remove `#include "stitch_standalone.h"` and the pasted helpers. Write `int main(void)`.
5. Add a comment at the line that triggers the bug.

## Pitfalls

- **Oracle failures** (`check failed: X`): end with `assert(cond && "X")`. `try` accepts an abort
  whose output contains `X`.
- **Undefined behavior on constants**: the compiler sees literal arguments, fuzzer inputs it does not.
  If a crash appears or disappears for no visible reason, look for UB on a constant such as
  `(int64_t)1e308`, and make the value opaque: `volatile double d = 1e308;`.
- **Garbage reads**: a bug that reads past a stack array or through a wild pointer depends on memory
  layout, which differs in the pasted code. Such crashes can turn into NO CRASH or DIFFERENT CRASH.
  Use the `stitch repro` folder for those.
- **Memory pressure**: crashes that need a memory limit to fail at an exact allocation may not survive
  edits that change allocations. Keep the original call order and sizes until the end.
- **Leaks** are ignored unless the expected crash is a leak.
