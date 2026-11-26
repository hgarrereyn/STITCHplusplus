// SAX2.h: a SAX handler whose every callback calls the xmlSAX2* function explicitly (and counts
// the events) builds the same document as the default parser, for XML and HTML.
#include "../objects.hpp"
#include <libxml/SAX2.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>

namespace {
int g_events = 0;  // events of the current parse (reset by each stub before parsing)
void w_internalSubset(void* c, const xmlChar* n, const xmlChar* e, const xmlChar* s) { g_events++; xmlSAX2InternalSubset(c, n, e, s); }
void w_externalSubset(void* c, const xmlChar* n, const xmlChar* e, const xmlChar* s) { g_events++; xmlSAX2ExternalSubset(c, n, e, s); }
int w_isStandalone(void* c) { return xmlSAX2IsStandalone(c); }
int w_hasInternalSubset(void* c) { return xmlSAX2HasInternalSubset(c); }
int w_hasExternalSubset(void* c) { return xmlSAX2HasExternalSubset(c); }
xmlParserInputPtr w_resolveEntity(void* c, const xmlChar* p, const xmlChar* s) { return xmlSAX2ResolveEntity(c, p, s); }
xmlEntityPtr w_getEntity(void* c, const xmlChar* n) { return xmlSAX2GetEntity(c, n); }
xmlEntityPtr w_getParameterEntity(void* c, const xmlChar* n) { return xmlSAX2GetParameterEntity(c, n); }
void w_entityDecl(void* c, const xmlChar* n, int t, const xmlChar* p, const xmlChar* s, xmlChar* v) { g_events++; xmlSAX2EntityDecl(c, n, t, p, s, v); }
void w_notationDecl(void* c, const xmlChar* n, const xmlChar* p, const xmlChar* s) { g_events++; xmlSAX2NotationDecl(c, n, p, s); }
void w_attributeDecl(void* c, const xmlChar* e, const xmlChar* f, int t, int d, const xmlChar* v, xmlEnumerationPtr tree) {
    g_events++;
    xmlSAX2AttributeDecl(c, e, f, t, d, v, tree);
}
void w_elementDecl(void* c, const xmlChar* n, int t, xmlElementContentPtr content) { g_events++; xmlSAX2ElementDecl(c, n, t, content); }
void w_unparsedEntityDecl(void* c, const xmlChar* n, const xmlChar* p, const xmlChar* s, const xmlChar* nd) {
    g_events++;
    xmlSAX2UnparsedEntityDecl(c, n, p, s, nd);
}
void w_setDocumentLocator(void* c, xmlSAXLocatorPtr loc) {
    xmlSAX2SetDocumentLocator(c, loc);
    xmlSAX2GetPublicId(c);
    xmlSAX2GetSystemId(c);
    xmlSAX2GetLineNumber(c);
    xmlSAX2GetColumnNumber(c);
}
void w_startDocument(void* c) { g_events++; xmlSAX2StartDocument(c); }
void w_endDocument(void* c) { g_events++; xmlSAX2EndDocument(c); }
void w_startElementNs(void* c, const xmlChar* l, const xmlChar* p, const xmlChar* u, int nn, const xmlChar** ns, int na, int nd,
                      const xmlChar** attrs) {
    g_events++;
    xmlSAX2StartElementNs(c, l, p, u, nn, ns, na, nd, attrs);
}
void w_endElementNs(void* c, const xmlChar* l, const xmlChar* p, const xmlChar* u) { g_events++; xmlSAX2EndElementNs(c, l, p, u); }
void w_startElement(void* c, const xmlChar* n, const xmlChar** a) { g_events++; xmlSAX2StartElement(c, n, a); }
void w_endElement(void* c, const xmlChar* n) { g_events++; xmlSAX2EndElement(c, n); }
void w_reference(void* c, const xmlChar* n) { g_events++; xmlSAX2Reference(c, n); }
void w_characters(void* c, const xmlChar* ch, int len) { g_events++; xmlSAX2Characters(c, ch, len); }
void w_ignorable(void* c, const xmlChar* ch, int len) { g_events++; xmlSAX2IgnorableWhitespace(c, ch, len); }
void w_pi(void* c, const xmlChar* t, const xmlChar* d) { g_events++; xmlSAX2ProcessingInstruction(c, t, d); }
void w_comment(void* c, const xmlChar* v) { g_events++; xmlSAX2Comment(c, v); }
void w_cdata(void* c, const xmlChar* v, int len) { g_events++; xmlSAX2CDataBlock(c, v, len); }
void w_serror(void*, const xmlError*) {}

// replace each callback the handler has with the wrapper that calls the same xmlSAX2 function
void wrap(xmlSAXHandler* h) {
#define W(field, fn) if (h->field) h->field = fn
    W(internalSubset, w_internalSubset);
    W(externalSubset, w_externalSubset);
    W(isStandalone, w_isStandalone);
    W(hasInternalSubset, w_hasInternalSubset);
    W(hasExternalSubset, w_hasExternalSubset);
    W(resolveEntity, w_resolveEntity);
    W(getEntity, w_getEntity);
    W(getParameterEntity, w_getParameterEntity);
    W(entityDecl, w_entityDecl);
    W(notationDecl, w_notationDecl);
    W(attributeDecl, w_attributeDecl);
    W(elementDecl, w_elementDecl);
    W(unparsedEntityDecl, w_unparsedEntityDecl);
    W(setDocumentLocator, w_setDocumentLocator);
    W(startDocument, w_startDocument);
    W(endDocument, w_endDocument);
    W(reference, w_reference);
    W(characters, w_characters);
    W(processingInstruction, w_pi);
    W(comment, w_comment);
    W(cdataBlock, w_cdata);
    W(startElement, w_startElement);
    W(endElement, w_endElement);
    W(startElementNs, w_startElementNs);
    W(endElementNs, w_endElementNs);
#undef W
    if (h->ignorableWhitespace == xmlSAX2IgnorableWhitespace) h->ignorableWhitespace = w_ignorable;
    else if (h->ignorableWhitespace == xmlSAX2Characters) h->ignorableWhitespace = w_characters;
    h->serror = w_serror;
}
}  // namespace

