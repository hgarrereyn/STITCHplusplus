// Expressions: parsing, printing (toString, toTex), evaluation, compilation, simplification,
// rationalization and derivatives.
// Oracles: the printed expression parses back to a tree that evaluates the same; compile().evaluate
// and evaluate agree; simplify, rationalize and resolve keep the value at sample points; the
// derivative matches a finite difference.
const { stitch, math, V, Node, BAILS, mathError, recursion, describe, Borrow, New, Str, OneOf, Src } = require("./common");

const Expr = stitch.datatype("Expr");
const VARS = ["x", "y", "z"];
const FUNCS1 = ["sin", "cos", "exp", "log", "sqrt", "abs", "sign", "floor", "ceil", "round", "cube", "square", "tan", "atan", "sinh", "cosh", "factorial", "gamma", "log10", "log2", "nthRoot", "re", "im", "conj", "not", "unaryMinus", "transpose", "det", "inv", "size", "sum", "max", "min", "mean"];
const OPS = [" + ", " - ", " * ", " / ", "^", " mod ", " % ", " == ", " != ", " < ", " >= ", " and ", " or ", " xor ", " & ", " | ", " << ", " >> ", " >>> ", " to ", " in ", ".*", "./", ".^", ":"];

stitch.decoder(Expr, (s) => {
  const e = (d) => {
    switch (s.choice(d > 3 ? 3 : 11)) {
      case 0: return s.pick(["1", "2", "0", "3.5", "1e3", "0.1", "-4", "pi", "e", "i", "true", "Infinity", "NaN", "null", "2i", "1/3", "0x1F", "0b101", "1e-20"]);
      case 1: return s.pick(VARS);
      case 2: return s.token();
      case 3: return `${e(d + 1)}${s.pick(OPS)}${e(d + 1)}`;
      case 4: return `${s.pick(FUNCS1)}(${e(d + 1)})`;
      case 5: return `(${e(d + 1)})`;
      case 6: return `${s.pick(["-", "+", "~", "not "])}${e(d + 1)}`;
      case 7: return `[${e(d + 1)}, ${e(d + 1)}${s.coin() ? `; ${e(d + 1)}, ${e(d + 1)}` : ""}]`;
      case 8: return `${e(d + 1)} ? ${e(d + 1)} : ${e(d + 1)}`;
      case 9: return `${s.range(-20, 20)} ${s.pick(["cm", "m", "kg", "s", "deg", "inch", "km/h", "degC"])}`;
      default: return s.pick([`f(${e(d + 1)})`, `${e(d + 1)}!`, `${e(d + 1)}'`, `{a: ${e(d + 1)}}.a`, `[1, 2, 3][${s.range(1, 3)}]`, `"${s.str(4)}"`]);
    }
  };
  let out = e(0);
  if (s.coin(0.1)) out = `f(t) = ${out.replace(/x/g, "t")}; f(${e(1)})`;
  if (s.coin(0.1)) out = `x = ${e(1)}; ${out}`;
  return out;
});

const scope = (s) => ({ x: s ? s.range(-5, 5) / 2 : 0.7, y: s ? s.range(-5, 5) : 2, z: s ? s.range(1, 9) / 3 : 1.5 });

function same(a, b) {
  try {
    if (typeof a === "number" && typeof b === "number") return a === b || (Number.isNaN(a) && Number.isNaN(b)) || Math.abs(a - b) <= 1e-9 * Math.max(1, Math.abs(a));
    return math.deepEqual(a, b) || math.format(a, { precision: 12 }) === math.format(b, { precision: 12 });
  } catch (e) {
    return math.format(a, { precision: 12 }) === math.format(b, { precision: 12 });
  }
}

function tryEval(fn) {
  try {
    return { ok: true, v: fn() };
  } catch (e) {
    if (e instanceof RangeError && /Maximum call stack/.test(e.message)) stitch.bail("deep recursion in the expression");
    if (!mathError(e)) throw e;
    return { ok: false, err: e.message };
  }
}

