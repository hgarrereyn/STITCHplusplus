// Building documents from scratch with the tree.h constructors, and every serializer of
// tree.h / xmlsave.h on the result: all of them write the same bytes, which parse again.
#include "../objects.hpp"
#include <unistd.h>
#include <libxml/xmlsave.h>
#include <libxml/xmlIO.h>

namespace {
const char* kNames[] = {"a", "b", "item", "x-y", "z.1", "_u"};
// text the serializer can always write and the parser reads back (no '&': some constructors
// read entity references in their content)
std::string text(stitch::Source& s, size_t n) {
    std::string t = lx::clean(s.str(n)), o;
    for (char c : t) if (c != '\r' && c != '&') o += c;
    return o;
}
}  // namespace

// A document built node by node with every constructor serializes to well-formed XML.
New<Doc> build_doc(Src<256> s, Bool with_dtd) {
    xmlDocPtr d = xmlNewDoc((const xmlChar*)"1.0");
    stitch::require(d, "no document");
    if (with_dtd) {
        xmlDtdPtr dtd = xmlNewDtd(d, (const xmlChar*)"root", nullptr, (const xmlChar*)"root.dtd");
        stitch::check(dtd && d->extSubset == dtd, "xmlNewDtd sets the external subset");
    }
    xmlNodePtr root = s->coin() ? xmlNewNode(nullptr, (const xmlChar*)"root") : xmlNewNodeEatName(nullptr, xmlStrdup((const xmlChar*)"root"));
    xmlNodePtr old = xmlDocSetRootElement(d, root);
    stitch::check(old == nullptr && xmlDocGetRootElement(d) == root, "DocSetRootElement");
    xmlNsPtr ns = xmlNewNs(root, (const xmlChar*)"urn:p", (const xmlChar*)"p");
    std::vector<xmlNodePtr> elems{root};
    int ops = 0;
    while (s->more() && ops++ < 40) {
        xmlNodePtr parent = elems[s->choice(elems.size())];
        const xmlChar* nm = (const xmlChar*)kNames[s->choice(6)];
        std::string t = text(*s, 16);
        const xmlChar* T = (const xmlChar*)t.c_str();
        switch (s->choice(14)) {
            case 0: elems.push_back(xmlNewChild(parent, s->coin() ? ns : nullptr, nm, s->coin() ? T : nullptr)); break;
            case 1: elems.push_back(xmlNewTextChild(parent, nullptr, nm, T)); break;
            case 2: {
                xmlNodePtr e = xmlNewDocNodeEatName(d, s->coin() ? ns : nullptr, xmlStrdup(nm), nullptr);
                xmlAddChild(parent, e);
                elems.push_back(e);
                break;
            }
            case 3: xmlAddChild(parent, xmlNewText(T)); break;
            case 4: xmlAddChild(parent, xmlNewTextLen(T, xmlUTF8Strsize(T, (int)s->choice((size_t)xmlUTF8Strlen(T) + 1)))); break;
            case 5: xmlAddChild(parent, xmlNewDocTextLen(d, T, (int)t.size())); break;
            case 6: if (t.find("--") == std::string::npos && (t.empty() || t.back() != '-')) xmlAddChild(parent, xmlNewComment(T)); break;
            case 7: {
                // (whitespace after the PI target separates it from the data: data cannot start with it)
                size_t k = t.find_first_not_of(" \t\n");
                std::string data = k == std::string::npos ? "" : t.substr(k);
                if (data.find("?>") == std::string::npos) xmlAddChild(parent, xmlNewPI((const xmlChar*)"pi", (const xmlChar*)data.c_str()));
                break;
            }
            case 8: if (!xmlHasProp(parent, nm)) xmlNewProp(parent, nm, T); break;
            case 9: if (!xmlHasNsProp(parent, nm, ns->href)) xmlNewNsProp(parent, ns, nm, T); break;
            case 10: if (!xmlHasNsProp(parent, nm, ns->href)) xmlNewNsPropEatName(parent, ns, xmlStrdup(nm), T); break;
            case 11: {
                // a list of new siblings added at once
                xmlNodePtr first = xmlNewDocNode(d, nullptr, nm, nullptr);
                xmlNodePtr second = xmlNewDocText(d, T);
                first->next = second;
                second->prev = first;
                xmlAddChildList(parent, first);
                elems.push_back(first);
                break;
            }
            case 12: {
                xmlAttrPtr a = xmlNewDocProp(d, nm, T);  // an unattached attribute
                xmlChar* v = xmlNodeListGetString(d, a->children, 1);
                stitch::check(xmlStrEqual(v, T) || (!v && t.empty()), "DocProp holds the value");
                xmlFree(v);
                xmlFreeProp(a);
                break;
            }
            default: {
                // move the namespace onto an element and back
                xmlSetNs(parent, s->coin() ? ns : nullptr);
                break;
            }
        }
        stitch::check(elems.back() != nullptr, "element created");
    }
    // attribute lookups agree with what was set
    for (xmlNodePtr e : elems)
        for (xmlAttrPtr a = e->properties; a; a = a->next) {
            xmlChar* v = nullptr;
            int rc = xmlNodeGetAttrValue(e, a->name, a->ns ? a->ns->href : nullptr, &v);
            xmlChar* g = a->ns ? xmlGetNsProp(e, a->name, a->ns->href) : xmlGetNoNsProp(e, a->name);
            stitch::check(rc == 0 && xmlStrEqual(v, g), "NodeGetAttrValue == GetNsProp");
            xmlFree(v);
            xmlFree(g);
        }
    New<Doc> out(d);
    stitch::set(out, "wellformed", 1);
    return out;
}
STITCH_ENDPOINT(build_doc);

