// XMLElement::InsertNew*: create a node and append it as the element's last child.
#include "../objects.hpp"

template <class T>
static New<Node> appended(Borrow<Doc>& d, XMLElement* e, T* r) {
    stitch::require(r, "insert failed");
    stitch::check(e->LastChild() == r && r->Parent() == e, "new node is the last child");
    stitch::check(r->GetDocument() == (XMLDocument*)d, "new node belongs to the document");
    return mk(d, r);
}

New<Node> insert_new_child_element(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name) {
    stitch::require(valid_name(name.c_str()), "bad name");
    XMLElement* e = need_elem(d, n);
    XMLElement* r = e->InsertNewChildElement(name.c_str());
    stitch::check(r && !strcmp(r->Name(), name.c_str()), "new element has the name");
    return appended(d, e, r);
}
STITCH_ENDPOINT(insert_new_child_element);

New<Node> insert_new_comment(Borrow<Doc> d, Borrow<Node> n, Str<64> text) {
    stitch::require(comment_ok(text.c_str()), "comment with --");
    XMLElement* e = need_elem(d, n);
    return appended(d, e, e->InsertNewComment(text.c_str()));
}
STITCH_ENDPOINT(insert_new_comment);

New<Node> insert_new_text(Borrow<Doc> d, Borrow<Node> n, Str<64> text) {
    XMLElement* e = need_elem(d, n);
    XMLText* t = e->InsertNewText(text.c_str());
    stitch::check(t && !strcmp(t->Value(), text.c_str()) && !t->CData(), "new text has the value");
    return appended(d, e, t);
}
STITCH_ENDPOINT(insert_new_text);

New<Node> insert_new_declaration(Borrow<Doc> d, Borrow<Node> n, Str<64> text) {
    stitch::require(decl_ok(text.c_str()), "declaration with ?>");
    XMLElement* e = need_elem(d, n);
    return appended(d, e, e->InsertNewDeclaration(text.c_str()));
}
STITCH_ENDPOINT(insert_new_declaration);

New<Node> insert_new_unknown(Borrow<Doc> d, Borrow<Node> n, Str<64> text) {
    stitch::require(unknown_ok(text.c_str()), "unknown with >");
    XMLElement* e = need_elem(d, n);
    return appended(d, e, e->InsertNewUnknown(text.c_str()));
}
STITCH_ENDPOINT(insert_new_unknown);

// LinkEndChild is the TinyXML-1 name of InsertEndChild.
void link_end_child(Borrow<Doc> d, Borrow<Node> parent, Borrow<Node> child) {
    stitch::require(live(d, parent) && live(d, child), "stale node");
    XMLNode* c = child;
    stitch::require(c->ToDocument() == nullptr, "cannot insert the document");
    for (XMLNode* p = parent; p; p = p->Parent()) stitch::require(p != c, "would create a cycle");
    XMLNode* r = parent->LinkEndChild(c);
    stitch::check(r == c && parent->LastChild() == c && c->Parent() == (XMLNode*)parent, "linked last");
}
STITCH_ENDPOINT(link_end_child);

void set_name(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name) {
    stitch::require(valid_name(name.c_str()), "bad name");
    XMLElement* e = need_elem(d, n);
    e->SetName(name.c_str());
    stitch::check(!strcmp(e->Name(), name.c_str()) && !strcmp(e->Value(), name.c_str()), "Name() after SetName");
}
STITCH_ENDPOINT(set_name);

// XMLDocument::DeleteNode: unlinks (if linked) and frees the node and its subtree.
void delete_node(Borrow<Doc> d, Borrow<Node> n) {
    stitch::require(live(d, n), "stale node");
    XMLNode* node = n;
    stitch::require(node->ToDocument() == nullptr, "cannot delete the document");
    XMLNode* parent = node->Parent();
    retok(d);
    d->DeleteNode(node);
    n.consume();
    if (parent) {
        for (XMLNode* c = parent->FirstChild(); c; c = c->NextSibling()) stitch::check(c != node, "deleted node is unlinked");
    }
}
STITCH_ENDPOINT(delete_node);

void set_user_data(Borrow<Doc> d, Borrow<Node> n, U32 v) {
    stitch::require(live(d, n), "stale node");
    void* p = (void*)(uintptr_t)(uint32_t)v;
    n->SetUserData(p);
    stitch::check(n->GetUserData() == p, "GetUserData returns what SetUserData stored");
}
STITCH_ENDPOINT(set_user_data);
