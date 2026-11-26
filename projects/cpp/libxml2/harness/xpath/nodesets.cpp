// Node-set API (xpathInternals.h): building sets, set algebra (EXSLT sets), the axis iterators
// xmlXPathNext*, and registering namespaces, variables and functions on a context.
#include "../objects.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
// Nodes of the document tree in document order: elements, their attributes, text, CDATA,
// comments and PIs (not entity content, not DTD nodes). At most `max`.
void tree_nodes(xmlNodePtr n, std::vector<xmlNodePtr>& out, size_t max) {
    for (xmlNodePtr c = n; c && out.size() < max; c = c->next) {
        switch (c->type) {
            case XML_ELEMENT_NODE:
                out.push_back(c);
                for (xmlAttrPtr a = c->properties; a && out.size() < max; a = a->next) out.push_back((xmlNodePtr)a);
                if (c->children) tree_nodes(c->children, out, max);
                break;
            case XML_TEXT_NODE: case XML_CDATA_SECTION_NODE: case XML_COMMENT_NODE: case XML_PI_NODE:
                out.push_back(c);
                break;
            default: break;
        }
    }
}
std::vector<xmlNodePtr> doc_nodes(xmlDocPtr d) {
    std::vector<xmlNodePtr> v;
    v.push_back((xmlNodePtr)d);
    tree_nodes(d->children, v, 400);
    return v;
}
bool before(xmlNodePtr a, xmlNodePtr b) { return xmlXPathCmpNodes(a, b) > 0; }  // a precedes b
std::vector<xmlNodePtr> items(xmlNodeSetPtr s) {
    std::vector<xmlNodePtr> v;
    if (s) for (int i = 0; i < s->nodeNr; i++) v.push_back(s->nodeTab[i]);
    return v;
}
bool has(const std::vector<xmlNodePtr>& v, xmlNodePtr n) { return std::find(v.begin(), v.end(), n) != v.end(); }
std::vector<xmlNodePtr> sorted_unique(std::vector<xmlNodePtr> v) {
    std::vector<xmlNodePtr> u;
    for (auto n : v) if (!has(u, n)) u.push_back(n);
    std::stable_sort(u.begin(), u.end(), before);
    return u;
}
// a result that may be one of the inputs: free it only if it is new
void free_result(xmlNodeSetPtr r, xmlNodeSetPtr a, xmlNodeSetPtr b = nullptr) {
    if (r && r != a && r != b) xmlXPathFreeNodeSet(r);
}
std::string strval(xmlNodePtr n) {
    xmlChar* s = xmlXPathCastNodeToString(n);
    std::string r = s ? (const char*)s : "";
    xmlFree(s);
    return r;
}
}  // namespace

