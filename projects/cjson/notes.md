# cJSON (DaveGamble/cJSON @ c859b25)

65 endpoints in `parse/`, `build/`, `tree/`, `query/`, `utils/`. Object types: `Json` (an owning
value), `Item` (a view into a tree, invalidated when its root changes), and `JRef` (a reference
node). Decoders: `JsonText`, `JsonKey`, `JsonPointer`. Oracles: print→parse round trip, deep
duplicate compares equal, generated patches apply and reproduce the target, merge-patch round trip,
and found pointers resolve back to the item.

## Library bugs (still present at upstream HEAD 6d9f244, September 2026)

1. **`cJSONUtils_SortObject` / `GeneratePatches` / `GenerateMergePatch` break the `child->prev`
   tail invariant.** `sort_list` (cJSON_Utils.c) relinks members but never sets `head->prev` to the
   new tail. cJSON's append functions use `array->child->prev` as the tail, so a later
   `cJSON_AddItemToArray` / `AddItemToObject` links the new item after the wrong node. The rest of the
   list is silently dropped (data loss + leak). If the stale `prev` is a node that was already
   detached, the new item isn't reachable at all.
   Buckets: "array grows by one", "added key is found". **Escalates to memory corruption:**
   `GeneratePatches(from, to)` followed by `ApplyPatches(from, …)` removes a member and then appends
   after the stale (already freed) tail. That's a heap-use-after-free WRITE in `suffix_object`
   (cJSON.c:1986). It reproduces at HEAD.
   Reproducer: `.stitch/repros/cjson-c1/` (verified with `run.sh`).
   Minimal sequence: `create_int_array(2 ints)`, `sort_object`, `detach_item_from_array`, `add_item_to_array`.
2. **`decode_pointer_inplace` mis-decodes RFC 6901 escapes.** For `~1` it writes `'/'` to
   `decoded_string[1]` instead of `[0]`, and it never shifts the characters that follow. As a result
   `cJSONUtils_ApplyPatches` cannot `remove` / `move` members whose names contain `/` or `~`, even
   with patches that `cJSONUtils_GeneratePatches` produced itself (`{"a/b":null}` → `{}`: the patch
   is `remove /a~1b`, and apply fails with error 13).
   Buckets: "generated patches apply", "from + patches == to".
   Reproducer: `.stitch/repros/cjson-c2/`.

3. **`overwrite_item` (cJSON_Utils.c:784) frees keys it does not own.** It calls
   `cJSON_free(root->string)` without checking `cJSON_StringIsConst`. cJSON core checks that flag
   everywhere (cJSON.c:268, 2093, 2420). Keys added with `cJSON_AddItemToObjectCS` are borrowed, and
   `cJSON_Duplicate` shares them. Applying a patch that replaces the root of such a value frees the
   borrowed key: a heap-use-after-free READ later (`get_object_item` → `strcmp`), or `free()` of a
   string literal in the typical CS usage. It reproduces at HEAD. The same function also frees
   `valuestring` without checking `cJSON_IsReference`.

Two SEGV buckets with NULL member names (`FindPointerFromObjectTo`, `GenerateMergePatch`) came from
harness misuse and are gone after the container-type fixes. They don't reproduce at HEAD either.

## Harness bugs found and fixed through triage

- **Detaching from a reference node** made a node that shares children with the referenced value
  look like an owning value (heap-use-after-free at drop). Detached references are now freed and
  bail; views below a reference are read-only.
- **Array APIs on objects / object APIs on arrays** created members without names. cJSON_Utils
  dereferences the NULL names (SEGV in `generate_merge_patch`, `FindPointerFromObjectTo`).
  Stubs now require the right container type.
- **`cJSON_CreateObjectReference` takes the container's first member** (cJSON's own tests), not
  the container itself.
- **`GeneratePatches` / `GenerateMergePatch` sort their inputs in place** (documented): stubs now
  bump the inputs' version so stale item views bail.
- **Oracle exclusions:** duplicate member names, unnamed members, `cJSON_Invalid` (a root removed by
  a patch), raw values, non-finite numbers.

## Campaign notes

- Key lookups originally missed about 97% of the time; picking an existing key with `Src<16>`
  fixed it.
- `refs`/`ver` counters use `stitch::get(obj, key, 0)`, so a missing key isn't reported.
