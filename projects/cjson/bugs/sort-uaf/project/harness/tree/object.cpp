#include "../objects.hpp"

void add_item_to_object(Borrow<Json> object, Str<64, JsonKey> key, Borrow<Json> item) {
    stitch::require(cJSON_IsObject(object), "not an object");
    cj::require_unreferenced(object);
    cj::require_unreferenced(item);
    stitch::require(cJSON_AddItemToObject(object, key.c_str(), item), "add failed");
    item.consume();
    cj::touch(object);
    stitch::check(cJSON_HasObjectItem(object, key.c_str()) || !cJSON_IsObject(object) || key.size() != strlen(key.c_str()),
                  "added key is found");
}
STITCH_ENDPOINT(add_item_to_object);

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
STITCH_ENDPOINT(add_item_to_object_cs);

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
STITCH_ENDPOINT(replace_item_in_object);

New<Json> detach_item_from_object(Borrow<Json> object, Str<64, JsonKey> fresh, Src<16> s, Bool case_sensitive) {
    cj::require_unreferenced(object);
    std::string key = cj::pick_key(object, *s, fresh.c_str());
    cJSON* d = case_sensitive ? cJSON_DetachItemFromObjectCaseSensitive(object, key.c_str())
                              : cJSON_DetachItemFromObject(object, key.c_str());
    stitch::require(d, "no such key");
    cj::touch(object);
    return cj::owning(d);
}
STITCH_ENDPOINT(detach_item_from_object);

void delete_item_from_object(Borrow<Json> object, Str<64, JsonKey> fresh, Src<16> s, Bool case_sensitive) {
    cj::require_unreferenced(object);
    std::string key = cj::pick_key(object, *s, fresh.c_str());
    if (case_sensitive) cJSON_DeleteItemFromObjectCaseSensitive(object, key.c_str());
    else cJSON_DeleteItemFromObject(object, key.c_str());
    cj::touch(object);
}
STITCH_ENDPOINT(delete_item_from_object);

// cJSON_Add*ToObject helpers: create a child under `name`.
void add_scalar_to_object(Borrow<Json> object, Str<64, JsonKey> name, Range<0, 6> which, F64 num, Str<128> text) {
    stitch::require(cJSON_IsObject(object), "not an object");
    cj::require_unreferenced(object);
    cJSON* c = nullptr;
    switch ((int)which) {
        case 0: c = cJSON_AddNullToObject(object, name.c_str()); break;
        case 1: c = cJSON_AddTrueToObject(object, name.c_str()); break;
        case 2: c = cJSON_AddFalseToObject(object, name.c_str()); break;
        case 3: c = cJSON_AddBoolToObject(object, name.c_str(), num > 0); break;
        case 4: c = cJSON_AddNumberToObject(object, name.c_str(), num); break;
        case 5: c = cJSON_AddStringToObject(object, name.c_str(), text.c_str()); break;
        default: c = cJSON_AddRawToObject(object, name.c_str(), text.c_str()); break;
    }
    stitch::require(c, "add helper failed");
    cj::touch(object);
}
STITCH_ENDPOINT(add_scalar_to_object);

void add_container_to_object(Borrow<Json> object, Str<64, JsonKey> name, Bool array) {
    stitch::require(cJSON_IsObject(object), "not an object");
    cj::require_unreferenced(object);
    cJSON* c = array ? cJSON_AddArrayToObject(object, name.c_str()) : cJSON_AddObjectToObject(object, name.c_str());
    stitch::require(c, "add helper failed");
    cj::touch(object);
}
STITCH_ENDPOINT(add_container_to_object);