// Build two node sets from nodes of a document and check the set operations against a model.
void nodeset_algebra(Borrow<Doc> d, Src<64> s) {
    std::vector<xmlNodePtr> all = doc_nodes(*d);
    std::vector<xmlNodePtr> ma, mb;  // models (insertion order, unique)
    xmlNodeSetPtr a = xmlXPathNodeSetCreate(nullptr), b = xmlXPathNodeSetCreate(nullptr);
    while (s->more() && ma.size() + mb.size() < 40) {
        xmlNodePtr n = all[s->choice(all.size())];
        bool to_a = s->coin();
        auto& m = to_a ? ma : mb;
        xmlNodeSetPtr set = to_a ? a : b;
        if (has(m, n)) {
            xmlXPathNodeSetAdd(set, n);  // Add skips duplicates
        } else {
            if (s->coin()) xmlXPathNodeSetAddUnique(set, n);
            else xmlXPathNodeSetAdd(set, n);
            m.push_back(n);
        }
        stitch::check(items(set) == m, "set holds the added nodes in order");
    }
    for (auto n : all) stitch::check(xmlXPathNodeSetContains(a, n) == has(ma, n), "NodeSetContains");
    // difference, intersection, has-same-nodes keep the order of the first set
    std::vector<xmlNodePtr> diff, inter;
    for (auto n : ma) (has(mb, n) ? inter : diff).push_back(n);
    xmlNodeSetPtr r = xmlXPathDifference(a, b);
    stitch::check(items(r) == diff, "difference");
    free_result(r, a, b);
    r = xmlXPathIntersection(a, b);
    stitch::check(items(r) == inter, "intersection");
    free_result(r, a, b);
    stitch::check(xmlXPathHasSameNodes(a, b) == !inter.empty(), "has-same-nodes");
    // merge: a + the nodes of b not in a
    xmlNodeSetPtr ca = xmlXPathNodeSetMerge(nullptr, a);
    stitch::check(items(ca) == ma, "merge into nothing copies");
    xmlNodeSetPtr m = xmlXPathNodeSetMerge(ca, b);
    std::vector<xmlNodePtr> merged = ma;
    for (auto n : mb) if (!has(merged, n)) merged.push_back(n);
    stitch::check(sorted_unique(items(m)) == sorted_unique(merged), "merge holds both sets");
    // sorting gives document order
    xmlXPathNodeSetSort(m);
    stitch::check(items(m) == sorted_unique(merged), "sort gives document order");
    // delete by node and by index
    if (!ma.empty()) {
        xmlNodePtr victim = ma[s->choice(ma.size())];
        xmlXPathNodeSetDel(a, victim);
        ma.erase(std::find(ma.begin(), ma.end(), victim));
        stitch::check(items(a) == ma, "NodeSetDel removes the node");
    }
    if (!mb.empty()) {
        int i = (int)s->choice(mb.size());
        xmlXPathNodeSetRemove(b, i);
        mb.erase(mb.begin() + i);
        stitch::check(items(b) == mb, "NodeSetRemove removes the index");
    }
    // EXSLT set:distinct keeps the first node of each string value, in document order
    xmlNodeSetPtr da = xmlXPathNodeSetMerge(nullptr, a);
    r = xmlXPathDistinct(da);
    std::set<std::string> seen;
    std::vector<xmlNodePtr> want;
    for (auto n : sorted_unique(ma))
        if (seen.insert(strval(n)).second) want.push_back(n);
    stitch::check(items(r) == want, "distinct");
    free_result(r, da);
    xmlXPathFreeNodeSet(da);
    std::vector<xmlNodePtr> sa = sorted_unique(ma), sb = sorted_unique(mb);
    // wrap a set in an object, copy the list, cast to boolean / number / string
    xmlXPathObjectPtr oa = xmlXPathNewNodeSetList(a);
    stitch::check(oa && items(oa->nodesetval) == ma, "NewNodeSetList copies the set");
    stitch::check(xmlXPathCastNodeSetToBoolean(a) == !ma.empty(), "boolean(node-set)");
    std::string first = ma.empty() ? "" : strval(sorted_unique(ma)[0]);
    xmlChar* sv = xmlXPathCastNodeSetToString(a);
    stitch::check((sv ? std::string((const char*)sv) : "") == first, "string(node-set) is the first node's value");
    xmlFree(sv);
    double nv = xmlXPathCastNodeSetToNumber(a);
    double fv = ma.empty() ? NAN : xmlXPathCastNodeToNumber(sorted_unique(ma)[0]);
    stitch::log("number(node-set) %g, first node %g (%zu nodes)", nv, fv, ma.size());
    stitch::check((std::isnan(nv) && std::isnan(fv)) || nv == fv, "number(node-set) is the first node's number");
    xmlXPathFreeObject(oa);
    xmlXPathObjectPtr wa = xmlXPathWrapNodeSet(a);  // takes `a`
    xmlXPathObjectPtr wb = xmlXPathWrapNodeSet(b);
    xmlXPathFreeNodeSetList(wb);                     // frees the object, not the set
    xmlXPathFreeObject(wa);
    xmlXPathFreeNodeSet(b);
    xmlXPathFreeNodeSet(m);
    // EXSLT set:leading / set:trailing: the nodes of a before / after the first node of b
    for (int which : {2, 3, 0, 1}) {  // trailing first, so a failed leading check does not hide them
        xmlNodeSetPtr x = xmlXPathNodeSetCreate(nullptr), y = xmlXPathNodeSetCreate(nullptr);
        for (auto n : ma) xmlXPathNodeSetAdd(x, n);
        for (auto n : mb) xmlXPathNodeSetAdd(y, n);
        xmlXPathNodeSetSort(x);
        xmlXPathNodeSetSort(y);
        std::vector<xmlNodePtr> w;
        if (sb.empty()) w = sa;
        else if (has(sa, sb[0])) {
            auto it = std::find(sa.begin(), sa.end(), sb[0]);
            if (which < 2) w.assign(sa.begin(), it);
            else w.assign(it + 1, sa.end());
        }
        const char* what[] = {"leading", "leading (sorted)", "trailing", "trailing (sorted)"};
        switch (which) {
            case 0: r = xmlXPathLeading(x, y); break;
            case 1: r = xmlXPathLeadingSorted(x, y); break;
            case 2: r = xmlXPathTrailing(x, y); break;
            default: r = xmlXPathTrailingSorted(x, y); break;
        }
        stitch::log("%s: a=%zu b=%zu want=%zu got=%d", what[which], sa.size(), sb.size(), w.size(), r ? r->nodeNr : -1);
        stitch::check(items(r) == w, what[which]);
        free_result(r, x, y);
        // the single-node variants
        if (!sb.empty()) {
            r = which < 2 ? (which ? xmlXPathNodeLeadingSorted(x, sb[0]) : xmlXPathNodeLeading(x, sb[0]))
                          : (which == 3 ? xmlXPathNodeTrailingSorted(x, sb[0]) : xmlXPathNodeTrailing(x, sb[0]));
            stitch::check(items(r) == w, what[which]);
            free_result(r, x);
        }
        xmlXPathFreeNodeSet(x);
        xmlXPathFreeNodeSet(y);
    }
}
STITCH_ENDPOINT(nodeset_algebra);

