# cjson: heap-use-after-free READ in get_object_item

| | |
|---|---|
| Sanitizer | `heap-use-after-free READ` |
| Location | `get_object_item cJSON.c:1948:83`, `replace_item_in_object cJSON.c:2432:48`, `replace_item_in_object object.cpp:32:32` |
| Library source | [https://github.com/DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) @ `c859b25da029` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `overwrite-const-key` |

## Standalone reproducer

`standalone/min.c` (14 lines of code) needs only the library: no harness, no STITCH runtime.

```c
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    cJSON *object = cJSON_CreateObject();
    char *key = strdup("k");
    cJSON_AddItemToObjectCS(object, key, cJSON_CreateFalse()); /* key is borrowed (cJSON_StringIsConst) */

    /* the duplicate shares the borrowed key pointer */
    cJSON *copy = cJSON_Duplicate(cJSON_GetObjectItem(object, "k"), 1);

    /* replacing the root: overwrite_item frees copy->string without checking cJSON_StringIsConst */
    cJSON *patches = cJSON_Parse("[{\"op\":\"replace\",\"path\":\"\",\"value\":1}]");
    cJSONUtils_ApplyPatchesCaseSensitive(copy, patches);

    /* `object` still names its member with the freed key */
    cJSON_ReplaceItemInObjectCaseSensitive(object, "k", cJSON_CreateNull());
    return 0;
}
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with AddressSanitizer, runs it). Build line: `clang $(stitch-flags --no-cov) -I. -I/src min.c /src/build/libcjson_utils.a /src/build/libcjson.a -lm -lm -lpthread -o /tmp/repro`

`standalone/generated.cpp` (211 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (11 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse_with_length(buf=Bytes[246]"{\n\x09\x09\"Image\": {\n\x09\x09\x09\"Width\":  800,\n\x09\x09\x09\"Height\": 600,\n\x09\x09\x09\"Title\":  \"View from 15th Floor\",\n\x09\x09\x09\"Thumbnail\": {\n\x09\x09\x09\x09\"Url\":    \"http:/*www.example.com/image/481989943\""...)
#1 create_false()
#2 add_item_to_object_cs(object=o0.0, key=Str[0,decoded]"", item=o1.0)
#3 get_pointer(root=o0.0, ptr=Str[0,decoded]"", case_sensitive=true)
#4 get_child_item(it=o3.0, s=Src[2]6d0f)
#5 duplicate_item(it=o4.0, recurse=true)
#6 generate_merge_patch(from=o0.0, to=o5.0, case_sensitive=false)
#7 create_string_reference(s=Str[0,decoded]"")
#8 duplicate_reference(r=o7.0)
#9 generate_patches(from=o5.0, to=o8.0, case_sensitive=true)
#10 replace_item_in_object(object=o0.0, fresh=Str[0,decoded]"", s=Src[1]e0, item=o6.0, case_sensitive=true)
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

`create_false` · harness/build/create.cpp

```cpp
New<Json> create_false() { return cJSON_CreateFalse(); }
```

`add_item_to_object_cs` · harness/tree/object.cpp

```cpp
// AddItemToObjectCS does not copy the key: it must outlive the item, so it is deliberately leaked.
void add_item_to_object_cs(Borrow<Json> object, Str<64, JsonKey> key, Borrow<Json> item) {
    stitch::require(cJSON_IsObject(object), "not an object");
    cj::require_unreferenced(object);
    cj::require_unreferenced(item);
    char* k = strdup(key.c_str());
    stitch::require(cJSON_AddItemToObjectCS(object, k, item), "add failed");
    item.consume();
    cj::touch(object);
}
```

`get_pointer` · harness/utils/utils.cpp

```cpp
New<Item> get_pointer(Borrow<Json> root, Str<128, JsonPointer> ptr, Bool case_sensitive) {
    cJSON* it = case_sensitive ? cJSONUtils_GetPointerCaseSensitive(root, ptr.c_str()) : cJSONUtils_GetPointer(root, ptr.c_str());
    stitch::require(it, "pointer does not resolve");
    return cj::view(root, it, nullptr, true);  // parent unknown; pointer may cross references
}
```

`get_child_item` · harness/query/query.cpp

```cpp
New<Item> get_child_item(Borrow<Item> it, Src<16> s) {
    cJSON* parent = cj::live_item(it);
    int idx = cj::pick_index(*s, cJSON_GetArraySize(parent));
    cJSON* c = cJSON_GetArrayItem(parent, idx);
    stitch::require(c, "no item at index");
    cJSON** root = stitch::get_obj<Json>(it, "root");
    return cj::view(*root, c, parent, stitch::get(it, "readonly", 0) != 0);
}
```

`duplicate_item` · harness/query/query.cpp

```cpp
New<Json> duplicate_item(Borrow<Item> it, Bool recurse) {
    cJSON* d = cJSON_Duplicate(cj::live_item(it), recurse);
    stitch::require(d, "duplicate failed");
    return d;
}
```

`generate_merge_patch` · harness/utils/utils.cpp

```cpp
New<Json> generate_merge_patch(Borrow<Json> from, Borrow<Json> to, Bool case_sensitive) {
    cj::require_unreferenced(from);
    cj::require_unreferenced(to);
    cj::touch(from);
    cj::touch(to);
    cJSON* p = case_sensitive ? cJSONUtils_GenerateMergePatchCaseSensitive(from, to) : cJSONUtils_GenerateMergePatch(from, to);
    stitch::require(p, "generate failed");
    if (case_sensitive && cJSON_IsObject(from) && cJSON_IsObject(to) && !cj::has_lossy(*from) && !cj::has_lossy(*to) &&
        !cj::has_null_value(*to)) {
        cJSON* merged = cJSONUtils_MergePatchCaseSensitive(cJSON_Duplicate(from, 1), p);
        stitch::check(merged && cJSON_Compare(merged, to, true), "from + merge patch == to");
        cJSON_Delete(merged);
    }
    return p;
}
```

`create_string_reference` · harness/tree/references.cpp

```cpp
New<JRef> create_string_reference(Str<256, JsonKey> s) {
    char* backing = strdup(s.c_str());
    cJSON* r = cJSON_CreateStringReference(backing);
    if (!r) {
        free(backing);
        stitch::bail("create reference failed");
    }
    return RefT{r, backing};
}
```

`duplicate_reference` · harness/tree/references.cpp

```cpp
New<Json> duplicate_reference(Borrow<JRef> r) {
    stitch::require(target_alive(r), "reference target is gone");
    cJSON* d = cJSON_Duplicate(r->ref, 1);
    stitch::require(d, "duplicate failed");
    return d;
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

`replace_item_in_object` · harness/tree/object.cpp

```cpp
void replace_item_in_object(Borrow<Json> object, Str<64, JsonKey> fresh, Src<16> s, Borrow<Json> item, Bool case_sensitive) {
    stitch::require(cJSON_IsObject(object), "not an object");
    cj::require_unreferenced(object);
    cj::require_unreferenced(item);
    std::string key = cj::pick_key(object, *s, fresh.c_str());
    bool ok = case_sensitive ? cJSON_ReplaceItemInObjectCaseSensitive(object, key.c_str(), item)
                             : cJSON_ReplaceItemInObject(object, key.c_str(), item);
    stitch::require(ok, "no such key");
    item.consume();
    cj::touch(object);
}
```

## Sanitizer report

```
==7==ERROR: AddressSanitizer: heap-use-after-free on address 0x502000005c10 at pc 0xaaaadbf4ecc8 bp 0xffffd2356f70 sp 0xffffd2356758
READ of size 1 at 0x502000005c10 thread T0
    #0 0xaaaadbf4ecc4 in strcmp (.stitch/build/repro+0x5ecc4) (BuildId: af61403dab2371baa9dce935e760a2c27cda663a)
    #1 0xaaaadc0be998 in get_object_item /src/cJSON.c:1948:83
    #2 0xaaaadc0c44fc in replace_item_in_object /src/cJSON.c:2432:48
    #3 0xaaaadc05ffa0 in replace_item_in_object(stitch::Borrow<Json>, stitch::Str<64ul, JsonKey>, stitch::Src<16ul>, stitch::Borrow<Json>, stitch::Bool) harness/tree/object.cpp:32:32
    #4 0xaaaadc0664e4 in void stitch::detail::Endpoint<&replace_item_in_object(stitch::Borrow<Json>, stitch::Str<64ul, JsonKey>, stitch::Src<16ul>, stitch::Borrow<Json>, stitch::Bool)>::call<0ul, 1ul, 2ul, 3ul, 4ul>(stitch::detail::Ctx&, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul>) /stitch/runtime/include/stitch.h:777:13

0x502000005c10 is located 0 bytes inside of 1-byte region [0x502000005c10,0x502000005c11)
freed by thread T0 here:
    #0 0xaaaadbfd72e8 in free (.stitch/build/repro+0xe72e8) (BuildId: af61403dab2371baa9dce935e760a2c27cda663a)
    #1 0xaaaadc0b914c in overwrite_item /src/cJSON_Utils.c:793:9
    #2 0xaaaadc0b64f8 in apply_patch /src/cJSON_Utils.c:869:13
    #3 0xaaaadc0b6bf8 in cJSONUtils_ApplyPatchesCaseSensitive /src/cJSON_Utils.c:1085:18
    #4 0xaaaadc079b10 in generate_patches(stitch::Borrow<Json>, stitch::Borrow<Json>, stitch::Bool) harness/utils/utils.cpp:32:18
    #5 0xaaaadc080e50 in void stitch::detail::Endpoint<&generate_patches(stitch::Borrow<Json>, stitch::Borrow<Json>, stitch::Bool)>::call<0ul, 1ul, 2ul>(stitch::detail::Ctx&, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul>) /stitch/runtime/include/stitch.h:779:19

previously allocated by thread T0 here:
    #0 0xaaaadbfbf170 in strdup (.stitch/build/repro+0xcf170) (BuildId: af61403dab2371baa9dce935e760a2c27cda663a)
    #1 0xaaaadc05efd4 in add_item_to_object_cs(stitch::Borrow<Json>, stitch::Str<64ul, JsonKey>, stitch::Borrow<Json>) harness/tree/object.cpp:20:15
    #2 0xaaaadc065b88 in void stitch::detail::Endpoint<&add_item_to_object_cs(stitch::Borrow<Json>, stitch::Str<64ul, JsonKey>, stitch::Borrow<Json>)>::call<0ul, 1ul, 2ul>(stitch::detail::Ctx&, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul>) /stitch/runtime/include/stitch.h:777:13
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH toolchain (base image Dockerfile, runtime, compiler wrappers); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug as a plain C/C++ program (`./run.sh --standalone`), if one was written