stitch.endpoint("parse", { text: Str(256, Expr) }, New(Node), ({ text }) => math.parse(text), { bailsOn: BAILS });
stitch.endpoint("evaluate", { text: Str(256, Expr), s: Src(8) }, New(V).optional(), ({ text, s }) => {
  const out = math.evaluate(text, scope(s));
  return out !== null && typeof out === "object" && !out.isResultSet && typeof out !== "function" ? out : null;
}, { bailsOn: [...BAILS, (e) => e instanceof RangeError && /Maximum call stack/.test(e.message)] });

for (const opts of [{}, { parenthesis: "all" }, { parenthesis: "auto" }, { implicit: "show" }]) {
  const tag = opts.parenthesis || opts.implicit || "keep";
  stitch.endpoint(`print_roundtrip_${tag}`, { n: Borrow(Node), s: Src(8) }, null, ({ n, s }) => {
    const text = n.toString(opts);
    stitch.log(`printed: ${text}`);
    let again;
    try {
      again = math.parse(text);
    } catch (e) {
      if (!mathError(e)) throw e;
      stitch.check(false, `toString(${JSON.stringify(opts)}) output parses (${e.message})`);
    }
    const sc = scope(s);
    const a = tryEval(() => n.evaluate({ ...sc }));
    const b = tryEval(() => again.evaluate({ ...sc }));
    stitch.log(`original: ${a.ok ? describeSafe(a.v) : a.err}; reparsed: ${b.ok ? describeSafe(b.v) : b.err}`);
    stitch.check(a.ok === b.ok, "the reparsed expression evaluates (or fails) like the original");
    if (a.ok && typeof a.v !== "function") stitch.check(same(a.v, b.v), "the reparsed expression evaluates to the same value");
  }, { bailsOn: BAILS });
}
const describeSafe = (v) => (typeof v === "function" ? "function" : describe(v));

stitch.endpoint("to_tex", { n: Borrow(Node), parenthesis: OneOf("keep", "auto", "all"), implicit: OneOf("hide", "show") }, null, ({ n, parenthesis, implicit }) => {
  const t = n.toTex({ parenthesis, implicit });
  stitch.check(typeof t === "string", "toTex returns a string");
}, { bailsOn: BAILS });

stitch.endpoint("to_html", { n: Borrow(Node) }, null, ({ n }) => {
  const h = n.toHTML();
  stitch.check(!/<script/i.test(h), "toHTML escapes markup");
}, { bailsOn: BAILS });

stitch.endpoint("compile_agrees", { n: Borrow(Node), s: Src(8) }, null, ({ n, s }) => {
  const sc = scope(s);
  const a = tryEval(() => n.evaluate({ ...sc }));
  const b = tryEval(() => n.compile().evaluate({ ...sc }));
  const c = tryEval(() => math.evaluate(n.toString(), { ...sc }));
  stitch.check(a.ok === b.ok, "Node.evaluate and compile().evaluate agree");
  if (a.ok && b.ok && typeof a.v !== "function") stitch.check(same(a.v, b.v), "Node.evaluate and compile().evaluate give the same value");
  if (a.ok && c.ok && typeof a.v !== "function") stitch.check(same(a.v, c.v), "math.evaluate(toString) gives the same value");
}, { bailsOn: BAILS });

const numericAt = (n, sc) => tryEval(() => {
  const v = n.evaluate({ ...sc });
  return typeof v === "number" ? v : math.isBigNumber(v) || math.isFraction(v) ? math.number(v) : undefined;
});

function keepsValue(name, transform) {
  stitch.endpoint(name, { n: Borrow(Node), s: Src(16) }, New(Node), ({ n, s }) => {
    const sym = new Set(n.filter((x) => x.isSymbolNode).map((x) => x.name));
    stitch.require([...sym].every((v) => VARS.includes(v) || v in math), "free symbols other than x, y, z");
    stitch.require(!n.filter((x) => x.isAssignmentNode || x.isFunctionAssignmentNode || x.isBlockNode).length, "assignments");
    const out = transform(n);
    stitch.log(`${n.toString()}  ->  ${out.toString()}`);
    for (let k = 0; k < 3; k++) {
      const sc = scope(s);
      const a = numericAt(n, sc), b = numericAt(out, sc);
      if (!a.ok || !b.ok || a.v === undefined || b.v === undefined || !Number.isFinite(a.v) || !Number.isFinite(b.v)) continue;
      stitch.check(Math.abs(a.v - b.v) <= 1e-6 * Math.max(1, Math.abs(a.v)), `${name} keeps the value at x=${sc.x}, y=${sc.y}, z=${sc.z} (${a.v} vs ${b.v})`);
    }
    return out;
  }, { bailsOn: BAILS });
}
keepsValue("simplify", (n) => math.simplify(n));
keepsValue("simplify_exact", (n) => math.simplify(n, {}, { exactFractions: true }));
keepsValue("simplify_core", (n) => (math.simplify.simplifyCore ? math.simplify.simplifyCore(n) : math.simplifyCore ? math.simplifyCore(n) : n));
keepsValue("rationalize", (n) => math.rationalize(n));
keepsValue("resolve", (n) => (math.resolve ? math.resolve(n, { x: 0.7 }) : n));

