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

void new_nodes(Borrow<Doc> d, Str<16, XmlName> name, Str<32> content, Range<0, 7> kind) {
    std::string c = lx::clean(content.s);
    stitch::require(xmlValidateName((const xmlChar*)name.c_str(), 0) == 0, "invalid name");
    xmlNodePtr n = nullptr;
    switch (kind.v) {
        case 0: n = xmlNewDocNode(*d, nullptr, (const xmlChar*)name.c_str(), (const xmlChar*)c.c_str()); break;
        case 1: n = xmlNewDocComment(*d, (const xmlChar*)c.c_str()); break;
        case 2: n = xmlNewCDataBlock(*d, (const xmlChar*)c.c_str(), (int)c.size()); break;
        case 3: n = xmlNewDocPI(*d, (const xmlChar*)name.c_str(), (const xmlChar*)c.c_str()); break;
        case 4: n = xmlNewDocText(*d, (const xmlChar*)c.c_str()); break;
        case 5: n = xmlNewCharRef(*d, (const xmlChar*)("#" + std::to_string(c.size() + 65)).c_str()); break;
        case 6: n = xmlNewDocRawNode(*d, nullptr, (const xmlChar*)name.c_str(), (const xmlChar*)c.c_str()); break;
        default: n = xmlNewDocFragment(*d); break;
    }
    stitch::require(n, "no node");
    xmlChar* got = xmlNodeGetContent(n);
    if (kind.v == 1 || kind.v == 2 || kind.v == 4) stitch::check(got && c == (char*)got, "content of a new text-like node");
    xmlFree(got);
    xmlNodePtr root = xmlDocGetRootElement(*d);
    if (root && kind.v != 7) { xmlAddChild(root, n); } else xmlFreeNode(n);
}
STITCH_ENDPOINT(new_nodes);

void sibling_ops(Borrow<Node> n, Str<16, XmlName> name, Range<0, 2> where) {
    stitch::require(xmlValidateName((const xmlChar*)name.c_str(), 0) == 0, "invalid name");
    stitch::require((*n)->parent && (*n)->parent->type == XML_ELEMENT_NODE, "no element parent");
    xmlNodePtr e = xmlNewDocNode((*n)->doc, nullptr, (const xmlChar*)name.c_str(), nullptr);
    xmlNodePtr r = where.v == 0 ? xmlAddPrevSibling(*n, e) : where.v == 1 ? xmlAddNextSibling(*n, e) : xmlAddSibling(*n, e);
    stitch::check(r == e, "an element sibling is added as is");
    stitch::check(e->parent == (*n)->parent, "the sibling has the same parent");
    if (where.v == 0) stitch::check(e->next == *n && (*n)->prev == e, "xmlAddPrevSibling links before");
    if (where.v == 1) stitch::check(e->prev == *n && (*n)->next == e, "xmlAddNextSibling links after");
    stitch::check(xmlGetLastChild((*n)->parent) != nullptr && xmlChildElementCount((*n)->parent) >= 2, "the parent has both");
}
STITCH_ENDPOINT(sibling_ops);

void element_navigation(Borrow<Node> n) {
    xmlNodePtr first = xmlFirstElementChild(*n), last = xmlLastElementChild(*n);
    unsigned long count = xmlChildElementCount(*n);
    stitch::check((first == nullptr) == (count == 0) && (last == nullptr) == (count == 0), "first / last element child exist iff there are children");
    unsigned long k = 0;
    for (xmlNodePtr c = first; c; c = xmlNextElementSibling(c)) { k++; if (k > count + 1) break; }
    stitch::check(k == count, "xmlNextElementSibling walks every element child");
    k = 0;
    for (xmlNodePtr c = last; c; c = xmlPreviousElementSibling(c)) { k++; if (k > count + 1) break; }
    stitch::check(k == count, "xmlPreviousElementSibling walks every element child");
    xmlGetLineNo(*n);
    xmlNodeIsText(*n);
    xmlIsBlankNode(*n);
}
STITCH_ENDPOINT(element_navigation);

void copy_node_ops(Borrow<Node> n, Range<0, 2> extended) {
    xmlNodePtr c = xmlCopyNode(*n, (int)extended.v);
    stitch::require(c, "copy failed");
    if (extended.v == 1 && !lx::uses_ns(*n)) {  // (a detached copy declares the namespaces it uses)
        xmlBufferPtr a = xmlBufferCreate(), b = xmlBufferCreate();
        xmlNodeDump(a, (*n)->doc, *n, 0, 0);
        xmlSetTreeDoc(c, (*n)->doc);
        xmlNodeDump(b, (*n)->doc, c, 0, 0);
        stitch::check(std::string((const char*)xmlBufferContent(a)) == (const char*)xmlBufferContent(b), "a recursive copy serializes like the original");
        xmlBufferFree(a); xmlBufferFree(b);
    }
    xmlFreeNode(c);
    xmlNodePtr dc = xmlDocCopyNode(*n, (*n)->doc, 1); xmlFreeNode(dc);
    xmlNodePtr lst = xmlCopyNodeList((*n)->children); xmlFreeNodeList(lst);
}
STITCH_ENDPOINT(copy_node_ops);

