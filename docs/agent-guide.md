# STITCH: agent guide

STITCH fuzzes a library's API by generating *programs*: sequences of calls to small C++ stubs you write
(one stub per API scenario). The engine (LibAFL) learns which call sequences and arguments reach new code.
Your job: describe how to build the library, write stubs, read the feedback, fix what blocks the fuzzer,
then triage what it finds. Everything runs in Docker; you only need `docker` and this CLI.

Add `--json` to any command for machine-readable output.

## 1. Set up a project

```sh
stitch init mylib --git https://github.com/org/mylib --rev v1.2.3   # or: --path ../mylib-checkout
cd mylib
```

This creates:

| File | What it is |
|---|---|
| `stitch.toml` | source (git URL + rev, or local path), harness compile/link flags, fuzzing options |
| `Dockerfile` | how to build the library. Starts `FROM ${STITCH_BASE}`; the source is the `source` build context |
| `harness/` | your stubs (`*.cpp`, any subfolders), `objects.hpp`, and `data/<Type>/` folders |

Edit the `Dockerfile` until `stitch build --image-only` succeeds. Rules:
- `COPY --from=source . /src` gets the library source (at the requested revision, or your local checkout).
- `CC`/`CXX` are already instrumented wrappers (coverage + cmplog + ASan). Build a **static** library with them.
  Tools that must not be instrumented: `STITCH_NO_INSTR=1 make tool`.
- Install system packages with `apt-get` (Ubuntu 24.04).

Then set `[build] cflags` (include paths) and `ldflags` (static libs, `-l...`) in `stitch.toml`, as seen
inside the image.

## 2. Write stubs

A stub is a function whose **signature is the spec**, registered with `STITCH_ENDPOINT`:

```cpp
// harness/objects.hpp
#pragma once
#include <stitch.h>
#include <cjson/cJSON.h>
STITCH_OBJECT(Json, cJSON*);                               // object type + C handle
STITCH_DROP(Json, [](cJSON* j) { cJSON_Delete(j); });      // optional: free leftovers at the end
STITCH_DATATYPE(JsonText);                                 // data type: harness/data/JsonText/

// harness/parse/parse.cpp
#include "../objects.hpp"
New<Json> parse(Str<4096, JsonText> text) {
    cJSON* j = cJSON_Parse(text.c_str());
    stitch::require(j, "parse failed");                    // named bail
    return j;
}
STITCH_ENDPOINT(parse);

void add_item(Borrow<Json> obj, Str<64> key, Take<Json> item) {
    stitch::require(cJSON_IsObject(obj), "not an object");
    if (!cJSON_AddItemToObject(obj, key.c_str(), item)) { cJSON_Delete(item); stitch::bail("add failed"); }
}
STITCH_ENDPOINT(add_item);
```

- **Objects** flow between calls: `Borrow<T>` uses an object and keeps it; `Take<T>` consumes it (you must
  free it or hand it to the library, also on bail paths after ownership moved); returning `New<T>` (or a
  `std::tuple` of `New<T>` / `std::optional<New<T>>`) produces objects. Inputs are never null; returning a
  null handle counts as a bail. Library takes ownership only sometimes? Use `Borrow<T>` and call
  `obj.consume()` when it did.
- **Fuzz parameters** are every other argument: `U8..U64`, `I8..I64`, `F32/F64`, `Bool`, `Range<Lo,Hi>`,
  `OneOf<A,B,C>` (enums/modes), `Flags<A,B,C>` (bit sets), `Bytes<Max,Tag>`, `Str<Max,Tag>`, `File<Max,Tag>`
  (a temp file path), `Raw<T>` (plain-data struct bytes), `Src<Max>` (choices for values that depend on object
  state, e.g. `s->choice(n)` to pick an existing index).
- **Bail** (`stitch::bail("reason")`, `stitch::require(cond, "reason")`) when a call cannot proceed for a
  legitimate reason (precondition not met, input rejected). Bails are not bugs. Always give a reason.
- **Check** (`stitch::check(cond, "what")`) invariants you know: a failed check is reported as a crash.
- **Attributes** remember state the library cannot tell you: `stitch::set(obj, "key", value)`,
  `stitch::get(obj, "key", default)`, `get_str`, `get_obj<T>` (relations), `has`, `del`, `global_set/get`.
  Prefer asking the library (`cJSON_IsArray(x)`) over an attribute. Prefer `Take<T>`/`consume()` over
  "alive/freed" flags. Every endpoint that produces an object type should set the keys others read.
- **Organize** stubs in folders by component (`harness/parse/`, `harness/tree/`); one file may hold several
  endpoints. Endpoint ids are the function names (namespaces allowed: `STITCH_ENDPOINT(tree::add)`); avoid
  names that clash with libc (`read`, `clone`, ...).
