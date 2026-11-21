#include "../objects.hpp"

std::optional<New<Node>> xpath(Borrow<Doc> d, Str<128, XPathExpr> expr, Range<0, 7> i) {
    xmlXPathContextPtr ctx = xmlXPathNewContext(*d);
    stitch::require(ctx, "no context");
    xmlXPathRegisterNs(ctx, (const xmlChar*)"a", (const xmlChar*)"urn:a");
    xmlXPathObjectPtr r = xmlXPathEvalExpression((const xmlChar*)expr.c_str(), ctx);
    xmlNodePtr pick = nullptr;
    if (r && r->type == XPATH_NODESET && r->nodesetval) {
        int k = 0;
        for (int j = 0; j < r->nodesetval->nodeNr; j++) {
            xmlNodePtr x = r->nodesetval->nodeTab[j];
            if (x && x->type == XML_ELEMENT_NODE) {
                stitch::check(x->doc == *d, "XPath results belong to the document");
                if (k++ == (long long)i) pick = x;
            }
        }
    } else if (r) {
        xmlChar* s = xmlXPathCastToString(r);
        xmlFree(s);
    }
    xmlXPathFreeObject(r);
    xmlXPathFreeContext(ctx);
    if (!pick) return std::nullopt;
    return New<Node>(pick);
}
STITCH_ENDPOINT(xpath);

// relative to an element
void xpath_from(Borrow<Node> n, Str<128, XPathExpr> expr) {
    xmlXPathContextPtr ctx = xmlXPathNewContext((*n)->doc);
    stitch::require(ctx, "no context");
    ctx->node = *n;
    xmlXPathObjectPtr r = xmlXPathEvalExpression((const xmlChar*)expr.c_str(), ctx);
    xmlXPathFreeObject(r);
    xmlXPathFreeContext(ctx);
}
STITCH_ENDPOINT(xpath_from);

void xpath_compile(Str<128, XPathExpr> expr) {
    xmlXPathCompExprPtr c = xmlXPathCompile((const xmlChar*)expr.c_str());
    xmlXPathFreeCompExpr(c);
}
STITCH_ENDPOINT(xpath_compile);