void ns_ops(Borrow<Node> n, Str<8, XmlName> prefix, Str<32, UriText> href) {
    stitch::require(xmlValidateNCName((const xmlChar*)prefix.c_str(), 0) == 0, "invalid prefix");
    stitch::require(strcmp(prefix.c_str(), "xml") && strcmp(prefix.c_str(), "xmlns"), "reserved prefix");
    stitch::require(lx::xml_chars_strict(href.s.substr(0, strlen(href.c_str()))), "href is not UTF-8 text");
    xmlNsPtr ns = xmlNewNs(*n, (const xmlChar*)href.c_str(), (const xmlChar*)prefix.c_str());
    if (ns) {
        stitch::check(xmlSearchNs((*n)->doc, *n, (const xmlChar*)prefix.c_str()) == ns, "xmlSearchNs finds the declaration");
        xmlNsPtr byhref = xmlSearchNsByHref((*n)->doc, *n, (const xmlChar*)href.c_str());
        stitch::check(byhref && xmlStrEqual(byhref->href, (const xmlChar*)href.c_str()), "xmlSearchNsByHref finds the namespace");
        xmlSetNsProp(*n, ns, (const xmlChar*)"attr", (const xmlChar*)"v");
        xmlChar* v = xmlGetNsProp(*n, (const xmlChar*)"attr", (const xmlChar*)href.c_str());
        stitch::check(v && xmlStrEqual(v, (const xmlChar*)"v"), "xmlGetNsProp reads what xmlSetNsProp wrote");
        xmlFree(v);
        stitch::check(xmlHasNsProp(*n, (const xmlChar*)"attr", (const xmlChar*)href.c_str()) != nullptr, "xmlHasNsProp");
        xmlNsPtr* list = xmlGetNsList((*n)->doc, *n); xmlFree(list);
    }
}
STITCH_ENDPOINT(ns_ops);

void node_lang_space_base(Borrow<Node> n, Str<8> lang, Bool preserve, Str<32, UriText> base) {
    std::string l = lx::clean(lang.s);
    xmlNodeSetLang(*n, (const xmlChar*)l.c_str());
    xmlChar* got = xmlNodeGetLang(*n);
    stitch::check(got && l == (char*)got, "xmlNodeGetLang reads xmlNodeSetLang");
    xmlFree(got);
    xmlNodeSetSpacePreserve(*n, preserve ? 1 : 0);
    stitch::check(xmlNodeGetSpacePreserve(*n) == (preserve ? 1 : 0), "xmlNodeGetSpacePreserve reads xmlNodeSetSpacePreserve");
    std::string b = lx::clean(base.s);
    xmlNodeSetBase(*n, (const xmlChar*)b.c_str());
    xmlChar* gb = xmlNodeGetBase((*n)->doc, *n); xmlFree(gb);
}
STITCH_ENDPOINT(node_lang_space_base);

void text_concat_merge(Borrow<Node> n, Str<32> a, Str<32> b) {
    std::string x = lx::clean(a.s), y = lx::clean(b.s);
    xmlNodePtr t1 = xmlNewDocText((*n)->doc, (const xmlChar*)x.c_str());
    xmlNodePtr t2 = xmlNewDocText((*n)->doc, (const xmlChar*)y.c_str());
    xmlTextConcat(t1, (const xmlChar*)"!", 1);
    xmlNodePtr m = xmlTextMerge(t1, t2);
    stitch::check(m == t1, "xmlTextMerge merges into the first node");
    xmlChar* c = xmlNodeGetContent(m);
    stitch::check(c && x + "!" + y == (char*)c, "merged content");
    xmlFree(c);
    xmlAddChild(*n, m);
}
STITCH_ENDPOINT(text_concat_merge);

void string_node_list(Borrow<Doc> d, Str<64, XmlText> text) {
    std::string t = lx::clean(text.s);
    xmlNodePtr l = xmlStringGetNodeList(*d, (const xmlChar*)t.c_str());
    xmlChar* back = xmlNodeListGetString(*d, l, 0);
    xmlChar* back1 = xmlNodeListGetString(*d, l, 1);
    xmlChar* raw = xmlNodeListGetRawString(*d, l, 0);
    xmlFree(back); xmlFree(back1); xmlFree(raw);
    xmlFreeNodeList(l);
    xmlNodePtr l2 = xmlStringLenGetNodeList(*d, (const xmlChar*)t.c_str(), (int)t.size() / 2);
    xmlFreeNodeList(l2);
}
STITCH_ENDPOINT(string_node_list);

