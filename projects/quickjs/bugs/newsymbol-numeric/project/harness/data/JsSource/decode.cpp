// JavaScript programs: statements over a few variables, exercising builtins (strings, arrays,
// typed arrays, Map/Set, Proxy/Reflect, RegExp, JSON, BigInt, Promise/async, generators, classes).
#include "../../objects.hpp"

static const char* VARS[] = {"a", "b", "c", "o", "arr", "s", "m", "f", "p", "t"};

static std::string var(stitch::Source& s) { return VARS[s.choice(10)]; }
static int g_decl = 0;  // unique let/const names within one program

// quoted JS string literal with escaped content
static std::string q(const std::string& raw, char quote = '"') {
    std::string o(1, quote);
    for (unsigned char c : raw) {
        if (c == (unsigned char)quote || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c < 0x20 || c == 0x7f) { char b[8]; snprintf(b, sizeof b, "\\x%02x", c); o += b; }
        else if (quote == '`' && c == '$') o += "\\$";
        else o += (char)c;
    }
    return o + quote;
}

static std::string expr(stitch::Source& s, int d);

static std::string args(stitch::Source& s, int d) {
    std::string o;
    for (int i = 0; i < 3 && s.more(); i++) o += (i ? ", " : "") + expr(s, d + 1);
    return o;
}

static std::string literal(stitch::Source& s) {
    switch (s.choice(10)) {
        case 0: return std::to_string(s.range(-10, 1000));
        case 1: return std::string(s.pick({"0", "-0", "NaN", "Infinity", "-Infinity", "2**53", "1e21", "0.1", "-1", "4294967295"}));
        case 2: return q(s.str(12));
        case 3: return q(s.token(), '\'');
        case 4: return std::to_string(s.range(0, 1 << 20)) + "n";
        case 5: return std::string(s.pick({"null", "undefined", "true", "false", "this", "[]", "{}", "globalThis"}));
        case 6: return std::string(s.pick({"/a+b*/g", "/(\\d+)-(?<y>\\w)/u", "/[^]*?$/m", "/(a|b)*c/y", "/\\p{L}+/v", "/(?=x)(?!y)./s"}));
        case 7: { std::string t = q(s.str(8), '`'); t.pop_back(); return t + "${" + std::to_string(s.range(0, 9)) + "}`"; }
        case 8: return "Symbol(" + std::string(s.pick({"", "'x'", "Symbol.iterator"})) + ")";
        default: return std::string(s.pick({"new Uint8Array(8)", "new Float64Array([1.5, NaN])", "new ArrayBuffer(16)",
                                             "new DataView(new ArrayBuffer(8))", "new Map([[1, 2]])", "new Set('abc')",
                                             "new WeakMap()", "new Proxy({}, {})", "new Date(0)", "new Error('e')",
                                             "Array(5)", "new Int32Array(new SharedArrayBuffer(8))"}));
    }
}

static std::string method(stitch::Source& s) {
    return std::string(s.pick({"push", "pop", "shift", "unshift", "slice", "splice", "concat", "join", "reverse", "sort",
                               "indexOf", "lastIndexOf", "includes", "map", "filter", "reduce", "reduceRight", "find",
                               "findLast", "some", "every", "flat", "flatMap", "fill", "copyWithin", "keys", "values",
                               "entries", "at", "toString", "toLocaleString", "valueOf", "toSorted", "toSpliced", "with",
                               "charAt", "charCodeAt", "codePointAt", "normalize", "padStart", "padEnd", "repeat",
                               "replace", "replaceAll", "split", "substring", "toUpperCase", "trim", "match", "matchAll",
                               "search", "localeCompare", "isWellFormed", "toFixed", "toPrecision", "toExponential",
                               "set", "get", "has", "delete", "add", "clear", "forEach", "then", "catch", "finally",
                               "exec", "test", "subarray", "getInt32", "setFloat64", "next", "return", "throw",
                               "bind", "call", "apply", "resize", "transfer"}));
}

