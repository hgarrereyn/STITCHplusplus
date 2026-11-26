// XPath object API (xpath.h / xpathInternals.h): object constructors, casts and conversions, the
// value stack of a parser context and the arithmetic / comparison operators on it.
#include "../objects.hpp"
#include <cmath>

namespace {
// An XPath context (no document) and a parser context on it, for the value stack.
struct Stack {
    xmlXPathContextPtr c;
    xmlXPathParserContextPtr p;
    explicit Stack(xmlDocPtr d = nullptr) {
        c = xmlXPathNewContext(d);
        p = c ? xmlXPathNewParserContext((const xmlChar*)"", c) : nullptr;
    }
    ~Stack() {
        if (p) xmlXPathFreeParserContext(p);
        if (c) xmlXPathFreeContext(c);
    }
};
bool same_num(double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; }
std::string s(const xmlChar* x) { return x ? std::string((const char*)x) : std::string(); }
}  // namespace

// Constructors and the cast functions agree with the XPath rules for each type.
void xpath_casts(F64 num, Str<48> text, Bool flag) {
    double x = num;
    std::string t = lx::clean(text.s);
    // boolean()
    stitch::check(xmlXPathCastNumberToBoolean(x) == (!std::isnan(x) && x != 0), "boolean(number)");
    stitch::check(xmlXPathCastStringToBoolean((const xmlChar*)t.c_str()) == !t.empty(), "boolean(string)");
    stitch::check(xmlXPathCastBooleanToNumber(flag) == (flag ? 1.0 : 0.0), "number(boolean)");
    xmlChar* bs = xmlXPathCastBooleanToString(flag);
    stitch::check(s(bs) == (flag ? "true" : "false"), "string(boolean)");
    xmlFree(bs);
    stitch::check(xmlXPathIsNaN(x) == (std::isnan(x) ? 1 : 0), "xmlXPathIsNaN");
    stitch::check(xmlXPathIsInf(x) == (std::isinf(x) ? (x > 0 ? 1 : -1) : 0), "xmlXPathIsInf");
    // string(number) -> number() gives the number back (up to the printed precision)
    xmlChar* ns = xmlXPathCastNumberToString(x);
    double back = xmlXPathCastStringToNumber(ns);
    if (std::isnan(x)) stitch::check(std::isnan(back), "number(string(NaN))");
    else if (std::isinf(x)) stitch::check(back == x || std::isnan(back), "number(string(Infinity))");
    // (15 significant digits, and number() scales by a power of ten: only check the normal range)
    else if (std::fabs(x) > 1e-290 && std::fabs(x) < 1e290)
        stitch::check(std::fabs(back - x) <= std::fabs(x) * 1e-12, "number(string(x)) ~ x");
    xmlFree(ns);
    // the object constructors and the generic casts
    xmlXPathObjectPtr o[6] = {xmlXPathNewFloat(x), xmlXPathNewString((const xmlChar*)t.c_str()),
                              xmlXPathNewCString(t.c_str()), xmlXPathWrapString(xmlStrdup((const xmlChar*)t.c_str())),
                              xmlXPathWrapCString((char*)xmlStrdup((const xmlChar*)t.c_str())), xmlXPathNewBoolean(flag)};
    for (auto* ob : o) stitch::check(ob != nullptr, "object created");
    for (int i = 1; i <= 4; i++) {
        stitch::check(o[i]->type == XPATH_STRING && s(o[i]->stringval) == t, "string object holds the string");
        stitch::check(xmlXPathCastToBoolean(o[i]) == !t.empty(), "boolean(string object)");
        stitch::check(same_num(xmlXPathCastToNumber(o[i]), xmlXPathCastStringToNumber((const xmlChar*)t.c_str())),
                      "number(string object)");
    }
    stitch::check(same_num(xmlXPathCastToNumber(o[0]), x) && xmlXPathCastToBoolean(o[5]) == (bool)flag, "number / boolean objects");
    xmlChar* a = xmlXPathCastToString(o[0]);
    xmlChar* b = xmlXPathCastNumberToString(x);
    stitch::check(s(a) == s(b), "string(number object)");
    xmlFree(a);
    xmlFree(b);
    // copies and conversions (the Convert* functions consume their argument)
    for (auto*& ob : o) {
        xmlXPathObjectPtr cp = xmlXPathObjectCopy(ob);
        stitch::check(cp && cp->type == ob->type, "copy has the type");
        xmlXPathObjectPtr cb = xmlXPathConvertBoolean(xmlXPathObjectCopy(ob));
        stitch::check(cb && cb->type == XPATH_BOOLEAN && cb->boolval == xmlXPathCastToBoolean(ob), "convert to boolean");
        xmlXPathObjectPtr cn = xmlXPathConvertNumber(xmlXPathObjectCopy(ob));
        stitch::check(cn && cn->type == XPATH_NUMBER && same_num(cn->floatval, xmlXPathCastToNumber(ob)), "convert to number");
        xmlXPathObjectPtr cs = xmlXPathConvertString(cp);
        xmlChar* direct = xmlXPathCastToString(ob);
        stitch::check(cs && cs->type == XPATH_STRING && s(cs->stringval) == s(direct), "convert to string");
        xmlFree(direct);
        xmlXPathFreeObject(cb);
        xmlXPathFreeObject(cn);
        xmlXPathFreeObject(cs);
        xmlXPathFreeObject(ob);
    }
}
STITCH_ENDPOINT(xpath_casts);