// Namespace nodes in node sets are copies (xmlXPathNodeSetAddNs, xmlXPathNodeSetFreeNs).
void nodeset_namespaces(Borrow<Node> n) {
    xmlNodePtr e = *n;
    stitch::require(e->type == XML_ELEMENT_NODE, "not an element");
    xmlNsPtr* list = nullptr;
    int rc = xmlGetNsListSafe(e->doc, e, &list);
    stitch::require(rc == 0 && list, "no namespaces in scope");
    xmlNodeSetPtr set = xmlXPathNodeSetCreate(nullptr);
    int count = 0;
    for (int i = 0; list[i]; i++, count++) stitch::check(xmlXPathNodeSetAddNs(set, e, list[i]) == 0, "AddNs");
    for (int i = 0; list[i]; i++) xmlXPathNodeSetAddNs(set, e, list[i]);  // already there
    stitch::check(set->nodeNr == count, "each namespace once");
    for (int i = 0; i < set->nodeNr; i++) stitch::check(set->nodeTab[i]->type == XML_NAMESPACE_DECL, "namespace node");
    xmlNodeSetPtr copy = xmlXPathNodeSetMerge(nullptr, set);
    stitch::check(copy && copy->nodeNr == count, "merge copies namespace nodes");
    if (count) xmlXPathNodeSetRemove(copy, 0);
    xmlXPathFreeNodeSet(copy);
    xmlNsPtr dup = (xmlNsPtr)set->nodeTab[0];
    xmlXPathNodeSetDel(set, (xmlNodePtr)dup);  // frees the copy
    if (set->nodeNr > 0) {                     // take the last one out and free it ourselves
        xmlNsPtr last = (xmlNsPtr)set->nodeTab[--set->nodeNr];
        set->nodeTab[set->nodeNr] = nullptr;
        xmlXPathNodeSetFreeNs(last);
    }
    xmlXPathFreeNodeSet(set);
    xmlFree(list);
}
STITCH_ENDPOINT(nodeset_namespaces);

// The axis iterators visit what the corresponding location path selects.
void xpath_next_axis(Borrow<Node> n, Range<0, 12> axis) {
    static const struct { const char* name; xmlNodePtr (*f)(xmlXPathParserContextPtr, xmlNodePtr); } axes[] = {
        {"self", xmlXPathNextSelf}, {"child", xmlXPathNextChild}, {"descendant", xmlXPathNextDescendant},
        {"descendant-or-self", xmlXPathNextDescendantOrSelf}, {"parent", xmlXPathNextParent},
        {"ancestor", xmlXPathNextAncestor}, {"ancestor-or-self", xmlXPathNextAncestorOrSelf},
        {"following-sibling", xmlXPathNextFollowingSibling}, {"preceding-sibling", xmlXPathNextPrecedingSibling},
        {"following", xmlXPathNextFollowing}, {"preceding", xmlXPathNextPreceding},
        {"attribute", xmlXPathNextAttribute}, {"namespace", xmlXPathNextNamespace},
    };
    xmlNodePtr e = *n;
    stitch::require(e->doc != nullptr, "detached");
    bool attached = false;
    for (xmlNodePtr x = e; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE) attached = true;
    stitch::require(attached, "unlinked subtree");
    const auto& A = axes[(int)axis];
    xmlXPathContextPtr c = xmlXPathNewContext(e->doc);
    stitch::require(c, "no context");
    xmlXPathSetContextNode(e, c);
    std::string expr = std::string(A.name) + "::node()";
    xmlXPathObjectPtr want = xmlXPathEval((const xmlChar*)expr.c_str(), c);
    xmlXPathSetContextNode(e, c);
    xmlXPathParserContextPtr p = xmlXPathNewParserContext((const xmlChar*)"", c);
    std::multiset<std::string> got_ns, want_ns;
    std::set<xmlNodePtr> got, wantset;
    int steps = 0;
    bool attr_axis = std::string(A.name) == "attribute";
    auto counted = [&](xmlNodePtr x) {
        if (attr_axis) return x->type == XML_ATTRIBUTE_NODE;
        return x->type == XML_ELEMENT_NODE || x->type == XML_TEXT_NODE || x->type == XML_CDATA_SECTION_NODE ||
               x->type == XML_COMMENT_NODE || x->type == XML_PI_NODE || x->type == XML_DOCUMENT_NODE;
    };
    for (xmlNodePtr cur = A.f(p, nullptr); cur && steps < 200000; cur = A.f(p, cur), steps++) {
        if (cur->type == XML_NAMESPACE_DECL) got_ns.insert(lx::str(((xmlNsPtr)cur)->prefix));
        else if (counted(cur)) got.insert(cur);
    }
    stitch::require(steps < 200000, "too many nodes");
    if (want && want->nodesetval)
        for (int i = 0; i < want->nodesetval->nodeNr; i++) {
            xmlNodePtr x = want->nodesetval->nodeTab[i];
            if (x->type == XML_NAMESPACE_DECL) want_ns.insert(lx::str(((xmlNsPtr)x)->prefix));
            else if (counted(x)) wantset.insert(x);
        }
    stitch::log("%s: iterator %zu+%zu, path %zu+%zu", A.name, got.size(), got_ns.size(), wantset.size(), want_ns.size());
    if (want) {
        stitch::check(got == wantset, "iterator visits the nodes of the location path");
        stitch::check(got_ns == want_ns, "iterator visits the namespaces of the location path");
    }
    xmlXPathFreeObject(want);
    xmlXPathFreeParserContext(p);
    xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpath_next_axis);

