// JSON documents. Mostly valid, with knobs for the corners parsers get wrong: deep nesting,
// escapes (incl. surrogate pairs), number formats, whitespace, and a small chance of garbage.
#include "../../objects.hpp"

static void ws(stitch::Source& s, std::string& o) {
    if (s.coin(0.15)) o += s.pick({" ", "\n", "\t", "\r\n", "  "});
}

static void string_lit(stitch::Source& s, std::string& o) {
    o += '"';
    while (s.more()) {
        switch (s.choice(6)) {
            case 0: o += s.str(12); break;                                   // raw content (cmplog-friendly)
            case 1: o += s.pick({"\\n", "\\t", "\\\"", "\\\\", "\\/", "\\b", "\\f", "\\r"}); break;
            case 2: { char b[8]; snprintf(b, sizeof b, "\\u%04x", (unsigned)s.u16()); o += b; break; }
            case 3: { char b[16]; snprintf(b, sizeof b, "\\ud%03x\\udc%02x", (unsigned)s.range(0x800, 0xbff), (unsigned)s.u8()); o += b; break; }
            case 4: o += s.token(); break;
            default: o += "\xc3\xa9\xe2\x82\xac"; break;                       // UTF-8
        }
    }
    o += '"';
}

static void number(stitch::Source& s, std::string& o) {
    switch (s.choice(5)) {
        case 0: o += std::to_string(s.range(-1000, 1000)); break;
        case 1: o += std::to_string((long long)s.i64()); break;
        case 2: o += std::to_string(s.range(0, 99999)) + "." + std::to_string(s.range(0, 99999)); break;
        case 3: o += std::to_string(s.range(-9, 9)) + s.pick({"e", "E", "e+", "e-"}) + std::to_string(s.range(0, 400)); break;
        default: o += s.pick({"0", "-0", "1e308", "-1e-308", "0.1", "1E400", "123456789012345678901234567890"}); break;
    }
}

static void value(stitch::Source& s, std::string& o, int depth) {
    ws(s, o);
    size_t kinds = depth > 12 ? 5 : 7;
    switch (s.choice(kinds)) {
        case 0: o += "null"; break;
        case 1: o += s.coin() ? "true" : "false"; break;
        case 2: number(s, o); break;
        case 3: string_lit(s, o); break;
        case 4: o += s.str(8); break;  // garbage
        case 5: {
            o += '[';
            bool first = true;
            while (s.more()) {
                if (!first) o += ',';
                first = false;
                value(s, o, depth + 1);
            }
            o += ']';
            break;
        }
        default: {
            o += '{';
            bool first = true;
            while (s.more()) {
                if (!first) o += ',';
                first = false;
                ws(s, o);
                if (s.coin(0.6)) o += "\"" + s.token() + "\"";
                else string_lit(s, o);
                ws(s, o);
                o += ':';
                value(s, o, depth + 1);
            }
            o += '}';
        }
    }
    ws(s, o);
}

static std::string json_text(stitch::Source& s) {
    std::string o;
    value(s, o, 0);
    if (s.coin(0.05)) o += s.str(8);  // trailing data
    return o;
}
STITCH_DECODER(JsonText, json_text);
