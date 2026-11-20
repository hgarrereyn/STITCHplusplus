#include "../../objects.hpp"
static std::string key(stitch::Source& s) {
    switch (s.choice(3)) {
        case 0: return s.token();
        case 1: return std::string(s.pick({"length", "prototype", "constructor", "__proto__", "0", "1", "-1", "4294967295", "toString", "valueOf", "then", "Symbol.iterator", "caller", "arguments", "name", "message", "stack", "lastIndex", "byteLength", "buffer"}));
        default: return s.str(12);
    }
}
STITCH_DECODER(JsKey, key);
