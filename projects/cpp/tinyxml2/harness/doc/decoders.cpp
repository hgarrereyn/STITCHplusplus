#include "../objects.hpp"

// Names: [A-Za-z_][A-Za-z0-9_.-]* with an optional single "prefix:" part.
static std::string gen_name(stitch::Source& s) {
    static const char first[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    static const char rest[]  = "abcdefghijklmnopqrstuvwxyz0123456789_.-";
    std::string n(1, first[s.choice(sizeof(first) - 1)]);
    int len = (int)s.range(0, 6);
    for (int i = 0; i < len; i++) n += rest[s.choice(sizeof(rest) - 1)];
    return n;
}
STITCH_DECODER(XmlName, [](stitch::Source& s) {
    if (s.coin(0.1)) return s.token();
    std::string n = gen_name(s);
    if (s.coin(0.15)) n += ":" + gen_name(s);
    return n;
});

static std::string text_run(stitch::Source& s) {
    static const char* bits[] = {"hello", " ", "a b", "&amp;", "&lt;", "&gt;", "&quot;", "&apos;", "&#65;", "&#x42;",
                                 "42", "-7", "3.5", "true", "false", "\n", "  x  ", "\xc3\xa9", "'", "\""};
    std::string t;
    int n = (int)s.range(1, 4);
    for (int i = 0; i < n; i++) t += bits[s.choice(sizeof(bits) / sizeof(*bits))];
    return t;
}
static std::string attr_val(stitch::Source& s) {
    std::string v = text_run(s);
    std::string o;
    for (char c : v) o += (c == '"') ? std::string("&quot;") : std::string(1, c);
    return o;
}
static std::string element(stitch::Source& s, int depth) {
    std::string name = gen_name(s);
    std::string o = "<" + name;
    while (s.more() && s.coin(0.5)) o += " " + gen_name(s) + "=\"" + attr_val(s) + "\"";
    if (depth > 5 || s.coin(0.15)) return o + "/>";
    o += ">";
    while (s.more()) {
        switch (s.choice(6)) {
            case 0: o += text_run(s); break;
            case 1: o += element(s, depth + 1); break;
            case 2: o += element(s, depth + 1); break;
            case 3: o += "<!--" + gen_name(s) + "-->"; break;
            case 4: o += "<![CDATA[" + gen_name(s) + " <&> ]]>"; break;
            default: o += "<?" + gen_name(s) + " " + gen_name(s) + "?>"; break;
        }
    }
    return o + "</" + name + ">";
}
STITCH_DECODER(XmlText, [](stitch::Source& s) {
    std::string o;
    if (s.coin(0.3)) o += "<?xml version=\"1.0\"?>";
    if (s.coin(0.1)) o += "<!DOCTYPE " + gen_name(s) + ">";
    if (s.coin(0.1)) o += "<!--top-->";
    o += element(s, 0);
    return o;
});
