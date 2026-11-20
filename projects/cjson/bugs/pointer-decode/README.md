# cjson: check failed: generated patches apply in generate_patches

| | |
|---|---|
| Sanitizer | `check failed: generated patches apply` |
| Location | `generate_patches utils.cpp:33:9`, `generate_patches (stub)` |
| Library source | [https://github.com/DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) @ `c859b25da029` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `pointer-decode` |

## Standalone reproducer

`standalone/min.c` (11 lines of code) needs only the library: no harness, no STITCH runtime.

```c
#include <assert.h>
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    cJSON *from = cJSON_Parse("{\"a/b\":null}");
    cJSON *to = cJSON_CreateObject();
    /* [{"op":"remove","path":"/a~1b"}] */
    cJSON *patches = cJSONUtils_GeneratePatchesCaseSensitive(from, to);
    /* decode_pointer_inplace mis-decodes the escaped "a~1b", so the key is not found */
    int rc = cJSONUtils_ApplyPatchesCaseSensitive(from, patches);
    assert(rc == 0 && "generated patches apply");
    return 0;
}
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with AddressSanitizer, runs it). Build line: `clang $(stitch-flags --no-cov) -I. -I/src min.c /src/build/libcjson_utils.a /src/build/libcjson.a -lm -lm -lpthread -o /tmp/repro`

`standalone/generated.cpp` (84 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (3 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse_with_length(buf=Bytes[16,decoded]"{\"@n/\" : null } ")
#1 parse(text=Str[3,decoded]"{} ")
#2 generate_patches(from=o0.0, to=o1.0, case_sensitive=true)
```

## Stubs used

What each call does (from `project/harness/`):

`parse_with_length` · harness/parse/parse.cpp

```cpp
New<Json> parse_with_length(Bytes<4096, JsonText> buf) {
    cJSON* j = cJSON_ParseWithLength((const char*)buf.data(), buf.size());
    stitch::require(j, "parse error");
    return j;
}
```

`parse` · harness/parse/parse.cpp

```cpp
New<Json> parse(Str<4096, JsonText> text) {
    cJSON* j = cJSON_Parse(text.c_str());
    if (!j) {
        const char* err = cJSON_GetErrorPtr();
        stitch::check(err == nullptr || (err >= text.c_str() && err <= text.c_str() + text.size()),
                      "error pointer inside the input");
        stitch::bail("parse error");
    }
    return j;
}
```

`generate_patches` · harness/utils/utils.cpp

```cpp
// Documented: GeneratePatches / GenerateMergePatch sort the members of `from` and `to` in place.
New<Json> generate_patches(Borrow<Json> from, Borrow<Json> to, Bool case_sensitive) {
    cj::require_unreferenced(from);
    cj::require_unreferenced(to);
    cj::touch(from);
    cj::touch(to);
    cJSON* patches = case_sensitive ? cJSONUtils_GeneratePatchesCaseSensitive(from, to) : cJSONUtils_GeneratePatches(from, to);
    stitch::require(patches, "generate failed");
    if (case_sensitive && !cj::has_lossy(*from) && !cj::has_lossy(*to)) {
        cJSON* copy = cJSON_Duplicate(from, 1);
        int rc = cJSONUtils_ApplyPatchesCaseSensitive(copy, patches);
        stitch::check(rc == 0, "generated patches apply");
        stitch::check(cJSON_Compare(copy, to, true), "from + patches == to");
        cJSON_Delete(copy);
    }
    return patches;
}
```

## Sanitizer report

```
[stitch] CHECK FAILED: generated patches apply (node 2)
AddressSanitizer:DEADLYSIGNAL
=================================================================
==7==ERROR: AddressSanitizer: ABRT on unknown address 0x000000000007 (pc 0xffff91ec74b8 bp 0xffffe4262c80 sp 0xffffe4262c70 T0)
    #3 0xaaaae5c0c654 in stitch::check(bool, char const*) /stitch/runtime/src/runtime.cpp:361:5
    #4 0xaaaae5bfac60 in generate_patches(stitch::Borrow<Json>, stitch::Borrow<Json>, stitch::Bool) harness/utils/utils.cpp:33:9
    #5 0xaaaae5c01f7c in void stitch::detail::Endpoint<&generate_patches(stitch::Borrow<Json>, stitch::Borrow<Json>, stitch::Bool)>::call<0ul, 1ul, 2ul>(stitch::detail::Ctx&, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul>) /stitch/runtime/include/stitch.h:779:19

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH toolchain (base image Dockerfile, runtime, compiler wrappers); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug as a plain C/C++ program (`./run.sh --standalone`), if one was written