// sax: 0 xmlSAXVersion(2), 1 xmlSAX2InitDefaultSAXHandler(warnings), 2 SAX1 (xmlSAXVersion(1))
void sax2_wrapped(Str<4096, XmlText> text, Range<0, 2> sax, Flags<XML_PARSE_NOENT, XML_PARSE_DTDATTR, XML_PARSE_NOCDATA, XML_PARSE_NOBLANKS> opts) {
    lx::silence();
    xmlSAXHandler h;
    memset(&h, 0, sizeof h);
    if ((int)sax == 1) xmlSAX2InitDefaultSAXHandler(&h, 1);
    else stitch::check(xmlSAXVersion(&h, (int)sax == 2 ? 1 : 2) == 0, "xmlSAXVersion");
    wrap(&h);
    int o = (int)opts | lx::kQuiet | ((int)sax == 2 ? XML_PARSE_SAX1 : 0);
    xmlDocPtr ref = xmlReadMemory(text.data(), (int)text.size(), "in.xml", nullptr, o);
    xmlParserCtxtPtr c = xmlNewSAXParserCtxt(&h, nullptr);
    stitch::require(c, "no context");
    g_events = 0;
    xmlDocPtr d = xmlCtxtReadMemory(c, text.data(), (int)text.size(), "in.xml", nullptr, o);
    stitch::log("events %d, ref %d, doc %d", g_events, ref != nullptr, d != nullptr);
    stitch::check((ref != nullptr) == (d != nullptr), "the wrapped handler accepts what the default one does");
    if (ref && d) {
        stitch::check(g_events > 0, "callbacks were called");
        stitch::check(lx::dump(ref, 0) == lx::dump(d, 0), "the wrapped handler builds the same document");
    }
    xmlFreeDoc(ref);
    xmlFreeDoc(d);
    xmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(sax2_wrapped);

void sax2_wrapped_html(Str<4096, HtmlText> text, lx::HtmlOpts opts) {
    lx::silence();
    xmlSAXHandler h;
    memset(&h, 0, sizeof h);
    xmlSAX2InitHtmlDefaultSAXHandler(&h);
    wrap(&h);
    int o = (int)opts | lx::kQuiet;
    htmlDocPtr ref = htmlReadMemory(text.data(), (int)text.size(), "in.html", nullptr, o);
    htmlParserCtxtPtr c = htmlNewSAXParserCtxt(&h, nullptr);
    stitch::require(c, "no context");
    g_events = 0;
    htmlDocPtr d = htmlCtxtReadMemory(c, text.data(), (int)text.size(), "in.html", nullptr, o);
    auto dump = [](htmlDocPtr x) {
        xmlChar* m = nullptr;
        int n = 0;
        htmlDocDumpMemory(x, &m, &n);
        std::string s = m ? std::string((const char*)m, (size_t)n) : "";
        xmlFree(m);
        return s;
    };
    stitch::check((ref != nullptr) == (d != nullptr), "the wrapped HTML handler accepts what the default one does");
    if (ref && d) stitch::check(dump(ref) == dump(d), "the wrapped HTML handler builds the same document");
    xmlFreeDoc(ref);
    xmlFreeDoc(d);
    htmlFreeParserCtxt(c);
}
STITCH_ENDPOINT(sax2_wrapped_html);
