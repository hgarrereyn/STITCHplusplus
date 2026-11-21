#include "../objects.hpp"
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <vector>
#include <libxml/xmlschemas.h>
#include <libxml/xmlautomata.h>
#include <libxml/xmlregexp.h>
#include <libxml/xmlreader.h>
#include <libxml/HTMLtree.h>


static void kids(xmlNodePtr n, std::vector<xmlNodePtr>& out) { for (xmlNodePtr c = n->children; c; c = c->next) if (c->type != XML_ATTRIBUTE_NODE && c->type != XML_DTD_NODE && c->type != XML_ENTITY_DECL && c->type != XML_XINCLUDE_START && c->type != XML_XINCLUDE_END) out.push_back(c); }
static void desc(xmlNodePtr n, std::vector<xmlNodePtr>& out) { std::vector<xmlNodePtr> k; kids(n, k); for (auto c : k) { out.push_back(c); if (c->type != XML_ENTITY_REF_NODE) desc(c, out); } }
static std::vector<xmlNodePtr> axis_ref(xmlNodePtr n, const std::string& ax) {
    std::vector<xmlNodePtr> v;
    if (ax == "child") kids(n, v);
    else if (ax == "descendant") desc(n, v);
    else if (ax == "descendant-or-self") { v.push_back(n); desc(n, v); }
    else if (ax == "self") v.push_back(n);
    else if (ax == "parent") { if (n->parent && n->parent->type != XML_DOCUMENT_NODE) v.push_back(n->parent); else if (n->parent) v.push_back(n->parent); }
    else if (ax == "ancestor") { for (xmlNodePtr p = n->parent; p; p = p->parent) v.push_back(p); }
    else if (ax == "ancestor-or-self") { for (xmlNodePtr p = n; p; p = p->parent) v.push_back(p); }
    else if (ax == "following-sibling") { for (xmlNodePtr s = n->next; s; s = s->next) if (s->type != XML_DTD_NODE) v.push_back(s); }
    else if (ax == "preceding-sibling") { for (xmlNodePtr s = n->prev; s; s = s->prev) if (s->type != XML_DTD_NODE) v.push_back(s); }
    return v;
}
static bool counted(xmlNodePtr x) { return x->type == XML_ELEMENT_NODE || x->type == XML_TEXT_NODE || x->type == XML_CDATA_SECTION_NODE || x->type == XML_COMMENT_NODE || x->type == XML_PI_NODE || x->type == XML_DOCUMENT_NODE; }

