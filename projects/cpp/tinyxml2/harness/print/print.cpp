#include "../objects.hpp"
#include <unistd.h>
#include <deque>
#include <memory>

// A document we expect to print to something re-parsable: an element at the top and no stray
// top-level text (tinyxml2 accepts such input but printing it back does not parse).
static void need_no_decl_below(const XMLNode* n) {
    for (const XMLNode* c = n->FirstChild(); c; c = c->NextSibling()) {
        stitch::require(!c->ToDeclaration() && !c->ToUnknown(), "declaration/doctype inside element");
        need_no_decl_below(c);
    }
}
static bool all_space(const char* s) {
    for (; *s; s++) if (!XMLUtil::IsWhiteSpace(*s)) return false;
    return true;
}
// Text the parser reads back unchanged in whitespace mode `ws`:
// - The parser normalizes CR / CRLF to LF, so text with CR cannot round-trip (XML spec behaviour).
// - An empty text node prints as <f></f> but reparses as <f/>.
// - Whitespace-only text is dropped by the parser, except in PEDANTIC_WHITESPACE mode when it is
//   the whole content of an element.
// - COLLAPSE_WHITESPACE trims text and folds whitespace runs into one space.
static void need_text_stable(const XMLNode* n, Whitespace ws) {
    if (n->Value()) stitch::require(!strchr(n->Value(), '\r'), "CR in text");
    if (const XMLText* t = n->ToText()) {
        const char* v = t->Value();
        stitch::require(*v, "empty text node");
        if (!t->CData()) {
            if (all_space(v)) {
                const XMLNode* p = t->Parent();
                stitch::require(ws == PEDANTIC_WHITESPACE && p && p->ToElement() && p->FirstChild() == t && p->LastChild() == t,
                                "whitespace-only text");
            }
            if (ws == COLLAPSE_WHITESPACE) {
                stitch::require(!XMLUtil::IsWhiteSpace(v[0]) && !XMLUtil::IsWhiteSpace(v[strlen(v) - 1]), "untrimmed text");
                for (const char* c = v; *c; c++)
                    stitch::require(!XMLUtil::IsWhiteSpace(*c) || (*c == ' ' && !XMLUtil::IsWhiteSpace(c[1])), "uncollapsed text");
            }
        }
    }
    if (const XMLElement* e = n->ToElement())
        for (const XMLAttribute* a = e->FirstAttribute(); a; a = a->Next()) stitch::require(!strchr(a->Value(), '\r'), "CR in attribute");
    for (const XMLNode* c = n->FirstChild(); c; c = c->NextSibling()) need_text_stable(c, ws);
}
static void need_wellformed_top(const XMLDocument* d) {
    // With processEntities=false values are printed raw, so quotes/angle brackets break the output (documented).
    stitch::require(d->ProcessEntities(), "processEntities is off");
    need_text_stable(d, d->WhitespaceMode());
    stitch::require(d->FirstChildElement() != nullptr, "no top-level element");
    bool seen_elem = false;
    for (const XMLNode* c = d->FirstChild(); c; c = c->NextSibling()) {
        stitch::require(!c->ToText(), "top-level text");
        if (c->ToElement()) { seen_elem = true; need_no_decl_below(c); }
        else if (c->ToDeclaration() || c->ToUnknown()) stitch::require(!seen_elem, "declaration after root");
    }
}
// Pretty printing indents next to text: mixed content changes on every round trip (known limitation).
static bool has_mixed(const XMLNode* n) {
    bool text = false, elem = false;
    for (const XMLNode* c = n->FirstChild(); c; c = c->NextSibling()) {
        if (c->ToText()) text = true;
        if (c->ToElement()) elem = true;
        if (has_mixed(c)) return true;
    }
    return text && elem;
}
static XMLDocument* same_settings(const XMLDocument* d) { return new XMLDocument(d->ProcessEntities(), d->WhitespaceMode()); }

// print -> parse -> print must reach a fixed point.
void roundtrip_doc(Borrow<Doc> d, Bool compact) {
    need_wellformed_top(d);
    if (!compact) stitch::require(!has_mixed(d), "mixed content");
    std::string s1 = print_node(d, compact);
    std::unique_ptr<XMLDocument> d2(same_settings(d));
    XMLError e = d2->Parse(s1.c_str());
    stitch::check(e == XML_SUCCESS, "printed document parses");
    std::string s2 = print_node(d2.get(), compact);
    stitch::check(s1 == s2, "print(parse(print(doc))) == print(doc)");
}
STITCH_ENDPOINT(roundtrip_doc);

