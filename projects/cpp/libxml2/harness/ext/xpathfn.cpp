#include "../objects.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>
#include <libxml/xmlwriter.h>
#include <libxml/xmlschemas.h>
#include <libxml/xmlschemastypes.h>
#include <libxml/relaxng.h>
#include <libxml/schematron.h>
#include <libxml/pattern.h>
#include <libxml/debugXML.h>
#include <libxml/list.h>
#include <libxml/hash.h>
#include <libxml/HTMLparser.h>
#include <libxml/entities.h>
#include <strings.h>


static std::string xp_eval_string(const std::string& e) {
    xmlDocPtr d = xmlReadMemory("<r/>", 4, "x.xml", nullptr, lx::kQuiet);
    xmlXPathContextPtr c = xmlXPathNewContext(d);
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)e.c_str(), c);
    std::string out = "<error>";
    if (o) { xmlChar* s = xmlXPathCastToString(o); out = (const char*)s; xmlFree(s); }
    xmlXPathFreeObject(o); xmlXPathFreeContext(c); xmlFreeDoc(d);
    return out;
}
static double xp_eval_number(const std::string& e) {
    xmlDocPtr d = xmlReadMemory("<r/>", 4, "x.xml", nullptr, lx::kQuiet);
    xmlXPathContextPtr c = xmlXPathNewContext(d);
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)e.c_str(), c);
    double v = o ? xmlXPathCastToNumber(o) : -12345.678;
    xmlXPathFreeObject(o); xmlXPathFreeContext(c); xmlFreeDoc(d);
    return v;
}
static std::string lit(const std::string& s) { return s.find('\'') == std::string::npos ? "'" + s + "'" : "\"" + s + "\""; }
static bool clean_lit(const std::string& s) { return !(s.find('\'') != std::string::npos && s.find('"') != std::string::npos) && lx::xml_chars_strict(s); }
static std::vector<std::string> u8chars(const std::string& s) {
    std::vector<std::string> v; size_t i = 0;
    while (i < s.size()) { int l = 0; lx::utf8_next((const unsigned char*)s.data() + i, s.size() - i, &l); if (l <= 0) l = 1; v.push_back(s.substr(i, l)); i += l; }
    return v;
}
static double xp_round(double x) { if (std::isnan(x) || std::isinf(x)) return x; if (x < 0 && x >= -0.5) return -0.0; return std::floor(x + 0.5); }

void xpath_eval_cmp(Str<128, XPathExpr> expr) {
    // evaluating the same expression twice, compiled and not, gives the same result
    xmlDocPtr d = xmlReadMemory("<r a='1'><x>2</x><x>3</x><y z='q'>s</y></r>", 44, "x.xml", nullptr, lx::kQuiet);
    xmlXPathContextPtr c = xmlXPathNewContext(d);
    xmlXPathObjectPtr a = xmlXPathEvalExpression((const xmlChar*)expr.c_str(), c);
    xmlXPathCompExprPtr comp = xmlXPathCompile((const xmlChar*)expr.c_str());
    xmlXPathObjectPtr b = comp ? xmlXPathCompiledEval(comp, c) : nullptr;
    stitch::check((a == nullptr) == (b == nullptr), "compiled and direct evaluation agree on errors");
    if (a && b) {
        stitch::check(a->type == b->type, "same result type");
        if (a->type == XPATH_NUMBER) stitch::check((std::isnan(a->floatval) && std::isnan(b->floatval)) || a->floatval == b->floatval, "same number");
        if (a->type == XPATH_STRING) stitch::check(xmlStrEqual(a->stringval, b->stringval), "same string");
        if (a->type == XPATH_BOOLEAN) stitch::check(a->boolval == b->boolval, "same boolean");
        if (a->type == XPATH_NODESET) stitch::check(xmlXPathNodeSetGetLength(a->nodesetval) == xmlXPathNodeSetGetLength(b->nodesetval), "same node count");
        xmlChar* s1 = xmlXPathCastToString(a); xmlFree(s1);
        xmlXPathCastToNumber(a); xmlXPathCastToBoolean(a);
    }
    xmlXPathFreeObject(a); xmlXPathFreeObject(b); xmlXPathFreeCompExpr(comp); xmlXPathFreeContext(c); xmlFreeDoc(d);
}
STITCH_ENDPOINT(xpath_eval_cmp);

