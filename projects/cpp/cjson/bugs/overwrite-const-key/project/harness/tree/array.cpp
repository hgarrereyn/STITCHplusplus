#include "../objects.hpp"

// Moving `item` into a container: it must be unreferenced; on success the container owns it.
static void adopt(Borrow<Json>& container, Borrow<Json>& item, bool ok) {
    stitch::require(ok, "add failed");
    item.consume();
    cj::touch(container);
}

void add_item_to_array(Borrow<Json> array, Borrow<Json> item) {
    stitch::require(cJSON_IsArray(array), "not an array");  // unnamed members would corrupt an object
    cj::require_unreferenced(array);
    cj::require_unreferenced(item);
    int before = cJSON_GetArraySize(array);
    adopt(array, item, cJSON_AddItemToArray(array, item));
    stitch::check(cJSON_GetArraySize(array) == before + 1, "array grows by one");
}
STITCH_ENDPOINT(add_item_to_array);

void insert_item_in_array(Borrow<Json> array, Src<16> s, Borrow<Json> item) {
    stitch::require(cJSON_IsArray(array), "not an array");  // unnamed members would corrupt an object
    cj::require_unreferenced(array);
    cj::require_unreferenced(item);
    int idx = cj::pick_index(*s, cJSON_GetArraySize(array));
    adopt(array, item, cJSON_InsertItemInArray(array, idx, item));
}
STITCH_ENDPOINT(insert_item_in_array);

void replace_item_in_array(Borrow<Json> array, Src<16> s, Borrow<Json> item) {
    stitch::require(cJSON_IsArray(array), "not an array");  // unnamed members would corrupt an object
    cj::require_unreferenced(array);
    cj::require_unreferenced(item);
    int idx = cj::pick_index(*s, cJSON_GetArraySize(array));
    adopt(array, item, cJSON_ReplaceItemInArray(array, idx, item));
}
STITCH_ENDPOINT(replace_item_in_array);

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
STITCH_ENDPOINT(detach_item_from_array);

void delete_item_from_array(Borrow<Json> array, Src<16> s) {
    cj::require_unreferenced(array);
    cJSON_DeleteItemFromArray(array, cj::pick_index(*s, cJSON_GetArraySize(array)));
    cj::touch(array);
}
STITCH_ENDPOINT(delete_item_from_array);
