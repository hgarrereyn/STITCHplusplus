#include "../objects.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>
#include <libxml/xmlwriter.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlschemastypes.h>
#include <libxml/relaxng.h>
#include <libxml/schematron.h>
#include <libxml/pattern.h>
#include <libxml/debugXML.h>
#include <libxml/list.h>
#include <libxml/hash.h>
#include <libxml/HTMLparser.h>
#include <libxml/entities.h>
#include <strings.h>

void sax_vs_dom(Str<2048, XmlText> text) {
    lx::silence();
    xmlDocPtr d = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, lx::kQuiet);
    stitch::require(d, "not well-formed");
    bool dtd = d->intSubset != nullptr;
    int elems = lx::count_elements(d);
    xmlFreeDoc(d);
    stitch::require(!dtd, "has a DTD (entities need a SAX getEntity handler)");
    xmlSAXHandler h; memset(&h, 0, sizeof h);
    h.initialized = XML_SAX2_MAGIC;
    struct C { int starts = 0, ends = 0; } c;
    h.startElementNs = [](void* u, const xmlChar*, const xmlChar*, const xmlChar*, int, const xmlChar**, int, int, const xmlChar**) { ((C*)u)->starts++; };
    h.endElementNs = [](void* u, const xmlChar*, const xmlChar*, const xmlChar*) { ((C*)u)->ends++; };
    xmlSAXUserParseMemory(&h, &c, text.data(), (int)text.size());
    stitch::check(c.starts == elems && c.ends == elems, "SAX2 sees as many elements as the DOM has");
}
STITCH_ENDPOINT(sax_vs_dom);

void parse_balanced_chunk(Borrow<Doc> d, Str<512, XmlText> text) {
    xmlNodePtr list = nullptr;
    int r = xmlParseBalancedChunkMemory(*d, nullptr, nullptr, 0, (const xmlChar*)text.c_str(), &list);
    if (r == 0) { for (xmlNodePtr n = list; n; n = n->next) stitch::check(n->doc == *d || n->doc == nullptr, "nodes of a balanced chunk belong to the document"); }
    xmlFreeNodeList(list);
}
STITCH_ENDPOINT(parse_balanced_chunk);

void parse_in_node_context(Borrow<Node> n, Str<256, XmlText> text) {
    xmlNodePtr list = nullptr;
    xmlParserErrors e = xmlParseInNodeContext(*n, text.data(), (int)text.size(), lx::kQuiet, &list);
    if (e == XML_ERR_OK) for (xmlNodePtr x = list; x; x = x->next) stitch::check(x->doc == (*n)->doc, "parsed nodes belong to the node's document");
    xmlFreeNodeList(list);
}
STITCH_ENDPOINT(parse_in_node_context);