- **Library initialization** that must run once: `STITCH_INIT { lib_init(); }`.

`stitch guide --spec` has the full reference.

### Harness design rules (learned the hard way)

- **One producer per kind of output.** The engine learns which producers work for which consumers
  (`by previous call` in `probe`). A `new_value(kind)` endpoint that makes numbers *or* tables hides
  that; `new_number` / `new_table` expose it.
- **Dynamically typed values: typed views.** When a producer's result type is only known at run time
  (Lua `get`, JS property reads), return the generic object plus `std::optional` typed objects chosen
  by the runtime type: `std::tuple<New<Ref>, std::optional<New<Table>>, std::optional<New<Func>>>`.
  Consumers then ask for `Borrow<Table>` and the engine wires them correctly.
- **Pick values that exist.** Lookups by random key or index mostly miss. Use a `Src<N>` parameter to
  choose an existing key/index/node most of the time (and a fresh one sometimes).
- **Read the library's own tests for API contracts.** Docs are often vague (cJSON's
  `CreateObjectReference` takes the *first member* of the container, not the container).
- **Copies copy attributes.** A stub that duplicates an object (`deflateCopy`, `inflateCopy`) must also
  copy the attributes later stubs rely on.
- **State the library mutates behind your back.** Some calls modify their inputs (cJSON's patch
  generators sort them); bump a version attribute and invalidate views of it.
- **Defaults mean "absence is normal".** `stitch::get(obj, "refs", 0)` is a counter that starts at 0;
  `stitch::get(obj, "cmod")` (no default) means "should have been set", and misses are reported.
- **Oracles must exclude cases where the property does not hold** (NaN, duplicate keys, raw values),
  or they drown real findings in false positives.
- **Keep stubs portable across library versions** so `--rev` and `bisect` can build them (e.g. use a
  template where a callback signature changed between releases).
- **Bail reasons should carry the library's error** (normalized: no positions or quoted names), so
  `probe` shows *why* calls fail.

## 3. Data types: seeds, dictionaries, decoders

Tag buffers with a data type (`Str<4096, JsonText>`) and give it a folder `harness/data/JsonText/`:
- `seeds/` - real sample files (a test suite is ideal).
- `*.dict` - tokens (`"true"`, `"\x00\x01"`), AFL dictionary format.
- a **decoder** (any `.cpp` in the harness): a function from structured choices to a value. The fuzzer
  mutates the choices, so every mutation stays well formed:

```cpp
static std::string json_value(stitch::Source& s, int depth) {
    switch (s.choice(depth > 4 ? 4 : 6)) {
        case 0: return "null";
        case 1: return s.coin() ? "true" : "false";
        case 2: return std::to_string(s.range(-1000, 1000000));
        case 3: return "\"" + s.str(16) + "\"";
        case 4: { std::string o = "["; while (s.more()) o += json_value(s, depth + 1) + ","; return o + "]"; }
        default: { std::string o = "{"; while (s.more()) o += "\"" + s.token() + "\":" + json_value(s, depth + 1) + ","; return o + "}"; }
    }
}
STITCH_DECODER(JsonText, [](stitch::Source& s) { return json_value(s, 0); });
```

Use `s.str()`/`s.bytes()` for literal content (cmplog can then solve string compares), compute lengths and
checksums in the decoder instead of drawing them, and keep it deterministic. Check what it produces with
`stitch sample JsonText -n 10`. The fuzzer also mutates decoded values as raw bytes, so malformed inputs
still get tested.

## 4. The loop

```sh
stitch build                 # library image + harness (errors show harness file:line)
stitch check                 # static issues: unreachable endpoints, attribute keys read but never set, ...
stitch probe [sel] --secs 60 # short run + what blocks progress (sel: 'tree/*,parse')
stitch fuzz --jobs 4         # background campaign; resumes its corpus next time
stitch status                # progress + health report
stitch crashes               # unique crashes (c1, c2, ...)
```

Read `probe`/`status` like this:
- **never succeeds**: the endpoint always bails. Look at the reasons. A precondition no producer satisfies, an
  attribute nobody sets (`read missing attr 'x'`), or an environment problem (needs a server, a file, a
  prior handshake). Fix the stub or add the producer stub that sets the state up. The engine down-weights
  these automatically, so they silently waste nothing but also test nothing.
- **mostly bails**: compare reasons. Parameter validation (`Range`, `OneOf`) is better than bailing on raw values.
- **outcome depends on the previous call**: the engine learns these pairs; it is only a problem if the
  dependency is a stub bug.
- **often skipped (missing input)**: the producer of an input often fails; fix the producer.
- **exec bail rate** above ~50% usually means wasted executions; aim lower, but bails on rejected *inputs*
  (parse errors) are healthy.

