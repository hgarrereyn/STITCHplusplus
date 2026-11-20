// RFC 6901 pointers: "" | ("/" segment)*, segments with ~0 / ~1 escapes and array indices.
#include "../../objects.hpp"
static std::string json_pointer(stitch::Source& s) {
    std::string o;
    while (s.more()) {
        o += '/';
        switch (s.choice(5)) {
            case 0: o += std::to_string(s.range(0, 8)); break;
            case 1: o += s.token(); break;
            case 2: o += s.pick({"~0", "~1", "~", "-", "~01", "~10"}); break;
            case 3: o += s.str(8); break;
            default: o += std::to_string(s.range(-1, 100000)); break;
        }
    }
    return o;
}
STITCH_DECODER(JsonPointer, json_pointer);
