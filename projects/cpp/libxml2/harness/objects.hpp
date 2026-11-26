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
#include <libxml/parserInternals.h>
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
#include <unistd.h>
#include <algorithm>

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
    // no network and no files: external entities, DTDs and XIncludes fail to load, except the
    // temporary files stubs write themselves (/tmp/stitch_lx_*), so the file entry points work
    xmlSetExternalEntityLoader([](const char* url, const char*, xmlParserCtxtPtr c) -> xmlParserInputPtr {
        if (c && url && strncmp(url, "/tmp/stitch_lx_", 15) == 0 && !strstr(url, "..")) return xmlNewInputFromFile(c, url);
        return nullptr;
    });
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
// Namespace-well-formed (what canonical XML requires): no prefixed name without a namespace
// (libxml2 keeps "p:x" as a plain name when p is undeclared) and no two attributes with the same
// expanded name on an element.
inline bool ns_wellformed(xmlDocPtr d) {
    std::vector<xmlNodePtr> st;
    for (xmlNodePtr c = d->children; c; c = c->next) st.push_back(c);
    while (!st.empty()) {
        xmlNodePtr x = st.back();
        st.pop_back();
        if (x->type != XML_ELEMENT_NODE) continue;
        if (!x->ns && x->name && strchr((const char*)x->name, ':')) return false;
        for (xmlAttrPtr a = x->properties; a; a = a->next) {
            if (!a->ns && a->name && strchr((const char*)a->name, ':')) return false;
            for (xmlAttrPtr b = a->next; b; b = b->next)
                if (xmlStrEqual(a->name, b->name) && xmlStrEqual(a->ns ? a->ns->href : nullptr, b->ns ? b->ns->href : nullptr))
                    return false;
        }
        for (xmlNodePtr c = x->children; c; c = c->next) st.push_back(c);
    }
    return true;
}

// Two absolute URIs are the same: equal strings, or equal after percent-decoding and removing dot
// segments, with an empty path read as "/" when
// there is an authority (RFC 3986 6.2.2 / 6.2.3), and an empty user read as no user.
inline bool same_uri(const xmlChar* a, const xmlChar* b) {
    if (xmlStrEqual(a, b)) return true;
    xmlURIPtr x = xmlParseURI((const char*)a), y = xmlParseURI((const char*)b);
    auto norm = [](const char* v) {
        std::string out;
        if (v) {
            char* u = xmlURIUnescapeString(v, 0, nullptr);
            if (u) { out = u; xmlFree(u); }
        }
        return out;
    };
    bool same = true;  // (a side that does not parse cannot be normalized: no verdict)
    if (x && y) {
        auto path = [&](xmlURIPtr u) {
            std::string p = norm(u->path);
            if (p.empty() && (u->server || u->user)) p = "/";
            std::vector<char> b(p.begin(), p.end());
            b.push_back(0);
            xmlNormalizeURIPath(b.data());  // (dot segments)
            return std::string(b.data());
        };
        same = norm(x->scheme) == norm(y->scheme) && norm(x->server) == norm(y->server) && x->port == y->port &&
               norm(x->user) == norm(y->user) && path(x) == path(y) && norm(x->query_raw ? x->query_raw : x->query) == norm(y->query_raw ? y->query_raw : y->query) &&
               norm(x->fragment) == norm(y->fragment) && norm(x->opaque) == norm(y->opaque);
    }
    xmlFreeURI(x);
    xmlFreeURI(y);
    return same;
}
// An in-memory resource loader (xmlCtxtSetResourceLoader and friends): every external DTD,
// entity and XInclude gets small fixed content, so these code paths run without files.
inline xmlParserErrors mem_loader(void*, const char* url, const char*, xmlResourceType type, xmlParserInputFlags flags,
                                  xmlParserInput** out) {
    const char* body;
    switch (type) {
        case XML_RESOURCE_DTD: body = "<!ELEMENT a ANY><!ATTLIST a x CDATA 'dflt' id ID #IMPLIED><!ENTITY ext 'ext-text'>"; break;
        case XML_RESOURCE_PARAMETER_ENTITY: body = "<!ENTITY pent 'from-pe'>"; break;
        case XML_RESOURCE_GENERAL_ENTITY: body = "ext <b>entity</b> content"; break;
        case XML_RESOURCE_XINCLUDE_TEXT: body = "included text & more"; break;
        default: body = "<inc xml:id='i1'><a x='1'>included</a><b/></inc>"; break;
    }
    if (url && strstr(url, "missing")) return XML_IO_ENOENT;
    if (url && strncmp(url, "/tmp/stitch_lx_", 15) == 0) return xmlNewInputFromUrl(url, flags, out);  // files stubs wrote
    *out = xmlNewInputFromString(url, body, (xmlParserInputFlags)(flags | XML_INPUT_BUF_STATIC));
    return *out ? XML_ERR_OK : XML_ERR_NO_MEMORY;
}
// a subtree that uses namespaces (copies of it declare them again)
inline bool uses_ns(xmlNodePtr n) {
    if (n->type == XML_ELEMENT_NODE) {
        if (n->ns || n->nsDef) return true;
        for (xmlAttrPtr a = n->properties; a; a = a->next) if (a->ns) return true;
    }
    for (xmlNodePtr c = n->children; c && n->type != XML_ENTITY_REF_NODE; c = c->next)
        if (uses_ns(c)) return true;
    return false;
}
// a reference to an entity the document does not declare (possible when the internal subset has
// unresolved parameter entity references; such references cannot survive serialization)
inline bool has_undeclared_ref(xmlDocPtr d) {
    std::vector<xmlNodePtr> st;
    for (xmlNodePtr c = d->children; c; c = c->next) st.push_back(c);
    while (!st.empty()) {
        xmlNodePtr x = st.back();
        st.pop_back();
        if (x->type == XML_ENTITY_REF_NODE && !xmlGetDocEntity(d, x->name)) return true;
        if (x->type == XML_ELEMENT_NODE)
            for (xmlNodePtr c = x->children; c; c = c->next) st.push_back(c);
    }
    return false;
}
// Temporary files the stubs write (the entity loader lets the parser read them), in-memory I/O
// callbacks.
inline std::string tmp_path(const char* tag) { return std::string("/tmp/stitch_lx_") + tag + "_" + std::to_string(getpid()); }
inline bool write_file(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}
inline std::string read_file(const std::string& path) {
    std::string out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    char tmp[4096];
    size_t k;
    while ((k = fread(tmp, 1, sizeof tmp, f)) > 0) out.append(tmp, k);
    fclose(f);
    return out;
}
struct StrIO { std::string data; size_t off = 0; };
inline int io_read(void* ctx, char* buf, int len) {
    StrIO* s = (StrIO*)ctx;
    size_t n = std::min((size_t)len, s->data.size() - s->off);
    memcpy(buf, s->data.data() + s->off, n);
    s->off += n;
    return (int)n;
}
inline int io_close(void*) { return 0; }
inline int str_write(void* ctx, const char* buf, int len) {
    ((std::string*)ctx)->append(buf, (size_t)len);
    return len;
}
inline int str_close(void*) { return 0; }
}  // namespace lx
