// lodash Lang functions on arbitrary values: the is* type checks against native equivalents,
// toNumber/toInteger/toFinite/toLength/toSafeInteger/toString against their definitions,
// eq/gt/gte/lt/lte, and clone/cloneDeep/isEqual (reflexive, symmetric, a deep clone is equal).
const stitch = require("stitch");
const { Val, Str, OneOf } = stitch;
const util = require("util");
const _ = require("lodash");
const { AnyVal } = require("./common");

const T = util.types;
const tag = (v) => Object.prototype.toString.call(v);
const objectLike = (v) => v !== null && typeof v === "object";
const isLen = (n) => typeof n === "number" && n > -1 && n % 1 === 0 && n <= Number.MAX_SAFE_INTEGER;
const arrayLike = (v) => v != null && typeof v !== "function" && isLen(v.length);

const PREDICATES = {
  isArguments: (v) => objectLike(v) && tag(v) === "[object Arguments]",
  isArray: (v) => Array.isArray(v),
  isArrayBuffer: (v) => T.isArrayBuffer(v),
  isArrayLike: arrayLike,
  isArrayLikeObject: (v) => objectLike(v) && arrayLike(v),
  isBoolean: (v) => typeof v === "boolean" || (objectLike(v) && tag(v) === "[object Boolean]"),
  isBuffer: (v) => Buffer.isBuffer(v),
  isDate: (v) => T.isDate(v),
  isElement: () => false,
  isError: (v) => objectLike(v) && (tag(v) === "[object Error]" || (typeof v.message === "string" && typeof v.name === "string" && !_.isPlainObject(v))),
  isFinite: (v) => Number.isFinite(v),
  isFunction: (v) => typeof v === "function",
  isInteger: (v) => Number.isInteger(v),
  isLength: isLen,
  isMap: (v) => T.isMap(v),
  isNaN: (v) => (typeof v === "number" || (objectLike(v) && tag(v) === "[object Number]")) && Number.isNaN(Number(v)),
  isNil: (v) => v == null,
  isNull: (v) => v === null,
  isNumber: (v) => typeof v === "number" || (objectLike(v) && tag(v) === "[object Number]"),
  isObject: (v) => v !== null && (typeof v === "object" || typeof v === "function"),
  isObjectLike: objectLike,
  isPlainObject: (v) => objectLike(v) && tag(v) === "[object Object]" && [null, Object.prototype].includes(Object.getPrototypeOf(v)),
  isRegExp: (v) => T.isRegExp(v),
  isSafeInteger: (v) => Number.isSafeInteger(v),
  isSet: (v) => T.isSet(v),
  isString: (v) => typeof v === "string" || (!Array.isArray(v) && objectLike(v) && tag(v) === "[object String]"),
  isSymbol: (v) => typeof v === "symbol" || (objectLike(v) && tag(v) === "[object Symbol]"),
  isTypedArray: (v) => T.isTypedArray(v),
  isUndefined: (v) => v === undefined,
  isWeakMap: (v) => objectLike(v) && tag(v) === "[object WeakMap]",
  isWeakSet: (v) => objectLike(v) && tag(v) === "[object WeakSet]",
  isNative: (v) => typeof v === "function" && /\{\s*\[native code\]\s*\}\s*$/.test(Function.prototype.toString.call(v)),
};

const name = (v) => { try { return typeof v === "symbol" ? v.toString() : `${tag(v)} ${String(v).slice(0, 40)}`; } catch (e) { return tag(v); } };

stitch.endpoint("type_checks", { v: Val(AnyVal, 512) }, null, ({ v }) => {
  for (const [fn, model] of Object.entries(PREDICATES)) {
    stitch.check(_[fn](v) === model(v), `${fn}() agrees with its definition (${name(v)})`);
  }
  if (Array.isArray(v) || typeof v === "string") stitch.check(_.isEmpty(v) === (v.length === 0), "isEmpty() of an array or string");
  if (_.isPlainObject(v)) stitch.check(_.isEmpty(v) === (Object.keys(v).length === 0), "isEmpty() of a plain object");
  if (T.isMap(v) || T.isSet(v)) stitch.check(_.isEmpty(v) === (v.size === 0), "isEmpty() of a Map or Set");
  if (typeof v === "number" || typeof v === "boolean" || v == null) stitch.check(_.isEmpty(v), "isEmpty() of a number, boolean or nil");
});

