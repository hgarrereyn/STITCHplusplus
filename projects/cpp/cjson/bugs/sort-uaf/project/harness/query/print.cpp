#include "../objects.hpp"

static void round_trip(const cJSON* v, const char* s) {
    if (cj::has_lossy(v)) return;
    cJSON* back = cJSON_Parse(s);
    stitch::check(back != nullptr, "printed JSON parses");
    stitch::check(cJSON_Compare(v, back, true), "print -> parse round trip");
    cJSON_Delete(back);
}

void print(Borrow<Json> v, Bool formatted) {
    char* s = formatted ? cJSON_Print(v) : cJSON_PrintUnformatted(v);
    stitch::require(s, "print failed");
    round_trip(v, s);
    cJSON_free(s);
}
STITCH_ENDPOINT(print);

void print_buffered(Borrow<Json> v, Range<-8, 512> prebuffer, Bool formatted) {
    char* s = cJSON_PrintBuffered(v, (int)prebuffer, formatted);
    stitch::require(s, "print failed");
    round_trip(v, s);
    cJSON_free(s);
}
STITCH_ENDPOINT(print_buffered);

void print_preallocated(Borrow<Json> v, Range<0, 4096> length, Bool formatted) {
    char* buf = (char*)malloc((size_t)length + 1);
    bool ok = cJSON_PrintPreallocated(v, buf, (int)length, formatted);
    if (ok) stitch::check(strlen(buf) < (size_t)length, "output fits the buffer");
    free(buf);
    stitch::require(ok, "buffer too small");
}
STITCH_ENDPOINT(print_preallocated);
