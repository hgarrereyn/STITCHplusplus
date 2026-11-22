#include "../objects.hpp"
#include <unistd.h>

// print -> parse -> print must reach a fixed point.
void roundtrip_doc(Borrow<Doc> d, Bool compact, Bool collapse) {
    std::string s1 = print_node(d, compact);
    stitch::require(!s1.empty(), "empty document");
    XMLDocument d2(true, collapse ? COLLAPSE_WHITESPACE : PRESERVE_WHITESPACE);
    XMLError e = d2.Parse(s1.c_str());
    stitch::check(e == XML_SUCCESS, "printed document parses");
    std::string s2 = print_node(&d2, compact);
    stitch::check(s1 == s2, "print(parse(print(doc))) == print(doc)");
}
STITCH_ENDPOINT(roundtrip_doc);

// Same on a subtree.
void roundtrip_node(Borrow<Doc> d, Borrow<Node> n) {
    stitch::require(live(d, n), "stale node");
    XMLNode* node = n;
    stitch::require(node->ToElement(), "not an element");
    std::string s1 = print_node(node, true);
    XMLDocument d2;
    stitch::check(d2.Parse(s1.c_str()) == XML_SUCCESS, "printed element parses");
    stitch::check(print_node(&d2, true) == s1, "element print is stable");
}
STITCH_ENDPOINT(roundtrip_node);

// Parsing and printing the same text twice must agree.
void parse_print_stable(Str<4096, XmlText> text, Bool compact) {
    XMLDocument d;
    stitch::require(d.Parse(text.c_str()) == XML_SUCCESS, "parse error");
    std::string s1 = print_node(&d, compact);
    XMLDocument d2;
    stitch::check(d2.Parse(s1.c_str()) == XML_SUCCESS || s1.empty(), "reparse of printed output");
    stitch::check(print_node(&d2, compact) == s1, "print is a fixed point");
}
STITCH_ENDPOINT(parse_print_stable);

// Deep-cloning the whole document content into another document must print identically.
void clone_doc(Borrow<Doc> d) {
    XMLDocument d2;
    for (const XMLNode* c = d->FirstChild(); c; c = c->NextSibling()) d2.InsertEndChild(c->DeepClone(&d2));
    stitch::check(print_node(d, false) == print_node(&d2, false), "cloned document prints the same");
}
STITCH_ENDPOINT(clone_doc);

void save_load(Borrow<Doc> d, Bool compact) {
    std::string path = "/tmp/stitch_tx2_" + std::to_string(getpid()) + ".xml";
    XMLError e = d->SaveFile(path.c_str(), compact);
    stitch::require(e == XML_SUCCESS, "save failed");
    XMLDocument d2;
    XMLError l = d2.LoadFile(path.c_str());
    remove(path.c_str());
    std::string s1 = print_node(d, compact);
    if (s1.empty()) return;
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

void printer_open(Borrow<Printer> p, Str<32, XmlName> name) {
    p->OpenElement(name.c_str());
    stitch::set(p, "depth", (long)stitch::get(p, "depth", 0) + 1);
    stitch::set(p, "open", 1);
}
STITCH_ENDPOINT(printer_open);

void printer_attr(Borrow<Printer> p, Str<32, XmlName> name, Str<32> v) {
    stitch::require(stitch::get(p, "open", 0) == 1, "no open start tag");
    p->PushAttribute(name.c_str(), v.c_str());
}
STITCH_ENDPOINT(printer_attr);

void printer_attr_int(Borrow<Printer> p, Str<32, XmlName> name, I32 v) {
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
