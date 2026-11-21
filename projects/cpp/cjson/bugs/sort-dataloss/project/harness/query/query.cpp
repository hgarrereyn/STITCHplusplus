#include "../objects.hpp"

void inspect(Borrow<Json> v) {
    int n = cJSON_IsInvalid(v) + cJSON_IsFalse(v) + cJSON_IsTrue(v) + cJSON_IsNull(v) + cJSON_IsNumber(v) +
            cJSON_IsString(v) + cJSON_IsArray(v) + cJSON_IsObject(v) + cJSON_IsRaw(v);
    stitch::check(n == 1 || cJSON_IsBool(v) == 0 || n == 1, "exactly one type predicate holds");
    if (char* s = cJSON_GetStringValue(v)) (void)strlen(s);
    (void)cJSON_GetNumberValue(v);
    int size = cJSON_GetArraySize(v);
    int counted = 0;
    for (cJSON* c = v->child; c; c = c->next) counted++;
    stitch::check(size == counted, "array size matches children");
}
STITCH_ENDPOINT(inspect);

void compare(Borrow<Json> a, Borrow<Json> b, Bool case_sensitive) {
    bool ab = cJSON_Compare(a, b, case_sensitive);
    bool ba = cJSON_Compare(b, a, case_sensitive);
    if (!cj::has_lossy(*a) && !cj::has_lossy(*b)) stitch::check(ab == ba, "compare is symmetric");
}
STITCH_ENDPOINT(compare);

void has_object_item(Borrow<Json> v, Str<64, JsonKey> fresh, Src<16> s) {
    std::string key = cj::pick_key(v, *s, fresh.c_str());
    bool has = cJSON_HasObjectItem(v, key.c_str());
    stitch::check(has == (cJSON_GetObjectItem(v, key.c_str()) != nullptr), "has == get != NULL");
}
STITCH_ENDPOINT(has_object_item);

// ----- item views: look up nodes inside a tree --------------------------------------------
New<Item> get_array_item(Borrow<Json> root, Src<16> s) {
    int idx = cj::pick_index(*s, cJSON_GetArraySize(root));
    cJSON* it = cJSON_GetArrayItem(root, idx);
    stitch::require(it, "no item at index");
    return cj::view(root, it, *root);
}
STITCH_ENDPOINT(get_array_item);

New<Item> get_object_item(Borrow<Json> root, Str<64, JsonKey> fresh, Src<16> s, Bool case_sensitive) {
    std::string key = cj::pick_key(root, *s, fresh.c_str());
    cJSON* it = case_sensitive ? cJSON_GetObjectItemCaseSensitive(root, key.c_str()) : cJSON_GetObjectItem(root, key.c_str());
    stitch::require(it, "no such key");
    return cj::view(root, it, *root);
}
STITCH_ENDPOINT(get_object_item);

New<Item> get_child_item(Borrow<Item> it, Src<16> s) {
    cJSON* parent = cj::live_item(it);
    int idx = cj::pick_index(*s, cJSON_GetArraySize(parent));
    cJSON* c = cJSON_GetArrayItem(parent, idx);
    stitch::require(c, "no item at index");
    cJSON** root = stitch::get_obj<Json>(it, "root");
    return cj::view(*root, c, parent, stitch::get(it, "readonly", 0) != 0);
}
STITCH_ENDPOINT(get_child_item);

void inspect_item(Borrow<Item> it, Bool formatted) {
    cJSON* v = cj::live_item(it);
    if (char* s = cJSON_GetStringValue(v)) (void)strlen(s);
    (void)cJSON_GetNumberValue(v);
    char* s = formatted ? cJSON_Print(v) : cJSON_PrintUnformatted(v);
    if (s) cJSON_free(s);
}
STITCH_ENDPOINT(inspect_item);

void set_item_string(Borrow<Item> it, Str<256> value) {
    cJSON* v = cj::live_item(it, true);
    char* r = cJSON_SetValuestring(v, value.c_str());
    stitch::require(r, "not a string (or a reference)");
    stitch::check(strcmp(v->valuestring, value.c_str()) == 0, "valuestring updated");
}
STITCH_ENDPOINT(set_item_string);

void set_item_number(Borrow<Item> it, F64 value) {
    cJSON* v = cj::live_item(it, true);
    stitch::require(cJSON_IsNumber(v), "not a number");
    cJSON_SetNumberHelper(v, value);
}
STITCH_ENDPOINT(set_item_number);

New<Json> duplicate_item(Borrow<Item> it, Bool recurse) {
    cJSON* d = cJSON_Duplicate(cj::live_item(it), recurse);
    stitch::require(d, "duplicate failed");
    return d;
}
STITCH_ENDPOINT(duplicate_item);

New<Json> detach_item_via_pointer(Borrow<Item> it) {
    cJSON* v = cj::live_item(it, true);
    stitch::require(it->parent != nullptr, "no parent known");
    cJSON* d = cJSON_DetachItemViaPointer(it->parent, v);
    stitch::require(d, "detach failed");
    cj::bump_root(it);
    it.consume();
    return cj::owning(d);
}
STITCH_ENDPOINT(detach_item_via_pointer);

void replace_item_via_pointer(Borrow<Item> it, Borrow<Json> replacement) {
    cJSON* v = cj::live_item(it, true);
    cj::require_unreferenced(replacement);
    stitch::require(it->parent != nullptr, "no parent known");
    // ReplaceItemViaPointer does not carry the member name over (ReplaceItemInObject does it
    // internally); without it the object gets an unnamed member
    if (cJSON_IsObject(it->parent) && v->string && !replacement->string) {
        replacement->string = strdup(v->string);
        replacement->type &= ~cJSON_StringIsConst;
    }
    stitch::require(cJSON_ReplaceItemViaPointer(it->parent, v, replacement), "replace failed");
    replacement.consume();
    cj::bump_root(it);
    it.consume();
}
STITCH_ENDPOINT(replace_item_via_pointer);
