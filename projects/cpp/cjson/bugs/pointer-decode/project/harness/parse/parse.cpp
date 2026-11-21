#include "../objects.hpp"

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
STITCH_ENDPOINT(parse);

New<Json> parse_with_length(Bytes<4096, JsonText> buf) {
    cJSON* j = cJSON_ParseWithLength((const char*)buf.data(), buf.size());
    stitch::require(j, "parse error");
    return j;
}
STITCH_ENDPOINT(parse_with_length);

New<Json> parse_with_opts(Str<4096, JsonText> text, Bool want_end, Bool require_null_terminated) {
    const char* end = nullptr;
    cJSON* j = cJSON_ParseWithOpts(text.c_str(), want_end ? &end : nullptr, require_null_terminated);
    stitch::require(j, "parse error");
    if (want_end) stitch::check(end >= text.c_str() && end <= text.c_str() + text.size(), "parse end inside the input");
    return j;
}
STITCH_ENDPOINT(parse_with_opts);

New<Json> parse_with_length_opts(Bytes<4096, JsonText> buf, Bool want_end, Bool require_null_terminated) {
    const char* p = (const char*)buf.data();
    const char* end = nullptr;
    cJSON* j = cJSON_ParseWithLengthOpts(p, buf.size(), want_end ? &end : nullptr, require_null_terminated);
    stitch::require(j, "parse error");
    if (want_end) stitch::check(end >= p && end <= p + buf.size(), "parse end inside the input");
    return j;
}
STITCH_ENDPOINT(parse_with_length_opts);

void minify(Str<4096, JsonText> text) {
    std::string buf = text.s;
    size_t before = strlen(buf.c_str());
    cJSON_Minify(&buf[0]);
    stitch::check(strlen(buf.c_str()) <= before, "minify does not grow the text");
}
STITCH_ENDPOINT(minify);