// Same on a subtree.
void roundtrip_node(Borrow<Doc> d, Borrow<Node> n) {
    stitch::require(live(d, n), "stale node");
    XMLNode* node = n;
    stitch::require(node->ToElement(), "not an element");
    stitch::require(d->ProcessEntities(), "processEntities is off");
    need_no_decl_below(node);
    need_text_stable(node, d->WhitespaceMode());
    std::string s1 = print_node(node, true);
    std::unique_ptr<XMLDocument> d2(same_settings(d));
    stitch::check(d2->Parse(s1.c_str()) == XML_SUCCESS, "printed element parses");
    stitch::check(print_node(d2.get(), true) == s1, "element print is stable");
}
STITCH_ENDPOINT(roundtrip_node);

// Parsing and printing the same text twice must agree.
void parse_print_stable(Str<4096, XmlText> text, Bool compact) {
    XMLDocument d;
    stitch::require(d.Parse(text.c_str()) == XML_SUCCESS, "parse error");
    need_wellformed_top(&d);
    if (!compact) stitch::require(!has_mixed(&d), "mixed content");
    std::string s1 = print_node(&d, compact);
    XMLDocument d2;
    stitch::check(d2.Parse(s1.c_str()) == XML_SUCCESS, "reparse of printed output");
    stitch::check(print_node(&d2, compact) == s1, "print is a fixed point");
}
STITCH_ENDPOINT(parse_print_stable);

// Deep-cloning the whole document content into another document must print identically.
void clone_doc(Borrow<Doc> d) {
    XMLDocument d2(d->ProcessEntities(), d->WhitespaceMode());
    for (const XMLNode* c = d->FirstChild(); c; c = c->NextSibling()) d2.InsertEndChild(c->DeepClone(&d2));
    d2.SetBOM(d->HasBOM());  // a document setting, not a node
    stitch::check(print_node(d, false) == print_node(&d2, false), "cloned document prints the same");
}
STITCH_ENDPOINT(clone_doc);

void save_load(Borrow<Doc> d, Bool compact) {
    need_wellformed_top(d);
    if (!compact) stitch::require(!has_mixed(d), "mixed content");
    std::string path = "/tmp/stitch_tx2_" + std::to_string(getpid()) + ".xml";
    XMLError e = d->SaveFile(path.c_str(), compact);
    stitch::require(e == XML_SUCCESS, "save failed");
    XMLDocument d2(d->ProcessEntities(), d->WhitespaceMode());
    XMLError l = d2.LoadFile(path.c_str());
    remove(path.c_str());
    std::string s1 = print_node(d, compact);
    stitch::check(l == XML_SUCCESS, "saved file loads");
    stitch::check(print_node(&d2, compact) == s1, "saved file prints the same");
}
STITCH_ENDPOINT(save_load);

// ---- manual XMLPrinter ----
// Attributes: "depth" (open elements), "open" (a start tag is still open for attributes),
// "started" (something was written: declarations are only valid before anything else).
New<Printer> new_printer(Bool compact, Range<0, 3> depth, Bool escape_apos) {
    New<Printer> p(new XMLPrinter(0, compact, (int)depth,
                                  escape_apos ? XMLPrinter::ESCAPE_APOS_CHARS_IN_ATTRIBUTES
                                              : XMLPrinter::DONT_ESCAPE_APOS_CHARS_IN_ATTRIBUTES));
    stitch::set(p, "depth", 0);
    stitch::set(p, "open", 0);
    stitch::set(p, "started", 0);
    return p;
}
STITCH_ENDPOINT(new_printer);

// OpenElement keeps the name pointer until CloseElement: it must outlive the call.
static const char* keep(const char* s) {
    static std::deque<std::string> pool;
    stitch::require(pool.size() < 100000, "pool full");
    pool.push_back(s);
    return pool.back().c_str();
}