// Every serializer writes the same bytes as xmlDocDumpMemory (or its formatted / encoded
// variant), and a well-formed document's output parses again.
void serializers(Borrow<Doc> d, Bool format) {
    stitch::require(!stitch::get(d, "html", 0), "HTML document");
    xmlDocPtr doc = *d;
    int fmt = format ? 1 : 0;
    xmlChar* m = nullptr;
    int n = 0;
    if (fmt) xmlDocDumpFormatMemory(doc, &m, &n, 1);
    else xmlDocDumpMemory(doc, &m, &n);
    stitch::require(m, "dump failed");
    std::string want((const char*)m, (size_t)n);
    xmlFree(m);
    xmlDocDumpFormatMemoryEnc(doc, &m, &n, "UTF-8", fmt);
    std::string want_enc = m ? std::string((const char*)m, (size_t)n) : "";
    xmlFree(m);
    std::string path = lx::tmp_path("save");
    // to a FILE
    FILE* f = fopen(path.c_str(), "wb");
    stitch::require(f, "cannot open a file");
    int r = fmt ? xmlDocFormatDump(f, doc, 1) : xmlDocDump(f, doc);
    fclose(f);
    if (r >= 0) stitch::check(lx::read_file(path) == want, "xmlDocDump == xmlDocDumpMemory");
    // to a file name, with and without an encoding
    r = fmt ? xmlSaveFormatFile(path.c_str(), doc, 1) : xmlSaveFile(path.c_str(), doc);
    if (r >= 0) stitch::check(lx::read_file(path) == want, "xmlSaveFile == xmlDocDumpMemory");
    r = fmt ? xmlSaveFormatFileEnc(path.c_str(), doc, "UTF-8", 1) : xmlSaveFileEnc(path.c_str(), doc, "UTF-8");
    if (r >= 0) stitch::check(lx::read_file(path) == want_enc, "xmlSaveFileEnc == xmlDocDumpMemoryEnc");
    unlink(path.c_str());
    // to an output buffer (no transcoding there: only for UTF-8 documents)
    bool utf8 = !doc->encoding || !xmlStrcasecmp(doc->encoding, (const xmlChar*)"UTF-8");
    std::string out;
    xmlOutputBufferPtr ob = xmlOutputBufferCreateIO(lx::str_write, lx::str_close, &out, nullptr);
    r = fmt ? xmlSaveFormatFileTo(ob, doc, nullptr, 1) : xmlSaveFileTo(ob, doc, nullptr);  // (closes `ob`)
    if (r >= 0 && utf8) stitch::check(out == want, "xmlSaveFileTo == xmlDocDumpMemory");
    // the root element alone, into an xmlBuf and an xmlBuffer
    if (xmlNodePtr root = xmlDocGetRootElement(doc)) {
        xmlBufferPtr b = xmlBufferCreate();
        int len = xmlNodeDump(b, doc, root, 0, fmt);
        std::string nd = len >= 0 ? std::string((const char*)xmlBufferContent(b), (size_t)xmlBufferLength(b)) : "";
        xmlBufferFree(b);
        xmlOutputBufferPtr o2 = xmlAllocOutputBuffer(nullptr);
        if (o2) {
            size_t bl = xmlBufNodeDump(o2->buffer, doc, root, 0, fmt);
            if (bl != (size_t)-1 && len >= 0) {
                std::string got((const char*)xmlBufContent(o2->buffer), xmlBufUse(o2->buffer));
                stitch::check(got == nd, "xmlBufNodeDump == xmlNodeDump");
                stitch::check(xmlBufEnd(o2->buffer) == xmlBufContent(o2->buffer) + xmlBufUse(o2->buffer), "xmlBufEnd");
                size_t half = xmlBufUse(o2->buffer) / 2;
                stitch::check(xmlBufShrink(o2->buffer, half) == half && xmlBufUse(o2->buffer) == got.size() - half, "xmlBufShrink");
            }
            xmlOutputBufferClose(o2);
        }
        xmlOutputBufferPtr o3 = xmlAllocOutputBuffer(nullptr);
        if (o3) {
            xmlBufGetNodeContent(o3->buffer, root);
            xmlChar* c = xmlNodeGetContent(root);
            std::string got((const char*)xmlBufContent(o3->buffer), xmlBufUse(o3->buffer));
            stitch::check(got == lx::str(c), "xmlBufGetNodeContent == xmlNodeGetContent");
            xmlFree(c);
            xmlOutputBufferClose(o3);
        }
        xmlChar* base = nullptr;
        stitch::check(xmlNodeGetBaseSafe(doc, root, &base) >= 0, "NodeGetBaseSafe");  // (1: an invalid xml:base)
        xmlFree(base);
    }
    if (stitch::get(d, "wellformed", 0) && lx::ns_wellformed(doc)) {
        xmlDocPtr again = xmlReadMemory(want.data(), (int)want.size(), "again.xml", nullptr, lx::kQuiet);
        stitch::check(again != nullptr, "the serialized document parses");
        xmlFreeDoc(again);
    }
}
STITCH_ENDPOINT(serializers);