void xpath_fn_substring(Str<24> s, Range<-5, 30> a, Range<-5, 30> n) {
    std::string t = s.c_str();
    stitch::require(clean_lit(t), "not a literal");
    std::string got = xp_eval_string("substring(" + lit(t) + ", " + std::to_string(a.v) + ", " + std::to_string(n.v) + ")");
    auto cs = u8chars(t); std::string want;
    for (size_t i = 0; i < cs.size(); i++) { double p = (double)(i + 1); if (p >= xp_round((double)a.v) && p < xp_round((double)a.v) + xp_round((double)n.v)) want += cs[i]; }
    stitch::check(got == want, "substring(s, a, n) per XPath 1.0 (" + got + " vs " + want + ")");
}
STITCH_ENDPOINT(xpath_fn_substring);

void xpath_fn_substring_frac(Str<24> s, Range<-50, 300> a10, Range<-50, 300> n10) {
    std::string t = s.c_str();
    stitch::require(clean_lit(t), "not a literal");
    double a = a10.v / 10.0, n = n10.v / 10.0;
    char ea[32], en[32]; snprintf(ea, sizeof ea, "%g", a); snprintf(en, sizeof en, "%g", n);
    std::string got = xp_eval_string("substring(" + lit(t) + ", " + ea + ", " + en + ")");
    auto cs = u8chars(t); std::string want;
    for (size_t i = 0; i < cs.size(); i++) { double p = (double)(i + 1); if (p >= xp_round(a) && p < xp_round(a) + xp_round(n)) want += cs[i]; }
    stitch::check(got == want, "substring with fractional arguments rounds them (" + got + " vs " + want + ")");
}
STITCH_ENDPOINT(xpath_fn_substring_frac);

void xpath_fn_translate(Str<24> s, Str<8> from, Str<8> to) {
    std::string t = s.c_str(), f = from.c_str(), o = to.c_str();
    stitch::require(clean_lit(t) && clean_lit(f) && clean_lit(o), "not literals");
    std::string got = xp_eval_string("translate(" + lit(t) + ", " + lit(f) + ", " + lit(o) + ")");
    auto cs = u8chars(t), fs = u8chars(f), os = u8chars(o); std::string want;
    for (auto& c : cs) { auto it = std::find(fs.begin(), fs.end(), c); if (it == fs.end()) want += c; else { size_t k = it - fs.begin(); if (k < os.size()) want += os[k]; } }
    stitch::check(got == want, "translate per XPath 1.0 (" + got + " vs " + want + ")");
}
STITCH_ENDPOINT(xpath_fn_translate);

void xpath_fn_normalize_space(Str<32> s) {
    std::string t = s.c_str();
    stitch::require(clean_lit(t), "not a literal");
    std::string got = xp_eval_string("normalize-space(" + lit(t) + ")");
    std::string want; bool sp = false;
    for (char c : t) { if (c == ' ' || c == '\t' || c == '\n' || c == '\r') sp = !want.empty(); else { if (sp) want += ' '; sp = false; want += c; } }
    stitch::check(got == want, "normalize-space (" + got + " vs " + want + ")");
}
STITCH_ENDPOINT(xpath_fn_normalize_space);

void xpath_fn_string_length(Str<32> s) {
    std::string t = s.c_str();
    stitch::require(clean_lit(t), "not a literal");
    double got = xp_eval_number("string-length(" + lit(t) + ")");
    stitch::check(got == (double)u8chars(t).size(), "string-length counts characters");
}
STITCH_ENDPOINT(xpath_fn_string_length);

void xpath_fn_concat_contains(Str<16> a, Str<16> b) {
    std::string x = a.c_str(), y = b.c_str();
    stitch::require(clean_lit(x) && clean_lit(y), "not literals");
    stitch::check(xp_eval_string("concat(" + lit(x) + ", " + lit(y) + ", " + lit(x) + ")") == x + y + x, "concat");
    stitch::check(xp_eval_string("contains(" + lit(x) + ", " + lit(y) + ")") == (x.find(y) != std::string::npos ? "true" : "false"), "contains");
    stitch::check(xp_eval_string("starts-with(" + lit(x) + ", " + lit(y) + ")") == (x.rfind(y, 0) == 0 ? "true" : "false"), "starts-with");
    size_t p = y.empty() ? 0 : x.find(y);
    std::string before = p == std::string::npos ? "" : x.substr(0, p), after = p == std::string::npos ? "" : x.substr(p + y.size());
    stitch::check(xp_eval_string("substring-before(" + lit(x) + ", " + lit(y) + ")") == before, "substring-before");
    stitch::check(xp_eval_string("substring-after(" + lit(x) + ", " + lit(y) + ")") == after, "substring-after");
}
STITCH_ENDPOINT(xpath_fn_concat_contains);

void xpath_fn_floor(Range<-100000, 100000> n, Range<1, 1000> d) {
    double v = (double)n.v / (double)d.v;
    char e[64]; snprintf(e, sizeof e, "floor(%.17g)", v);
    double got = xp_eval_number(e), want = std::floor(v);
    stitch::check(got == want && std::signbit(got) == std::signbit(want), std::string("floor(") + e + ")");
}
STITCH_ENDPOINT(xpath_fn_floor);

