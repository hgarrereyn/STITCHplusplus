#include "../objects.hpp"

static New<Doc> finish(XMLDocument* d, XMLError e) {
    if (e != XML_SUCCESS) { delete d; stitch::bail("parse error"); }
    New<Doc> r(d);
    stitch::set(r, "tok", fresh_tok());
    return r;
}

New<Doc> new_doc(Bool processEntities, WsMode ws) {
    XMLDocument* d = new XMLDocument(processEntities, (Whitespace)(int)ws);
    New<Doc> r(d);
    stitch::set(r, "tok", fresh_tok());
    return r;
}
STITCH_ENDPOINT(new_doc);

New<Doc> parse(Str<4096, XmlText> text, Bool processEntities, WsMode ws) {
    XMLDocument* d = new XMLDocument(processEntities, (Whitespace)(int)ws);
    return finish(d, d->Parse(text.c_str()));
}
STITCH_ENDPOINT(parse);

// Parse with an explicit length, possibly cutting the text short.
New<Doc> parse_len(Str<4096, XmlText> text, Range<0, 4096> n) {
    XMLDocument* d = new XMLDocument();
    size_t len = std::min((size_t)n, text.size());
    return finish(d, d->Parse(text.c_str(), len));
}
STITCH_ENDPOINT(parse_len);

// Arbitrary bytes, not necessarily XML.
New<Doc> parse_bytes(Bytes<512> b) {
    XMLDocument* d = new XMLDocument();
    return finish(d, d->Parse((const char*)b.data(), b.size()));
}
STITCH_ENDPOINT(parse_bytes);

New<Doc> load_file(File<4096, XmlText> f) {
    XMLDocument* d = new XMLDocument();
    return finish(d, d->LoadFile(f.path()));
}
STITCH_ENDPOINT(load_file);

// Parse into a document that already has content (Parse clears it first).
void parse_into(Borrow<Doc> d, Str<4096, XmlText> text) {
    XMLError e = d->Parse(text.c_str());
    retok(d);
    stitch::require(e == XML_SUCCESS, "parse error");
}
STITCH_ENDPOINT(parse_into);

void clear_doc(Borrow<Doc> d) {
    d->Clear();
    retok(d);
}
STITCH_ENDPOINT(clear_doc);

void delete_doc(Take<Doc> d) { delete (XMLDocument*)d; }
STITCH_ENDPOINT(delete_doc);

// The document as a Node, to insert top-level children or walk from the top.
New<Node> doc_node(Borrow<Doc> d) {
    XMLDocument* doc = d;
    return mk(d, static_cast<XMLNode*>(doc));
}
STITCH_ENDPOINT(doc_node);

New<Node> root_element(Borrow<Doc> d) {
    XMLElement* e = d->RootElement();
    stitch::require(e, "no root element");
    return mk(d, e);
}
STITCH_ENDPOINT(root_element);
