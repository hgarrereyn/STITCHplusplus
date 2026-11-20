#include "../../objects.hpp"

// key=value pairs separated by ';'. Keys come from the dictionary (kv.dict + learned tokens).
static std::string kv(stitch::Source& s) {
    std::string o;
    do {
        if (!o.empty()) o += ';';
        o += s.coin(0.7) ? s.token() : s.str(20);
        o += '=';
        o += s.coin(0.5) ? std::to_string(s.range(-100, 100000)) : s.str(24);
    } while (s.more());
    return o;
}
STITCH_DECODER(KvText, kv);
