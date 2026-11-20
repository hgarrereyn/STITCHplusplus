# cjson: heap-use-after-free WRITE in suffix_object

| | |
|---|---|
| Sanitizer | `heap-use-after-free WRITE` |
| Location | `suffix_object cJSON.c:1986:16`, `add_item_to_array cJSON.c:2037:13`, `add_item_to_object cJSON.c:2101:12` |
| Library source | [https://github.com/DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) @ `c859b25da029` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `sort-uaf` |

## Standalone reproducer

`standalone/min.c` (9 lines of code) needs only the library: no harness, no STITCH runtime.

```c
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    cJSON *from = cJSON_Parse("{\"a\":1,\"c\":2,\"b\":3}");
    cJSON *to = cJSON_Parse("{\"a\":1,\"c\":2,\"d\":4}");
    /* sorts `from` in place: a,b,c, but the head's prev (the tail pointer) still points to "b" */
    cJSON *patches = cJSONUtils_GeneratePatchesCaseSensitive(from, to);
    /* [remove /b, add /d]: removing "b" frees it, adding "d" appends after the stale tail */
    cJSONUtils_ApplyPatches(from, patches);
    return 0;
}
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with AddressSanitizer, runs it). Build line: `clang $(stitch-flags --no-cov) -I. -I/src min.c /src/build/libcjson_utils.a /src/build/libcjson.a -lm -lm -lpthread -o /tmp/repro`

`standalone/generated.cpp` (235 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (4 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse_with_length(buf=Bytes[3476]"{\"web-app\": {\n  \"servlet\": [  \n    {\n      \"servlet-oame\": \"cofaxCDS\",\n      \"servlet-class\": \"org.cofax.cds.CDSServlet\",\n      \"init-param\": {\n        \"configG"...)
#1 parse(text=Str[3372]"{\"web-app\": {\n  \"servlet\": [  \n    {\n      \"servlet-name\": \"cofaxCDS\",\n      \"servle44444444444444t-class\": \"org.cofax.cds.CDSServlet\",\n      \"init-param\": {\n  "...)
#2 generate_patches(from=o0.0, to=o1.0, case_sensitive=true)
#3 apply_patches(target=o0.0, patches=o2.0, case_sensitive=false)
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

`apply_patches` · harness/utils/utils.cpp

```cpp
void apply_patches(Borrow<Json> target, Borrow<Json> patches, Bool case_sensitive) {
    cj::require_unreferenced(target);
    int rc = case_sensitive ? cJSONUtils_ApplyPatchesCaseSensitive(target, patches) : cJSONUtils_ApplyPatches(target, patches);
    cj::touch(target);
    stitch::require(rc == 0, "patch rejected");
}
```

## Sanitizer report

```
==7==ERROR: AddressSanitizer: heap-use-after-free on address 0x506000004640 at pc 0xaaaaca11f6e4 bp 0xffffeb4f0570 sp 0xffffeb4f0568
WRITE of size 8 at 0x506000004640 thread T0
    #0 0xaaaaca11f6e0 in suffix_object /src/cJSON.c:1986:16
    #1 0xaaaaca11f6e0 in add_item_to_array /src/cJSON.c:2037:13
    #2 0xaaaaca11f6e0 in add_item_to_object /src/cJSON.c:2101:12
    #3 0xaaaaca11f6e0 in cJSON_AddItemToObject /src/cJSON.c:2106:12
    #4 0xaaaaca116ae0 in apply_patch /src/cJSON_Utils.c:1015:9
    #5 0xaaaaca115dec in cJSONUtils_ApplyPatches /src/cJSON_Utils.c:1056:18
    #6 0xaaaaca0dabcc in apply_patches(stitch::Borrow<Json>, stitch::Borrow<Json>, stitch::Bool) harness/utils/utils.cpp:43:87

0x506000004640 is located 0 bytes inside of 64-byte region [0x506000004640,0x506000004680)
freed by thread T0 here:
    #0 0xaaaaca0372e8 in free (.stitch/build/repro+0xe72e8) (BuildId: af61403dab2371baa9dce935e760a2c27cda663a)
    #1 0xaaaaca11aab0 in cJSON_Delete /src/cJSON.c:273:9
    #2 0xaaaaca11624c in apply_patch /src/cJSON_Utils.c:896:9
    #3 0xaaaaca115dec in cJSONUtils_ApplyPatches /src/cJSON_Utils.c:1056:18
    #4 0xaaaaca0dabcc in apply_patches(stitch::Borrow<Json>, stitch::Borrow<Json>, stitch::Bool) harness/utils/utils.cpp:43:87

previously allocated by thread T0 here:
    #0 0xaaaaca037584 in malloc (.stitch/build/repro+0xe7584) (BuildId: af61403dab2371baa9dce935e760a2c27cda663a)
    #1 0xaaaaca127d74 in cJSON_New_Item /src/cJSON.c:243:27
    #2 0xaaaaca127d74 in parse_object /src/cJSON.c:1688:27
    #3 0xaaaaca11bf44 in parse_value /src/cJSON.c:1411:16
    #4 0xaaaaca1282b4 in parse_object /src/cJSON.c:1734:14
    #5 0xaaaaca11bf44 in parse_value /src/cJSON.c:1411:16
    #6 0xaaaaca11b334 in cJSON_ParseWithLengthOpts /src/cJSON.c:1167:10
    #7 0xaaaaca09c2dc in parse_with_length harness/parse/parse.cpp:16:16
    #8 0xaaaaca09c2dc in void stitch::detail::Endpoint<&parse_with_length(stitch::Bytes<4096ul, JsonText>)>::call<0ul>(stitch::detail::Ctx&, std::integer_sequence<unsigned long, 0ul>) /stitch/runtime/include/stitch.h:779:19
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH toolchain (base image Dockerfile, runtime, compiler wrappers); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug as a plain C/C++ program (`./run.sh --standalone`), if one was written

