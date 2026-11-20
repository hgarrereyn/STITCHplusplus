#include "../../objects.hpp"
static std::string json_key(stitch::Source& s) {
    switch (s.choice(4)) {
        case 0: return s.token();
        case 1: return s.str(16);
        case 2: return std::string(s.pick({"a", "A", "key", "KEY", "", "0", "name", "\xc3\xa9"}));
        default: return s.token() + s.str(4);
    }
}
STITCH_DECODER(JsonKey, json_key);
