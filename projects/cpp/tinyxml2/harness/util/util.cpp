// XMLUtil: number <-> text conversions, character references, UTF-8 encoding, character classes.
#include "../objects.hpp"
#include <climits>

// reference UTF-8 encoder (for code points up to 0x1FFFFF, like ConvertUTF32ToUTF8)
static std::string utf8(unsigned long c) {
    std::string o;
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    return o;
}

void util_utf32_to_utf8(U32 cp) {
    char out[8] = {0};
    int len = -1;
    XMLUtil::ConvertUTF32ToUTF8((unsigned long)(uint32_t)cp, out, &len);
    if ((uint32_t)cp >= 0x200000) stitch::check(len == 0, "out-of-range code point is rejected");
    else stitch::check(len >= 1 && len <= 4 && std::string(out, (size_t)len) == utf8((uint32_t)cp), "UTF-8 encoding");
}
STITCH_ENDPOINT(util_utf32_to_utf8);

// GetCharacterRef decodes "&#NNN;" / "&#xHH;" at p (p points at '&').
void util_char_ref(Str<32> body, Bool hex, Bool raw) {
    std::string in = raw ? "&" + body.s : std::string(hex ? "&#x" : "&#") + body.s + ";";
    char value[8] = {0};
    int len = -1;
    const char* p = in.c_str();
    const char* r = XMLUtil::GetCharacterRef(p, value, &len);
    stitch::check(len >= 0 && len <= 4, "decoded length");
    if (!r) return;
    stitch::check(r > p && r <= p + in.size(), "end pointer inside the input");
    if (len == 0) {
        stitch::check(r == p + 1, "not a character reference: skip the '&'");
        return;
    }
    // a decoded reference ends right after its ';' and holds a code point <= 0x10FFFF
    stitch::check(r[-1] == ';', "reference ends at ';'");
    std::string digits(p + (p[2] == 'x' ? 3 : 2), r - 1);
    unsigned long cp = 0;
    bool ok = !digits.empty();
    for (char c : digits) {
        int dg = (c >= '0' && c <= '9') ? c - '0' : (p[2] == 'x' && isxdigit((unsigned char)c)) ? (tolower(c) - 'a' + 10) : -1;
        if (dg < 0 || cp > 0x10FFFF) { ok = false; break; }
        cp = cp * (p[2] == 'x' ? 16 : 10) + (unsigned long)dg;
    }
    stitch::check(ok && cp <= 0x10FFFF, "decoded only a valid reference");
    stitch::check(std::string(value, (size_t)len) == utf8(cp), "decoded value");
}
STITCH_ENDPOINT(util_char_ref);

// Bool serialization is global: always restore the defaults.
struct BoolSerialization {
    BoolSerialization(const char* t, const char* f) { XMLUtil::SetBoolSerialization(t, f); }
    ~BoolSerialization() { XMLUtil::SetBoolSerialization(nullptr, nullptr); }
};

// ToStr -> To* round trips for every number type.
void util_number_roundtrip(I32 i, U32 u, I64 l, U64 ul, F64 dd, F32 f, Bool b, OneOf<0, 1, 2> bools) {
    char buf[200];
    int gi; unsigned gu; int64_t gl; uint64_t gul; double gd; float gf; bool gb;
    XMLUtil::ToStr((int)i, buf, sizeof buf);
    stitch::check(XMLUtil::ToInt(buf, &gi) && gi == (int)i, "int round trip");
    XMLUtil::ToStr((unsigned)u, buf, sizeof buf);
    stitch::check(XMLUtil::ToUnsigned(buf, &gu) && gu == (unsigned)u, "unsigned round trip");
    XMLUtil::ToStr((int64_t)l, buf, sizeof buf);
    stitch::check(XMLUtil::ToInt64(buf, &gl) && gl == (int64_t)l, "int64 round trip");
    XMLUtil::ToStr((uint64_t)ul, buf, sizeof buf);
    stitch::check(XMLUtil::ToUnsigned64(buf, &gul) && gul == (uint64_t)ul, "uint64 round trip");
    if (std::isfinite((double)dd)) {
        XMLUtil::ToStr((double)dd, buf, sizeof buf);
        stitch::check(XMLUtil::ToDouble(buf, &gd) && gd == (double)dd, "double round trip");
    }
    if (std::isfinite((float)f)) {
        XMLUtil::ToStr((float)f, buf, sizeof buf);
        stitch::check(XMLUtil::ToFloat(buf, &gf) && std::fabs(gf - (float)f) <= std::fabs((float)f) * 2e-6f, "float round trip");
    }
    static const char* names[][2] = {{nullptr, nullptr}, {"1", "0"}, {"True", "FALSE"}};
    BoolSerialization keep(names[(int)bools][0], names[(int)bools][1]);
    XMLUtil::ToStr((bool)b, buf, sizeof buf);
    stitch::check(XMLUtil::ToBool(buf, &gb) && gb == (bool)b, "bool round trip");
}
STITCH_ENDPOINT(util_number_roundtrip);

// Parse arbitrary text as every number type; the bool parser accepts every int.
void util_parse_text(Str<48> text) {
    const char* s = text.c_str();
    int i = 0; unsigned u = 0; int64_t l = 0; uint64_t ul = 0; double dd = 0; float f = 0; bool b = false;
    bool oki = XMLUtil::ToInt(s, &i);
    XMLUtil::ToUnsigned(s, &u);
    XMLUtil::ToInt64(s, &l);
    XMLUtil::ToUnsigned64(s, &ul);
    XMLUtil::ToDouble(s, &dd);
    XMLUtil::ToFloat(s, &f);
    bool okb = XMLUtil::ToBool(s, &b);
    if (oki) stitch::check(okb && b == (i != 0), "ToBool agrees with ToInt");
    bool hex = XMLUtil::IsPrefixHex(s);
    const char* t = XMLUtil::SkipWhiteSpace(s, nullptr);
    stitch::check(hex == (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')), "IsPrefixHex");
}
STITCH_ENDPOINT(util_parse_text);

// Character classes, whitespace skipping with line counting, string comparison and BOM detection.
void util_chars(Bytes<64> data, Range<0, 80> n) {
    std::string s((const char*)data.data(), data.size());
    const char* p = s.c_str();
    int line = 0;
    const char* q = XMLUtil::SkipWhiteSpace(p, &line);
    int nl = 0;
    for (const char* x = p; x < q; x++) {
        stitch::check(XMLUtil::IsWhiteSpace(*x), "skipped only whitespace");
        nl += *x == '\n';
    }
    stitch::check(line == nl, "SkipWhiteSpace counts newlines");
    stitch::check(!XMLUtil::IsWhiteSpace(*q) || *q == 0, "stopped at non-whitespace");
    for (const char* x = p; *x; x++) {
        unsigned char c = (unsigned char)*x;
        if (XMLUtil::IsNameStartChar(c)) stitch::check(XMLUtil::IsNameChar(c), "name start chars are name chars");
        stitch::check(XMLUtil::IsUTF8Continuation(*x) == (c >= 0x80), "IsUTF8Continuation");
    }
    std::string other = s.substr(0, s.size() / 2);
    stitch::check(XMLUtil::StringEqual(p, other.c_str(), (int)n) == (strncmp(p, other.c_str(), (size_t)n) == 0), "StringEqual");
    bool bom = false;
    const char* r = XMLUtil::ReadBOM(p, &bom);
    stitch::check(bom == (strncmp(p, "\xEF\xBB\xBF", 3) == 0) && r == p + (bom ? 3 : 0), "ReadBOM");
}
STITCH_ENDPOINT(util_chars);