void ctxt_reuse(Str<1024, XmlText> a, Str<1024, XmlText> b) {
    lx::silence();
    xmlParserCtxtPtr c = xmlNewParserCtxt();
    xmlDocPtr d1 = xmlCtxtReadMemory(c, a.data(), (int)a.size(), "a.xml", nullptr, lx::kQuiet);
    xmlDocPtr d2 = xmlCtxtReadMemory(c, b.data(), (int)b.size(), "b.xml", nullptr, lx::kQuiet);
    xmlDocPtr d3 = xmlReadMemory(b.data(), (int)b.size(), "b.xml", nullptr, lx::kQuiet);
    stitch::check((d2 == nullptr) == (d3 == nullptr), "a reused parser context gives the same verdict as a fresh one");
    if (d2 && d3) stitch::check(lx::dump(d2, 0) == lx::dump(d3, 0), "and the same document");
    xmlCtxtReset(c);
    xmlFreeDoc(d1); xmlFreeDoc(d2); xmlFreeDoc(d3); xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(ctxt_reuse);

void pattern_match(Borrow<Doc> d, Str<32, XPathExpr> pat) {
    xmlPatternPtr p = xmlPatterncompile((const xmlChar*)pat.c_str(), (*d)->dict, 0, nullptr);
    stitch::require(p, "not a pattern");
    std::function<void(xmlNodePtr)> walk = [&](xmlNodePtr x) { for (; x; x = x->next) { if (x->type == XML_ELEMENT_NODE) { xmlPatternMatch(p, x); walk(x->children); } } };
    walk((*d)->children);
    xmlStreamCtxtPtr s = xmlPatternGetStreamCtxt(p);
    if (s) {
        xmlStreamPush(s, (const xmlChar*)"r", nullptr);
        xmlStreamPush(s, (const xmlChar*)"a", nullptr);
        xmlStreamPop(s); xmlStreamPop(s);
        xmlFreeStreamCtxt(s);
    }
    xmlPatternMaxDepth(p); xmlPatternMinDepth(p); xmlPatternStreamable(p);
    xmlFreePattern(p);
}
STITCH_ENDPOINT(pattern_match);

void html_push_vs_oneshot(Str<2048, HtmlText> text, Range<1, 64> chunk) {
    lx::silence();
    htmlDocPtr a = htmlReadMemory(text.data(), (int)text.size(), "a.html", nullptr, lx::kQuiet);
    htmlParserCtxtPtr c = htmlCreatePushParserCtxt(nullptr, nullptr, nullptr, 0, "a.html", XML_CHAR_ENCODING_UTF8);
    htmlCtxtUseOptions(c, lx::kQuiet);
    for (size_t off = 0; off < text.size(); off += (size_t)chunk.v) htmlParseChunk(c, text.data() + off, (int)std::min<size_t>(chunk.v, text.size() - off), 0);
    htmlParseChunk(c, nullptr, 0, 1);
    htmlDocPtr b = c->myDoc;
    htmlFreeParserCtxt(c);
    if (a && b) {
        xmlChar *ma = nullptr, *mb = nullptr; int na = 0, nb = 0;
        htmlDocDumpMemory(a, &ma, &na); htmlDocDumpMemory(b, &mb, &nb);
        xmlFree(ma); xmlFree(mb);
    }
    xmlFreeDoc(a); xmlFreeDoc(b);
}
STITCH_ENDPOINT(html_push_vs_oneshot);

void html_lookups(Str<16> name) {
    const htmlElemDesc* e = htmlTagLookup((const xmlChar*)name.c_str());
    if (e) { stitch::check(xmlStrcasecmp((const xmlChar*)e->name, (const xmlChar*)name.c_str()) == 0, "htmlTagLookup finds the tag"); }
    const htmlEntityDesc* en = htmlEntityLookup((const xmlChar*)name.c_str());
    if (en) { stitch::check(htmlEntityValueLookup(en->value) != nullptr, "an entity's value maps back to an entity"); }
    htmlIsBooleanAttr((const xmlChar*)name.c_str());
    htmlIsScriptAttribute((const xmlChar*)name.c_str());
    htmlElementAllowedHere(htmlTagLookup((const xmlChar*)"div"), (const xmlChar*)name.c_str());
}
STITCH_ENDPOINT(html_lookups);

void html_encode(Str<64> text) {
    unsigned char out[512]; int outlen = sizeof out, inlen = (int)strlen(text.c_str());
    int r = htmlEncodeEntities(out, &outlen, (const unsigned char*)text.c_str(), &inlen, '"');
    if (r == 0) { std::string o((char*)out, outlen); stitch::check(o.find('<') == std::string::npos && o.find('"') == std::string::npos, "htmlEncodeEntities escapes markup"); }
    outlen = sizeof out; inlen = (int)strlen(text.c_str());
    UTF8ToHtml(out, &outlen, (const unsigned char*)text.c_str(), &inlen);
}
STITCH_ENDPOINT(html_encode);

void entities_ops(Borrow<Doc> d, Str<16, XmlName> name, Str<32> content) {
    std::string c = lx::clean(content.s);
    stitch::require(xmlValidateName((const xmlChar*)name.c_str(), 0) == 0, "invalid name");
    // (the content is an entity value: '&' and '%' start references and must be escaped)
    stitch::require(c.find('&') == std::string::npos && c.find('%') == std::string::npos, "unescaped & or %");
    stitch::require((*d)->intSubset != nullptr || xmlCreateIntSubset(*d, (const xmlChar*)"r", nullptr, nullptr) != nullptr, "no DTD");
    xmlEntityPtr e = xmlAddDocEntity(*d, (const xmlChar*)name.c_str(), XML_INTERNAL_GENERAL_ENTITY, nullptr, nullptr, (const xmlChar*)c.c_str());
    if (e) {
        stitch::check(xmlGetDocEntity(*d, (const xmlChar*)name.c_str()) == e, "xmlGetDocEntity finds an added entity");
        xmlNodePtr ref = xmlNewReference(*d, (const xmlChar*)name.c_str());
        stitch::check(ref && ref->type == XML_ENTITY_REF_NODE, "xmlNewReference");
        xmlChar* s = xmlNodeListGetString(*d, ref, 1); xmlFree(s);
        xmlFreeNode(ref);
    }
    xmlGetPredefinedEntity((const xmlChar*)name.c_str());
}
STITCH_ENDPOINT(entities_ops);

void c14n_variants(Borrow<Doc> d, Range<0, 2> mode, Bool comments, Str<32, XPathExpr> expr) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)expr.c_str(), c);
    xmlNodeSetPtr set = (o && o->type == XPATH_NODESET) ? o->nodesetval : nullptr;
    xmlChar* prefixes[] = {(xmlChar*)"a", nullptr};
    xmlChar* out = nullptr;
    xmlC14NDocDumpMemory(*d, set, (int)mode.v, mode.v == XML_C14N_EXCLUSIVE_1_0 ? prefixes : nullptr, comments ? 1 : 0, &out);
    if (out) {
        std::string s = (char*)out;
        xmlDocPtr again = xmlReadMemory(s.data(), (int)s.size(), "c.xml", nullptr, lx::kQuiet);
        if (again && !set && lx::ns_wellformed(*d)) {
            xmlChar* out2 = nullptr;
            xmlC14NDocDumpMemory(again, nullptr, (int)mode.v, nullptr, comments ? 1 : 0, &out2);
            stitch::check(out2 && s == (char*)out2, "canonical XML is a fixed point");
            xmlFree(out2);
        }
        xmlFreeDoc(again);
    }
    xmlFree(out); xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(c14n_variants);