Don't over-guard. Each added guard hides behavior; after changing a stub, re-run `stitch probe` and
compare its bail rate.

## 5. Triage

```sh
stitch crashes                # buckets by sanitizer type + top frames; example programs
stitch exec c1 --trace        # every call with arguments (+ stitch::log lines), then the sanitizer report
stitch exec c1 --rev HEAD     # does it still reproduce upstream?
stitch minimize c1            # fewer calls, smaller buffers, same signature
```

Decide for each bucket:
- **Library bug**: the stubs used the API as documented. Write it up: `stitch repro c1` creates a
  self-contained reproducer (Dockerfile + source pin + harness + program + README) and verifies it from
  scratch; `stitch bisect c1` finds the commit that introduced it (`--good v1.0` if you know one).
  `stitch exec c1 --rev HEAD` tells you whether it still reproduces upstream; if not,
  `stitch bisect c1 --fixed` finds the commit that fixed it.
- **Harness bug / API misuse**: the stub violated a documented precondition (freed twice, wrong ownership,
  missing init). Fix the stub (add the guard or state the docs require), rebuild, and confirm with
  `stitch exec c1` that it no longer crashes. Re-run `probe` to check you did not over-guard.
- **Unclear**: read the library source around the top frames before deciding.

### Standalone C/C++ reproducers (for bug reports)

`stitch write c1` turns the crashing program into one C++ file, `.stitch/standalone/c1/repro.cpp`:
every call is pasted into `main()` with the stub code and its arguments filled in, plus the harness
helpers it needs. `stitch_standalone.h` stands in for the STITCH runtime, so the file builds against
the library alone. It is verbose but correct; `stitch write` checks that it crashes the same way.

Then minimize it yourself. The goal is a few lines of plain library calls a maintainer can read:

```sh
cp .stitch/standalone/c1/repro.cpp .stitch/standalone/c1/min.c   # .c for C libraries, .cpp otherwise
stitch try .stitch/standalone/c1/min.c    # after EVERY edit: SAME CRASH / DIFFERENT / NO CRASH / DIVERGED / COMPILE ERROR
```

`stitch try` builds the file in the project image (the library's include paths and link flags from
`stitch.toml`), runs it, and compares the result with `expected.json`: the same sanitizer kind and the
same innermost library function. Exit code 0 means SAME CRASH. `--rev X` runs it against another revision.

How to shrink, in roughly this order (re-run `stitch try` each time, undo when it stops saying SAME CRASH):
1. Delete calls that the crash does not need, from the end and from the middle.
2. Replace the arguments: shrink big strings or JSON to the few bytes that matter. Turn `Src` draws
   (the `// draws:` comments say what was picked) into the literal key or index.
3. Inline the stub: replace `Borrow<T> x = obj;` and harness helpers with plain variables and direct
   library calls. Drop `stitch::require` guards, which only matter during fuzzing.
4. Remove `#include "stitch_standalone.h"` and the pasted helper section, and write `int main(void)`.
5. Comment the line that triggers the bug and the library state that makes it possible.

DIVERGED means a pasted `stitch::require` failed where the original run passed: your edit changed the
path. Literal arguments are constants to the compiler, unlike fuzzer inputs. If an edit makes a crash
appear or vanish for no visible reason, look for undefined behavior on a constant (e.g. `(int64_t)1e308`).
Keep such values opaque, e.g. `volatile double d = 1e308;`. The generated file already does this via
`stitch::opaque`.

Limitation: when a bug reads garbage, such as stack slots past an array or a wild pointer, the result depends
on memory layout. The pasted code has a different stack layout, so such crashes can show up differently
or not at all (about 1 in 150 crashing corpus programs in testing). `stitch write` reports this as NO CRASH
or DIFFERENT. Fall back to `stitch repro`, whose bundle runs the harness program itself. For an oracle failure ("check failed: X"), end with `assert(cond && "X")`; `try` accepts an abort
that prints the same message. Leak detection is off unless the expected crash is a leak.

## 6. Versions and local code

- `stitch build --rev <tag|branch|commit>` builds another revision (cached per commit).
- `[source] path = "../checkout"` fuzzes local code as-is, uncommitted changes included. If that path is a
  git checkout, `stitch bisect` walks its history.
- Reproducers pin the exact commit (or bundle a snapshot of local sources).

## Files

- `.stitch/build/` - `fuzzer`, `repro`, `manifest.json` (what the engine sees: endpoints, inputs, params)
- `.stitch/campaigns/<name>/` - `w*/corpus`, `w*/crashes`, `w*/stats.json`, `triage.json`
- `.stitch/repros/` - reproducers; `.stitch/bisect/` - per-commit builds and results
- logs of image builds: `~/.cache/stitch/logs/`
