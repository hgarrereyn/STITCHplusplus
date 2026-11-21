// cJSON_Utils: JSON pointers, patches, merge patches, sorting.
#include "../objects.hpp"

New<Item> get_pointer(Borrow<Json> root, Str<128, JsonPointer> ptr, Bool case_sensitive) {
    cJSON* it = case_sensitive ? cJSONUtils_GetPointerCaseSensitive(root, ptr.c_str()) : cJSONUtils_GetPointer(root, ptr.c_str());
    stitch::require(it, "pointer does not resolve");
    return cj::view(root, it, nullptr, true);  // parent unknown; pointer may cross references
}
STITCH_ENDPOINT(get_pointer);

void find_pointer(Borrow<Item> it) {
    cJSON* v = cj::live_item(it);
    cJSON** root = stitch::get_obj<Json>(it, "root");
    char* p = cJSONUtils_FindPointerFromObjectTo(*root, v);
    stitch::require(p, "not found");
    if (!cj::has_lossy(*root))  // duplicate member names resolve to the first one
        stitch::check(cJSONUtils_GetPointerCaseSensitive(*root, p) == v, "found pointer resolves to the item");
    cJSON_free(p);
}
STITCH_ENDPOINT(find_pointer);

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
STITCH_ENDPOINT(generate_patches);

void apply_patches(Borrow<Json> target, Borrow<Json> patches, Bool case_sensitive) {
    cj::require_unreferenced(target);
    int rc = case_sensitive ? cJSONUtils_ApplyPatchesCaseSensitive(target, patches) : cJSONUtils_ApplyPatches(target, patches);
    cj::touch(target);
    stitch::require(rc == 0, "patch rejected");
}
STITCH_ENDPOINT(apply_patches);

void add_patch_to_array(Borrow<Json> patches, Range<0, 6> op, Str<128, JsonPointer> path, Borrow<Json> value) {
    cj::require_unreferenced(patches);
    static const char* ops[] = {"add", "remove", "replace", "move", "copy", "test", "bogus"};
    stitch::require(cJSON_IsArray(patches), "not an array");
    cJSONUtils_AddPatchToArray(patches, ops[(int)op], path.c_str(), value);
    cj::touch(patches);
}
STITCH_ENDPOINT(add_patch_to_array);

// MergePatch consumes the target (it may delete it and return a new root).
New<Json> merge_patch(Borrow<Json> target, Borrow<Json> patch, Bool case_sensitive) {
    cj::require_unreferenced(target);
    cJSON* r = case_sensitive ? cJSONUtils_MergePatchCaseSensitive(target, patch) : cJSONUtils_MergePatch(target, patch);
    target.consume();
    stitch::require(r, "merge failed");
    return r;
}
STITCH_ENDPOINT(merge_patch);

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
STITCH_ENDPOINT(generate_merge_patch);

void sort_object(Borrow<Json> object, Bool case_sensitive) {
    cj::require_unreferenced(object);
    if (case_sensitive) cJSONUtils_SortObjectCaseSensitive(object);
    else cJSONUtils_SortObject(object);
    cj::touch(object);
}
STITCH_ENDPOINT(sort_object);