void printer_open(Borrow<Printer> p, Str<32, XmlName> name) {
    stitch::require(valid_name(name.c_str()), "bad name");
    p->OpenElement(keep(name.c_str()));
    stitch::set(p, "started", 1);
    stitch::set(p, "depth", (long)stitch::get(p, "depth", 0) + 1);
    stitch::set(p, "open", 1);
}
STITCH_ENDPOINT(printer_open);

void printer_attr(Borrow<Printer> p, Str<32, XmlName> name, Str<32> v) {
    stitch::require(valid_name(name.c_str()), "bad name");
    stitch::require(stitch::get(p, "open", 0) == 1, "no open start tag");
    p->PushAttribute(name.c_str(), v.c_str());
}
STITCH_ENDPOINT(printer_attr);

void printer_attr_int(Borrow<Printer> p, Str<32, XmlName> name, I32 v) {
    stitch::require(valid_name(name.c_str()), "bad name");
    stitch::require(stitch::get(p, "open", 0) == 1, "no open start tag");
    p->PushAttribute(name.c_str(), (int)v);
}
STITCH_ENDPOINT(printer_attr_int);

void printer_text(Borrow<Printer> p, Str<32> v, Bool cdata) {
    stitch::require(stitch::get(p, "depth", 0) > 0, "no open element");
    p->PushText(v.c_str(), cdata);
    stitch::set(p, "open", 0);
}
STITCH_ENDPOINT(printer_text);

void printer_text_num(Borrow<Printer> p, I64 v) {
    stitch::require(stitch::get(p, "depth", 0) > 0, "no open element");
    p->PushText((int64_t)v);
    stitch::set(p, "open", 0);
}
STITCH_ENDPOINT(printer_text_num);

void printer_text_double(Borrow<Printer> p, F64 v) {
    stitch::require(stitch::get(p, "depth", 0) > 0, "no open element");
    p->PushText((double)v);
    stitch::set(p, "open", 0);
}
STITCH_ENDPOINT(printer_text_double);

void printer_comment(Borrow<Printer> p, Str<32> v) {
    stitch::require(comment_ok(v.c_str()), "comment with --");
    p->PushComment(v.c_str());
    stitch::set(p, "started", 1);
    stitch::set(p, "open", 0);
}
STITCH_ENDPOINT(printer_comment);

void printer_close(Borrow<Printer> p, Bool peekClosing) {
    stitch::require(stitch::get(p, "depth", 0) > 0, "nothing to close");
    p->CloseElement(peekClosing);
    stitch::set(p, "depth", (long)stitch::get(p, "depth", 0) - 1);
    stitch::set(p, "open", 0);
}
STITCH_ENDPOINT(printer_close);

void printer_attr_typed(Borrow<Printer> p, Str<32, XmlName> name, Range<0, 4> kind, U64 v, F64 dv) {
    stitch::require(valid_name(name.c_str()), "bad name");
    stitch::require(stitch::get(p, "open", 0) == 1, "no open start tag");
    switch ((int)kind) {
        case 0: p->PushAttribute(name.c_str(), (unsigned)v); break;
        case 1: p->PushAttribute(name.c_str(), (uint64_t)v); break;
        case 2: p->PushAttribute(name.c_str(), (int64_t)v); break;
        case 3: p->PushAttribute(name.c_str(), (bool)(v & 1)); break;
        default: p->PushAttribute(name.c_str(), (double)dv); break;
    }
}
STITCH_ENDPOINT(printer_attr_typed);

void printer_text_typed(Borrow<Printer> p, Range<0, 4> kind, U64 v, F32 fv) {
    stitch::require(stitch::get(p, "depth", 0) > 0, "no open element");
    switch ((int)kind) {
        case 0: p->PushText((unsigned)v); break;
        case 1: p->PushText((uint64_t)v); break;
        case 2: p->PushText((int)v); break;
        case 3: p->PushText((bool)(v & 1)); break;
        default: p->PushText((float)fv); break;
    }
    stitch::set(p, "open", 0);
}
STITCH_ENDPOINT(printer_text_typed);

// "<?value?>": only valid before anything else in the document.
void printer_declaration(Borrow<Printer> p, Str<32> v) {
    stitch::require(stitch::get(p, "started", 0) == 0, "declaration after content");
    stitch::require(decl_ok(v.c_str()), "declaration with ?>");
    p->PushDeclaration(v.c_str());
}
STITCH_ENDPOINT(printer_declaration);