void axis_child(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"child::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "child");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("child: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "child::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_child);

void axis_descendant(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"descendant::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "descendant");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("descendant: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "descendant::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_descendant);

void axis_descendant_or_self(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"descendant-or-self::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "descendant-or-self");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("descendant-or-self: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "descendant-or-self::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_descendant_or_self);

void axis_self(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"self::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "self");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("self: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "self::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_self);

void axis_parent(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"parent::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "parent");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("parent: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "parent::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_parent);

void axis_ancestor(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"ancestor::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "ancestor");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("ancestor: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "ancestor::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_ancestor);

void axis_ancestor_or_self(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"ancestor-or-self::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "ancestor-or-self");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("ancestor-or-self: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "ancestor-or-self::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_ancestor_or_self);

void axis_following_sibling(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"following-sibling::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "following-sibling");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("following-sibling: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "following-sibling::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_following_sibling);

void axis_preceding_sibling(Borrow<Node> n) {
    stitch::require((*n)->doc != nullptr, "detached");
    bool attached = false; for (xmlNodePtr x = *n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"preceding-sibling::node()", c);
    std::vector<xmlNodePtr> want = axis_ref(*n, "preceding-sibling");
    std::set<xmlNodePtr> w; for (auto x : want) if (counted(x)) w.insert(x);
    std::set<xmlNodePtr> got;
    if (o && o->nodesetval) for (int i = 0; i < o->nodesetval->nodeNr; i++) got.insert(o->nodesetval->nodeTab[i]);
    stitch::log("preceding-sibling: xpath %zu nodes, tree walk %zu", got.size(), w.size());
    stitch::check(got == w, "preceding-sibling::node() selects the nodes a tree walk finds");
    if (o && o->nodesetval) for (int i = 1; i < o->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(o->nodesetval->nodeTab[i - 1], o->nodesetval->nodeTab[i]) >= 0, "results are in document order");
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_preceding_sibling);

void axis_following(Borrow<Node> n) {
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"following::node()", c);
    xmlXPathObjectPtr cnt = xmlXPathEvalExpression((const xmlChar*)"count(following::node())", c);
    if (o && cnt) stitch::check(xmlXPathNodeSetGetLength(o->nodesetval) == (int)cnt->floatval, "count(following::node()) == the node-set size");
    xmlXPathFreeObject(o); xmlXPathFreeObject(cnt); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_following);

void axis_preceding(Borrow<Node> n) {
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"preceding::node()", c);
    xmlXPathObjectPtr cnt = xmlXPathEvalExpression((const xmlChar*)"count(preceding::node())", c);
    if (o && cnt) stitch::check(xmlXPathNodeSetGetLength(o->nodesetval) == (int)cnt->floatval, "count(preceding::node()) == the node-set size");
    xmlXPathFreeObject(o); xmlXPathFreeObject(cnt); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_preceding);

void axis_attribute(Borrow<Node> n) {
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"attribute::node()", c);
    xmlXPathObjectPtr cnt = xmlXPathEvalExpression((const xmlChar*)"count(attribute::node())", c);
    if (o && cnt) stitch::check(xmlXPathNodeSetGetLength(o->nodesetval) == (int)cnt->floatval, "count(attribute::node()) == the node-set size");
    xmlXPathFreeObject(o); xmlXPathFreeObject(cnt); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_attribute);

void axis_namespace(Borrow<Node> n) {
    xmlXPathContextPtr c = xmlXPathNewContext((*n)->doc);
    c->node = *n;
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"namespace::node()", c);
    xmlXPathObjectPtr cnt = xmlXPathEvalExpression((const xmlChar*)"count(namespace::node())", c);
    if (o && cnt) stitch::check(xmlXPathNodeSetGetLength(o->nodesetval) == (int)cnt->floatval, "count(namespace::node()) == the node-set size");
    xmlXPathFreeObject(o); xmlXPathFreeObject(cnt); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(axis_namespace);

void xpfn_count(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"count(//*)", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"count(//*)");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_count);

void xpfn_sum______number______number(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"sum(//@*[number(.) = number(.)])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"sum(//@*[number(.) = number(.)])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_sum______number______number);

void xpfn_string_____1(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"string(//*[1])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"string(//*[1])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_string_____1);

void xpfn_number______1(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"number(//@*[1])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"number(//@*[1])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_number______1);

void xpfn_boolean___text(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"boolean(//text())", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"boolean(//text())");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_boolean___text);

void xpfn_local_name_____last(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"local-name(//*[last()])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"local-name(//*[last()])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_local_name_____last);

void xpfn_name_____2(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"name(//*[2])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"name(//*[2])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_name_____2);

void xpfn_namespace_uri_____1(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"namespace-uri(//*[1])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"namespace-uri(//*[1])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_namespace_uri_____1);

void xpfn_lang__en(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"lang('en')", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"lang('en')");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_lang__en);

void xpfn_id__a_b_c(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"id('a b c')", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"id('a b c')");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_id__a_b_c);

void xpfn_count_____position___mod_2___0(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"count(//*[position() mod 2 = 0])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"count(//*[position() mod 2 = 0])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_count_____position___mod_2___0);

void xpfn_count___node___last(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"count(//node()[last()])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"count(//node()[last()])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_count___node___last);

void xpfn_concat_name___________count(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"concat(name(/*), '-', count(//@*))", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"concat(name(/*), '-', count(//@*))");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_concat_name___________count);

void xpfn_string_length_string(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"string-length(string(/))", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"string-length(string(/))");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_string_length_string);

void xpfn_normalize_space_string(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"normalize-space(string(/))", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"normalize-space(string(/))");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_normalize_space_string);

void xpfn_count______id(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"count(//*[@id])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"count(//*[@id])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_count______id);

void xpfn_self__a_or_self__b(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"//*[self::a or self::b]", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"//*[self::a or self::b]");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_self__a_or_self__b);

void xpfn_count___comment_______processing_instruc(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"count(//comment() | //processing-instruction())", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"count(//comment() | //processing-instruction())");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_count___comment_______processing_instruc);

void xpfn_count__descendant____not(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"count(/descendant::*[not(*)])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"count(/descendant::*[not(*)])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_count__descendant____not);

void xpfn_count______________1(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathRegisterNs(c, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)"count(//*[. = ../*[1]])", c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)"count(//*[. = ../*[1]])");
    xmlXPathObjectPtr o2 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    if (o && o2) {
        xmlChar *a = xmlXPathCastToString(o), *b = xmlXPathCastToString(o2);
        stitch::check(xmlStrEqual(a, b), "compiled and interpreted evaluation agree");
        xmlFree(a); xmlFree(b);
    }
    xmlXPathFreeObject(o); xmlXPathFreeObject(o2); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpfn_count______________1);
