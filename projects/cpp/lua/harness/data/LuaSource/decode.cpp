// Lua programs over a few globals, exercising the standard library, metatables, closures,
// varargs, integer/float corners, coroutines and to-be-closed variables.
#include "../../objects.hpp"

static const char* VARS[] = {"a", "b", "c", "t", "s", "f", "co", "mt"};
static std::string var(stitch::Source& s) { return VARS[s.choice(8)]; }

static std::string q(const std::string& raw) {
    std::string o = "\"";
    for (unsigned char ch : raw) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch < 32 || ch >= 127) { char b[8]; snprintf(b, sizeof b, "\\%03u", ch); o += b; }
        else o += (char)ch;
    }
    return o + "\"";
}

static std::string expr(stitch::Source& s, int d);

static std::string lit(stitch::Source& s) {
    switch (s.choice(7)) {
        case 0: return std::to_string(s.range(-100, 1000));
        case 1: return std::string(s.pick({"math.maxinteger", "math.mininteger", "0/0", "1/0", "-0.0", "2^53", "0x7fffffffffffffff", "1e308", "3.14", "0x10p4"}));
        case 2: return q(s.str(16));
        case 3: return q(s.token());
        case 4: return std::string(s.pick({"nil", "true", "false", "{}", "{1, 2, 3}", "{x = 1, [2] = 'y'}", "..."}));
        case 5: return "[[" + s.str(8) + "]]";
        default: return "function(x, ...) return " + std::string(s.pick({"x", "...", "#{...}", "select('#', ...)"})) + " end";
    }
}

static std::string call(stitch::Source& s, int d) {
    std::string fn = s.pick({"string.format", "string.rep", "string.sub", "string.byte", "string.char", "string.find",
                             "string.match", "string.gmatch", "string.gsub", "string.pack", "string.unpack", "string.packsize",
                             "string.reverse", "string.upper", "table.insert", "table.remove", "table.sort", "table.concat",
                             "table.move", "table.unpack", "table.pack", "math.floor", "math.fmod", "math.tointeger",
                             "math.abs", "math.ult", "utf8.char", "utf8.len", "utf8.codepoint", "utf8.offset", "tostring",
                             "tonumber", "select", "rawequal", "rawlen", "rawget", "rawset", "setmetatable", "getmetatable",
                             "pcall", "error", "next", "ipairs", "pairs", "type", "assert", "load", "coroutine.wrap",
                             "coroutine.yield", "coroutine.status", "coroutine.isyieldable", "coroutine.close", "collectgarbage", "os.time", "os.date"});
    std::string args;
    int n = (int)s.range(0, 3);
    for (int i = 0; i < n; i++) args += (i ? ", " : "") + expr(s, d + 1);
    return fn + "(" + args + ")";
}

static std::string expr(stitch::Source& s, int d) {
    if (d > 3) return s.coin() ? var(s) : lit(s);
    switch (s.choice(8)) {
        case 0: return lit(s);
        case 1: return var(s);
        case 2: return call(s, d);
        case 3: return "(" + expr(s, d + 1) + " " + s.pick({"+", "-", "*", "/", "//", "%", "^", "..", "==", "<", "<=", "and", "or", "&", "|", "~", "<<", ">>"}) + " " + expr(s, d + 1) + ")";
        case 4: return std::string(s.pick({"-", "not ", "#", "~"})) + var(s);
        case 5: return var(s) + "[" + expr(s, d + 1) + "]";
        case 6: return var(s) + ":" + std::string(s.pick({"len", "rep", "sub", "byte", "format", "upper"})) + "(" + expr(s, d + 1) + ")";
        default: return "{" + expr(s, d + 1) + ", k = " + expr(s, d + 1) + ", [" + expr(s, d + 1) + "] = 1}";
    }
}

static std::string stmt(stitch::Source& s, int d) {
    if (d > 3) return var(s) + " = " + expr(s, d);
    switch (s.choice(10)) {
        case 0: return var(s) + " = " + expr(s, d);
        case 1: return "local l" + std::to_string(s.choice(4)) + std::string(s.pick({"", " <const>", " <close>"})) + " = " + expr(s, d);
        case 2: return "if " + expr(s, d) + " then " + stmt(s, d + 1) + " else " + stmt(s, d + 1) + " end";
        case 3: return "for i = " + std::to_string(s.range(-3, 3)) + ", " + std::to_string(s.range(0, 40)) + ", " + std::string(s.pick({"1", "2", "-1", "0.5"})) + " do " + stmt(s, d + 1) + " end";
        case 4: return "for k, v in pairs(" + var(s) + " or {}) do " + stmt(s, d + 1) + " end";
        case 5: return "pcall(function(...) " + stmt(s, d + 1) + " end, " + expr(s, d) + ")";
        case 6: return "mt = {__index = " + expr(s, d) + ", __add = function(x, y) return " + expr(s, d + 1) + " end, __gc = function() " + var(s) + " = nil end, __close = function() end, __len = function() return " + lit(s) + " end, __eq = function() return true end, __call = f}; setmetatable(t, mt)";
        case 7: return "co = coroutine.create(function(x) " + stmt(s, d + 1) + "; coroutine.yield(" + expr(s, d + 1) + ") end); coroutine.resume(co, " + expr(s, d) + ")";
        case 8: return "f = function(x, ...) local up = " + expr(s, d + 1) + "; return function() up = up and " + expr(s, d + 1) + "; return up end end";
        default: return call(s, d);
    }
}

static std::string lua_source(stitch::Source& s) {
    std::string o = "a, b, c, t, s, f, co, mt = 1, 2.5, 'x', {1, 2, 3}, 'hello', print, nil, {}\n";
    do {
        o += "pcall(function() " + stmt(s, 0) + " end)\n";
    } while (s.more());
    if (s.coin(0.5)) o += "return " + expr(s, 0) + "\n";
    return o;
}
STITCH_DECODER(LuaSource, lua_source);