void replace_node(Borrow<Node> n, Str<16, XmlName> name) {
    stitch::require(xmlValidateName((const xmlChar*)name.c_str(), 0) == 0, "invalid name");
    stitch::require((*n)->parent && (*n)->parent->type == XML_ELEMENT_NODE, "no element parent");
    xmlNodePtr parent = (*n)->parent;
    xmlNodePtr e = xmlNewDocNode((*n)->doc, nullptr, (const xmlChar*)name.c_str(), nullptr);
    xmlNodePtr old = xmlReplaceNode(*n, e);
    stitch::check(old == *n && e->parent == parent && (*n)->parent == nullptr, "xmlReplaceNode swaps the nodes");
}
STITCH_ENDPOINT(replace_node);

void dom_wrap_adopt(Borrow<Node> n, Borrow<Doc> other) {
    stitch::require((*n)->doc != *other, "same document");
    stitch::require((*n)->parent && (*n)->parent->type == XML_ELEMENT_NODE, "root or detached");
    xmlNodePtr copy = xmlDocCopyNode(*n, (*n)->doc, 1);
    stitch::require(copy, "copy failed");
    xmlDOMWrapCtxtPtr c = xmlDOMWrapNewCtxt();
    int r = xmlDOMWrapAdoptNode(c, (*n)->doc, copy, *other, nullptr, 0);
    if (r == 0) {
        xmlNodePtr root = xmlDocGetRootElement(*other);
        if (root) { xmlAddChild(root, copy); xmlDOMWrapReconcileNamespaces(c, copy, 0); } else xmlFreeNode(copy);
    } else xmlFreeNode(copy);
    xmlDOMWrapFreeCtxt(c);
}
STITCH_ENDPOINT(dom_wrap_adopt);

void dom_wrap_clone(Borrow<Node> n) {
    xmlNodePtr clone = nullptr;
    xmlDOMWrapCtxtPtr c = xmlDOMWrapNewCtxt();
    int r = xmlDOMWrapCloneNode(c, (*n)->doc, *n, &clone, (*n)->doc, nullptr, 1, 0);
    if (r == 0 && clone) xmlFreeNode(clone);
    xmlDOMWrapFreeCtxt(c);
}
STITCH_ENDPOINT(dom_wrap_clone);

void doc_level_ops(Borrow<Doc> d, Str<16, XmlName> name) {
    stitch::require(xmlValidateName((const xmlChar*)name.c_str(), 0) == 0, "invalid name");
    xmlDtdPtr dtd = xmlGetIntSubset(*d);
    if (!dtd) dtd = xmlCreateIntSubset(*d, (const xmlChar*)name.c_str(), nullptr, nullptr);
    stitch::check(xmlGetIntSubset(*d) == dtd, "xmlGetIntSubset returns the internal subset");
    xmlDocPtr c = xmlCopyDoc(*d, 0);
    xmlFreeDoc(c);
    xmlGetDocCompressMode(*d);
    xmlDocGetRootElement(*d);
}
STITCH_ENDPOINT(doc_level_ops);

void set_content_new_node(Borrow<Doc> d, Str<64, XmlText> text) {
    xmlNodePtr e = xmlNewDocNode(*d, nullptr, (const xmlChar*)"tmp", nullptr);
    std::string t = lx::clean(text.s);
    xmlNodeSetContent(e, (const xmlChar*)t.c_str());
    xmlChar* c = xmlNodeGetContent(e);
    xmlFree(c);
    xmlNodeSetContentLen(e, (const xmlChar*)t.c_str(), (int)t.size() / 2);
    xmlNodeAddContentLen(e, (const xmlChar*)t.c_str(), (int)t.size());
    xmlBufferPtr b = xmlBufferCreate(); xmlNodeBufGetContent(b, e); xmlBufferFree(b);
    xmlFreeNode(e);
}
STITCH_ENDPOINT(set_content_new_node);

void props_iteration(Borrow<Node> n) {
    int k = 0;
    for (xmlAttrPtr a = (*n)->properties; a; a = a->next) {
        stitch::check(a->parent == *n, "attributes point back at their element");
        xmlChar* v = xmlNodeListGetString((*n)->doc, a->children, 1); xmlFree(v);
        stitch::check(xmlHasProp(*n, a->name) != nullptr, "xmlHasProp finds every attribute");
        xmlAttrPtr copy = xmlCopyProp(nullptr, a); xmlFreeProp(copy);
        if (++k > 1000) break;
    }
}
STITCH_ENDPOINT(props_iteration);

void reconciliate_get_path(Borrow<Node> n) {
    xmlReconciliateNs((*n)->doc, *n);
    xmlChar* p = xmlGetNodePath(*n);
    if (p && (*n)->doc) {
        xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
        xmlXPathObjectPtr o = xmlXPathEvalExpression(p, c);
        bool found = false;
        if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) found |= o->nodesetval->nodeTab[i] == *n;
        bool attached = true; for (xmlNodePtr x = *n; x; x = x->parent) if (!x->parent && x->type != XML_DOCUMENT_NODE) attached = false;
        if (attached && o && !strchr((char*)p, ':')) stitch::check(found, std::string("xmlGetNodePath selects the node: ") + (char*)p);
        xmlXPathFreeObject(o); xmlXPathFreeContext(c);
    }
    xmlFree(p);
}
STITCH_ENDPOINT(reconciliate_get_path);