void debug_dump(Borrow<Doc> d) {
    FILE* f = fopen("/dev/null", "w");
    xmlDebugDumpDocument(f, *d);
    xmlDebugCheckDocument(f, *d);
    if (xmlDocGetRootElement(*d)) xmlDebugDumpNode(f, xmlDocGetRootElement(*d), 2);
    xmlElemDump(f, *d, xmlDocGetRootElement(*d));
    fclose(f);
}
STITCH_ENDPOINT(debug_dump);

void save_options(Borrow<Doc> d, Flags<XML_SAVE_FORMAT, XML_SAVE_NO_DECL, XML_SAVE_NO_EMPTY, XML_SAVE_NO_XHTML, XML_SAVE_XHTML, XML_SAVE_AS_XML, XML_SAVE_AS_HTML, XML_SAVE_WSNONSIG> opts, OneOf<0, 1, 2, 3> enc) {
    static const char* encs[] = {nullptr, "UTF-8", "ISO-8859-1", "US-ASCII"};
    xmlBufferPtr b = xmlBufferCreate();
    xmlSaveCtxtPtr s = xmlSaveToBuffer(b, encs[(int)enc], (int)opts);
    stitch::require(s, "no save context");
    xmlSaveDoc(s, *d);
    xmlSaveClose(s);
    // (without a declaration, output in the document's own non-UTF-8 encoding cannot be read back)
    bool utf8 = (int)enc == 1 || !(*d)->encoding || !xmlStrcasecmp((*d)->encoding, (const xmlChar*)"UTF-8");
    bool needs_decl = ((int)opts & XML_SAVE_NO_DECL) && !utf8;
    if (!((int)opts & (XML_SAVE_AS_HTML | XML_SAVE_XHTML)) && (int)enc <= 1 && !needs_decl && stitch::get(d, "wellformed", 0) && !stitch::get(d, "html", 0)) {
        std::string out((const char*)xmlBufferContent(b), xmlBufferLength(b));
        xmlDocPtr again = xmlReadMemory(out.data(), (int)out.size(), "s.xml", nullptr, lx::kQuiet);
        stitch::check(again != nullptr, "saved output parses (" + std::to_string((int)opts) + ")");
        xmlFreeDoc(again);
    }
    xmlBufferFree(b);
}
STITCH_ENDPOINT(save_options);

