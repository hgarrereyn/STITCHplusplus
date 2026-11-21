#include "../objects.hpp"

static int wellformed(Borrow<Doc>& d) { return (int)stitch::get(d, "wellformed", 0); }

// A well-formed document serializes to XML that parses again and serializes the same.
void serialize(Borrow<Doc> d, Range<0, 1> format) {
    std::string a = stitch::get(d, "html", 0) ? "" : lx::dump(*d, (int)(long long)format);
    if (stitch::get(d, "html", 0)) {
        xmlChar* mem = nullptr;
        int n = 0;
        htmlDocDumpMemoryFormat(*d, &mem, &n, (int)(long long)format);
        xmlFree(mem);
        return;
    }
    if (!wellformed(d)) return;
    stitch::log("serialized: %s", a.size() > 600 ? (a.substr(0, 300) + " ... " + a.substr(a.size() - 300)).c_str() : a.c_str());
    xmlDocPtr again = xmlReadMemory(a.data(), (int)a.size(), "again.xml", nullptr, lx::kQuiet);
    if (!again) {
        const xmlError* e = xmlGetLastError();
        if (e) stitch::log("reparse error %d at line %d col %d: %s", e->code, e->line, e->int2, e->message ? e->message : "");
    }
    stitch::check(again != nullptr, "serialized document parses");
    // (a tree built in memory can hold what parsing never produces, e.g. empty text nodes, so the
    // fixed point is checked from the first reparse on)
    std::string b = lx::dump(again, 0);
    xmlFreeDoc(again);
    xmlDocPtr third = xmlReadMemory(b.data(), (int)b.size(), "again.xml", nullptr, lx::kQuiet);
    stitch::check(third != nullptr, "reserialized document parses");
    std::string c = lx::dump(third, 0);
    xmlFreeDoc(third);
    if (b != c) stitch::log("reserialized: %s\nthird: %s", b.c_str(), c.c_str());
    stitch::check(b == c, "serialization is a fixed point after one parse");
}
STITCH_ENDPOINT(serialize);

void c14n(Borrow<Doc> d, Range<0, 2> mode, Bool comments) {
    xmlChar* out = nullptr;
    int n = xmlC14NDocDumpMemory(*d, nullptr, (int)(long long)mode, nullptr, comments ? 1 : 0, &out);
    if (out) xmlFree(out);
    stitch::require(n >= 0, "c14n failed");
}
STITCH_ENDPOINT(c14n);

void validate(Borrow<Doc> d) {
    stitch::require((*d)->intSubset != nullptr, "no DTD");
    xmlValidCtxtPtr v = xmlNewValidCtxt();
    v->error = nullptr;
    v->warning = nullptr;
    xmlValidateDocument(v, *d);
    xmlFreeValidCtxt(v);
}
STITCH_ENDPOINT(validate);

New<Doc> copy_doc(Borrow<Doc> d, Bool xinclude) {
    xmlDocPtr c = xmlCopyDoc(*d, 1);
    stitch::require(c, "copy failed");
    if (xinclude) xmlXIncludeProcessFlags(c, lx::kQuiet);  // (on the copy: it replaces nodes)
    New<Doc> out(c);
    stitch::set(out, "wellformed", 0);
    return out;
}
STITCH_ENDPOINT(copy_doc);

std::optional<New<Node>> root(Borrow<Doc> d) {
    xmlNodePtr r = xmlDocGetRootElement(*d);
    if (!r) return std::nullopt;
    return New<Node>(r);
}
STITCH_ENDPOINT(root);

std::optional<New<Node>> child(Borrow<Node> n, Range<0, 7> i) {
    int k = 0;
    for (xmlNodePtr c = (*n)->children; c; c = c->next)
        if (c->type == XML_ELEMENT_NODE && k++ == (long long)i) return New<Node>(c);
    return std::nullopt;
}
STITCH_ENDPOINT(child);

std::optional<New<Node>> parent(Borrow<Node> n) {
    xmlNodePtr p = (*n)->parent;
    if (!p || p->type != XML_ELEMENT_NODE) return std::nullopt;
    return New<Node>(p);
}
STITCH_ENDPOINT(parent);

