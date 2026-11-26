// Object types, helpers and data types shared by all stubs.
#pragma once
#include <stitch.h>
#include <tinyxml2.h>
#include <string>
#include <cstring>
#include <cmath>

using namespace tinyxml2;

// A document owns every node. Nodes are borrowed pointers into it.
STITCH_OBJECT(Doc, XMLDocument*);
STITCH_DROP(Doc, [](XMLDocument* d) { delete d; });
// Any node (element, text, comment, declaration, unknown, or the document itself).
STITCH_OBJECT(Node, XMLNode*);
STITCH_OBJECT(Printer, XMLPrinter*);
STITCH_DROP(Printer, [](XMLPrinter* p) { delete p; });

// XMLDocument whitespace modes
using WsMode = OneOf<PRESERVE_WHITESPACE, COLLAPSE_WHITESPACE, PEDANTIC_WHITESPACE>;

STITCH_DATATYPE(XmlText);   // whole documents
STITCH_DATATYPE(XmlName);   // element / attribute names

// Every document has a "tok" attribute; nodes remember the token of the document state they were
// made in. Operations that free nodes (Clear, Parse, DeleteChild...) give the document a new token,
// so stale Node objects are rejected instead of used after free.
inline long fresh_tok() {
    long t = stitch::global_get("tok", 0) + 1;
    stitch::global_set("tok", t);
    return t;
}
inline void retok(Borrow<Doc>& d) { stitch::set(d, "tok", fresh_tok()); }
inline void stamp(New<Node>& n, Borrow<Doc>& d) { stitch::set(n, "tok", (long)stitch::get(d, "tok", 0)); }
inline bool live(Borrow<Doc>& d, Borrow<Node>& n) {
    XMLNode* p = n;
    return stitch::get(n, "tok", -1) == stitch::get(d, "tok", -2) && p->GetDocument() == (XMLDocument*)d;
}
inline New<Node> mk(Borrow<Doc>& d, XMLNode* n) {
    New<Node> r(n);
    stamp(r, d);
    return r;
}
inline bool valid_name(const char* s) {
    if (!*s || !(isalpha((unsigned char)*s) || *s == '_')) return false;
    for (; *s; s++) if (!(isalnum((unsigned char)*s) || *s == '_' || *s == '.' || *s == '-' || *s == ':')) return false;
    return true;
}
inline std::string print_node(const XMLNode* n, bool compact) {
    XMLPrinter p(0, compact);
    n->Accept(&p);
    return std::string(p.CStr());
}
inline XMLElement* need_elem(Borrow<Doc>& d, Borrow<Node>& n) {
    stitch::require(live(d, n), "stale node");
    XMLElement* e = ((XMLNode*)n)->ToElement();
    stitch::require(e, "not an element");
    return e;
}
// Text the parser reads back as one comment / declaration / unknown node.
inline bool comment_ok(const char* s) { return !strstr(s, "--") && s[strlen(s) ? strlen(s) - 1 : 0] != '-'; }
inline bool decl_ok(const char* s) { return !strstr(s, "?>"); }
inline bool unknown_ok(const char* s) {
    return *s && !strchr(s, '>') && strncmp(s, "--", 2) != 0 && strncmp(s, "[CDATA[", 7) != 0;
}
