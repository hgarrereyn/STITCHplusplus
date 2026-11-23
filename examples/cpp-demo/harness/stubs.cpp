#include <stitch.h>
extern "C" {
#include <kv.h>
}

STITCH_OBJECT(Store, kv_store*);
STITCH_DROP(Store, [](kv_store* st) { kv_free(st); });
STITCH_DATATYPE(KvText);

STITCH_DECODER(KvText, [](stitch::Source& s) {
    std::string out;
    while (s.more()) out += (s.coin(0.3) ? s.token() : s.str(8)) + "=" + s.str(12) + "\n";
    return out;
});

New<Store> new_store() { return kv_new(); }
STITCH_ENDPOINT(new_store);

New<Store> load(Str<256, KvText> text) {
    kv_store* st = kv_load(text.c_str());
    stitch::require(st, "invalid text");
    return st;
}
STITCH_ENDPOINT(load);

void put(Borrow<Store> st, Str<16> key, Str<32> value) {
    stitch::require(kv_put(st, key.c_str(), value.c_str()) == 0, "invalid key or value");
}
STITCH_ENDPOINT(put);

void remove_key(Borrow<Store> st, Str<16> key) { kv_remove(st, key.c_str()); }   // not `remove`: a libc name
STITCH_ENDPOINT(remove_key);

void roundtrip(Borrow<Store> st) {
    char* text = kv_dump(st);
    kv_store* again = kv_load(text);
    free(text);
    stitch::require(again, "dump did not load");
    size_t n = kv_len(again);
    kv_free(again);
    stitch::check(n == kv_len(st), "dump/load roundtrip");
}
STITCH_ENDPOINT(roundtrip);

// A typed data type: Pairs decodes to a list of (key, value), drawing each with Pair's decoder.
STITCH_DATATYPE(Pair);
STITCH_DECODER(Pair, [](stitch::Source& s) {
    std::string k = s.coin(0.3) ? s.token() : s.str(8);
    return std::make_pair(k, s.coin() ? s.str(12) : std::to_string(s.range(0, 100)));
});
STITCH_DATATYPE(Pairs);
STITCH_DECODER(Pairs, [](stitch::Source& s) {
    std::vector<std::pair<std::string, std::string>> out;
    while (s.more()) out.push_back(s.draw<Pair>());
    return out;
});

void put_all(Borrow<Store> st, stitch::Val<Pairs, 256> kvs) {
    for (auto& [k, v] : *kvs) stitch::require(kv_put(st, k.c_str(), v.c_str()) == 0, "invalid key or value");
}
STITCH_ENDPOINT(put_all);