stitch.endpoint("derivative", { n: Borrow(Node), s: Src(8) }, New(Node), ({ n, s }) => {
  const sym = new Set(n.filter((x) => x.isSymbolNode).map((x) => x.name));
  stitch.require([...sym].every((v) => VARS.includes(v) || v in math), "free symbols other than x, y, z");
  const d = math.derivative(n, "x");
  stitch.log(`d/dx ${n.toString()} = ${d.toString()}`);
  const sc = scope(s), h = 1e-6;
  const f0 = numericAt(n, { ...sc, x: sc.x - h }), f1 = numericAt(n, { ...sc, x: sc.x + h }), fx = numericAt(n, sc), dv = numericAt(d, sc);
  if ([f0, f1, fx, dv].every((r) => r.ok && Number.isFinite(r.v))) {
    const fd = (f1.v - f0.v) / (2 * h);
    // (finite differences are meaningless at kinks and steps: only checked where f is smooth enough)
    const smooth = Math.abs((f1.v - fx.v) / h - (fx.v - f0.v) / h) < 1e-2 * Math.max(1, Math.abs(fd));
    if (smooth) stitch.check(Math.abs(fd - dv.v) <= 1e-3 * Math.max(1, Math.abs(fd)), `derivative matches a finite difference at x=${sc.x} (${dv.v} vs ${fd})`);
  }
  return d;
}, { bailsOn: BAILS });

// Node methods
const NODE_METHODS = ["clone", "cloneDeep", "toJSON", "getIdentifier", "getContent", "toString"];
for (const m of NODE_METHODS) {
  stitch.endpoint(`node_${m}`, { n: Borrow(Node) }, null, ({ n }) => {
    const out = n[m]();
    if (m === "cloneDeep") stitch.check(out.equals(n), "cloneDeep equals the original");
    if (m === "toJSON") {
      const back = JSON.parse(JSON.stringify(n), math.reviver);
      stitch.check(back.equals(n), "a node survives JSON with math.reviver");
    }
  }, { bailsOn: BAILS });
}
stitch.endpoint("node_transform_constants", { n: Borrow(Node) }, New(Node), ({ n }) =>
  n.transform((x) => (x.isConstantNode && typeof x.value === "number" ? new math.ConstantNode(x.value * 2) : x)), { bailsOn: BAILS });
stitch.endpoint("node_traverse", { n: Borrow(Node) }, null, ({ n }) => {
  let count = 0;
  n.traverse(() => count++);
  stitch.check(count === n.filter(() => true).length, "traverse and filter visit the same nodes");
}, { bailsOn: BAILS });
stitch.endpoint("node_substitute", { n: Borrow(Node), m: Borrow(Node) }, New(Node), ({ n, m }) =>
  n.transform((x) => (x.isSymbolNode && x.name === "x" ? m.clone() : x)), { bailsOn: BAILS });

// a Parser keeps variables and functions between lines
stitch.endpoint("parser_session", { a: Str(128, Expr), b: Str(128, Expr), s: Src(8) }, null, ({ a, b, s }) => {
  const p = math.parser();
  p.set("x", scope(s).x);
  p.evaluate(a);
  const v1 = tryEval(() => p.evaluate(b));
  const vars = p.getAll();
  p.clear();
  stitch.check(Object.keys(p.getAll()).length === 0, "clear removes every variable");
  void v1;
  void vars;
}, { bailsOn: BAILS });