static void twice(xmlXPathParserContextPtr ctxt, int nargs) {
    if (nargs != 1) { xmlXPathErr(ctxt, XPATH_INVALID_ARITY); return; }
    double v = xmlXPathPopNumber(ctxt);
    xmlXPathValuePush(ctxt, xmlXPathNewFloat(v * 2));
}
static xmlXPathObjectPtr var_lookup(void* data, const xmlChar* name, const xmlChar* ns) {
    if (!xmlStrEqual(name, (const xmlChar*)"magic") || ns) return nullptr;
    return xmlXPathNewFloat(*(double*)data);
}
static xmlXPathFunction func_lookup(void*, const xmlChar* name, const xmlChar*) {
    return xmlStrEqual(name, (const xmlChar*)"twice2") ? twice : nullptr;
}

// Namespaces, variables and functions registered on a context are found by lookups and used by
// expressions; the cleanup functions remove them.
void xpath_registry(Str<16, XmlName> prefix, Str<48, UriText> uri, Str<16, XmlName> var, F64 value) {
    stitch::require(xmlValidateNCName((const xmlChar*)prefix.c_str(), 0) == 0, "prefix is not an NCName");
    stitch::require(xmlValidateNCName((const xmlChar*)var.c_str(), 0) == 0, "variable is not an NCName");
    stitch::require(uri.size() > 0 && strlen(uri.c_str()) == uri.size(), "empty uri");
    stitch::require(strcmp(prefix.c_str(), "xml") != 0, "the xml prefix is fixed");
    xmlXPathContextPtr c = xmlXPathNewContext(nullptr);
    stitch::require(c, "no context");
    const xmlChar* P = (const xmlChar*)prefix.c_str();
    const xmlChar* U = (const xmlChar*)uri.c_str();
    const xmlChar* V = (const xmlChar*)var.c_str();
    stitch::check(xmlXPathRegisterNs(c, P, U) == 0, "register namespace");
    stitch::check(xmlStrEqual(xmlXPathNsLookup(c, P), U), "namespace lookup");
    stitch::check(xmlXPathRegisterVariable(c, V, xmlXPathNewFloat(value)) == 0, "register variable");
    stitch::check(xmlXPathRegisterVariableNS(c, V, U, xmlXPathNewFloat(-(double)value)) == 0, "register ns variable");
    xmlXPathObjectPtr got = xmlXPathVariableLookup(c, V);
    xmlXPathObjectPtr gotns = xmlXPathVariableLookupNS(c, V, U);
    double v = value;
    auto same = [](double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; };
    stitch::check(got && gotns && same(got->floatval, v) && same(gotns->floatval, -v), "variable lookups");
    xmlXPathFreeObject(got);
    xmlXPathFreeObject(gotns);
    stitch::require(xmlXPathFunctionLookup(c, V) == nullptr, "a core function name");
    stitch::check(xmlXPathRegisterFunc(c, V, twice) == 0 && xmlXPathFunctionLookup(c, V) == twice, "register function");
    stitch::check(xmlXPathRegisterFuncNS(c, V, U, twice) == 0 && xmlXPathFunctionLookupNS(c, V, U) == twice, "register ns function");
    // expressions see them: $var + $prefix:var == 0 and prefix:var(1) == 2
    std::string e1 = std::string("$") + var.c_str() + " + $" + prefix.c_str() + ":" + var.c_str();
    xmlXPathObjectPtr r = xmlXPathEval((const xmlChar*)e1.c_str(), c);
    stitch::check(r && r->type == XPATH_NUMBER && (std::isnan(r->floatval) || std::isinf(v) || r->floatval == 0), "variables in an expression");
    xmlXPathFreeObject(r);
    std::string e2 = std::string(prefix.c_str()) + ":" + var.c_str() + "(21)";
    r = xmlXPathEval((const xmlChar*)e2.c_str(), c);
    stitch::check(r && r->type == XPATH_NUMBER && r->floatval == 42, "function in an expression");
    xmlXPathFreeObject(r);
    // removing a variable: register NULL
    xmlXPathRegisterVariable(c, V, nullptr);
    got = xmlXPathVariableLookup(c, V);
    stitch::check(got == nullptr, "unregistered variable is gone");
    // lookup hooks
    double magic = 7;
    xmlXPathRegisterVariableLookup(c, var_lookup, &magic);
    xmlXPathRegisterFuncLookup(c, func_lookup, nullptr);
    r = xmlXPathEval((const xmlChar*)"twice2($magic)", c);
    stitch::check(r && r->type == XPATH_NUMBER && r->floatval == 14, "lookup hooks");
    xmlXPathFreeObject(r);
    xmlXPathRegisterVariableLookup(c, nullptr, nullptr);
    xmlXPathRegisterFuncLookup(c, nullptr, nullptr);
    // cleanup
    xmlXPathRegisteredNsCleanup(c);
    stitch::check(xmlXPathNsLookup(c, P) == nullptr || xmlStrEqual(P, (const xmlChar*)"xml"), "namespaces cleaned up");
    xmlXPathRegisteredVariablesCleanup(c);
    stitch::check(xmlXPathVariableLookupNS(c, V, U) == nullptr, "variables cleaned up");
    xmlXPathRegisteredFuncsCleanup(c);
    stitch::check(xmlXPathFunctionLookup(c, V) == nullptr && xmlXPathFunctionLookupNS(c, V, U) == nullptr, "functions cleaned up");
    xmlXPathRegisterAllFunctions(c);  // (core functions are built in)
    stitch::check(xmlXPathFunctionLookup(c, (const xmlChar*)"count") != nullptr, "core functions are found");
    xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpath_registry);

