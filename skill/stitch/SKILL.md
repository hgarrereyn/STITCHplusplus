---
name: stitch
description: Fuzz the API of a C or C++ library with STITCH. Covers the Dockerfile that builds the library, C++ harness stubs, fuzzing campaigns, the health report, crash triage, and reproducers. Use when asked to fuzz a C/C++ library, write or fix a STITCH harness, or triage STITCH crashes.
---

# STITCH

STITCH fuzzes a library through its API. You write small C++ functions (stubs) that call the
library. The fuzzer chains stubs into programs, mutates their arguments, and reports crashes.
Everything builds and runs in Docker.

## Setup

```sh
pip install stitch-fuzz
stitch setup
```

`stitch setup` checks Docker and downloads the base image (or builds it once if no prebuilt image
exists for this version). Needs Docker with buildx. Every command accepts `--json`.

## Workflow

1. `stitch init`: create the project.
2. Edit `Dockerfile` until the library builds.
3. Write stubs in `harness/`.
4. `stitch probe`: fix stubs that fail.
5. `stitch fuzz`: run a campaign.
6. `stitch crashes`: triage each crash.

## 1. Create a project

```sh
stitch init cjson --git https://github.com/DaveGamble/cJSON --rev v1.7.18
cd cjson
```

For a local checkout use `--path ../cJSON` instead of `--git`. This creates:

- `stitch.toml`: source, compile flags, fuzzing options
- `Dockerfile`: builds the library
- `harness/`: your stubs

## 2. Build the library

The Dockerfile starts from the STITCH base image. The library source arrives as the `source` build context.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN cmake -B build -DBUILD_SHARED_LIBS=OFF -DENABLE_CJSON_UTILS=ON && cmake --build build -j$(nproc)
```

- `CC` and `CXX` already add coverage and AddressSanitizer. Build a static library.
- Install packages with `RUN apt-get update && apt-get install -y libfoo-dev` (Ubuntu 24.04).
- Build a tool without instrumentation: `RUN STITCH_NO_INSTR=1 make tool`.

Then set how stubs compile and link, with paths inside the image:

```toml
[build]
cflags = ["-I/src"]
ldflags = ["/src/build/libcjson_utils.a", "/src/build/libcjson.a", "-lm"]
```

`stitch build --image-only` builds the library. `stitch build` builds the library and the harness.

## 3. Write stubs

A stub is one API scenario. Its signature tells the fuzzer what it needs and what it produces.

```cpp
// harness/objects.hpp
#pragma once
#include <stitch.h>
#include <cJSON.h>

STITCH_OBJECT(Json, cJSON*);                            // object type and its C handle
STITCH_DROP(Json, [](cJSON* j) { cJSON_Delete(j); });   // frees objects left at the end
STITCH_DATATYPE(JsonText);                              // data type, see section 4
```

```cpp
// harness/parse/parse.cpp
#include "../objects.hpp"

New<Json> parse(Str<4096, JsonText> text) {
    cJSON* j = cJSON_Parse(text.c_str());
    stitch::require(j, "parse error");
    return j;
}
STITCH_ENDPOINT(parse);