// The operators on the value stack compute what C computes on doubles.
void xpath_arith(F64 a, F64 b, Range<0, 4> op) {
    Stack st;
    stitch::require(st.p, "no context");
    xmlXPathValuePush(st.p, xmlXPathNewFloat(a));
    xmlXPathValuePush(st.p, xmlXPathNewFloat(b));
    double want = 0;
    switch ((int)op) {
        case 0: xmlXPathAddValues(st.p); want = a + b; break;
        case 1: xmlXPathSubValues(st.p); want = a - b; break;
        case 2: xmlXPathMultValues(st.p); want = a * b; break;
        case 3: xmlXPathDivValues(st.p); want = a / b; break;
        default: xmlXPathModValues(st.p); want = (double)b == 0 ? NAN : std::fmod(a, b); break;
    }
    stitch::check(st.p->error == 0 && st.p->valueNr == 1, "one result");
    xmlXPathValueFlipSign(st.p);
    double got = xmlXPathPopNumber(st.p);
    stitch::check(same_num(got, -want), "arithmetic result");
    stitch::check(st.p->valueNr == 0, "stack empty");
}
STITCH_ENDPOINT(xpath_arith);

// =, != and <, <=, >, >= on numbers, strings and booleans follow the XPath 1.0 rules.
void xpath_compare(F64 a, F64 b, Str<16> sa, Bool use_strings, Range<0, 5> op) {
    Stack st;
    stitch::require(st.p, "no context");
    std::string t = lx::clean(sa.s);
    xmlXPathObjectPtr x = use_strings ? xmlXPathNewString((const xmlChar*)t.c_str()) : xmlXPathNewFloat(a);
    double xa = use_strings ? xmlXPathCastStringToNumber((const xmlChar*)t.c_str()) : (double)a;
    xmlXPathValuePush(st.p, x);
    xmlXPathValuePush(st.p, xmlXPathNewFloat(b));
    int got = 0;
    bool want = false;
    switch ((int)op) {
        case 0: got = xmlXPathEqualValues(st.p); want = xa == (double)b; break;
        case 1: got = xmlXPathNotEqualValues(st.p); want = !(xa == (double)b); break;
        case 2: got = xmlXPathCompareValues(st.p, 1, 1); want = xa < (double)b; break;
        case 3: got = xmlXPathCompareValues(st.p, 1, 0); want = xa <= (double)b; break;
        case 4: got = xmlXPathCompareValues(st.p, 0, 1); want = xa > (double)b; break;
        default: got = xmlXPathCompareValues(st.p, 0, 0); want = xa >= (double)b; break;
    }
    stitch::check(st.p->error == 0, "no error");
    stitch::check((got != 0) == want, "comparison result");
    stitch::check(st.p->valueNr == 0, "operands consumed");
    // booleans: true() = boolean(x)
    xmlXPathValuePush(st.p, xmlXPathNewBoolean(1));
    xmlXPathValuePush(st.p, xmlXPathNewFloat(a));
    stitch::check(xmlXPathEqualValues(st.p) == (!std::isnan((double)a) && (double)a != 0), "true() = number");
}
STITCH_ENDPOINT(xpath_compare);

