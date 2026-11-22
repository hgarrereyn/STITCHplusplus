#include "../objects.hpp"
#include <unistd.h>
#include <deque>
#include <memory>

// A document we expect to print to something re-parsable: an element at the top and no stray
// top-level text (tinyxml2 accepts such input but printing it back does not parse).
static void need_wellformed_top(const XMLDocument* d) {
    stitch::require(d->FirstChildElement() != nullptr, "no top-level element");
    for (const XMLNode* c = d->FirstChild(); c; c = c->NextSibling())
        stitch::require(!c->ToText(), "top-level text");
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
    std::string s1 = print_node(node, true);
    std::unique_ptr<XMLDocument> d2(same_settings(d));
    stitch::check(d2->Parse(s1.c_str()) == XML_SUCCESS, "printed element parses");
    stitch::check(print_node(d2.get(), true) == s1, "element print is stable");
}
STITCH_ENDPOINT(roundtrip_node);

// Parsing and printing the same text twice must agree.
void parse_print_stable(Str<4096, XmlText> text, Bool compact, Bool processEntities) {
    XMLDocument d(processEntities);
    stitch::require(d.Parse(text.c_str()) == XML_SUCCESS, "parse error");
    need_wellformed_top(&d);
    if (!compact) stitch::require(!has_mixed(&d), "mixed content");
    std::string s1 = print_node(&d, compact);
    XMLDocument d2(processEntities);
    stitch::check(d2.Parse(s1.c_str()) == XML_SUCCESS, "reparse of printed output");
    stitch::check(print_node(&d2, compact) == s1, "print is a fixed point");
}
STITCH_ENDPOINT(parse_print_stable);

// Deep-cloning the whole document content into another document must print identically.
void clone_doc(Borrow<Doc> d) {
    XMLDocument d2(d->ProcessEntities(), d->WhitespaceMode());
    for (const XMLNode* c = d->FirstChild(); c; c = c->NextSibling()) d2.InsertEndChild(c->DeepClone(&d2));
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
New<Printer> new_printer(Bool compact) {
    New<Printer> p(new XMLPrinter(0, compact));
    stitch::set(p, "depth", 0);
    stitch::set(p, "open", 0);
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
    stitch::require(!strstr(v.c_str(), "--"), "comment with --");
    p->PushComment(v.c_str());
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

// When all elements are closed, the output must be well-formed.
void printer_finish(Borrow<Printer> p) {
    stitch::require(stitch::get(p, "depth", 0) == 0, "unclosed elements");
    std::string out = p->CStr();
    stitch::require(!out.empty(), "empty output");
    stitch::check(p->CStrSize() == (int)out.size() + 1, "CStrSize matches the buffer");
    XMLDocument d;
    stitch::check(d.Parse(out.c_str()) == XML_SUCCESS, "printer output parses");
}
STITCH_ENDPOINT(printer_finish);
