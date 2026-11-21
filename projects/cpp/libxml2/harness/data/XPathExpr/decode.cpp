#include "../../objects.hpp"
static void expr(stitch::Source& s, std::string& o, int depth);
static void step(stitch::Source& s, std::string& o, int depth) {
    if (s.coin(0.3)) o += s.pick({"child::", "descendant::", "ancestor::", "following-sibling::", "preceding::", "attribute::", "self::",
                                  "namespace::", "ancestor-or-self::", "following::", "parent::", "descendant-or-self::"});
    o += s.pick({"*", "a", "b", "node()", "text()", "comment()", "processing-instruction()", "@*", "@id", "a:x", "..", "."});
    while (depth < 4 && s.coin(0.2)) {
        o += "[";
        expr(s, o, depth + 1);
        o += "]";
    }
}
static void expr(stitch::Source& s, std::string& o, int depth) {
    switch (s.choice(depth > 4 ? 3 : 7)) {
        case 0: o += std::to_string(s.range(-3, 10)); break;
        case 1: o += "'" + s.str(6) + "'"; break;
        case 2: o += s.pick({"1.5", "-0", "NaN", "1e3", "last()", "position()", "true()", "$v", "0 div 0", "999999999999999999"}); break;
        case 3:
            if (s.coin()) o += s.pick({"/", "//"});
            step(s, o, depth);
            while (s.coin(0.4)) { o += s.pick({"/", "//"}); step(s, o, depth); }
            break;
        case 4:
            expr(s, o, depth + 1);
            o += s.pick({" | ", " or ", " and ", " = ", " != ", " < ", " >= ", " + ", " - ", " * ", " div ", " mod "});
            expr(s, o, depth + 1);
            break;
        case 5: {
            o += s.pick({"count", "string", "concat", "substring", "substring-before", "translate", "normalize-space", "contains", "starts-with",
                         "number", "sum", "floor", "round", "name", "local-name", "namespace-uri", "id", "lang", "string-length", "boolean",
                         "not", "escape-uri", "boolean", "ceiling"});
            o += "(";
            int n = (int)s.range(0, 3);
            for (int i = 0; i < n; i++) { if (i) o += ","; expr(s, o, depth + 1); }
            o += ")";
            break;
        }
        default: o += "("; expr(s, o, depth + 1); o += ")"; break;
    }
}
static std::string xpath_expr(stitch::Source& s) {
    std::string o;
    expr(s, o, 0);
    return o;
}
STITCH_DECODER(XPathExpr, xpath_expr);