// "<!value>"
void printer_unknown(Borrow<Printer> p, Str<32> v) {
    stitch::require(unknown_ok(v.c_str()), "unknown with >");
    p->PushUnknown(v.c_str());
    stitch::set(p, "open", 0);
    stitch::set(p, "started", 1);
}
STITCH_ENDPOINT(printer_unknown);

// Start over: only between top-level nodes, so the remaining output stays balanced.
void printer_clear(Borrow<Printer> p, Bool reset_first) {
    stitch::require(stitch::get(p, "depth", 0) == 0, "elements are open");
    p->ClearBuffer(reset_first);
    stitch::check(p->CStrSize() == 1 && p->CStr()[0] == 0, "ClearBuffer empties the buffer");
    stitch::set(p, "started", 0);
}
STITCH_ENDPOINT(printer_clear);

// Print a parsed node through the printer (XMLPrinter as a visitor).
void printer_visit(Borrow<Printer> p, Borrow<Doc> d, Borrow<Node> n) {
    stitch::require(live(d, n), "stale node");
    XMLNode* node = n;
    stitch::require(!node->ToDocument(), "document");
    stitch::require(d->ProcessEntities(), "processEntities is off");
    need_no_decl_below(node);
    if (node->ToDeclaration()) stitch::require(stitch::get(p, "depth", 0) == 0 && stitch::get(p, "started", 0) == 0, "declaration after content");
    if (node->ToText()) stitch::require(stitch::get(p, "depth", 0) > 0, "text at the top level");
    node->Accept(p);
    stitch::set(p, "open", 0);
    stitch::set(p, "started", 1);
}
STITCH_ENDPOINT(printer_visit);

// When all elements are closed, the output must be well-formed.
void printer_finish(Borrow<Printer> p) {
    stitch::require(stitch::get(p, "depth", 0) == 0, "unclosed elements");
    std::string out = p->CStr();
    stitch::require(!out.empty(), "empty output");
    stitch::check(p->CStrSize() == out.size() + 1, "CStrSize matches the buffer");
    XMLDocument d;
    stitch::check(d.Parse(out.c_str()) == XML_SUCCESS, "printer output parses");
}
STITCH_ENDPOINT(printer_finish);

// A whole well-formed printing session driven by one Src: balanced elements, attributes, text.
static void script(stitch::Source& s, XMLPrinter& p, int depth, std::deque<std::string>& names) {
    static const char* nm[] = {"a", "b", "item", "x:y", "_k"};
    std::string n = nm[s.choice(5)];
    names.push_back(n);
    p.OpenElement(names.back().c_str(), s.coin(0.2));
    int used = 0;
    while (used < 5 && s.more() && s.coin(0.5)) {
        std::string an = nm[used++];   // distinct names: duplicate attributes are not well-formed
        switch (s.choice(5)) {
            case 0: p.PushAttribute(an.c_str(), s.str(8).c_str()); break;
            case 1: p.PushAttribute(an.c_str(), (int)s.i32()); break;
            case 2: p.PushAttribute(an.c_str(), s.coin()); break;
            case 3: p.PushAttribute(an.c_str(), s.f64(-1e6, 1e6)); break;
            default: p.PushAttribute(an.c_str(), (int64_t)s.i64()); break;
        }
    }
    while (depth < 4 && s.more()) {
        switch (s.choice(6)) {
            case 0: p.PushText(s.str(8).c_str(), s.coin()); break;
            case 1: p.PushText((int)s.i32()); break;
            case 2: p.PushText(s.f64(-1e6, 1e6)); break;
            case 3: p.PushComment("c"); break;
            default: script(s, p, depth + 1, names); break;
        }
    }
    p.CloseElement(s.coin());
}
void printer_script(Src<64> s, Bool compact, Bool header) {
    XMLPrinter p(0, compact);
    if (header) p.PushHeader(s->coin(), true);
    std::deque<std::string> names;
    script(*s, p, 0, names);
    std::string out = p.CStr();
    XMLDocument d;
    XMLError e = d.Parse(out.c_str());
    if (e != XML_SUCCESS) stitch::log("%s", out.c_str());
    stitch::check(e == XML_SUCCESS, "printed session parses");
}
STITCH_ENDPOINT(printer_script);
