#include "../objects.hpp"

// A node can be moved, but not into itself or one of its descendants, and not across documents.
static void need_insertable(Borrow<Doc>& d, Borrow<Node>& parent, Borrow<Node>& child) {
    stitch::require(live(d, parent) && live(d, child), "stale node");
    XMLNode* c = child;
    stitch::require(c->ToDocument() == nullptr, "cannot insert the document");
    for (XMLNode* p = parent; p; p = p->Parent()) stitch::require(p != c, "would create a cycle");
}

void insert_end(Borrow<Doc> d, Borrow<Node> parent, Borrow<Node> child) {
    need_insertable(d, parent, child);
    XMLNode* r = parent->InsertEndChild(child);
    stitch::check(r == (XMLNode*)child, "InsertEndChild returns the node");
    stitch::check(parent->LastChild() == (XMLNode*)child && child->Parent() == (XMLNode*)parent, "inserted last");
}
STITCH_ENDPOINT(insert_end);

void insert_first(Borrow<Doc> d, Borrow<Node> parent, Borrow<Node> child) {
    need_insertable(d, parent, child);
    XMLNode* r = parent->InsertFirstChild(child);
    stitch::check(r == (XMLNode*)child, "InsertFirstChild returns the node");
    stitch::check(parent->FirstChild() == (XMLNode*)child && child->Parent() == (XMLNode*)parent, "inserted first");
}
STITCH_ENDPOINT(insert_first);

void insert_after(Borrow<Doc> d, Borrow<Node> parent, Borrow<Node> child, Src<8> s) {
    need_insertable(d, parent, child);
    int n = 0;
    for (XMLNode* c = parent->FirstChild(); c; c = c->NextSibling()) n++;
    stitch::require(n > 0, "no children");
    XMLNode* after = parent->FirstChild();
    for (size_t i = s->choice(n); i > 0; i--) after = after->NextSibling();
    XMLNode* r = parent->InsertAfterChild(after, child);
    stitch::check(r == (XMLNode*)child, "InsertAfterChild returns the node");
    stitch::check(child->PreviousSibling() == after || (XMLNode*)child == after, "inserted after");
}
STITCH_ENDPOINT(insert_after);

// Delete one child of `parent` (chosen). Frees the subtree: every node object becomes stale.
void delete_child(Borrow<Doc> d, Borrow<Node> parent, Src<8> s) {
    stitch::require(live(d, parent), "stale node");
    int n = 0;
    for (XMLNode* c = parent->FirstChild(); c; c = c->NextSibling()) n++;
    stitch::require(n > 0, "no children");
    XMLNode* victim = parent->FirstChild();
    for (size_t i = s->choice(n); i > 0; i--) victim = victim->NextSibling();
    retok(d);
    parent->DeleteChild(victim);
}
STITCH_ENDPOINT(delete_child);

void delete_children(Borrow<Doc> d, Borrow<Node> parent) {
    stitch::require(live(d, parent), "stale node");
    retok(d);
    parent->DeleteChildren();
    stitch::check(parent->NoChildren(), "no children after DeleteChildren");
}
STITCH_ENDPOINT(delete_children);

void set_value(Borrow<Doc> d, Borrow<Node> n, Str<32> v) {
    stitch::require(live(d, n), "stale node");
    XMLNode* node = n;
    stitch::require(!node->ToDocument(), "document has no value");
    if (node->ToElement()) stitch::require(valid_name(v.c_str()), "bad name");
    if (node->ToComment()) stitch::require(!strstr(v.c_str(), "--"), "comment with --");
    n->SetValue(v.c_str());
    stitch::check(!strcmp(n->Value(), v.c_str()), "Value() after SetValue");
}
STITCH_ENDPOINT(set_value);

// Clone a subtree into `target` (the same or another document).
New<Node> deep_clone(Borrow<Doc> src, Borrow<Node> n, Borrow<Doc> target) {
    stitch::require(live(src, n), "stale node");
    XMLNode* node = n;
    stitch::require(node->ToDocument() == nullptr, "cannot clone the document");
    XMLNode* c = node->DeepClone(target);
    stitch::require(c, "clone failed");
    stitch::check(node->ShallowEqual(c), "clone is shallow-equal");
    stitch::check(print_node(node, true) == print_node(c, true), "clone prints the same");
    return mk(target, c);
}
STITCH_ENDPOINT(deep_clone);

New<Node> shallow_clone(Borrow<Doc> src, Borrow<Node> n, Borrow<Doc> target) {
    stitch::require(live(src, n), "stale node");
    XMLNode* node = n;
    stitch::require(node->ToDocument() == nullptr, "cannot clone the document");
    XMLNode* c = node->ShallowClone(target);
    stitch::require(c, "clone failed");
    stitch::check(node->ShallowEqual(c), "shallow clone is equal");
    stitch::check(c->NoChildren(), "shallow clone has no children");
    return mk(target, c);
}
STITCH_ENDPOINT(shallow_clone);