static std::string expr(stitch::Source& s, int d) {
    if (d > 4) return s.coin() ? var(s) : literal(s);
    switch (s.choice(13)) {
        case 0: return literal(s);
        case 1: return var(s);
        case 2: return var(s) + "." + method(s) + "(" + args(s, d) + ")";
        case 3: return "(" + expr(s, d + 1) + " " + s.pick({"+", "-", "*", "/", "%", "**", "&", "|", "^", "<<", ">>", ">>>", "==", "===", "<", "&&", "||", "??", "in", "instanceof"}) + " " + expr(s, d + 1) + ")";
        case 4: return std::string(s.pick({"JSON.stringify", "JSON.parse", "Math.max", "Math.round", "Object.keys",
                                           "Object.assign", "Object.freeze", "Object.getOwnPropertyDescriptors",
                                           "Object.entries", "Array.from", "Array.of", "String.fromCharCode",
                                           "Number.parseFloat", "parseInt", "BigInt", "BigInt.asUintN", "Reflect.ownKeys",
                                           "Reflect.apply", "Reflect.construct", "Promise.resolve", "Promise.all",
                                           "structuredClone", "Atomics.add", "escape", "unescape", "encodeURIComponent",
                                           "decodeURI", "Symbol.for", "Object.groupBy", "Array.fromAsync", "Iterator.from"})) + "(" + args(s, d) + ")";
        case 5: return "[" + args(s, d) + (s.coin(0.2) ? ", ..." + var(s) : "") + "]";
        case 6: return "({" + q(s.token()) + ": " + expr(s, d + 1) + ", [" + expr(s, d + 1) + "]: 1, get g() { return " + expr(s, d + 1) + " }})";
        case 7: return var(s) + "[" + expr(s, d + 1) + "]";
        case 8: return "((x, y) => " + expr(s, d + 1) + ")";
        case 9: return std::string(s.pick({"typeof ", "void ", "!", "-", "~", "delete ", "+"})) + var(s);
        case 10: return var(s) + " = " + expr(s, d + 1);
        case 11: return "new " + std::string(s.pick({"Uint8Array", "Map", "Set", "RegExp", "Proxy", "Promise", "WeakRef", "FinalizationRegistry", "Error", "ArrayBuffer", "Function"})) + "(" + args(s, d) + ")";
        default: return var(s) + "?.(" + args(s, d) + ")";
    }
}

static std::string stmt(stitch::Source& s, int d) {
    if (d > 3) return expr(s, d) + ";";
    switch (s.choice(10)) {
        case 0: {
            std::string kw = s.pick({"var ", "let ", "const "});
            std::string name = kw == "var " ? "_" + std::to_string(s.choice(5)) : "_d" + std::to_string(g_decl++);
            return kw + name + " = " + expr(s, d) + ";";
        }
        case 1: return var(s) + " = " + expr(s, d) + ";";
        case 2: return "if (" + expr(s, d) + ") { " + stmt(s, d + 1) + " } else { " + stmt(s, d + 1) + " }";
        case 3: return "for (let i = 0; i < " + std::to_string(s.range(0, 50)) + "; i++) { " + stmt(s, d + 1) + " }";
        case 4: return "try { " + stmt(s, d + 1) + " } catch (e) { " + var(s) + " = e; } finally { " + stmt(s, d + 1) + " }";
        case 5: return "function " + std::string(s.pick({"f", "g"})) + "(x, y = " + literal(s) + ", ...z) { " + stmt(s, d + 1) + " return " + expr(s, d) + "; }";
        case 6: {
            bool ext = s.coin();
            return "class C" + std::to_string(g_decl++) + (ext ? " extends " + std::string(s.pick({"Array", "Map", "Error", "Object", "Uint8Array", "Promise"})) : "") +
                   " { #p = 1; static s = " + literal(s) + "; constructor(...a) { " + (ext ? "super(...a); " : "") +
                   "} m() { return this.#p; } get x() { return " + expr(s, d + 1) + "; } }";
        }
        case 7: return "for (const k of " + expr(s, d) + ") { " + stmt(s, d + 1) + " }";
        case 8: {
            bool async = s.coin(), gen = s.coin();
            std::string pre = async && s.coin() ? "await " : gen && s.coin() ? "yield " : "";
            return std::string(async ? "async " : "") + "function" + (gen ? "*" : "") + " h" + std::to_string(g_decl++) + "() { " + pre + expr(s, d) + "; }";
        }
        default: return expr(s, d) + ";";
    }
}

static std::string js(stitch::Source& s) {
    g_decl = 0;
    std::string o = "var a, b, c, o = {}, arr = [1, 2, 3], s = 'abc', m = new Map(), f = function(){}, p, t;\n";
    do {
        o += stmt(s, 0) + "\n";
    } while (s.more());
    if (s.coin(0.5)) o += expr(s, 0) + ";\n";  // completion value
    return o;
}
STITCH_DECODER(JsSource, js);
