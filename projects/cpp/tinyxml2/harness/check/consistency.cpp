#include "../objects.hpp"

static int walk(const XMLNode* n, int depth, int& elems) {
    stitch::check(depth < 5000, "tree depth");
    int count = 1;
    if (n->ToElement()) elems++;
    const XMLNode* prev = nullptr;
    for (const XMLNode* c = n->FirstChild(); c; c = c->NextSibling()) {
        stitch::check(c->Parent() == n, "child's Parent() is the node");
        stitch::check(c->PreviousSibling() == prev, "PreviousSibling is consistent");
        stitch::check(c->GetDocument() == n->GetDocument(), "child belongs to the same document");
        count += walk(c, depth + 1, elems);
        prev = c;
        stitch::check(count < 100000, "tree size (cycle?)");
    }
    stitch::check(n->LastChild() == prev, "LastChild is the last child");
    stitch::check(n->NoChildren() == (prev == nullptr), "NoChildren");
    return count;
}

struct CountVisitor : XMLVisitor {
    int elems = 0, enters = 0, exits = 0, texts = 0;
    bool VisitEnter(const XMLElement&, const XMLAttribute*) override { elems++; enters++; return true; }
    bool VisitExit(const XMLElement&) override { exits++; return true; }
    bool Visit(const XMLText&) override { texts++; return true; }
};

void check_tree(Borrow<Doc> d) {
    int elems = 0;
    walk(d, 0, elems);
    CountVisitor v;
    d->Accept(&v);
    stitch::check(v.elems == elems, "visitor sees every element");
    stitch::check(v.enters == v.exits, "visitor enter/exit balance");
}
STITCH_ENDPOINT(check_tree);

void check_error_state(Borrow<Doc> d) {
    XMLError e = d->ErrorID();
    stitch::check(e == XML_SUCCESS || d->Error(), "Error() agrees with ErrorID()");
    stitch::check(d->ErrorName() != nullptr, "name");
    if (e != XML_SUCCESS) stitch::check(d->ErrorStr() != nullptr, "err str");
}
STITCH_ENDPOINT(check_error_state);

// Elements reachable by name from the document agree with a manual search.
void check_root(Borrow<Doc> d) {
    XMLElement* r = d->RootElement();
    XMLElement* m = d->FirstChildElement();
    stitch::check(r == m, "RootElement == FirstChildElement of the document");
}
STITCH_ENDPOINT(check_root);
