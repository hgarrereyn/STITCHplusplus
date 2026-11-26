// The XPath evaluation entry points (xpath.h) agree with each other, with and without the object
// cache and the element-order index; XPointer evaluation; debug dumps of objects and expressions.
#include "../objects.hpp"
#include <libxml/debugXML.h>
#include <libxml/xpointer.h>
#include <cmath>

namespace {
bool same_obj(xmlXPathObjectPtr a, xmlXPathObjectPtr b) {
    if (!a || !b) return a == b;
    if (a->type != b->type) return false;
    switch (a->type) {
        case XPATH_NUMBER: return (std::isnan(a->floatval) && std::isnan(b->floatval)) || a->floatval == b->floatval;
        case XPATH_BOOLEAN: return a->boolval == b->boolval;
        case XPATH_STRING: return xmlStrEqual(a->stringval, b->stringval);
        case XPATH_NODESET: {
            int na = a->nodesetval ? a->nodesetval->nodeNr : 0, nb = b->nodesetval ? b->nodesetval->nodeNr : 0;
            if (na != nb) return false;
            for (int i = 0; i < na; i++) {
                xmlNodePtr x = a->nodesetval->nodeTab[i], y = b->nodesetval->nodeTab[i];
                if (x->type == XML_NAMESPACE_DECL) {
                    if (y->type != XML_NAMESPACE_DECL || !xmlStrEqual(((xmlNsPtr)x)->prefix, ((xmlNsPtr)y)->prefix)) return false;
                } else if (x != y) return false;
            }
            return true;
        }
        default: return true;
    }
}
void count_error(void* data, const xmlError*) { ++*(int*)data; }
bool attached(xmlNodePtr n) {
    for (xmlNodePtr x = n; x; x = x->parent) if (x->type == XML_DOCUMENT_NODE || x->type == XML_HTML_DOCUMENT_NODE) return true;
    return false;
}
}  // namespace

void xpath_eval_variants(Borrow<Node> n, Str<128, XPathExpr> expr, Bool cache) {
    xmlNodePtr e = *n;
    stitch::require(e->doc && attached(e), "unlinked node");
    const xmlChar* x = (const xmlChar*)expr.c_str();
    xmlXPathContextPtr c = xmlXPathNewContext(e->doc);
    stitch::require(c, "no context");
    int errors = 0;
    xmlXPathSetErrorHandler(c, count_error, &errors);
    xmlXPathContextSetCache(c, cache ? 1 : 0, cache ? 3 : 0, 0);
    xmlXPathObjectPtr r1 = xmlXPathNodeEval(e, x, c);
    stitch::check(xmlXPathSetContextNode(e, c) == 0 && c->node == e, "SetContextNode");
    xmlXPathObjectPtr r2 = xmlXPathEval(x, c);
    xmlXPathSetContextNode(e, c);
    xmlXPathObjectPtr r3 = xmlXPathEvalExpression(x, c);
    xmlXPathCompExprPtr comp = xmlXPathCtxtCompile(c, x);
    xmlXPathSetContextNode(e, c);
    xmlXPathObjectPtr r4 = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    stitch::log("results: %d %d %d %d, errors %d", r1 != nullptr, r2 != nullptr, r3 != nullptr, r4 != nullptr, errors);
    stitch::check(same_obj(r1, r2), "xmlXPathNodeEval == xmlXPathEval");
    stitch::check(same_obj(r2, r3), "xmlXPathEval == xmlXPathEvalExpression");
    if (comp) stitch::check(same_obj(r2, r4), "xmlXPathEval == compile + xmlXPathCompiledEval");
    if (comp && r4) {
        xmlXPathSetContextNode(e, c);
        int b = xmlXPathCompiledEvalToBoolean(comp, c);
        if (b >= 0) stitch::check(b == xmlXPathCastToBoolean(r4), "CompiledEvalToBoolean == boolean(CompiledEval)");
    }
    // the debug dumps walk the whole object / expression
    if (FILE* devnull = fopen("/dev/null", "w")) {
        if (r2) xmlXPathDebugDumpObject(devnull, r2, 0);
        if (comp) xmlXPathDebugDumpCompExpr(devnull, comp, 0);
        fclose(devnull);
    }
    xmlXPathFreeCompExpr(comp);
    xmlXPathFreeObject(r1);
    xmlXPathFreeObject(r2);
    xmlXPathFreeObject(r3);
    xmlXPathFreeObject(r4);
    xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpath_eval_variants);

// Indexing the element order (xmlXPathOrderDocElems, for static documents) does not change what
// an expression selects. Done on a copy: the index is stale once the tree changes.
void xpath_ordered_doc(Borrow<Doc> d, Str<128, XPathExpr> expr) {
    xmlDocPtr copy = xmlCopyDoc(*d, 1);
    stitch::require(copy, "copy failed");
    xmlXPathContextPtr c = xmlXPathNewContext(copy);
    xmlXPathObjectPtr before = xmlXPathEval((const xmlChar*)expr.c_str(), c);
    long n = xmlXPathOrderDocElems(copy);
    stitch::check(n >= 0, "OrderDocElems counts the elements");
    stitch::check(n == lx::count_elements(copy), "OrderDocElems numbers every element");
    xmlXPathObjectPtr after = xmlXPathEval((const xmlChar*)expr.c_str(), c);
    stitch::check(same_obj(before, after), "same result with the element index");
    xmlXPathFreeObject(before);
    xmlXPathFreeObject(after);
    xmlXPathFreeContext(c);
    xmlFreeDoc(copy);
}
STITCH_ENDPOINT(xpath_ordered_doc);

// XPointer: xpointer(expr) and element() schemes; xpointer(expr) selects what expr selects.
void xpointer_eval(Borrow<Doc> d, Str<128, XPathExpr> expr, Bool scheme) {
    std::string e = expr.c_str();
    std::string ptr = scheme ? "xpointer(" + e + ")" : e;
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    stitch::require(c, "no context");
    c->node = (xmlNodePtr)*d;
    xmlXPathObjectPtr r = xmlXPtrEval((const xmlChar*)ptr.c_str(), c);
    c->node = (xmlNodePtr)*d;
    if (scheme && r && r->type == XPATH_NODESET && e.find(')') == std::string::npos && e.find('^') == std::string::npos) {
        xmlXPathObjectPtr w = xmlXPathEval((const xmlChar*)e.c_str(), c);
        if (w && w->type == XPATH_NODESET) stitch::check(same_obj(r, w), "xpointer(expr) selects what expr selects");
        xmlXPathFreeObject(w);
    }
    xmlXPathFreeObject(r);
    xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpointer_eval);
