#include "../objects.hpp"
#include <stdio.h>

// Install the default allocator explicitly (cJSON_InitHooks with malloc/free), and check that the
// version string agrees with the header.
STITCH_INIT {
    cJSON_Hooks hooks = {malloc, free};
    cJSON_InitHooks(&hooks);
    char want[32];
    snprintf(want, sizeof want, "%d.%d.%d", CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR, CJSON_VERSION_PATCH);
    if (strcmp(cJSON_Version(), want) != 0) abort();
}

New<Json> create_null() { return cJSON_CreateNull(); }
STITCH_ENDPOINT(create_null);
New<Json> create_true() { return cJSON_CreateTrue(); }
STITCH_ENDPOINT(create_true);
New<Json> create_false() { return cJSON_CreateFalse(); }
STITCH_ENDPOINT(create_false);
New<Json> create_bool(Bool b) { return cJSON_CreateBool(b); }
STITCH_ENDPOINT(create_bool);
New<Json> create_number(F64 n) { return cJSON_CreateNumber(n); }
STITCH_ENDPOINT(create_number);
New<Json> create_string(Str<256, JsonKey> s) { return cJSON_CreateString(s.c_str()); }
STITCH_ENDPOINT(create_string);
New<Json> create_raw(Str<512, JsonText> raw) { return cJSON_CreateRaw(raw.c_str()); }
STITCH_ENDPOINT(create_raw);
New<Json> create_array() { return cJSON_CreateArray(); }
STITCH_ENDPOINT(create_array);
New<Json> create_object() { return cJSON_CreateObject(); }
STITCH_ENDPOINT(create_object);

New<Json> create_int_array(Bytes<256> data) {
    std::vector<int> v(data.size() / sizeof(int));
    memcpy(v.data(), data.data(), v.size() * sizeof(int));
    cJSON* a = cJSON_CreateIntArray(v.data(), (int)v.size());
    stitch::require(a, "create_int_array failed");
    stitch::check(cJSON_GetArraySize(a) == (int)v.size(), "int array has count items");
    return a;
}
STITCH_ENDPOINT(create_int_array);

New<Json> create_float_array(Bytes<256> data) {
    std::vector<float> v(data.size() / sizeof(float));
    memcpy(v.data(), data.data(), v.size() * sizeof(float));
    cJSON* a = cJSON_CreateFloatArray(v.data(), (int)v.size());
    stitch::require(a, "create_float_array failed");
    return a;
}
STITCH_ENDPOINT(create_float_array);

New<Json> create_double_array(Bytes<256> data) {
    std::vector<double> v(data.size() / sizeof(double));
    memcpy(v.data(), data.data(), v.size() * sizeof(double));
    cJSON* a = cJSON_CreateDoubleArray(v.data(), (int)v.size());
    stitch::require(a, "create_double_array failed");
    return a;
}
STITCH_ENDPOINT(create_double_array);

New<Json> create_string_array(Src<256> s) {
    std::vector<std::string> strs;
    while (strs.size() < 16 && s->more()) strs.push_back(s->str(24));
    std::vector<const char*> ptrs;
    for (auto& x : strs) ptrs.push_back(x.c_str());
    cJSON* a = cJSON_CreateStringArray(ptrs.data(), (int)ptrs.size());
    stitch::require(a, "create_string_array failed");
    return a;
}
STITCH_ENDPOINT(create_string_array);

New<Json> duplicate(Borrow<Json> v, Bool recurse) {
    cJSON* d = cJSON_Duplicate(v, recurse);
    stitch::require(d, "duplicate failed");
    if (recurse && !cj::has_lossy(*v)) stitch::check(cJSON_Compare(v, d, true), "deep duplicate compares equal");
    return d;
}
STITCH_ENDPOINT(duplicate);

void destroy(Borrow<Json> v) {
    cj::require_unreferenced(v);
    cJSON_Delete(v);
    v.consume();
}
STITCH_ENDPOINT(destroy);