New<Node> new_child(Borrow<Node> n, Str<24, XmlName> name, Str<32> text) {
    std::string content = lx::clean(text.s);
    stitch::require(xmlValidateName((const xmlChar*)name.c_str(), 0) == 0, "invalid name");
    xmlNodePtr c = xmlNewTextChild(*n, nullptr, (const xmlChar*)name.c_str(), content.empty() ? nullptr : (const xmlChar*)content.c_str());
    stitch::require(c, "xmlNewTextChild failed");
    stitch::check(c->parent == *n, "new child has the parent");
    return New<Node>(c);
}
STITCH_ENDPOINT(new_child);

void set_prop(Borrow<Node> n, Str<24, XmlName> name, Str<32> text) {
    std::string value = lx::clean(text.s);
    stitch::require(xmlValidateName((const xmlChar*)name.c_str(), 0) == 0, "invalid name");
    xmlSetProp(*n, (const xmlChar*)name.c_str(), (const xmlChar*)value.c_str());
    xmlChar* back = xmlGetNoNsProp(*n, (const xmlChar*)name.c_str());  // (xmlGetProp ignores namespaces: it can return xml:space for "space")
    if (name.s.find(':') == std::string::npos)  // (QNames are set in their namespace; xmlGetProp reads no-namespace attributes)
        stitch::check(back && value == (const char*)back, "xmlGetProp reads what xmlSetProp wrote");
    xmlFree(back);
}
STITCH_ENDPOINT(set_prop);

void unset_prop(Borrow<Node> n, Str<24, XmlName> name) { xmlUnsetProp(*n, (const xmlChar*)name.c_str()); }
STITCH_ENDPOINT(unset_prop);

void add_content(Borrow<Node> n, Str<64> text) {
    xmlNodeAddContent(*n, (const xmlChar*)lx::clean(text.s).c_str());
}
STITCH_ENDPOINT(add_content);

void rename_node(Borrow<Node> n, Str<24, XmlName> name) {
    stitch::require(xmlValidateName((const xmlChar*)name.c_str(), 0) == 0, "invalid name");
    xmlNodeSetName(*n, (const xmlChar*)name.c_str());
}
STITCH_ENDPOINT(rename_node);

void inspect(Borrow<Node> n) {
    xmlChar* c = xmlNodeGetContent(*n);
    xmlFree(c);
    xmlChar* p = xmlGetNodePath(*n);
    xmlFree(p);
    xmlChar* base = xmlNodeGetBase((*n)->doc, *n);
    xmlFree(base);
    xmlChar* l = xmlNodeGetLang(*n);
    xmlFree(l);
    xmlBufferPtr b = xmlBufferCreate();
    xmlNodeDump(b, (*n)->doc, *n, 0, 1);
    xmlBufferFree(b);
    xmlChildElementCount(*n);
}
STITCH_ENDPOINT(inspect);

// Move an element under another one (possibly in another document); never into its own subtree.
void move_node(Borrow<Node> n, Borrow<Node> to) {
    stitch::require(!lx::is_descendant(*to, *n), "would create a cycle");
    stitch::require((*n)->doc == (*to)->doc, "different documents");
    stitch::require((*n)->parent != nullptr && (*n)->parent->type == XML_ELEMENT_NODE, "root or detached");
    xmlUnlinkNode(*n);
    xmlNodePtr r = xmlAddChild(*to, *n);
    stitch::check(r == *n, "xmlAddChild adds an element as is");
    xmlReconciliateNs((*to)->doc, *n);
}
STITCH_ENDPOINT(move_node);

void unlink_node(Borrow<Node> n) {
    stitch::require((*n)->parent != nullptr && (*n)->parent->type == XML_ELEMENT_NODE, "root or detached");
    xmlUnlinkNode(*n);  // leaks: other handles may point into it
}
STITCH_ENDPOINT(unlink_node);

void encode_entities(Borrow<Doc> d, Str<128> text) {
    xmlChar* e = xmlEncodeEntitiesReentrant(*d, (const xmlChar*)text.c_str());
    xmlFree(e);
    xmlChar* s = xmlEncodeSpecialChars(*d, (const xmlChar*)text.c_str());
    xmlFree(s);
}
STITCH_ENDPOINT(encode_entities);
