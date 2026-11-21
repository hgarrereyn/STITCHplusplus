// XML documents: a prolog (declaration, internal DTD with entities / attribute defaults / content
// models), then a tree of elements with namespaces, attributes, references, CDATA, comments, PIs.
#include "../../objects.hpp"

static const char* kNames[] = {"a", "b", "root", "x:a", "a:b", "item", "doc", "p", "xi:include", "e1", "_n", "\xc3\xa9l"};
static const char* kNs[] = {"urn:a", "urn:b", "http://www.w3.org/2001/XInclude", "http://www.w3.org/XML/1998/namespace", ""};

template <size_t N>
static const char* pk(stitch::Source& s, const char* (&a)[N]) { return a[s.choice(N)]; }

static std::string name(stitch::Source& s) { return s.coin(0.8) ? pk(s, kNames) : s.token(); }

static void text(stitch::Source& s, std::string& o) {
    switch (s.choice(8)) {
        case 0: o += s.str(12); break;
        case 1: o += s.pick({"&amp;", "&lt;", "&gt;", "&quot;", "&apos;"}); break;
        case 2: { char b[16]; snprintf(b, sizeof b, "&#%u;", (unsigned)s.range(0, 0x110000)); o += b; break; }
        case 3: { char b[16]; snprintf(b, sizeof b, "&#x%x;", (unsigned)s.u16()); o += b; break; }
        case 4: o += "&" + std::string(s.pick({"e", "ext", "big", "p", "x"})) + ";"; break;
        case 5: o += s.pick({" ", "\n", "\t", "\r\n"}); break;
        case 6: o += "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"; break;
        default: o += s.token(); break;
    }
}

static void attrs(stitch::Source& s, std::string& o) {
    while (s.coin(0.4)) {
        o += ' ';
        if (s.coin(0.25)) {
            o += s.coin() ? std::string("xmlns") : "xmlns:" + std::string(s.pick({"x", "a", "xi", "xml"}));
            o += "=\"" + std::string(pk(s, kNs)) + "\"";
            continue;
        }
        o += s.coin(0.15) ? std::string(s.pick({"xml:lang", "xml:space", "xml:base", "xml:id", "id", "href", "parse"})) : name(s);
        char q = s.coin(0.8) ? '"' : '\'';
        o += '=';
        o += q;
        while (s.more()) text(s, o);
        o += q;
    }
}

static void content(stitch::Source& s, std::string& o, int depth) {
    while (s.more()) {
        switch (s.choice(depth > 10 ? 5 : 7)) {
            case 0: text(s, o); break;
            case 1: o += "<![CDATA[" + s.str(16) + "]]>"; break;
            case 2: o += "<!--" + s.str(12) + "-->"; break;
            case 3: o += "<?" + std::string(s.pick({"pi", "xml-stylesheet", "t"})) + " " + s.str(8) + "?>"; break;
            case 4: o += s.str(6); break;  // stray
            default: {
                std::string n = name(s);
                o += "<" + n;
                attrs(s, o);
                if (s.coin(0.15)) { o += "/>"; break; }
                o += ">";
                content(s, o, depth + 1);
                o += s.coin(0.95) ? "</" + n + ">" : "</" + name(s) + ">";
            }
        }
    }
}

static void dtd(stitch::Source& s, std::string& o, const std::string& root) {
    o += "<!DOCTYPE " + root;
    if (s.coin(0.1)) o += " SYSTEM \"" + std::string(s.pick({"a.dtd", "http://x/y.dtd", "/dev/zero"})) + "\"";
    o += " [";
    while (s.more()) {
        switch (s.choice(6)) {
            case 0: o += "<!ENTITY " + std::string(s.pick({"e", "big", "x"})) + " \"" + s.str(10) + "\">"; break;
            case 1: o += "<!ENTITY ext SYSTEM \"" + std::string(s.pick({"a.xml", "file:///etc/hostname"})) + "\">"; break;
            case 2: o += "<!ENTITY % p \"" + s.str(10) + "\">%p;"; break;
            case 3: o += "<!ELEMENT " + name(s) + " " + std::string(s.pick({"EMPTY", "ANY", "(#PCDATA)", "(a|b)*", "(a,b?)+", "(#PCDATA|a|b)*", "(a,(b|c)*)"})) + ">"; break;
            case 4: o += "<!ATTLIST " + name(s) + " " + name(s) + " " + std::string(s.pick({"CDATA", "ID", "IDREF", "IDREFS", "NMTOKEN", "(x|y)", "ENTITY", "NOTATION (n)"})) + " " + std::string(s.pick({"#IMPLIED", "#REQUIRED", "\"d\"", "#FIXED \"x\""})) + ">"; break;
            default: o += "<!ENTITY big \"&e;&e;&e;&e;&e;&e;&e;&e;\">"; break;
        }
    }
    o += "]>";
}

static std::string xml_text(stitch::Source& s) {
    std::string o;
    if (s.coin(0.4)) o += s.pick({"<?xml version=\"1.0\"?>", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<?xml version='1.1'?>",
                                  "<?xml version=\"1.0\" encoding=\"ISO-8859-1\" standalone=\"yes\"?>", "<?xml version=\"1.0\" encoding=\"UTF-16\"?>",
                                  "\xef\xbb\xbf<?xml version=\"1.0\"?>"});
    std::string root = name(s);
    if (s.coin(0.35)) dtd(s, o, root);
    o += "<" + root;
    attrs(s, o);
    o += ">";
    content(s, o, 0);
    o += "</" + root + ">";
    if (s.coin(0.05)) o += s.str(8);
    return o;
}
STITCH_DECODER(XmlText, xml_text);
