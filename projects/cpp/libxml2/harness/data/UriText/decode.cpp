#include "../../objects.hpp"
static std::string uri_text(stitch::Source& s) {
    std::string o;
    if (s.coin(0.7)) o += std::string(s.pick({"http", "file", "urn", "mailto", "a+b.c", "HTTP", "x"})) + ":";
    if (s.coin(0.6)) {
        o += "//";
        if (s.coin(0.3)) o += s.token() + (s.coin() ? ":" + s.token() : "") + "@";
        o += s.coin(0.7) ? std::string(s.pick({"example.com", "[::1]", "[v1.x]", "127.0.0.1", "", "h\xc3\xa9"})) : s.str(8);
        if (s.coin(0.3)) o += ":" + std::to_string(s.range(-1, 99999999));
    }
    while (s.more()) {
        switch (s.choice(5)) {
            case 0: o += "/" + s.token(); break;
            case 1: o += s.pick({"/.", "/..", "./", "../", "//"}); break;
            case 2: { char b[8]; snprintf(b, sizeof b, "%%%02X", (unsigned)s.u8()); o += b; break; }
            case 3: o += s.str(6); break;
            default: o += s.pick({"?", "#", "?a=b&c", "#frag", ";p", "%", "%zz"}); break;
        }
    }
    return o;
}
STITCH_DECODER(UriText, uri_text);