// Name scanning on a parser context, and the node-type keywords.
void xpath_scan_names(Str<48, XPathExpr> text) {
    std::string t = text.s.substr(0, strlen(text.c_str()));
    xmlXPathContextPtr c = xmlXPathNewContext(nullptr);
    xmlXPathParserContextPtr p = xmlXPathNewParserContext((const xmlChar*)t.c_str(), c);
    xmlChar* name = xmlXPathParseName(p);
    if (name) {
        stitch::check(t.compare(0, (size_t)xmlStrlen(name), (const char*)name) == 0, "ParseName returns a prefix of the input");
    }
    xmlXPathParserContextPtr q = xmlXPathNewParserContext((const xmlChar*)t.c_str(), c);
    xmlChar* nc = xmlXPathParseNCName(q);
    if (nc) {
        stitch::check(t.compare(0, (size_t)xmlStrlen(nc), (const char*)nc) == 0, "ParseNCName returns a prefix of the input");
        stitch::check(!strchr((const char*)nc, ':'), "an NCName has no colon");
        if (name) stitch::check(xmlStrlen(nc) <= xmlStrlen(name), "an NCName is not longer than the Name");
    }
    static const char* types[] = {"node", "text", "comment", "processing-instruction"};
    bool is_type = false;
    for (auto ty : types) is_type |= t == ty;
    stitch::check((xmlXPathIsNodeType((const xmlChar*)t.c_str()) != 0) == is_type, "IsNodeType");
    // xmlXPathRoot pushes the document; xmlXPathErr records an error code
    xmlXPathRoot(p);
    xmlXPathErr(p, XPATH_EXPR_ERROR);
    stitch::check(p->error == XPATH_EXPR_ERROR, "xmlXPathErr sets the error");
    xmlFree(name);
    xmlFree(nc);
    xmlXPathFreeParserContext(p);
    xmlXPathFreeParserContext(q);
    xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpath_scan_names);