// Copying, moving and removing attribute and namespace lists between elements.
void prop_ns_lists(Borrow<Node> from, Borrow<Node> to, Bool remove) {
    xmlNodePtr a = *from, b = *to;
    stitch::require(a->type == XML_ELEMENT_NODE && b->type == XML_ELEMENT_NODE && a != b, "need two elements");
    stitch::require(a->doc == b->doc, "different documents");
    if (a->nsDef) {
        xmlNsPtr copy = xmlCopyNamespaceList(a->nsDef);
        int n1 = 0, n2 = 0;
        for (xmlNsPtr x = a->nsDef; x; x = x->next) n1++;
        for (xmlNsPtr x = copy; x; x = x->next) n2++;
        stitch::check(n1 == n2, "CopyNamespaceList copies every namespace");
        xmlNsPtr one = xmlCopyNamespace(a->nsDef);
        stitch::check(one && xmlStrEqual(one->href, a->nsDef->href) && xmlStrEqual(one->prefix, a->nsDef->prefix), "CopyNamespace");
        xmlFreeNs(one);
        xmlFreeNsList(copy);
    }
    // copy a's attributes into a detached element of b's document and compare the values
    xmlNodePtr tmp = xmlNewDocNode(b->doc, nullptr, (const xmlChar*)"tmp", nullptr);
    if (a->properties) {
        xmlAttrPtr copies = xmlCopyPropList(tmp, a->properties);
        stitch::require(copies, "copy failed");
        xmlAttrPtr x = a->properties, y = copies;
        for (; x && y; x = x->next, y = y->next) {
            xmlChar* vx = xmlNodeListGetString(a->doc, x->children, 1);
            xmlChar* vy = xmlNodeListGetString(b->doc, y->children, 1);
            stitch::check(xmlStrEqual(x->name, y->name) && xmlStrEqual(vx, vy), "CopyPropList copies names and values");
            xmlFree(vx);
            xmlFree(vy);
        }
        stitch::check(x == nullptr && y == nullptr, "same number of attributes");
        xmlFreePropList(copies);
    }
    xmlFreeNode(tmp);
    // remove attributes of b (the element keeps working)
    if (remove && b->properties) {
        xmlAttrPtr p = b->properties;
        if (p->ns) {
            xmlChar* name = xmlStrdup(p->name);
            stitch::check(xmlUnsetNsProp(b, p->ns, name) == 0, "UnsetNsProp removes the attribute");
            xmlFree(name);
        } else {
            stitch::check(xmlRemoveProp(p) == 0, "RemoveProp");
        }
    }
    // serialize an attribute value as attribute text
    xmlBufferPtr buf = xmlBufferCreate();
    if (b->properties) {
        xmlChar* v = xmlNodeListGetString(b->doc, b->properties->children, 1);
        if (v) xmlAttrSerializeTxtContent(buf, b->doc, b->properties, v);
        xmlFree(v);
    }
    xmlBufferFree(buf);
}
STITCH_ENDPOINT(prop_ns_lists);

