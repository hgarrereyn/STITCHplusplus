#include "../objects.hpp"

New<Node> new_element(Borrow<Doc> d, Str<32, XmlName> name) { stitch::require(valid_name(name.c_str()), "bad name"); return mk(d, d->NewElement(name.c_str())); }
STITCH_ENDPOINT(new_element);

New<Node> new_text(Borrow<Doc> d, Str<64> text, Bool cdata) {
    XMLText* t = d->NewText(text.c_str());
    t->SetCData(cdata);
    return mk(d, t);
}
STITCH_ENDPOINT(new_text);

New<Node> new_comment(Borrow<Doc> d, Str<64> text) {
    stitch::require(!strstr(text.c_str(), "--"), "comment with --");
    return mk(d, d->NewComment(text.c_str()));
}
STITCH_ENDPOINT(new_comment);

New<Node> new_declaration(Borrow<Doc> d, Str<64> text) { return mk(d, d->NewDeclaration(text.c_str())); }
STITCH_ENDPOINT(new_declaration);

New<Node> new_declaration_default(Borrow<Doc> d) { return mk(d, d->NewDeclaration()); }
STITCH_ENDPOINT(new_declaration_default);

New<Node> new_unknown(Borrow<Doc> d, Str<64> text) { return mk(d, d->NewUnknown(text.c_str())); }
STITCH_ENDPOINT(new_unknown);
