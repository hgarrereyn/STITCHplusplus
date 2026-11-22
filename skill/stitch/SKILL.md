---
name: stitch
description: Fuzz the API of a C, C++, Python, JavaScript, Java, Go, Rust, Ruby or Swift library with STITCH++. Covers the Dockerfile that builds the library, harness stubs, fuzzing campaigns, the health report, crash triage, coverage and reproducers. Use when asked to fuzz a library, write or fix a STITCH++ harness, or triage STITCH++ crashes.
---

# STITCH++

STITCH++ fuzzes a library through its API. You write **stubs**: small functions that each make
one API call. The fuzzer chains stubs into **programs** (sequences of calls), generates their
arguments, and reports crashes, uncaught exceptions and failed checks. Everything builds and runs
in Docker.

Supported languages: C/C++, Python, JavaScript, Java, Go, Rust, Ruby and Swift. The commands and
the workflow are the same for all of them. The stub syntax, the `Dockerfile` and `stitch.toml`
differ: **read your language's page before writing stubs**:

```sh
stitch guide cpp          # or python, javascript, java, go, rust, ruby, swift
stitch guide reproducers  # turning a crash into a short standalone file
```

The examples on this page are C++. Every language page shows the same features in its own syntax.

## Concepts

A stub's signature tells the fuzzer what the stub needs and what it produces:

```cpp
New<Json> new_object() { ... }                                          // produces a Json object
New<Json> parse(Str<4096, JsonText> text) { ... }                       // produces one from a string
void add_item(Borrow<Json> obj, Str<64> key, Take<Json> item) { ... }   // uses one, consumes another
void roundtrip(Borrow<Json> j) { ... }                                  // uses one
```

- **Object**: a value one stub produces and later stubs use: a parsed document, a handle, a
  connection. A stub can **borrow** an object (use it; it stays available), **take** it (consume
  it), or return a **new** one.
- **Fuzzed parameter**: every other parameter. Numbers, ranges, choices from a list, strings and
  byte buffers. The fuzzer picks the values.
- **Program**: a list of calls, where object inputs refer to the outputs of earlier calls.
  `stitch exec --trace` prints programs like this:
  ```
  [stitch] #0 new_object()
  [stitch]     -> ok produced o0.0
  [stitch] #1 parse(text=Str[4,decoded]"null")
  [stitch]     -> ok produced o1.0
  [stitch] #2 add_item(obj=o0.0, key=Str[3]"2/l", item=take o1.0)
  [stitch]     -> ok
  [stitch] #3 roundtrip(j=o0.0)
  [stitch]     -> ok
  ```
- **Bail**: the call failed in an expected way (invalid input, unmet precondition). It is not a
  bug. The call's outputs are not created; the rest of the program runs.
- **Bug**: a crash, a sanitizer report, an exception the stub did not declare as expected, a
  failed `check` (a property you assert), or a program slower than the timeout.
- **Data type**: a tag on a string or byte parameter that gives it sample inputs, dictionary
  tokens, or a decoder that generates well-formed values.
- **Attribute**: state the harness stores on an object, for facts the library cannot tell you.

## Workflow

```sh
stitch setup --lang cpp                  # once: check Docker, get the language image
stitch init cjson --git https://github.com/DaveGamble/cJSON --rev v1.7.18
cd cjson                                 # edit Dockerfile, stitch.toml, harness/
stitch build                             # build the library and the stubs
stitch probe --secs 60                   # short run + report of stubs that fail
stitch fuzz --jobs 4                     # campaign in the background
stitch status                            # progress and the same report
stitch crashes                           # unique crashes: c1, c2, ...
stitch exec c1 --trace                   # the calls, their arguments, the report
stitch write c1                          # the crash as one standalone source file
```

Every command accepts `--json`. `stitch setup` needs Docker with buildx.

## 1. Create a project

```sh
stitch init cjson --git https://github.com/DaveGamble/cJSON --rev v1.7.18          # C/C++
stitch init tomlkit --lang python --git https://github.com/python-poetry/tomlkit --rev 0.13.2
stitch init mylib --lang rust --path ../mylib                                       # local checkout
```

This creates:

- `stitch.toml`: the library source, build settings and fuzzing options
- `Dockerfile`: builds or installs the library
- `harness/`: your stubs, with a commented example

## 2. Build the library

