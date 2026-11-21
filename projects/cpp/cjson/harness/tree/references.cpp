// Reference nodes point into other values without owning them. While a reference exists, its
// target is marked ("refs" > 0) so stubs never modify, move or delete it.
#include "../objects.hpp"

static New<JRef> make_ref(Borrow<Json>& target, cJSON* ref) {
    stitch::require(ref, "create reference failed");
    stitch::set(target, "refs", stitch::get(target, "refs", 0) + 1);
    New<JRef> r(RefT{ref, nullptr});
    stitch::set(r, "target", target);
    return r;
}

// The argument is the first member of the referenced container (see cJSON's tests), not the
// container itself: the reference shares the member list.
New<JRef> create_object_reference(Borrow<Json> target) {
    stitch::require(cJSON_IsObject(target) && target->child, "need a non-empty object");
    return make_ref(target, cJSON_CreateObjectReference(target->child));
}
STITCH_ENDPOINT(create_object_reference);

New<JRef> create_array_reference(Borrow<Json> target) {
    stitch::require(cJSON_IsArray(target), "need an array");
    return make_ref(target, cJSON_CreateArrayReference(target->child));
}
STITCH_ENDPOINT(create_array_reference);

New<JRef> create_string_reference(Str<256, JsonKey> s) {
    char* backing = strdup(s.c_str());
    cJSON* r = cJSON_CreateStringReference(backing);
    if (!r) {
        free(backing);
        stitch::bail("create reference failed");
    }
    return RefT{r, backing};
}
STITCH_ENDPOINT(create_string_reference);

static bool target_alive(const Borrow<JRef>& r) {
    return r->str != nullptr || stitch::get_obj<Json>(r, "target") != nullptr;
}

void print_reference(Borrow<JRef> r, Bool formatted) {
    stitch::require(target_alive(r), "reference target is gone");
    char* s = formatted ? cJSON_Print(r->ref) : cJSON_PrintUnformatted(r->ref);
    stitch::require(s, "print failed");
    cJSON_free(s);
}
STITCH_ENDPOINT(print_reference);

New<Json> duplicate_reference(Borrow<JRef> r) {
    stitch::require(target_alive(r), "reference target is gone");
    cJSON* d = cJSON_Duplicate(r->ref, 1);
    stitch::require(d, "duplicate failed");
    return d;
}
STITCH_ENDPOINT(duplicate_reference);

void compare_reference(Borrow<JRef> r, Borrow<Json> other, Bool case_sensitive) {
    stitch::require(target_alive(r), "reference target is gone");
    cJSON_Compare(r->ref, other, case_sensitive);
}
STITCH_ENDPOINT(compare_reference);

// The container takes the reference node. Its target stays marked for the rest of the run
// (the container may live on), and string backings are leaked on purpose.
void add_reference_to_array(Borrow<Json> array, Take<JRef> r) {
    if (!cJSON_IsArray(array) || stitch::get(array, "refs", 0) != 0 || !cJSON_AddItemToArray(array, r->ref)) {
        cJSON_Delete(r->ref);
        free(r->str);
        if (cJSON** t = stitch::get_obj<Json>(r, "target")) stitch::set(*t, "refs", stitch::get(*t, "refs", 0) - 1);
        stitch::bail("add reference failed");
    }
    cj::touch(array);
}
STITCH_ENDPOINT(add_reference_to_array);

void destroy_reference(Take<JRef> r) {
    if (cJSON** t = stitch::get_obj<Json>(r, "target")) stitch::set(*t, "refs", stitch::get(*t, "refs", 0) - 1);
    cJSON_Delete(r->ref);
    free(r->str);
}
STITCH_ENDPOINT(destroy_reference);

// cJSON_AddItemReferenceTo*: the container gets a reference to `item`; item stays marked.
void add_item_reference_to_array(Borrow<Json> array, Borrow<Json> item) {
    cj::require_unreferenced(array);
    stitch::require(cJSON_IsArray(array), "not an array");
    stitch::require(cJSON_AddItemReferenceToArray(array, item), "add reference failed");
    stitch::set(item, "refs", stitch::get(item, "refs", 0) + 1);
    cj::touch(array);
}
STITCH_ENDPOINT(add_item_reference_to_array);

void add_item_reference_to_object(Borrow<Json> object, Str<64, JsonKey> key, Borrow<Json> item) {
    cj::require_unreferenced(object);
    stitch::require(cJSON_IsObject(object), "not an object");
    stitch::require(cJSON_AddItemReferenceToObject(object, key.c_str(), item), "add reference failed");
    stitch::set(item, "refs", stitch::get(item, "refs", 0) + 1);
    cj::touch(object);
}
STITCH_ENDPOINT(add_item_reference_to_object);
