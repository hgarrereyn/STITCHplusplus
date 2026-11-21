// Constructors of values and conversions between number types.
const { stitch, math, V, BAILS, make, num, small, describe, UNITS, Borrow, New, Str, OneOf, Src, Range } = require("./common");

stitch.endpoint("value", { s: Src(64) }, New(V), ({ s }) => make(s), { bailsOn: BAILS });
stitch.endpoint("number_value", { s: Src(16) }, New(V), ({ s }) => num(s));
stitch.endpoint("bignumber_from_string", { text: Str(48) }, New(V), ({ text }) => math.bignumber(text), { bailsOn: BAILS });
stitch.endpoint("fraction_from_string", { text: Str(32) }, New(V), ({ text }) => math.fraction(text), { bailsOn: BAILS });
stitch.endpoint("complex_from_string", { text: Str(32) }, New(V), ({ text }) => math.complex(text), { bailsOn: BAILS });
stitch.endpoint("unit_from_string", { text: Str(32) }, New(V), ({ text }) => math.unit(text), { bailsOn: BAILS });
stitch.endpoint("unit_value", { v: Range(-1000, 1000), u: OneOf(...UNITS) }, New(V), ({ v, u }) => math.unit(v, u), { bailsOn: BAILS });
stitch.endpoint("matrix_value", { s: Src(96), sparse: stitch.Bool }, New(V), ({ s, sparse }) => {
  const n = s.range(1, 4), m = s.range(1, 4);
  const d = Array.from({ length: n }, () => Array.from({ length: m }, () => s.range(-9, 9)));
  return sparse ? math.sparse(d) : math.matrix(d);
});
stitch.endpoint("to_bignumber", { x: Borrow(V) }, New(V), ({ x }) => math.bignumber(x), { bailsOn: BAILS });
stitch.endpoint("to_fraction", { x: Borrow(V) }, New(V), ({ x }) => math.fraction(x), { bailsOn: BAILS });
stitch.endpoint("to_number", { x: Borrow(V) }, New(V), ({ x }) => math.number(x), { bailsOn: BAILS });
stitch.endpoint("to_string", { x: Borrow(V) }, New(V), ({ x }) => math.string(x), { bailsOn: BAILS });
stitch.endpoint("to_complex", { x: Borrow(V) }, New(V), ({ x }) => math.complex(x), { bailsOn: BAILS });
stitch.endpoint("to_matrix", { x: Borrow(V) }, New(V), ({ x }) => math.matrix(x), { bailsOn: BAILS });
stitch.endpoint("to_sparse", { x: Borrow(V) }, New(V), ({ x }) => math.sparse(x), { bailsOn: BAILS });
stitch.endpoint("to_array", { x: Borrow(V) }, New(V), ({ x }) => (math.isMatrix(x) ? x.toArray() : stitch.bail("not a matrix")));

// format(x) parses back to x (numbers): the printer and the number parser agree
stitch.endpoint("format_roundtrip", { x: Borrow(V), notation: OneOf("auto", "fixed", "exponential", "engineering", "hex", "bin", "oct"), precision: Range(1, 20) }, null,
  ({ x, notation, precision }) => {
    stitch.require(math.isNumber(x) || math.isBigNumber(x), "not a number");
    const s = math.format(x, { notation, precision: notation === "hex" || notation === "bin" || notation === "oct" ? undefined : precision });
    stitch.log(`${describe(x)} -> ${s}`);
    const back = math.isBigNumber(x) ? math.bignumber(s) : math.number(s);
    if (notation === "hex" || notation === "bin" || notation === "oct") {
      stitch.require(math.isInteger(x), "radix notations are for integers");
      stitch.check(math.equal(back, x), `${notation}: format(x) parses back to x`);
    } else if (!math.isNaN(x)) {
      const rel = math.abs(math.subtract(back, x));
      const tol = math.multiply(math.max(math.abs(x), math.isBigNumber(x) ? math.bignumber(1e-300) : 1e-300), 10 ** (1 - precision));
      stitch.check(!(math.isBigNumber(x) ? x.isFinite() : Number.isFinite(x)) ? math.equal(back, x) : math.smallerEq(rel, math.multiply(tol, 1.0000001)) || notation === "fixed",
        `${notation}: format(x) with ${precision} digits parses back within precision`);
    }
  }, { bailsOn: BAILS });