// Pushing values and popping them with the typed Pop* functions; wrong types set an error.
void xpath_stack(Src<64> src) {
    Stack st;
    stitch::require(st.p, "no context");
    int tag = 0;
    while (src->more() && st.p->valueNr < 32) {
        switch (src->choice(6)) {
            case 0: xmlXPathValuePush(st.p, xmlXPathNewFloat(src->f64(-1e9, 1e9))); break;
            case 1: xmlXPathValuePush(st.p, xmlXPathNewCString(src->str(8).c_str())); break;
            case 2: xmlXPathValuePush(st.p, xmlXPathNewBoolean(src->coin())); break;
            case 3: xmlXPathValuePush(st.p, xmlXPathWrapExternal(&tag)); break;
            case 4: xmlXPathValuePush(st.p, xmlXPathNewNodeSet(nullptr)); break;
            default: {
                if (st.p->valueNr == 0) break;
                int type = st.p->value->type;
                int before = st.p->valueNr;
                st.p->error = 0;
                switch (src->choice(6)) {
                    case 0: xmlXPathPopBoolean(st.p); break;
                    case 1: xmlXPathPopNumber(st.p); break;
                    case 2: xmlFree(xmlXPathPopString(st.p)); break;
                    case 3: {
                        xmlNodeSetPtr ns = xmlXPathPopNodeSet(st.p);
                        if (ns) xmlXPathFreeNodeSet(ns);
                        if (type != XPATH_NODESET) stitch::check(ns == nullptr && st.p->error != 0, "PopNodeSet of a non node-set fails");
                        break;
                    }
                    case 4: {
                        void* u = xmlXPathPopExternal(st.p);
                        if (type == XPATH_USERS) stitch::check(u == &tag, "PopExternal gives the wrapped pointer");
                        else stitch::check(u == nullptr && st.p->error != 0, "PopExternal of a non external fails");
                        break;
                    }
                    default: {
                        xmlXPathObjectPtr o = xmlXPathValuePop(st.p);
                        stitch::check(o && o->type == type, "ValuePop returns the top");
                        xmlXPathFreeObject(o);
                    }
                }
                if (st.p->error == 0) stitch::check(st.p->valueNr == before - 1, "a pop removes one value");
                else stitch::check(st.p->valueNr == before, "a failed pop leaves the stack");
            }
        }
    }
}
STITCH_ENDPOINT(xpath_stack);