void add_item(Borrow<Json> obj, Str<64> key, Take<Json> item) {
    if (!cJSON_IsObject(obj) || !cJSON_AddItemToObject(obj, key.c_str(), item)) {
        cJSON_Delete(item);  // we own `item` until the library takes it
        stitch::bail("add failed");
    }
}
STITCH_ENDPOINT(add_item);
```

Objects:

| In the signature | Meaning |
|---|---|
| `Borrow<T> x` | uses an object; it stays available |
| `Take<T> x` | consumes the object: free it or give it to the library, also when you bail |
| `x.consume()` | on a `Borrow`: the library took ownership in this call |
| returns `New<T>` | produces an object |

Every other parameter is fuzzed data:

| Type | Value |
|---|---|
| `U8`..`U64`, `I8`..`I64`, `F32`, `F64`, `Bool` | numbers |
| `Range<0, 100>` | integer in the range |
| `OneOf<A, B, C>` | one of the constants (modes, enum values) |
| `Flags<A, B, C>` | any OR of the flags |
| `Str<Max, Tag>`, `Bytes<Max, Tag>` | string or buffer, `Tag` is an optional data type |
| `File<Max, Tag>` | temporary file holding the data: `f.path()` |
| `Raw<T>` | the bytes of a plain struct |
| `Src<Max>` | choices made inside the stub: `s->choice(n)` |

Control:

```cpp
stitch::require(cond, "reason");   // stop this call; not a bug (bad input, unmet precondition)
stitch::bail("reason");            // same, unconditionally
stitch::check(cond, "what");       // a bug if false: reported as a crash
STITCH_INIT { lib_global_init(); } // runs once per process
```

Attributes keep state the library does not expose:

```cpp
stitch::set(obj, "refs", stitch::get(obj, "refs", 0) + 1);
stitch::require(stitch::get(obj, "refs", 0) == 0, "object is referenced");
stitch::set(view, "root", root);                      // link two objects
cJSON** r = stitch::get_obj<Json>(view, "root");      // nullptr once the root is gone
```

Ask the library first (`cJSON_IsArray(x)`). Use attributes only for what it cannot tell you.

Full API: [reference/harness-api.md](reference/harness-api.md).

### Rules

- One producer per kind of object: `new_number` and `new_string`, not `new_value(kind)`.
- If the result type depends on the input (a parsed value, a Lua variable), return typed optional
  outputs: `std::tuple<New<Ref>, std::optional<New<Table>>, std::optional<New<Func>>>`. Stubs that
  need a table then take `Borrow<Table>`.
- Pick existing keys and indexes with a `Src` parameter. Random keys almost always miss.
- Read the library's tests for the API contract. Docs often leave out preconditions.
- A stub that copies an object must copy its attributes too.
- If a call changes its inputs (sorts them, reallocates), update the attributes that depend on them.
- `stitch::get(obj, "k", 0)` when a missing key is normal. `stitch::get(obj, "k")` when it must be
  set: misses are reported.
- Write `check` only for properties that always hold. Exclude edge cases such as NaN or duplicate keys.
- Keep stubs compiling across library versions so `--rev` and `bisect` work.
- Put the library's error in bail reasons, without positions or names: `"parse error"`, not
  `"parse error at 17"`.
- Group stubs in folders by component (`harness/parse/`, `harness/tree/`). Avoid libc names (`read`, `open`).

## 4. Data types

A data type gives a buffer seeds, tokens and structure. Tag the parameter (`Str<4096, JsonText>`)
and create `harness/data/JsonText/` with any of:

- `seeds/`: sample inputs. Copy the library's test files.
- `*.dict`: tokens, one per line: `"true"`, `"\x00\x01"`.
- a decoder: builds a valid value from fuzzer choices, so mutations stay well formed.

```cpp
static std::string value(stitch::Source& s, int depth) {
    switch (s.choice(depth > 4 ? 4 : 6)) {
        case 0: return "null";
        case 1: return s.coin() ? "true" : "false";
        case 2: return std::to_string(s.range(-1000, 1000000));
        case 3: return "\"" + s.str(16) + "\"";
        case 4: { std::string a = "["; while (s.more()) a += value(s, depth + 1) + ","; return a + "]"; }
        default: { std::string o = "{"; while (s.more()) o += "\"" + s.token() + "\":" + value(s, depth + 1) + ","; return o + "}"; }
    }
}
STITCH_DECODER(JsonText, [](stitch::Source& s) { return value(s, 0); });
```

Draw literal text with `s.str()` and `s.token()`. Compute lengths and checksums instead of drawing
them. Look at the output with `stitch sample JsonText -n 5`.

## 5. Run

```sh
stitch build            # compile errors point to harness file:line
stitch check            # static checks: unreachable stubs, attributes read but never set
stitch probe --secs 60  # short run and a report of what fails
stitch fuzz --jobs 4    # campaign in the background; resumes its corpus
stitch status           # progress and the same report
stitch stop
```

Read the report:

| Report says | Do |
|---|---|
| never succeeds | The stub always bails. Read the reasons: a missing producer, an attribute nobody sets (`read missing attr 'x'`), or a missing environment (server, file). Fix the stub or add a producer. |
| mostly bails | Use `Range` or `OneOf` instead of rejecting raw values. Parse errors on fuzzed input are fine. |
| often skipped | The producer of an input usually fails. Fix the producer. |
| depends on previous call | Normal; the fuzzer learns these pairs. Investigate only if it is a stub bug. |
| exec bail rate > 50% | Most executions are wasted. Find the stubs that bail most. |

After each change, run `stitch probe` again and compare. Do not add guards the library does not need:
each guard hides behavior.

## 6. Triage

```sh
stitch crashes             # unique crashes: c1, c2, ...
stitch exec c1 --trace     # every call with its arguments, then the sanitizer report
stitch minimize c1         # fewer calls, smaller inputs
stitch exec c1 --rev HEAD  # does it still crash upstream?
```

Decide for each crash:

- **Harness bug**: the stub broke the API contract (double free, wrong ownership, missing init).
  Fix the stub, run `stitch build`, and confirm `stitch exec c1` no longer crashes.
- **Library bug**: the stubs followed the contract. Report it:
  ```sh
  stitch repro c1           # Docker reproducer folder, verified from scratch
  stitch bisect c1          # commit that introduced the bug (--good v1.0 if you know a good version)
  stitch bisect c1 --fixed  # commit that fixed it, if HEAD no longer crashes
  stitch write c1           # the crash as one standalone C++ file
  ```
- **Unsure**: read the library source at the top stack frames.

### Minimal reproducer

`stitch write c1` creates `.stitch/standalone/c1/repro.cpp`: each call with its stub code pasted in
and its arguments filled in. Shrink a copy to plain library calls and check after every edit:

```sh
cp .stitch/standalone/c1/repro.cpp .stitch/standalone/c1/min.c
stitch try .stitch/standalone/c1/min.c
```

`stitch try` prints SAME CRASH (exit code 0), DIFFERENT CRASH, NO CRASH, DIVERGED or COMPILE ERROR.
A finished `min.c` looks like this:

```c
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    cJSON *from = cJSON_Parse("{\"a\":1,\"c\":2,\"b\":3}");
    cJSON *to = cJSON_Parse("{\"a\":1,\"c\":2,\"d\":4}");
    /* sorts `from`, but the head's prev (the tail pointer) still points to "b" */
    cJSON *patches = cJSONUtils_GeneratePatchesCaseSensitive(from, to);
    /* removing "b" frees it; adding "d" then writes to the freed tail */
    cJSONUtils_ApplyPatches(from, patches);
    return 0;
}
```

Steps and pitfalls: [reference/reproducers.md](reference/reproducers.md).

## Versions and local code

- `stitch build --rev v1.7.17` builds another version (cached per commit).
- `[source] path = "../lib"` in `stitch.toml` fuzzes a local checkout, uncommitted changes included.
  If it is a git checkout, `stitch bisect` walks its history.

## Commands

| Command | Does |
|---|---|
| `stitch setup` | check Docker, get the base image |
| `stitch init DIR --git URL --rev REV` | new project (`--path DIR` for local code) |
| `stitch build [--rev REV] [--image-only]` | build the library image and the harness |
| `stitch check` | build + static checks |
| `stitch probe [STUBS] --secs N` | short run + report (`STUBS`: `'parse,tree/*'`) |
| `stitch fuzz [--jobs N] [--secs N]` | start a campaign in the background |
| `stitch status`, `stitch stop` | campaign progress, stop it |
| `stitch crashes` | unique crashes |
| `stitch exec ID [--trace] [--rev REV]` | run a crash or `.stp` program |
| `stitch minimize ID` | shrink a crashing program |
| `stitch repro ID` | Docker reproducer folder |
| `stitch bisect ID [--good REV] [--fixed]` | find the commit that introduced (or fixed) a crash |
| `stitch write ID` | standalone C++ file of a crash |
| `stitch try FILE` | build and run a standalone file, compare the crash |
| `stitch sample TYPE -n N` | values a decoder produces |
| `stitch shell` | shell in the project image (project at `/project`) |
| `stitch guide [api\|reproducers]` | print this guide or a reference |

## Files

- `.stitch/build/`: `fuzzer`, `repro`, `manifest.json` (the stubs as the fuzzer sees them)
- `.stitch/campaigns/<name>/`: corpus, crashes, stats, `triage.json`
- `.stitch/repros/`, `.stitch/standalone/`, `.stitch/bisect/`: reproducers and bisect results
- Image build logs: `~/.cache/stitch/logs/`