// number, BigNumber and Fraction agree on exact integer arithmetic
const EXACT = ["add", "subtract", "multiply", "mod", "gcd", "lcm", "abs", "unaryMinus", "floor", "ceil", "round", "fix", "sign", "max", "min", "compare", "equal", "larger", "smaller", "cube", "square"];
stitch.endpoint("types_agree", { a: Range(-1000, 1000), b: Range(-1000, 1000), op: OneOf(...EXACT) }, null, ({ a, b, op }) => {
  const f = math[op];
  const unary = ["abs", "unaryMinus", "floor", "ceil", "round", "fix", "sign", "cube", "square"].includes(op);
  const run = (conv) => (unary ? f(conv(a)) : f(conv(a), conv(b)));
  const outs = [];
  for (const conv of [(x) => x, (x) => math.bignumber(x), (x) => math.fraction(x)]) {
    try {
      outs.push(math.number(run(conv)));
    } catch (e) {
      if (!mathErrorLocal(e)) throw e;
      outs.push("error");
    }
  }
  stitch.log(`${op}(${a}, ${b}): number ${outs[0]} BigNumber ${outs[1]} Fraction ${outs[2]}`);
  const errs = outs.filter((x) => x === "error").length;
  stitch.require(errs === 0 || errs === 3 || op === "gcd" || op === "lcm", "an operation a type does not support");
  if (!errs) stitch.check(outs[0] === outs[1] && outs[1] === outs[2], `${op}: number, BigNumber and Fraction agree`);
}, { bailsOn: BAILS });
const mathErrorLocal = (e) => require("./common").mathError(e);

// unit conversion round trip
stitch.endpoint("unit_roundtrip", { v: Range(-1000, 1000), from: OneOf(...UNITS), to: OneOf(...UNITS) }, null, ({ v, from, to }) => {
  const u = math.unit(v, from);
  let w;
  try {
    w = u.to(to);
  } catch (e) {
    stitch.bail("incompatible units");
  }
  const back = w.to(from);
  stitch.log(`${u} -> ${w} -> ${back}`);
  const a = u.toNumber(from), b = back.toNumber(from);
  stitch.check(Math.abs(a - b) <= 1e-9 * Math.max(1, Math.abs(a)), "converting a unit there and back keeps its value");
}, { bailsOn: BAILS });

// Unit / Matrix / Complex / Fraction methods
const METHODS = {
  unit: ["toString", "toJSON", "valueOf", "clone", "simplify", "toSI", "splitUnit", "formatUnits", "toNumeric", "equalBase"],
  matrix: ["size", "clone", "toArray", "valueOf", "toString", "toJSON", "datatype", "density", "storage"],
  complex: ["toPolar", "toString", "toJSON", "clone", "neg", "conjugate", "inverse", "sign", "abs", "arg", "isNaN", "isZero", "isFinite"],
  fraction: ["toString", "toLatex", "toFraction", "toContinued", "valueOf", "neg", "inverse", "abs", "floor", "ceil", "round", "simplify"],
};
const isKind = { unit: math.isUnit, matrix: math.isMatrix, complex: math.isComplex, fraction: math.isFraction };
for (const [kind, names] of Object.entries(METHODS)) {
  for (const m of names) {
    stitch.endpoint(`${kind}_${m}`, { x: Borrow(V), s: Src(8) }, New(V).optional(), ({ x, s }) => {
      stitch.require(isKind[kind](x), `not a ${kind}`);
      stitch.require(typeof x[m] === "function", "no such method");
      const arg = m === "splitUnit" ? [["m", "cm"]] : m === "equalBase" ? [math.unit("1 m")] : m === "toContinued" || m === "simplify" ? [s.pick([0.001, 0.1])] : m === "round" || m === "floor" || m === "ceil" ? [s.range(0, 5)] : [];
      const out = x[m](...arg);
      return out !== null && typeof out === "object" ? out : null;
    }, { bailsOn: BAILS });
  }
}