// Calling the core function implementations directly on pushed arguments gives what
// evaluating the same call as an expression gives (arguments passed as variables $a, $b, $c, $n).
void xpath_core_function(Range<0, 13> fn, Str<24> a1, Str<24> a2, Str<24> a3, F64 num) {
    static const struct { const char* name; xmlXPathFunction f; int nargs; bool numeric; } fns[] = {
        {"string-length", xmlXPathStringLengthFunction, 1, false},
        {"concat", xmlXPathConcatFunction, 3, false},
        {"contains", xmlXPathContainsFunction, 2, false},
        {"starts-with", xmlXPathStartsWithFunction, 2, false},
        {"substring", xmlXPathSubstringFunction, 3, false},
        {"substring-before", xmlXPathSubstringBeforeFunction, 2, false},
        {"substring-after", xmlXPathSubstringAfterFunction, 2, false},
        {"normalize-space", xmlXPathNormalizeFunction, 1, false},
        {"translate", xmlXPathTranslateFunction, 3, false},
        {"not", xmlXPathNotFunction, 1, false},
        {"number", xmlXPathNumberFunction, 1, false},
        {"floor", xmlXPathFloorFunction, 1, true},
        {"ceiling", xmlXPathCeilingFunction, 1, true},
        {"round", xmlXPathRoundFunction, 1, true},
    };
    const auto& F = fns[(int)fn];
    std::string args[3] = {lx::clean(a1.s), lx::clean(a2.s), lx::clean(a3.s)};
    if (F.name == std::string("substring")) args[1] = std::to_string((long long)std::fmod((double)num, 50.0));
    static const char* vars[3] = {"a", "b", "c"};
    Stack st;
    stitch::require(st.p, "no context");
    std::string expr = std::string(F.name) + "(";
    for (int i = 0; i < F.nargs; i++) {
        expr += std::string(i ? "," : "") + "$" + (F.numeric ? "n" : vars[i]);
        xmlXPathValuePush(st.p, F.numeric ? xmlXPathNewFloat(num) : xmlXPathNewString((const xmlChar*)args[i].c_str()));
        if (!F.numeric) xmlXPathRegisterVariable(st.c, (const xmlChar*)vars[i], xmlXPathNewString((const xmlChar*)args[i].c_str()));
    }
    expr += ")";
    xmlXPathRegisterVariable(st.c, (const xmlChar*)"n", xmlXPathNewFloat(num));
    F.f(st.p, F.nargs);
    stitch::check(st.p->error == 0 && st.p->valueNr == 1, "one result");
    xmlXPathObjectPtr got = xmlXPathValuePop(st.p);
    xmlXPathObjectPtr want = xmlXPathEval((const xmlChar*)expr.c_str(), st.c);
    stitch::log("%s", expr.c_str());
    stitch::check(want != nullptr, "expression evaluates");
    stitch::check(got->type == want->type, "same result type");
    if (got->type == XPATH_NUMBER) stitch::check(same_num(got->floatval, want->floatval), "same number");
    if (got->type == XPATH_STRING) stitch::check(s(got->stringval) == s(want->stringval), "same string");
    if (got->type == XPATH_BOOLEAN) stitch::check(got->boolval == want->boolval, "same boolean");
    xmlXPathFreeObject(got);
    xmlXPathFreeObject(want);
    xmlXPathRegisteredVariablesCleanup(st.c);
}
STITCH_ENDPOINT(xpath_core_function);

// xmlXPathEvalPredicate / xmlXPathEvaluatePredicateResult: numbers compare with the position,
// everything else is converted to a boolean.
void xpath_predicate(F64 num, Str<16> text, Range<1, 9> pos) {
    Stack st;
    stitch::require(st.p, "no context");
    st.c->proximityPosition = (int)pos;
    xmlXPathObjectPtr n = xmlXPathNewFloat(num);
    xmlXPathObjectPtr t = xmlXPathNewCString(text.c_str());
    stitch::check(xmlXPathEvalPredicate(st.c, n) == ((double)num == (double)(int)pos), "number predicate is position()");
    stitch::check(xmlXPathEvaluatePredicateResult(st.p, n) == xmlXPathEvalPredicate(st.c, n), "both predicate functions agree");
    stitch::check(xmlXPathEvalPredicate(st.c, t) == xmlXPathCastToBoolean(t), "string predicate is boolean()");
    stitch::check(xmlXPathEvaluatePredicateResult(st.p, t) == xmlXPathCastToBoolean(t), "string predicate result");
    xmlXPathFreeObject(n);
    xmlXPathFreeObject(t);
}
STITCH_ENDPOINT(xpath_predicate);
