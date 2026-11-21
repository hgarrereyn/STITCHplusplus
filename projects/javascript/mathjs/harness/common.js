// mathjs harness: shared object types, value constructors and the error policy.
//
// Values (`V`) are anything mathjs computes with: numbers, BigNumbers, Fractions, Complex numbers,
// Units, dense and sparse matrices, arrays, strings, booleans. Expression trees are `Node`.
//
// Bugs: JavaScript runtime errors inside mathjs (TypeError "Cannot read properties", "x is not a
// function", stack overflow, invalid array length), failed checks, prototype pollution. mathjs
// reports bad input with its own errors (typed-function "Unexpected type of argument", "Too many
// arguments", DimensionError, IndexError, ArgumentsError, and plain Errors with a message): bails.
const stitch = require("stitch");
const { Borrow, New, Str, OneOf, Bool, Src, Range } = stitch;
const math = require("mathjs");

const V = stitch.object("V");
const Node = stitch.object("Node");

const RUNTIME = /Cannot read propert|Cannot set propert|is not a function|is not iterable|is not a constructor|undefined is not|null is not|Cannot convert undefined|Cannot convert null|Maximum call stack|Invalid array length|Invalid string length|Cannot create property|Reduce of empty array/;
// bails: mathjs's own error reports; a JS runtime error inside mathjs is a bug
// (mathjs reports invalid input as SyntaxError too: "String "a" is no valid number", "At least one
// matrix expected", complex.js "Invalid Param")
const mathError = (e) => e instanceof Error && !RUNTIME.test(String(e.message));
// a user-defined recursive function in an expression recurses until the stack runs out
const recursion = (text) => (e) => e instanceof RangeError && /Maximum call stack/.test(e.message) && /\w\s*\([^)]*\)\s*=/.test(text);
const BAILS = [mathError];

function small(x) {
  // bound sizes: matrices up to 64 elements, strings up to 2000 characters
  if (math.isMatrix(x)) stitch.require(x.size().reduce((a, b) => a * b, 1) <= 64, "large matrix");
  if (Array.isArray(x)) stitch.require(JSON.stringify(x).length < 4000, "large array");
  if (typeof x === "string") stitch.require(x.length <= 2000, "long string");
}

const UNITS = ["m", "cm", "km", "inch", "ft", "s", "ms", "h", "kg", "g", "lb", "N", "J", "W", "V", "A", "Hz", "degC", "degF", "K", "rad", "deg", "L", "mL", "m^2", "m/s", "km/h", "kWh", "bytes", "b", "mol", "Pa", "bar", "psi", "ohm", "%"];

function num(s) {
  switch (s.choice(6)) {
    case 0: return s.range(-10, 10);
    case 1: return s.pick([0, -0, 0.5, -1.5, 1e-10, 1e10, 3.14159, 2.718281828, 1 / 3, Number.MAX_SAFE_INTEGER, 2 ** 53 + 2, Infinity, -Infinity, NaN]);
    case 2: return s.range(-1000, 1000) / s.range(1, 64);
    case 3: return s.range(-(2 ** 31), 2 ** 31);
    default: return s.range(-3, 30);
  }
}

function make(s, depth = 0) {
  switch (s.choice(depth > 1 ? 7 : 10)) {
    case 0: case 1: return num(s);
    case 2: return math.bignumber(s.pick(["0", "1", "-1", "0.1", "1e-30", "123456789012345678901234567890", "3.14159265358979323846", "-7.5", "1e40", "NaN", "Infinity", String(s.range(-99, 99))]));
    case 3: return math.fraction(s.range(-50, 50), s.pick([1, 2, 3, 4, 7, 10, 12]));
    case 4: return math.complex(num(s), num(s));
    case 5: return math.unit(s.range(-100, 100) / s.pick([1, 4, 10]), s.pick(UNITS));
    case 6: return s.pick([true, false, "1", "2.5", "a", "0x1F", "1e3", "", "[1, 2]", "5 cm", null]);
    default: {
      const rows = s.range(0, 4), cols = s.range(1, 4);
      const data = [];
      for (let i = 0; i < rows; i++) {
        const r = [];
        for (let j = 0; j < cols; j++) r.push(s.coin(0.85) ? s.range(-9, 9) : make(s, depth + 1));
        data.push(r);
      }
      const kind = s.choice(4);
      if (kind === 0) return data;
      if (kind === 1 && rows) return data.map((r) => r[0]);
      return kind === 2 ? math.sparse(data.every((r) => r.every((x) => typeof x === "number")) ? data : [[1]]) : math.matrix(data);
    }
  }
}

function describe(x) {
  try {
    return `${math.typeOf(x)} ${math.format(x, { precision: 8 }).slice(0, 120)}`;
  } catch (e) {
    return math.typeOf(x);
  }
}

module.exports = { stitch, math, V, Node, BAILS, mathError, recursion, small, make, num, describe, UNITS, Borrow, New, Str, OneOf, Bool, Src, Range };