void encoding_handlers(Str<64> text, OneOf<0, 1, 2, 3, 4, 5, 6, 7> which) {
    static const char* names[] = {"UTF-16LE", "UTF-16BE", "ISO-8859-1", "ASCII", "UTF-16", "ISO-8859-2", "EUC-JP", "UCS-4"};
    xmlCharEncodingHandlerPtr h = xmlFindCharEncodingHandler(names[(int)which]);
    stitch::require(h, "no handler");
    std::string in = lx::clean(text.s);
    xmlBufferPtr src = xmlBufferCreate(); xmlBufferAdd(src, (const xmlChar*)in.data(), (int)in.size());
    xmlBufferPtr enc = xmlBufferCreate();
    int r = xmlCharEncOutFunc(h, enc, src);
    if (r >= 0) {
        xmlBufferPtr back = xmlBufferCreate();
        int r2 = xmlCharEncInFunc(h, back, enc);
        bool repr = (int)which < 2 || (int)which == 4 || (int)which == 7 || std::all_of(in.begin(), in.end(), [&](char c) { return (unsigned char)c < ((int)which == 3 ? 0x80 : 0x80); });
        if (r2 >= 0 && repr) stitch::check(std::string((const char*)xmlBufferContent(back), xmlBufferLength(back)) == in, std::string("decode(encode(s)) == s for ") + names[(int)which]);
        xmlBufferFree(back);
    }
    xmlBufferFree(enc); xmlBufferFree(src);
    xmlCharEncCloseFunc(h);
}
STITCH_ENDPOINT(encoding_handlers);

void isolat1_utf8(Bytes<64> data) {
    unsigned char out[256]; int outlen = sizeof out, inlen = (int)data.size();
    int r = isolat1ToUTF8(out, &outlen, data.data(), &inlen);
    if (r >= 0 && inlen == (int)data.size()) {
        unsigned char back[256]; int blen = sizeof back, in2 = outlen;
        int r2 = UTF8Toisolat1(back, &blen, out, &in2);
        stitch::check(r2 >= 0 && blen == (int)data.size() && memcmp(back, data.data(), data.size()) == 0, "UTF8Toisolat1(isolat1ToUTF8(x)) == x");
    }
}
STITCH_ENDPOINT(isolat1_utf8);

void detect_encoding(Bytes<8> data) {
    xmlCharEncoding e = xmlDetectCharEncoding(data.data(), (int)data.size());
    const char* n = xmlGetCharEncodingName(e);
    if (n) stitch::check(xmlParseCharEncoding(n) == e || e == XML_CHAR_ENCODING_UTF8 || e == XML_CHAR_ENCODING_EBCDIC || e == XML_CHAR_ENCODING_UCS4_2143 || e == XML_CHAR_ENCODING_UCS4_3412, std::string("xmlParseCharEncoding(xmlGetCharEncodingName(e)) == e: ") + n);
}
STITCH_ENDPOINT(detect_encoding);
