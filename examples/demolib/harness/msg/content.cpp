#include "../objects.hpp"

void append(Borrow<Msg> m, Bytes<512> data) {
    size_t before = msg_len(m);
    if (msg_append(m, data.data(), data.size()) != 0) stitch::bail("does not fit");
    stitch::check(msg_len(m) == before + data.size(), "append grows by data size");
}
STITCH_ENDPOINT(append);

void set_label(Borrow<Msg> m, Str<64> label) {
    msg_set_label(m, label.c_str());
    stitch::check(strcmp(msg_label(m), label.c_str()) == 0, "label round-trips");
}
STITCH_ENDPOINT(set_label);

void parse_headers(Borrow<Msg> m, Str<512, KvText> text) {
    int n = msg_parse_kv(m, text.c_str());
    stitch::require(n >= 0, "malformed headers");
    stitch::set(m, "has_headers", 1);
}
STITCH_ENDPOINT(parse_headers);

// Look up a key that was actually parsed (pick it with an in-stub source).
void get_header(Borrow<Msg> m, Str<32> key) {
    stitch::require(stitch::get(m, "has_headers") == 1, "no headers parsed yet");
    msg_get(m, key.c_str());
}
STITCH_ENDPOINT(get_header);

// Several appends from one typed value: a Val parameter of the Chunks data type.
void append_all(Borrow<Msg> m, stitch::Val<Chunks, 512> chunks) {
    for (auto& c : *chunks) {
        size_t before = msg_len(m);
        if (msg_append(m, c.data(), c.size()) != 0) stitch::bail("does not fit");
        stitch::check(msg_len(m) == before + c.size(), "append grows by data size");
    }
}
STITCH_ENDPOINT(append_all);
