// Compressible plaintext: literals, runs and back-references to earlier output.
#include "../../objects.hpp"
static std::string plain(stitch::Source& s) {
    std::string o;
    while (s.more() && o.size() < 16384) {
        switch (s.choice(4)) {
            case 0: o += s.str(32); break;
            case 1: o.append((size_t)s.range(1, 300), (char)s.u8()); break;
            case 2:
                if (!o.empty()) {
                    size_t dist = 1 + (size_t)s.range(0, (int64_t)o.size() - 1);
                    size_t len = (size_t)s.range(3, 258);
                    for (size_t i = 0; i < len; i++) o += o[o.size() - dist];
                }
                break;
            default: o += s.token(); break;
        }
    }
    return o;
}
STITCH_DECODER(Plain, plain);
