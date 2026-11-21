// libxml2 harness: documents and their element nodes.
// Element handles stay valid until the program ends: no stub frees a node (unlinked nodes leak),
// and no stub replaces an element's children (xmlNodeSetContent would free them).
#pragma once
#include <stitch.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <libxml/c14n.h>
#include <libxml/chvalid.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/uri.h>
#include <libxml/valid.h>
#include <libxml/xinclude.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlregexp.h>
#include <libxml/xmlsave.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/xmlversion.h>
#include <string>
#include <vector>
#include <string.h>

STITCH_OBJECT(Doc, xmlDocPtr);
STITCH_DROP(Doc, [](xmlDocPtr d) { xmlFreeDoc(d); });
STITCH_OBJECT(Node, xmlNodePtr);  // an element of some document

STITCH_DATATYPE(XmlText);
STITCH_DATATYPE(HtmlText);
STITCH_DATATYPE(XPathExpr);
STITCH_DATATYPE(UriText);
STITCH_DATATYPE(RegexText);
STITCH_DATATYPE(XmlName);

#if LIBXML_VERSION >= 21200
#define LX_ERR const xmlError*
#else
#define LX_ERR xmlError*
#endif

namespace stitch {
inline void check(bool cond, const std::string& what) { check(cond, what.c_str()); }
}  // namespace stitch

namespace lx {
// parser options worth fuzzing; NONET always (no network), never DTDLOAD / XINCLUDE (no files)
using ParseOpts = Flags<XML_PARSE_RECOVER, XML_PARSE_NOENT, XML_PARSE_DTDATTR, XML_PARSE_NOBLANKS, XML_PARSE_NSCLEAN,
                        XML_PARSE_NOCDATA, XML_PARSE_HUGE, XML_PARSE_OLD10, XML_PARSE_IGNORE_ENC, XML_PARSE_BIG_LINES,
                        XML_PARSE_PEDANTIC, XML_PARSE_NODICT, XML_PARSE_OLDSAX>;
using HtmlOpts = Flags<HTML_PARSE_RECOVER, HTML_PARSE_NODEFDTD, HTML_PARSE_NOBLANKS, HTML_PARSE_NOIMPLIED, HTML_PARSE_COMPACT,
                       HTML_PARSE_IGNORE_ENC>;
constexpr int kQuiet = XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET;

inline std::string str(const xmlChar* s) { return s ? std::string((const char*)s) : std::string(); }

// serialized form of a document (format 0 or 1), or "" if serialization failed
inline std::string dump(xmlDocPtr d, int format) {
    xmlChar* mem = nullptr;
    int n = 0;
    xmlDocDumpFormatMemoryEnc(d, &mem, &n, "UTF-8", format);
    std::string out = mem ? std::string((const char*)mem, (size_t)n) : std::string();
    xmlFree(mem);
    return out;
}

inline bool is_descendant(xmlNodePtr maybe_child, xmlNodePtr ancestor) {
    for (xmlNodePtr p = maybe_child; p; p = p->parent)
        if (p == ancestor) return true;
    return false;
}

inline void quiet_generic(void*, const char*, ...) {}
inline void silence() {
    static bool done = false;
    if (done) return;
    done = true;
    xmlSetGenericErrorFunc(nullptr, quiet_generic);
#if LIBXML_VERSION >= 21200
    xmlSetStructuredErrorFunc(nullptr, [](void*, const xmlError*) {});
#else
    xmlSetStructuredErrorFunc(nullptr, [](void*, xmlError*) {});
#endif
    // no file or network access: external entities, DTDs and XIncludes fail to load
    xmlSetExternalEntityLoader([](const char*, const char*, xmlParserCtxtPtr) -> xmlParserInputPtr { return nullptr; });
}

// strict UTF-8 decoder (reference for the libxml2 one): code point and length, or -1
inline int utf8_next(const unsigned char* p, size_t n, int* len) {
    if (n == 0) return -1;
    unsigned c = p[0];
    int need;
    unsigned v, min;
    if (c < 0x80) { *len = 1; return (int)c; }
    else if ((c & 0xe0) == 0xc0) { need = 1; v = c & 0x1f; min = 0x80; }
    else if ((c & 0xf0) == 0xe0) { need = 2; v = c & 0x0f; min = 0x800; }
    else if ((c & 0xf8) == 0xf0) { need = 3; v = c & 0x07; min = 0x10000; }
    else return -1;
    if (n < (size_t)need + 1) return -1;
    for (int i = 1; i <= need; i++) {
        if ((p[i] & 0xc0) != 0x80) return -1;
        v = (v << 6) | (p[i] & 0x3f);
    }
    if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return -1;
    *len = need + 1;
    return (int)v;
}

// the XML characters of s (invalid UTF-8 and non-XML characters dropped; stops at NUL)
inline std::string clean(const std::string& in) {
    std::string out;
    const unsigned char* p = (const unsigned char*)in.c_str();
    size_t n = strlen((const char*)p);
    while (n) {
        int len = 0;
        int c = utf8_next(p, n, &len);
        if (c < 0) { p++; n--; continue; }
        if (xmlIsCharQ(c)) out.append((const char*)p, (size_t)len);
        p += len;
        n -= (size_t)len;
    }
    return out;
}
// UTF-8 made of XML characters only
inline bool xml_chars_strict(const std::string& s) { return clean(s) == s; }
// elements of a document, iteratively and without entering entity references
inline int count_elements(xmlDocPtr d) {
    int n = 0;
    std::vector<xmlNodePtr> st;
    for (xmlNodePtr c = d->children; c; c = c->next) st.push_back(c);
    while (!st.empty()) {
        xmlNodePtr x = st.back();
        st.pop_back();
        if (x->type == XML_ELEMENT_NODE) n++;
        if (x->type == XML_ELEMENT_NODE || x->type == XML_DOCUMENT_NODE)
            for (xmlNodePtr c = x->children; c; c = c->next) st.push_back(c);
    }
    return n;
}
}  // namespace lx