// ---- conversions ----
const MAX = Number.MAX_VALUE;
const finite = (n) => (n === Infinity ? MAX : n === -Infinity ? -MAX : n === n ? n : 0);
const isPrim = (v) => v == null || ["string", "number", "boolean"].includes(typeof v);
const flatPrims = (v) => isPrim(v) || (Array.isArray(v) && Array.from(v).every((x) => isPrim(x) || (Array.isArray(x) && Array.from(x).every(isPrim))));
// lodash toString: nil -> "", -0 -> "-0" (also inside arrays, where nil and holes become "null"/"undefined")
const baseStr = (v) => (typeof v === "string" ? v : Array.isArray(v) ? Array.from(v, baseStr).join(",") : Object.is(v, -0) ? "-0" : String(v));
const toStr = (v) => (v == null ? "" : baseStr(v));

stitch.endpoint("conversions", { v: Val(AnyVal, 512) }, null, ({ v }) => {
  let n;
  try {
    n = _.toNumber(v);
  } catch (e) {
    // an object whose valueOf/toString cannot make a primitive (null prototype, BigInt) throws like Number() does
    stitch.require(!(e instanceof TypeError), "not convertible to a number");
    throw e;
  }
  if (typeof v === "number") stitch.check(Object.is(n, v), "toNumber(number) is the number");
  if (typeof v === "symbol") stitch.check(Number.isNaN(n), "toNumber(symbol) is NaN");
  if (typeof v === "string" && v.length < 40) stitch.check(Object.is(n, Number(v)), `toNumber(string) == Number(string) (${JSON.stringify(v)})`);
  const f = _.toFinite(v);
  stitch.check(Object.is(f, finite(n)) || (f === 0 && finite(n) === 0), "toFinite() clamps infinities and maps NaN to 0");
  const i = _.toInteger(v);
  stitch.check(i === Math.trunc(f), "toInteger() == trunc(toFinite())");
  stitch.check(_.toLength(v) === Math.min(Math.max(i, 0), 4294967295), "toLength() clamps toInteger() to [0, 2^32-1]");
  stitch.check(_.toSafeInteger(v) === Math.min(Math.max(i, -Number.MAX_SAFE_INTEGER), Number.MAX_SAFE_INTEGER), "toSafeInteger() clamps toInteger()");
  if (flatPrims(v)) stitch.check(_.toString(v) === toStr(v), `toString() (${name(v)})`);
  if (typeof v === "symbol") stitch.check(_.toString(v) === v.toString(), "toString(symbol)");
});

stitch.endpoint("number_text", { text: Str(48), radix: OneOf(undefined, 0, 2, 8, 10, 16, 36) }, null, ({ text, radix }) => {
  stitch.check(Object.is(_.toNumber(text), Number(text)), "toNumber(string) == Number(string)");
  stitch.check(Object.is(_.parseInt(text, radix), Number.parseInt(text, radix || 0)), "parseInt() == Number.parseInt()");
});

stitch.endpoint("relational", { a: Val(AnyVal, 256), b: Val(AnyVal, 256) }, null, ({ a, b }) => {
  stitch.check(_.eq(a, b) === (a === b || (a !== a && b !== b)), "eq() is SameValueZero");
  if ((typeof a === "number" || typeof a === "string") && typeof a === typeof b) {
    stitch.check(_.gt(a, b) === a > b && _.gte(a, b) === a >= b && _.lt(a, b) === a < b && _.lte(a, b) === a <= b, "gt/gte/lt/lte compare like the operators");
  }
});

// cloneDeep turns these into {} at the top level (uncloneable), by documentation
const uncloneable = (v) => typeof v === "function" || T.isWeakMap(v) || T.isWeakSet(v) || v instanceof Error || tag(v) === "[object BigInt]";

stitch.endpoint("clone_equal", { a: Val(AnyVal, 512), b: Val(AnyVal, 512), deep: OneOf("cloneDeep", "cloneDeepWith") }, null, ({ a, b, deep }) => {
  stitch.check(_.isEqual(a, a), `isEqual(v, v) (${name(a)})`);
  stitch.check(_.isEqual(a, b) === _.isEqual(b, a), "isEqual() is symmetric");
  stitch.check(_.isEqualWith(a, b, () => undefined) === _.isEqual(a, b), "isEqualWith(undefined customizer) == isEqual");
  if (!uncloneable(a)) {
    const c = deep === "cloneDeep" ? _.cloneDeep(a) : _.cloneDeepWith(a, () => undefined);
    stitch.check(_.isEqual(c, a) && _.isEqual(a, c), `${deep}(v) isEqual v (${name(a)})`);
    const s = _.clone(a);
    stitch.check(_.isEqual(s, a), `clone(v) isEqual v (${name(a)})`);
    if (objectLike(a)) stitch.check(c !== a, `${deep}() makes a new object`);
  }
});
