// Shared object types, data types and helpers for the lodash harness.
const stitch = require("stitch");
const _ = require("lodash");

const Obj = stitch.object("Obj");      // a plain object / array graph (deep get/set/merge)
const List = stitch.object("List");    // an array of mostly primitive values
const Memo = stitch.object("Memo");    // a memoized function (its .cache is a lodash MapCache)
const PropPath = stitch.datatype("PropPath");
const ListVal = stitch.datatype("ListVal");
const AnyVal = stitch.datatype("AnyVal");

// SameValueZero, the equality lodash's array functions use
const svz = (a, b) => a === b || (a !== a && b !== b);
const has = (arr, x) => arr.some((y) => svz(x, y));
const uniqM = (arr) => arr.filter((x, i) => arr.findIndex((y) => svz(x, y)) === i);
// element-wise SameValueZero, recursing into arrays
const same = (a, b) => (Array.isArray(a) && Array.isArray(b)
  ? a.length === b.length && a.every((x, i) => same(x, b[i]))
  : svz(a, b));
const show = (x) => {
  try {
    return JSON.stringify(x, (k, v) => (typeof v === "number" && !Number.isFinite(v) ? String(v) : v === undefined ? "<u>" : v)).slice(0, 300);
  } catch (e) {
    return String(e);
  }
};

const PRIMS = [0, -0, 1, -1, 2, 1.5, NaN, Infinity, -Infinity, 2 ** 53, "", "a", "b", "0", "1", "10", "abc", " ", "__proto__",
  "constructor", "toString", true, false, null, undefined];
const prim = (s) => (s.coin(0.7) ? s.pick(PRIMS) : s.coin(0.5) ? s.range(-20, 20) : s.str(4));

// small deterministic PRNG, to fill large arrays from one seed
function rng(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// lists: usually short and mixed; sometimes 150..420 elements (past LARGE_ARRAY_SIZE = 200, where
// lodash switches to hashed caches) drawn from a small range so they repeat
stitch.decoder(ListVal, (s) => {
  if (s.coin(0.15)) {
    const n = s.range(150, 420), k = s.range(1, 400), r = rng(s.u32());
    const kind = s.choice(3);
    return Array.from({ length: n }, () => {
      const x = Math.floor(r() * k);
      return kind === 0 ? x : kind === 1 ? `s${x}` : (x % 5 === 0 ? s.pick(PRIMS) : x);
    });
  }
  const out = [];
  while (s.more() && out.length < 24) {
    const c = s.choice(20);
    out.push(c < 17 ? prim(s) : c < 19 ? [prim(s), s.coin(0.3) ? [prim(s)] : prim(s)] : { k: prim(s) });
  }
  return out;
});

// any JavaScript value: primitives, boxed primitives, built-in objects, containers, cycles
function anyValue(s, d) {
  const leaf = () => {
    switch (s.choice(12)) {
      case 0: return s.pick(PRIMS);
      case 1: return s.range(-1000, 1000);
      case 2: return s.f64();
      case 3: return s.str(8);
      case 4: return s.pick([Symbol.iterator, Symbol.for("s")]);
      case 5: return Object(s.pick([1, "s", "", true, NaN, 0n]));
      case 6: return new Date(s.pick([0, NaN, 1e12, -1, 8.64e15]));
      case 7: { const re = new RegExp(s.pick(["a+", "^x$", "", "(?:)"]), s.pick(["", "g", "gi", "y"])); re.lastIndex = s.range(0, 3); return re; }
      case 8: return s.pick([() => 1, function named(a, b) { return a + b; }, Math.max, class K {}, async () => 1, function* gen() {}, Math.max.bind(null)]);
      case 9: return new (s.pick([Error, TypeError, RangeError]))(s.str(4));
      case 10: return Buffer.from(s.bytes(6));
      default: {
        const T = s.pick([Uint8Array, Int16Array, Float64Array, Uint8ClampedArray, Int32Array, Float32Array]);
        return new T(Array.from({ length: s.range(0, 4) }, () => s.range(-300, 300)));
      }
    }
  };
  if (d >= 2 || s.coin(0.45)) return leaf();
  const KEYS = ["a", "b", "0", "1", "length", "constructor", "__proto__", "toString", "valueOf", "k"];
  const fill = (o) => {
    const n = s.choice(4);
    for (let i = 0; i < n; i++) {
      const k = s.pick(KEYS);
      // (an array's own "length" cannot be redefined; an own "constructor" on an array is a shape only
      // code makes, and cloneDeep calls it as the array's constructor)
      if (Array.isArray(o) && (k === "length" || k === "constructor")) continue;
      // a "__proto__" key from data (JSON.parse, defineProperty) is an own property, not the prototype
      Object.defineProperty(o, k, { value: anyValue(s, d + 1), enumerable: true, writable: true, configurable: true });
    }
    return o;
  };
  switch (s.choice(12)) {
    case 0: case 1: return Array.from({ length: s.choice(5) }, () => anyValue(s, d + 1));
    case 2: case 3: return fill({});
    case 4: return fill(Object.create(null));
    case 5: return new Map(Array.from({ length: s.choice(4) }, () => [s.coin(0.5) ? prim(s) : anyValue(s, d + 1), anyValue(s, d + 1)]));
    case 6: return new Set(Array.from({ length: s.choice(4) }, () => (s.coin(0.5) ? prim(s) : anyValue(s, d + 1))));
    case 7: return (function () { return arguments; })(...Array.from({ length: s.choice(4) }, () => anyValue(s, d + 1)));
    case 8: { class P { constructor() { this.a = 1; } } return fill(new P()); }
    case 9: { const o = fill({}); o.self = o; return o; }
    case 10: {
      const b = new ArrayBuffer(s.range(0, 8));
      new Uint8Array(b).set(s.bytes(b.byteLength));
      return s.coin(0.5) ? b : new DataView(b);
    }
    default: {
      const o = fill(s.coin(0.5) ? [] : {});
      o[Symbol.for("sym")] = anyValue(s, d + 1);
      return o;
    }
  }
}
stitch.decoder(AnyVal, (s) => anyValue(s, 0));

module.exports = { Obj, List, Memo, PropPath, ListVal, AnyVal, svz, has, uniqM, same, show, prim, PRIMS, rng };