// Copy a node list into a document (xmlDocCopyNodeList), move it there (xmlSetListDoc),
// and remove a node with the DOM wrapper (xmlDOMWrapRemoveNode).
void copy_list_into(Borrow<Node> n, Borrow<Doc> target, Bool remove_one) {
    xmlNodePtr e = *n;
    stitch::require(e->children, "no children");
    xmlNodePtr copy = xmlDocCopyNodeList(*target, e->children);
    stitch::require(copy, "copy failed");
    for (xmlNodePtr c = copy; c; c = c->next) stitch::check(c->doc == *target, "copies belong to the target");
    xmlNodePtr holder = xmlNewDocNode(*target, nullptr, (const xmlChar*)"holder", nullptr);
    xmlAddChildList(holder, copy);
    if (remove_one && holder->children) {
        xmlDOMWrapCtxtPtr w = xmlDOMWrapNewCtxt();
        xmlNodePtr victim = holder->children;
        int r = xmlDOMWrapRemoveNode(w, *target, victim, 0);
        stitch::check(r != 0 || (victim->parent == nullptr && holder->children != victim), "DOMWrapRemoveNode unlinks");
        if (r == 0) xmlFreeNode(victim);
        xmlDOMWrapFreeCtxt(w);
    }
    // a second, detached document takes the list over
    xmlDocPtr other = xmlNewDoc(nullptr);
    xmlNodePtr list = holder->children;
    if (list) {
        for (xmlNodePtr c = list; c; c = c->next) c->parent = nullptr;
        holder->children = holder->last = nullptr;
        xmlSetListDoc(list, other);
        for (xmlNodePtr c = list; c; c = c->next) stitch::check(c->doc == other, "SetListDoc moves the list");
        xmlFreeNodeList(list);
    }
    xmlFreeNode(holder);
    xmlFreeDoc(other);
}
STITCH_ENDPOINT(copy_list_into);

// The xmlBuffer API against a std::string model; XHTML detection; compression mode.
void buffer_ops(Src<128> s) {
    xmlBufferPtr b = xmlBufferCreate();
    std::string model;
    xmlBufferSetAllocationScheme(b, s->coin() ? XML_BUFFER_ALLOC_DOUBLEIT : XML_BUFFER_ALLOC_EXACT);
    while (s->more() && model.size() < 4096) {
        std::string t = s->str(16);
        t = t.substr(0, strlen(t.c_str()));
        switch (s->choice(5)) {
            case 0: xmlBufferCat(b, (const xmlChar*)t.c_str()); model += t; break;
            case 1: xmlBufferWriteCHAR(b, (const xmlChar*)t.c_str()); model += t; break;
            case 2: xmlBufferWriteChar(b, t.c_str()); model += t; break;
            case 3: xmlBufferCCat(b, t.c_str()); model += t; break;
            default: {
                xmlBufferPtr st = xmlBufferCreateStatic((void*)t.data(), t.size());
                if (st) {
                    stitch::check((size_t)xmlBufferLength(st) == t.size() && !memcmp(xmlBufferContent(st), t.data(), t.size()), "static buffer");
                    xmlBufferFree(st);
                }
            }
        }
        stitch::check(std::string((const char*)xmlBufferContent(b), (size_t)xmlBufferLength(b)) == model, "buffer holds the writes");
    }
    if (FILE* f = fopen("/dev/null", "w")) {
        stitch::check(xmlBufferDump(f, b) == (int)model.size(), "BufferDump writes everything");
        fclose(f);
    }
    xmlChar* det = xmlBufferDetach(b);
    stitch::check(lx::str(det) == model, "Detach returns the content");
    stitch::check(xmlBufferLength(b) == 0, "Detach empties the buffer");
    xmlFree(det);
    xmlBufferFree(b);
    static const char* ids[] = {"-//W3C//DTD XHTML 1.0 Strict//EN", "-//W3C//DTD XHTML 1.0 Transitional//EN", "-//W3C//DTD HTML 4.01//EN"};
    int k = (int)s->choice(3);
    stitch::check(xmlIsXHTML(nullptr, (const xmlChar*)ids[k]) == (k < 2 ? 1 : 0), "IsXHTML by public id");
    xmlDocPtr d = xmlNewDoc(nullptr);
    xmlSetDocCompressMode(d, (int)s->range(-1, 10));
    int cm = xmlGetDocCompressMode(d);
    stitch::check(cm >= 0 && cm <= 9, "compression level in range");
    xmlFreeDoc(d);
}
STITCH_ENDPOINT(buffer_ops);
