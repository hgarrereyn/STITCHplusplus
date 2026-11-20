# cjson: check failed: array grows by one in add_item_to_array

| | |
|---|---|
| Sanitizer | `check failed: array grows by one` |
| Location | `add_item_to_array array.cpp:16:5` |
| Library source | [https://github.com/DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) @ `c859b25da029` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `sort-dataloss` |

## Standalone reproducer

`standalone/min.c` (13 lines of code) needs only the library: no harness, no STITCH runtime.

```c
#include <assert.h>
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    int nums[] = {1, 2};
    cJSON *array = cJSON_CreateIntArray(nums, 2);
    /* sorting an array (members have no names) relinks it but leaves the tail pointer
       (array->child->prev) pointing at the old last element */
    cJSONUtils_SortObjectCaseSensitive(array);
    cJSON *item = cJSON_DetachItemFromArray(array, 0);
    cJSON_AddItemToArray(array, item); /* appended after the stale tail: lost */
    assert(cJSON_GetArraySize(array) == 2 && "array grows by one");
    return 0;
}
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with AddressSanitizer, runs it). Build line: `clang $(stitch-flags --no-cov) -I. -I/src min.c /src/build/libcjson_utils.a /src/build/libcjson.a -lm -lm -lpthread -o /tmp/repro`

`standalone/generated.cpp` (90 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (4 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 create_int_array(data=Bytes[8]"\xa1\xaf\x81\x02\xb4\x91uO")
#1 sort_object(object=o0.0, case_sensitive=true)
#2 detach_item_from_array(array=o0.0, s=Src[0])
#3 add_item_to_array(array=o0.0, item=o2.0)
```

## Stubs used

What each call does (from `project/harness/`):

`create_int_array` · harness/build/create.cpp

```cpp
New<Json> create_int_array(Bytes<256> data) {
    std::vector<int> v(data.size() / sizeof(int));
    memcpy(v.data(), data.data(), v.size() * sizeof(int));
    cJSON* a = cJSON_CreateIntArray(v.data(), (int)v.size());
    stitch::require(a, "create_int_array failed");
    stitch::check(cJSON_GetArraySize(a) == (int)v.size(), "int array has count items");
    return a;
}
```

`sort_object` · harness/utils/utils.cpp

```cpp
void sort_object(Borrow<Json> object, Bool case_sensitive) {
    cj::require_unreferenced(object);
    if (case_sensitive) cJSONUtils_SortObjectCaseSensitive(object);
    else cJSONUtils_SortObject(object);
    cj::touch(object);
}
```

`detach_item_from_array` · harness/tree/array.cpp

```cpp
New<Json> detach_item_from_array(Borrow<Json> array, Src<16> s) {
    cj::require_unreferenced(array);
    int idx = cj::pick_index(*s, cJSON_GetArraySize(array));
    cJSON* d = cJSON_DetachItemFromArray(array, idx);
    stitch::require(d, "no item at index");
    cj::touch(array);
    cj::owning(d);
    stitch::check(d->next == nullptr && d->prev == nullptr, "detached item is unlinked");
    return d;
}
```

`add_item_to_array` · harness/tree/array.cpp

```cpp
void add_item_to_array(Borrow<Json> array, Borrow<Json> item) {
    stitch::require(cJSON_IsArray(array), "not an array");  // unnamed members would corrupt an object
    cj::require_unreferenced(array);
    cj::require_unreferenced(item);
    int before = cJSON_GetArraySize(array);
    adopt(array, item, cJSON_AddItemToArray(array, item));
    stitch::check(cJSON_GetArraySize(array) == before + 1, "array grows by one");
}
```

## Sanitizer report

```
[stitch] CHECK FAILED: array grows by one (node 3)
AddressSanitizer:DEADLYSIGNAL
=================================================================
==7==ERROR: AddressSanitizer: ABRT on unknown address 0x000000000007 (pc 0xffffbbec74b8 bp 0xffffd204cce0 sp 0xffffd204ccd0 T0)
    #3 0xaaaad96cb528 in stitch::check(bool, char const*) /stitch/runtime/src/runtime.cpp:356:5
    #4 0xaaaad96969bc in add_item_to_array(stitch::Borrow<Json>, stitch::Borrow<Json>) harness/tree/array.cpp:16:5

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH toolchain (base image Dockerfile, runtime, compiler wrappers); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug as a plain C/C++ program (`./run.sh --standalone`), if one was written