The `Dockerfile` starts from the STITCH++ image for your language. The library source arrives as
the `source` build context and is copied to `/src`:

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN cmake -B build -DBUILD_SHARED_LIBS=OFF && cmake --build build -j$(nproc)
```

Your language's page shows what to install and how `stitch.toml` points at it.

```sh
stitch build --image-only   # build only the library image
stitch shell                # shell in the image, to try build commands (project at /project)
```

When a build fails, the error and the log path are printed. Logs are in `~/.cache/stitch/logs/`.

## 3. Write stubs

Start with the calls that create objects (constructors, parsers), then the calls that use and
change them, then checks of properties that must hold. Read the library's tests to learn the API
contract: docs often leave out preconditions. Your language's page starts with a complete example
harness to copy from.

### Bail, require, check

| Call | Meaning |
|---|---|
| `bail("reason")` | stop this call; not a bug |
| `require(cond, "reason")` | bail unless `cond` |
| `check(cond, "what")` | a bug if `cond` is false: "check failed: what" |

In languages with exceptions, list the exceptions that mean "invalid input" on the stub
(`bails_on`, `bailsOn`); any other exception is a bug.

Write bail reasons as short fixed strings: `"parse error"`, not `"parse error at line 17"`. The
report counts bails by reason.

### Rules for good stubs

**One producer per kind of object.** Write `new_number` and `new_string`, not
`new_value(kind)`. The fuzzer learns which producer each consumer needs.

**Constrain parameters instead of rejecting values.** A stub that bails on most values wastes
runs:

```cpp
void set_level(Borrow<Enc> e, I32 level) { stitch::require(level >= 0 && level <= 9, "bad level"); ... }  // bad
void set_level(Borrow<Enc> e, Range<0, 9> level) { ... }                                                // good
void set_mode(Borrow<Enc> e, OneOf<MODE_FAST, MODE_SMALL> mode) { ... }                                 // good
```

Keep a few invalid values reachable when the library must reject them: a separate stub with a
plain `I32` tests the error path.

**Pick existing keys and indexes.** A random key almost never exists. Take a `Src` parameter and
choose from what is there. Pick from a stable order (sort map keys first), so a program replays
the same way:

```cpp
void delete_member(Borrow<Json> obj, Src<16> s) {
    int n = cJSON_GetArraySize(obj);
    stitch::require(cJSON_IsObject(obj) && n > 0, "no members");
    cJSON_DeleteItemFromArray(obj, (int)s->choice(n));
}
```

**Return typed optional outputs when the result type depends on the input.** A parser that can
return a table or a function returns both as optional outputs, so a stub that needs a table
takes a table:

```cpp
std::tuple<New<Value>, std::optional<New<Table>>, std::optional<New<Func>>> eval(Str<256> code);
```

**Respect ownership.** A `Take<T>` input is yours to free or hand to the library, on every path:

```cpp
void add_item(Borrow<Json> obj, Str<64> key, Take<Json> item) {
    if (!cJSON_AddItemToObject(obj, key.c_str(), item)) {
        cJSON_Delete(item);          // still ours: free it before bailing
        stitch::bail("add failed");
    }
}
```

If the library takes ownership only on success, borrow the input and mark it consumed after the
call succeeds (`item.consume()` in C++).

**Use attributes for state the library cannot tell you.** Ask the library first
(`cJSON_IsArray(x)`); store an attribute only when there is no API for it:

```cpp
void make_reference(Borrow<Json> target) {
    ...
    stitch::set(target, "refs", stitch::get(target, "refs", 0) + 1);
}
void delete_json(Take<Json> j) {
    stitch::require(stitch::get(j, "refs", 0) == 0, "still referenced");   // would be a use-after-free
    cJSON_Delete(j);
}
```

- A stub that copies an object copies its attributes too.
- A stub that changes an object (sorts it, reallocates it) updates the attributes that depend on it.
- `get(obj, "k", 0)` when a missing key is normal; `get(obj, "k")` when it must be set (the report
  lists missing reads).

**Write `check` only for properties that always hold.** Round trips (print then parse),
idempotence (`normalize(normalize(x)) == normalize(x)`), agreement between two APIs. Exclude
known edge cases (NaN, duplicate keys) with an `if` before the check.

**Do not add guards the library does not need.** Each guard hides behavior the fuzzer could
have tested. Add one only when the report or a crash shows that the stub breaks the API contract.

**Keep stubs compiling across library versions**, so `stitch exec --rev` and `stitch bisect`
work.

**Organize by component**: `harness/parse/`, `harness/tree/`. Folder names group stubs in
reports and in `stitch probe 'tree/*'`. In C/C++, avoid libc names for stubs (`read`, `open`).

## 4. Data types

Tag a string or byte parameter with a data type, then create `harness/data/<Name>/` with any of:

```
harness/data/JsonText/
  seeds/          sample inputs, one per file (copy the library's test files)
  json.dict       tokens, one per line: "true"  "null"  "\x00\x01"
```

and optionally a **decoder**, written in your harness language, that builds a value from fuzzer
choices (`s.choice(n)`, `s.str(max)`, `s.token()`, ...). Mutations then change the choices, so
values stay well formed. A JSON decoder:

```cpp
STITCH_DECODER(JsonText, [](stitch::Source& s) {
    std::string o = "{";
    while (s.more()) o += "\"" + s.token() + "\":" + std::to_string(s.range(-1000, 1000)) + ",";
    return o + "}";
});
New<Json> parse(Str<4096, JsonText> text);      // the tagged parameter
```

`stitch sample JsonText -n 5` prints values the decoder makes.

A data type with only a `.dict` file starts from random bytes, and mutations mix the tokens in.
For structured input, add seeds or a decoder. `stitch check` warns about data types with neither.

Decoder tips:

- Draw literal text with `s.str(max)` and dictionary tokens with `s.token()`.
- Compute lengths, counts and checksums from the data; do not draw them.
- Loop with `while (s.more())` so the fuzzer can add and remove elements.
- A decoder can return a structured value (a struct, a list of records) instead of text. Take it
  as a `Val` parameter. When a structure does not depend on object state, prefer this to building
  it from a `Src` inside the stub: the fuzzer then mutates it element by element.

## 5. Probe and fuzz

```sh
stitch build            # prints compile errors with file and line
stitch check            # static checks: unreachable stubs, attributes read but never set
stitch probe --secs 60  # short run (one worker) and a report of stubs with problems
stitch probe 'parse,tree/*' --secs 30   # only some stubs
stitch fuzz --jobs 4    # campaign 'main' in the background; resumes its corpus
stitch fuzz --fresh     # start the corpus over
stitch fuzz --secs 900 --foreground     # wait, then print the final stats
stitch status           # progress and the report
stitch stop
```

A probe report:

```
probe: 20s, 42.8k execs (2.1k/s), 733/3298 edges, corpus 461, calls bailing 13% (programs with a bail 66%), 0 crash/timeout programs saved
never succeeds (1): the fuzzer stops picking these
  item_string  [600 calls] "root is gone" x600; read missing attr 'root'
outcome depends on the previous call on an input (engine learns this; fix the stub if unexpected):
  add_item.obj: ok 2% after parse vs 100% after new_object
  delete_key.obj: ok 0% after parse vs 79% after print_opts
often skipped because an input object was missing (1): its producer bailed or returned no object
  item_string  skipped 13797x, ran 600x
```

Here `item_string` reads an attribute `root` that its producer never managed to set (the
producer linked to another output of the same call, which does not work). `add_item.obj` only
succeeds on objects, and `parse` often returns arrays or numbers: that is expected.

| Report line | What to do |
|---|---|
| never succeeds | The stub always bails or always crashes. Read its reasons: a missing producer, an attribute nobody sets (`read missing attr 'x'`), or something missing in the environment (a server, a file). Fix the stub or add a producer. |
| mostly bails | Use `Range`/`OneOf` instead of rejecting values. Bails from parse errors on fuzzed input are fine. |
| outcome depends on the previous call | Usually normal: the fuzzer learns these pairs. Investigate only if the stub should accept both. |
| often skipped | The producer of one of its inputs usually fails. Fix the producer. |
| never called | No producer makes one of its inputs, or the run was too short. |
| calls bailing above 50% | Most work is wasted. Fix the stubs that bail most. |

Run `stitch probe` again after each change and compare.

## 6. Coverage

```sh
stitch cov                   # the latest campaign's corpus
stitch cov probe --crashes   # another campaign, crashing programs included
```

```
library:
  lines         2942 / 3375     87.2%
  functions      148 / 151      98.0%
  branches      1252 / 1514     82.7%

never called: 3 library functions
  cJSON_Version  src/cJSON.c:125
  cJSON_InitHooks  src/cJSON.c:210
  cJSON_IsBool  src/cJSON.c:2989
```

Languages whose coverage tool has no branch data print `branches -`. A never-called function
needs a stub. Open `.stitch/cov/<campaign>/html/index.html` to find
branches the corpus never takes. The same folder has `lcov.info`, `summary.json` (per file, with
uncovered functions) and `report.txt`.

## 7. Triage

```sh
stitch crashes             # unique crashes of the latest `stitch fuzz` campaign
stitch crashes probe       # of the probe
stitch exec c1 --trace     # every call with its arguments, then the bug report
stitch exec c1 --args      # the same, long arguments in full
stitch minimize c1         # fewer calls, smaller inputs (can take minutes); c1 then refers to the result
stitch exec c1 --rev HEAD  # does it still crash on the newest library code?
```

```
1 unique crash in 'probe':
  c1   x3     heap-use-after-free WRITE  in patch
         suffix_object cJSON.c:1948:16
         add_item_to_array cJSON.c:1999:13
         example (14 calls): .stitch/campaigns/probe/w0/crashes/41f9cd94c7dfe525.stp
```

Crashes are grouped by kind and innermost library frames. The stub named in the list comes from
one example program; other programs in the group may crash in another stub.

Crash ids: `c1` is in the latest campaign that is not `probe` (or in `probe` if there is no
other). Use `probe:c1` or `--campaign probe` for another campaign. Ids stay the same while a
campaign runs. After you change the harness, `stitch crashes` reruns the crashes with the new
build, so crashes caused by fixed stubs disappear.

Decide for each crash:

- **Harness bug**: the stubs broke the API contract (double free, wrong ownership, a missing init
  call, a precondition the library documents). Fix the stub, `stitch build`, and confirm that
  `stitch exec c1` no longer crashes.
- **Library bug**: the stubs followed the contract. Make a reproducer (below), and check whether
  it is fixed upstream with `stitch exec c1 --rev HEAD`.
- **Unsure**: read the library source at the top stack frames, and the library's tests and docs
  for that function.

A `timeout` crash is a program that ran longer than `timeout_ms`, named after the call that was
running. It is usually a real performance bug (a regex with catastrophic backtracking, a quadratic
loop), unless the stub itself loops for long.

## 8. Reproducers

```sh
stitch repro c1           # Docker reproducer folder (.stitch/repros/<project>-c1), verified from scratch
stitch write c1           # the crash as one source file in your harness language
stitch try min.c          # build and run an edited copy: SAME CRASH, NO CRASH, ...
stitch bisect c1          # the commit that introduced the bug (--good v1.0 if you know one)
stitch bisect c1 --fixed  # the commit that fixed it, if HEAD no longer crashes
```

`stitch write` turns the crashing program into one file in your harness language, with the
arguments filled in. Shrink a copy to plain library calls, running `stitch try` after every edit.
A finished reproducer:

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

Steps and pitfalls: `stitch guide reproducers`.

## Versions and local code

- `stitch build --rev v1.7.17` builds another version of the library (cached per commit).
- `[source] path = "../lib"` in `stitch.toml` fuzzes a local checkout, uncommitted changes
  included. If it is a git checkout, `stitch bisect` walks its history.

## Commands

| Command | Does |
|---|---|
| `stitch setup [--lang L]` | check Docker, get the language image |
| `stitch init DIR (--git URL --rev REV \| --path DIR) [--lang L]` | new project |
| `stitch build [--rev REV] [--image-only]` | build the library image and the harness |
| `stitch check` | build + static checks |
| `stitch probe [STUBS] [--secs N]` | short run + report (`STUBS`: `'parse,tree/*'`) |
| `stitch fuzz [--jobs N] [--secs N] [--name NAME] [--only STUBS] [--fresh] [--foreground]` | campaign, in the background by default |
| `stitch status [NAME]`, `stitch stop [NAME]` | campaign progress; stop it |
| `stitch crashes [NAME]` | unique crashes |
| `stitch exec ID [--trace\|--args] [--rev REV]` | run a crash or a `.stp` program file |
| `stitch minimize ID` | shrink a crashing program |
| `stitch cov [NAME\|FILES] [--crashes] [--rev REV]` | coverage of a corpus: lcov + HTML |
| `stitch repro ID` | Docker reproducer folder |
| `stitch write ID` | standalone source file of a crash |
| `stitch try FILE [ID] [--rev REV] [--show]` | build and run a standalone file, compare with the crash |
| `stitch bisect ID [--good REV] [--fixed]` | the commit that introduced (or fixed) a crash |
| `stitch sample TYPE [-n N]` | values a data type's decoder makes |
| `stitch shell` | shell in the project image |
| `stitch guide [TOPIC]` | this guide; topics: a language name, `reproducers` |

## Files

- `.stitch/build/manifest.json`: the stubs as the fuzzer sees them (inputs, outputs, parameters)
- `.stitch/campaigns/<name>/`: corpus, crashes, stats
- `.stitch/cov/<campaign>/`: `lcov.info`, `html/`, `summary.json`, `report.txt`
- `.stitch/repros/`, `.stitch/standalone/`, `.stitch/bisect/`: reproducers and bisect results
- `~/.cache/stitch/logs/`: image build logs
