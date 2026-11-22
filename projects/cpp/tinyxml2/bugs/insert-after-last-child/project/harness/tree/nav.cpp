#include "../objects.hpp"

#define NAV(NAME, EXPR)                                         \
    New<Node> NAME(Borrow<Doc> d, Borrow<Node> n) {             \
        stitch::require(live(d, n), "stale node");              \
        XMLNode* r = (EXPR);                                    \
        stitch::require(r, "none");                             \
        return mk(d, r);                                        \
    }                                                           \
    STITCH_ENDPOINT(NAME);

NAV(first_child, n->FirstChild())
NAV(last_child, n->LastChild())
NAV(next_sibling, n->NextSibling())
NAV(prev_sibling, n->PreviousSibling())
NAV(parent_node, n->Parent())
NAV(first_child_element_any, n->FirstChildElement())
NAV(last_child_element_any, n->LastChildElement())
NAV(next_sibling_element_any, n->NextSiblingElement())
NAV(prev_sibling_element_any, n->PreviousSiblingElement())

New<Node> first_child_element(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name) {
    stitch::require(live(d, n), "stale node");
    XMLElement* e = n->FirstChildElement(name.c_str());
    stitch::require(e, "no such child");
    stitch::check(!strcmp(e->Name(), name.c_str()), "child has requested name");
    return mk(d, e);
}
STITCH_ENDPOINT(first_child_element);

New<Node> next_sibling_element(Borrow<Doc> d, Borrow<Node> n, Str<32, XmlName> name) {
    stitch::require(live(d, n), "stale node");
    XMLElement* e = n->NextSiblingElement(name.c_str());
    stitch::require(e, "no such sibling");
    return mk(d, e);
}
STITCH_ENDPOINT(next_sibling_element);

// Pick the name of an existing child so the lookup finds something.
New<Node> child_by_existing_name(Borrow<Doc> d, Borrow<Node> n, Src<8> s) {
    stitch::require(live(d, n), "stale node");
    int cnt = 0;
    for (XMLElement* e = n->FirstChildElement(); e; e = e->NextSiblingElement()) cnt++;
    stitch::require(cnt > 0, "no child elements");
    XMLElement* e = n->FirstChildElement();
    for (size_t i = s->choice(cnt); i > 0; i--) e = e->NextSiblingElement();
    XMLElement* f = n->FirstChildElement(e->Name());
    stitch::check(f != nullptr, "FirstChildElement(name) finds an existing child");
    XMLElement* l = n->LastChildElement(e->Name());
    stitch::check(l != nullptr, "LastChildElement(name) finds an existing child");
    return mk(d, f);
}
STITCH_ENDPOINT(child_by_existing_name);