void xpath_fn_ceiling(Range<-100000, 100000> n, Range<1, 1000> d) {
    double v = (double)n.v / (double)d.v;
    char e[64]; snprintf(e, sizeof e, "ceiling(%.17g)", v);
    double got = xp_eval_number(e), want = std::ceil(v);
    stitch::check(got == want && std::signbit(got) == std::signbit(want), std::string("ceiling(") + e + ")");
}
STITCH_ENDPOINT(xpath_fn_ceiling);

void xpath_fn_round(Range<-100000, 100000> n, Range<1, 1000> d) {
    double v = (double)n.v / (double)d.v;
    char e[64]; snprintf(e, sizeof e, "round(%.17g)", v);
    double got = xp_eval_number(e), want = xp_round(v);
    stitch::check(got == want && std::signbit(got) == std::signbit(want), std::string("round(") + e + ")");
}
STITCH_ENDPOINT(xpath_fn_round);

void xpath_number_string_roundtrip(Range<-1000000000, 1000000000> n, Range<0, 12> e) {
    double v = (double)n.v * std::pow(10.0, (double)e.v - 6);
    xmlChar* s = xmlXPathCastNumberToString(v);
    double back = xmlXPathCastStringToNumber(s);
    stitch::check(back == v || std::fabs(back - v) <= 1e-15 * std::fabs(v), std::string("string(number) parses back: ") + (char*)s);
    std::string t = (char*)s;
    stitch::check(t.find('e') == std::string::npos && t.find('E') == std::string::npos, "XPath number strings have no exponent");
    xmlFree(s);
}
STITCH_ENDPOINT(xpath_number_string_roundtrip);

void xpath_string_to_number(Str<24> s) {
    double v = xmlXPathCastStringToNumber((const xmlChar*)s.c_str());
    double w = xmlXPathStringEvalNumber((const xmlChar*)s.c_str());
    stitch::check((std::isnan(v) && std::isnan(w)) || v == w, "xmlXPathCastStringToNumber == xmlXPathStringEvalNumber");
}
STITCH_ENDPOINT(xpath_string_to_number);

void xpath_fn_boolean_number(Str<16> s, Range<-5, 5> n) {
    std::string t = s.c_str();
    stitch::require(clean_lit(t), "not a literal");
    stitch::check(xp_eval_string("boolean(" + lit(t) + ")") == (t.empty() ? "false" : "true"), "boolean(string)");
    stitch::check(xp_eval_string("not(" + std::to_string(n.v) + ")") == (n.v == 0 ? "true" : "false"), "not(number)");
    stitch::check(xp_eval_number("number(true()) + number(false())") == 1, "number(boolean)");
    stitch::check(xp_eval_string("string(" + std::to_string(n.v) + ")") == std::to_string(n.v), "string(integer)");
}
STITCH_ENDPOINT(xpath_fn_boolean_number);

void xpath_fn_nodes(Borrow<Doc> d, Range<0, 6> which) {
    static const char* exprs[] = {"count(//*)", "sum(//@*)", "count(//text())", "local-name(/*)", "namespace-uri(/*)", "name(//*[last()])", "string(/)"};
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathObjectPtr o = xmlXPathEvalExpression((const xmlChar*)exprs[which.v], c);
    if (which.v == 0 && o) {
        
        int n = lx::count_elements(*d);
        stitch::check(o->floatval == n, "count(//*) == the number of elements");
    }
    xmlXPathFreeObject(o); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpath_fn_nodes);

void xpath_ordering(Borrow<Doc> d) {
    xmlXPathContextPtr c = xmlXPathNewContext(*d);
    xmlXPathObjectPtr a = xmlXPathEvalExpression((const xmlChar*)"//node()", c);
    if (a && a->nodesetval) {
        for (int i = 1; i < a->nodesetval->nodeNr; i++) stitch::check(xmlXPathCmpNodes(a->nodesetval->nodeTab[i - 1], a->nodesetval->nodeTab[i]) >= 0, "node sets are in document order");
        xmlXPathObjectPtr r = xmlXPathEvalExpression((const xmlChar*)"(//node())[last()]", c);
        if (r && r->nodesetval && r->nodesetval->nodeNr == 1 && a->nodesetval->nodeNr) stitch::check(r->nodesetval->nodeTab[0] == a->nodesetval->nodeTab[a->nodesetval->nodeNr - 1], "last() is the last node");
        xmlXPathFreeObject(r);
    }
    xmlXPathFreeObject(a); xmlXPathFreeContext(c);
}
STITCH_ENDPOINT(xpath_ordering);
