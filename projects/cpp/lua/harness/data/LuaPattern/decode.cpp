#include "../../objects.hpp"
static std::string pattern(stitch::Source& s) {
    std::string o;
    while (s.more()) o += s.pick({"%a+", "%d*", ".-", "[%w_]", "(%s*)", "%b()", "%f[%w]", "^", "$", "[^x]", "%1", "()", ".", "x?"});
    return o;
}
STITCH_DECODER(LuaPattern, pattern);
