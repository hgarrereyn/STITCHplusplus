// lodash Array, Collection and Math functions on lists of mixed values, checked against plain
// JavaScript models (SameValueZero equality, stable sorts). Lists past 200 elements take lodash's
// hashed-cache paths. Functions that do not mutate are checked to leave their inputs alone.
const stitch = require("stitch");
const { Borrow, New, OneOf, Range, Src, Bool, Val } = stitch;
const _ = require("lodash");
const { List, ListVal, svz, has, same, show, prim } = require("./common");

stitch.endpoint("list", { v: Val(ListVal, 4096) }, New(List), ({ v }) => v);

stitch.endpoint("list_from", { a: Borrow(List), op: OneOf("castArray", "toArray", "concat", "compact", "times") }, New(List), ({ a, op }) => {
  if (op === "castArray") {
    stitch.check(_.castArray(a) === a, "castArray(array) returns the array");
    const x = a[0];
    stitch.check(Array.isArray(x) ? _.castArray(x) === x : same(_.castArray(x), [x]), "castArray(x) wraps a non-array");
    return a.slice();
  }
  if (op === "toArray") return _.toArray(a);
  if (op === "concat") return _.concat(a, a[0], [a[1]]);
  if (op === "compact") return _.compact(a);
  return _.times(Math.min(a.length, 400), (i) => a[a.length - 1 - i]);
});

const unchanged = (list, copy, what) => stitch.check(same(list, copy), `${what} leaves its input unchanged`);

const ITER = {
  floor: Math.floor,
  String: (x) => String(x),
  k: (x) => (x == null ? undefined : x.k),   // the "k" property shorthand
  typeof: (x) => typeof x,
};
const iterArg = (name) => (name === "k" ? "k" : ITER[name]);

// ---- set-like functions: plain, *By(iteratee) and *With(comparator) against one model ----
stitch.endpoint("set_ops", {
  a: Borrow(List), b: Borrow(List),
  op: OneOf("difference", "intersection", "union", "xor", "uniq", "without"),
  variant: OneOf("", "By", "With"), it: OneOf("floor", "String", "k", "typeof"),
}, null, ({ a, b, op, variant, it }) => {
  if (op === "without") variant = "";
  const f = variant === "" ? (x) => x : ITER[it];
  const eq = (x, y) => svz(f(x), f(y));
  const hasE = (arr, x) => arr.some((y) => eq(x, y));
  const uniqE = (arr) => arr.filter((x, i) => arr.findIndex((y) => eq(x, y)) === i);
  const want = {
    difference: () => a.filter((x) => !hasE(b, x)),
    without: () => a.filter((x) => !hasE(b, x)),
    intersection: () => uniqE(a.filter((x) => hasE(b, x))),
    union: () => uniqE(a.concat(b)),
    xor: () => uniqE([...a.filter((x) => !hasE(b, x)), ...b.filter((x) => !hasE(a, x))]),
    uniq: () => uniqE(a),
  }[op]();
  const ca = a.slice(), cb = b.slice();
  const extra = variant === "By" ? [iterArg(it)] : variant === "With" ? [eq] : [];
  let got;
  if (op === "uniq") got = _[op + variant](a, ...extra);
  else if (op === "without") got = _.without(a, ...b);
  else got = _[op + variant](a, b, ...extra);
  if (!same(got, want)) stitch.log(`got ${show(got)}\nwant ${show(want)}`);
  stitch.check(same(got, want), `${op}${variant}() matches the SameValueZero model`);
  unchanged(a, ca, op);
  unchanged(b, cb, op);
});

// ---- functions that mutate the list in place ----
const PREDS = {
  truthy: (x) => Boolean(x),
  num: (x) => typeof x === "number",
  small: (x) => typeof x === "number" && x < 5,
  str: (x) => typeof x === "string",
  arr: (x) => Array.isArray(x),
};
const PRED = OneOf(...Object.keys(PREDS));

stitch.endpoint("list_mutate", {
  a: Borrow(List), b: Borrow(List), op: OneOf("pull", "pullAll", "pullAllBy", "pullAllWith", "pullAt", "remove", "reverse", "fill"),
  n: Range(-3, 30), m: Range(-3, 30), pred: PRED, it: OneOf("floor", "String", "k", "typeof"), s: Src(8),
}, null, ({ a, b, op, n, m, pred, it, s }) => {
  const before = a.slice(), vals = b.slice();
  const f = op === "pullAllBy" || op === "pullAllWith" ? ITER[it] : (x) => x;
  const eq = (x, y) => svz(f(x), f(y));
  let ret, want, wantRet;
  switch (op) {
    case "pull": ret = _.pull(a, ...vals); wantRet = a; want = before.filter((x) => !vals.some((y) => eq(x, y))); break;
    case "pullAll": ret = _.pullAll(a, b); wantRet = a; want = before.filter((x) => !vals.some((y) => eq(x, y))); break;
    case "pullAllBy": ret = _.pullAllBy(a, b, iterArg(it)); wantRet = a; want = before.filter((x) => !vals.some((y) => eq(x, y))); break;
    case "pullAllWith": ret = _.pullAllWith(a, b, eq); wantRet = a; want = before.filter((x) => !vals.some((y) => eq(x, y))); break;
    case "pullAt": {
      const idx = [n, m];
      ret = _.pullAt(a, idx);
      wantRet = idx.map((i) => (i >= 0 ? before[i] : undefined));
      want = before.filter((_x, i) => !idx.includes(i));
      break;
    }
    case "remove": ret = _.remove(a, PREDS[pred]); wantRet = before.filter(PREDS[pred]); want = before.filter((x) => !PREDS[pred](x)); break;
    case "reverse": ret = _.reverse(a); wantRet = a; want = before.slice().reverse(); break;
    default: {
      const v = s.coin(0.5) && before.length ? s.pick(before) : prim(s);
      ret = _.fill(a, v, n, m); wantRet = a; want = before.slice().fill(v, n, m);
    }
  }
  if (!same(a, want)) stitch.log(`got ${show(a)}\nwant ${show(want)}`);
  stitch.check(same(a, want), `${op}() leaves the list as the model`);
  // (fill() of an empty array returns a new empty array)
  stitch.check(wantRet === a ? ret === a || (before.length === 0 && same(ret, [])) : same(ret, wantRet), `${op}() returns the model's result`);
});

// ---- slicing and searching (non-mutating) ----
const fromStart = (len, n) => (n < 0 ? Math.max(len + n, 0) : n);
const fromEnd = (len, n) => (n === undefined ? len - 1 : n < 0 ? Math.max(len + n, 0) : Math.min(n, len - 1));
const findFrom = (a, p, from) => { for (let i = from; i < a.length; i++) if (p(a[i])) return i; return -1; };
const findBack = (a, p, from) => { for (let i = from; i >= 0; i--) if (p(a[i])) return i; return -1; };
const firstFalse = (a, p) => { const i = a.findIndex((x) => !p(x)); return i < 0 ? a.length : i; };
const lastFalse = (a, p) => { for (let i = a.length - 1; i >= 0; i--) if (!p(a[i])) return i; return -1; };

stitch.endpoint("slice_ops", {
  a: Borrow(List), b: Borrow(List),
  op: OneOf("chunk", "drop", "dropRight", "take", "takeRight", "slice", "nth", "head", "first", "last", "initial", "tail", "compact",
    "flatten", "flattenDeep", "flattenDepth", "concat", "join", "indexOf", "lastIndexOf", "findIndex", "findLastIndex",
    "dropWhile", "dropRightWhile", "takeWhile", "takeRightWhile"),
  n: Range(-3, 25), m: Range(-3, 25), pred: PRED, s: Src(8),
}, null, ({ a, b, op, n, m, pred, s }) => {
  const p = PREDS[pred], len = a.length, copy = a.slice();
  const v = s.coin(0.7) && len ? s.pick(a) : prim(s);
  let got, want;
  switch (op) {
    case "chunk": {
      got = _.chunk(a, n);
      want = [];
      if (n >= 1) for (let i = 0; i < len; i += n) want.push(a.slice(i, i + n));
      break;
    }
    case "drop": got = _.drop(a, n); want = a.slice(Math.max(n, 0)); break;
    case "dropRight": got = _.dropRight(a, n); want = a.slice(0, Math.max(len - n, 0)); break;
    case "take": got = _.take(a, n); want = a.slice(0, Math.max(n, 0)); break;
    case "takeRight": got = _.takeRight(a, n); want = a.slice(Math.max(len - n, 0)); break;
    case "slice": got = _.slice(a, n, m); want = a.slice(n, m); break;
    case "nth": got = [_.nth(a, n)]; want = [a.at(n)]; break;
    case "head": case "first": got = [_[op](a)]; want = [a[0]]; break;
    case "last": got = [_.last(a)]; want = [a[len - 1]]; break;
    case "initial": got = _.initial(a); want = a.slice(0, -1); break;
    case "tail": got = _.tail(a); want = a.slice(1); break;
    case "compact": got = _.compact(a); want = a.filter(Boolean); break;
    case "flatten": got = _.flatten(a); want = a.flat(1); break;
    case "flattenDeep": got = _.flattenDeep(a); want = a.flat(Infinity); break;
    case "flattenDepth": got = _.flattenDepth(a, n); want = a.flat(n); break;
    case "concat": got = _.concat(a, b, v); want = a.concat(b, v); break;
    case "join": { const sep = s.pick([",", "", "-", undefined]); got = [_.join(a, sep)]; want = [a.join(sep)]; break; }
    case "indexOf": {
      const from = m === -3 ? undefined : n;
      got = [_.indexOf(a, v, from)];
      want = [len ? findFrom(a, (x) => svz(x, v), fromStart(len, from || 0)) : -1];
      break;
    }
    case "lastIndexOf": {
      const from = m === -3 ? undefined : n;
      got = [_.lastIndexOf(a, v, from)];
      want = [len ? findBack(a, (x) => svz(x, v), fromEnd(len, from)) : -1];
      break;
    }
    case "findIndex": got = [_.findIndex(a, p, n)]; want = [len ? findFrom(a, p, fromStart(len, n)) : -1]; break;
    case "findLastIndex": got = [_.findLastIndex(a, p, n)]; want = [len ? findBack(a, p, fromEnd(len, n)) : -1]; break;
    case "dropWhile": got = _.dropWhile(a, p); want = a.slice(firstFalse(a, p)); break;
    case "takeWhile": got = _.takeWhile(a, p); want = a.slice(0, firstFalse(a, p)); break;
    case "dropRightWhile": got = _.dropRightWhile(a, p); want = a.slice(0, lastFalse(a, p) + 1); break;
    default: got = _.takeRightWhile(a, p); want = a.slice(lastFalse(a, p) + 1); break;
  }
  if (!same(got, want)) stitch.log(`got ${show(got)}\nwant ${show(want)}`);
  stitch.check(same(got, want), `${op}() matches the Array model`);
  unchanged(a, copy, op);
});

// ---- binary searches on a sorted copy ----
stitch.endpoint("sorted_ops", {
  a: Borrow(List), kind: OneOf("number", "string"),
  op: OneOf("sortedIndex", "sortedLastIndex", "sortedIndexOf", "sortedLastIndexOf", "sortedIndexBy", "sortedLastIndexBy", "sortedUniq", "sortedUniqBy"),
  s: Src(8),
}, null, ({ a, kind, op, s }) => {
  const xs = kind === "number"
    ? a.filter((x) => typeof x === "number" && x === x).sort((x, y) => (x < y ? -1 : x > y ? 1 : 0))
    : a.filter((x) => typeof x === "string").sort();
  const v = s.coin(0.6) && xs.length ? s.pick(xs) : kind === "number" ? s.pick([0, -0, 0.5, -1, 1e9, Infinity, -Infinity, s.range(-20, 400)]) : s.pick(["", "a", "s1", "s99", "~"]);
  // a monotone iteratee keeps the copy sorted
  const fl = (x) => (typeof x === "number" ? Math.floor(x) : x);
  const lower = (key, val) => { const i = xs.findIndex((x) => !(key(x) < key(val))); return i < 0 ? xs.length : i; };
  const upper = (key, val) => { const i = xs.findIndex((x) => key(x) > key(val)); return i < 0 ? xs.length : i; };
  const id = (x) => x;
  let got, want;
  switch (op) {
    case "sortedIndex": got = _.sortedIndex(xs, v); want = lower(id, v); break;
    case "sortedLastIndex": got = _.sortedLastIndex(xs, v); want = upper(id, v); break;
    case "sortedIndexOf": got = _.sortedIndexOf(xs, v); want = xs.findIndex((x) => x === v); break;
    case "sortedLastIndexOf": got = _.sortedLastIndexOf(xs, v); want = xs.findLastIndex((x) => x === v); break;
    case "sortedIndexBy": got = _.sortedIndexBy(xs, v, fl); want = lower(fl, v); break;
    case "sortedLastIndexBy": got = _.sortedLastIndexBy(xs, v, fl); want = upper(fl, v); break;
    case "sortedUniq": got = _.sortedUniq(xs); want = xs.filter((x, i) => i === 0 || !svz(x, xs[i - 1])); break;
    default: got = _.sortedUniqBy(xs, fl); want = xs.filter((x, i) => i === 0 || !svz(fl(x), fl(xs[i - 1]))); break;
  }
  if (!same(got, want)) stitch.log(`xs ${show(xs)} v ${show(v)}\ngot ${show(got)} want ${show(want)}`);
  stitch.check(same(got, want), `${op}() matches the linear-search model`);
});

// ---- zip / unzip / pairs ----
stitch.endpoint("zip_ops", { a: Borrow(List), b: Borrow(List) }, null, ({ a, b }) => {
  const n = Math.max(a.length, b.length);
  const z = _.zip(a, b);
  const wantZ = Array.from({ length: n }, (_x, i) => [a[i], b[i]]);
  stitch.check(same(z, wantZ), "zip() pairs elements");
  stitch.check(same(_.zipWith(a, b, (x, y) => [x, y]), wantZ), "zipWith(pair) == zip()");
  const pad = (arr) => Array.from({ length: n }, (_x, i) => arr[i]);
  const wantU = n ? [pad(a), pad(b)] : [];
  stitch.check(same(_.unzip(z), wantU), "unzip(zip(a, b)) == [a, b] padded");
  stitch.check(same(_.unzipWith(z, (...xs) => xs), wantU), "unzipWith(rest) == unzip()");
  const o = _.zipObject(a, b);
  stitch.check(Object.getPrototypeOf(o) === Object.prototype, "zipObject() result keeps Object.prototype");
  const model = new Map();
  a.forEach((k, i) => model.set(String(k), b[i]));
  stitch.check(same(Object.keys(o).sort(), [...model.keys()].sort()), "zipObject() has one key per distinct prop");
  for (const [k, v] of model) stitch.check(svz(o[k], v), "zipObject() keeps the last value of each key");
  if (a.length >= b.length) {
    const fp = _.fromPairs(z);
    stitch.check(Object.getPrototypeOf(fp) === Object.prototype && same(Object.keys(fp).sort(), Object.keys(o).sort()) &&
      Object.keys(o).every((k) => svz(fp[k], o[k])), "fromPairs(zip(a, b)) == zipObject(a, b)");
  }
});

// ---- Collection functions ----
stitch.endpoint("collection_query", { a: Borrow(List), pred: PRED, n: Range(-3, 25), s: Src(8) }, null, ({ a, pred, n, s }) => {
  const p = PREDS[pred], copy = a.slice(), len = a.length;
  const v = s.coin(0.7) && len ? s.pick(a) : prim(s);
  stitch.check(same(_.filter(a, p), a.filter(p)), "filter()");
  stitch.check(same(_.reject(a, p), a.filter((x) => !p(x))), "reject()");
  stitch.check(same(_.partition(a, p), [a.filter(p), a.filter((x) => !p(x))]), "partition()");
  stitch.check(_.every(a, p) === a.every(p) && _.some(a, p) === a.some(p), "every()/some()");
  const fi = findFrom(a, p, fromStart(len, n));
  stitch.check(svz(_.find(a, p, n), fi < 0 ? undefined : a[fi]), "find(fromIndex)");
  const li = len ? findBack(a, p, fromEnd(len, n)) : -1;
  stitch.check(svz(_.findLast(a, p, n), li < 0 ? undefined : a[li]), "findLast(fromIndex)");
  stitch.check(_.includes(a, v, n) === (len ? findFrom(a, (x) => svz(x, v), n > 0 ? n : fromStart(len, n)) >= 0 : false), "includes(fromIndex)");
  stitch.check(_.size(a) === len, "size()");
  const seen = [];
  _.forEach(a, (x) => { seen.push(x); return !svz(x, v); });
  const stop = a.findIndex((x) => svz(x, v));
  stitch.check(same(seen, stop < 0 ? a : a.slice(0, stop + 1)), "forEach() stops when the iteratee returns false");
  const back = [];
  _.eachRight(a, (x) => { back.push(x); });
  stitch.check(same(back, a.slice().reverse()), "eachRight() visits backwards");
  stitch.check(_.each === _.forEach && _.forEachRight === _.eachRight, "each/eachRight are aliases");
  unchanged(a, copy, "collection queries");
});

stitch.endpoint("collection_map", { a: Borrow(List), d: Range(-1, 4) }, null, ({ a, d }) => {
  const copy = a.slice();
  const f = (x) => (typeof x === "number" ? x * 2 : x);
  stitch.check(same(_.map(a, f), a.map(f)), "map()");
  stitch.check(same(_.flatMap(a, (x) => [x, x]), a.flatMap((x) => [x, x])), "flatMap()");
  stitch.check(same(_.flatMapDeep(a, (x) => [[x]]), a.map((x) => [[x]]).flat(Infinity)), "flatMapDeep()");
  stitch.check(same(_.flatMapDepth(a, (x) => [[x]], d), a.map((x) => [[x]]).flat(d)), "flatMapDepth()");
  stitch.check(_.reduce(a, (t) => t + 1, 0) === a.length, "reduce() visits every element");
  stitch.check(same(_.reduceRight(a, (acc, x) => { acc.push(x); return acc; }, []), a.slice().reverse()), "reduceRight() visits backwards");
  if (a.length) stitch.check(svz(_.reduce(a, (acc) => acc), a[0]), "reduce() without accumulator starts at the first element");
  stitch.check(same(_.invokeMap(a, "toString"), a.map((x) => (x == null ? undefined : x.toString()))), "invokeMap('toString')");
  stitch.check(same(_.toArray(a), a) && _.toArray(a) !== a, "toArray() copies");
  unchanged(a, copy, "collection maps");
});

const sortKey = (x) => (typeof x === "number" && x === x ? x : -1);
const stableSort = (a, key, desc) => a.map((x, i) => [key(x), i, x])
  .sort((p, q) => (p[0] < q[0] ? (desc ? 1 : -1) : p[0] > q[0] ? (desc ? -1 : 1) : p[1] - q[1])).map((t) => t[2]);
const counts = (arr) => { const m = new Map(); for (const x of arr) m.set(x, (m.get(x) || 0) + 1); return m; };
const subMultiset = (sub, arr) => { const m = counts(arr); return sub.every((x) => { const c = m.get(x) || 0; m.set(x, c - 1); return c > 0; }); };

stitch.endpoint("collection_group", { a: Borrow(List), it: OneOf("floor", "String", "k", "typeof"), n: Range(-2, 30) }, null, ({ a, it, n }) => {
  const copy = a.slice(), f = ITER[it], key = (x) => String(f(x));
  const g = _.groupBy(a, iterArg(it)), c = _.countBy(a, iterArg(it)), k = _.keyBy(a, iterArg(it));
  const keys = [...new Set(a.map(key))].sort();
  for (const r of [g, c, k]) stitch.check(same(Object.keys(r).sort(), keys) && Object.getPrototypeOf(r) === Object.prototype, "groupBy/countBy/keyBy have one own key per group");
  for (const kk of keys) {
    const members = a.filter((x) => key(x) === kk);
    stitch.check(same(g[kk], members), "groupBy() groups in order");
    stitch.check(c[kk] === members.length, "countBy() counts the group");
    stitch.check(svz(k[kk], members[members.length - 1]), "keyBy() keeps the last element");
  }
  stitch.check(same(_.sortBy(a, sortKey), stableSort(a, sortKey, false)), "sortBy() is a stable ascending sort");
  stitch.check(same(_.orderBy(a, [sortKey], ["desc"]), stableSort(a, sortKey, true)), "orderBy(desc) is a stable descending sort");
  const sh = _.shuffle(a);
  stitch.check(sh.length === a.length && subMultiset(sh, a), "shuffle() is a permutation");
  const ss = _.sampleSize(a, n);
  stitch.check(ss.length === Math.min(Math.max(n, 0), a.length) && subMultiset(ss, a), "sampleSize() picks distinct positions");
  const one = _.sample(a);
  stitch.check(a.length ? has(a, one) : one === undefined, "sample() is an element");
  unchanged(a, copy, "grouping and sorting");
});

// ---- Math and Number ----
const nums = (a) => a.filter((x) => typeof x === "number");

stitch.endpoint("math", { a: Borrow(List), p: Range(-6, 16), floating: Bool, s: Src(16) }, null, ({ a, p, floating, s }) => {
  const xs = nums(a), real = xs.filter((x) => x === x);
  stitch.check(svz(_.max(xs), real.length ? Math.max(...real) : undefined), "max() ignores NaN");
  stitch.check(svz(_.min(xs), real.length ? Math.min(...real) : undefined), "min() ignores NaN");
  const mb = _.maxBy(xs, Math.abs), nb = _.minBy(xs, Math.abs);
  if (real.length) stitch.check(Math.abs(mb) === Math.max(...real.map(Math.abs)) && Math.abs(nb) === Math.min(...real.map(Math.abs)), "maxBy/minBy(abs)");
  const sum = xs.reduce((t, x) => t + x, 0);
  stitch.check(svz(_.sum(xs), sum) && svz(_.sumBy(xs, (x) => x), sum), "sum()/sumBy() add left to right");
  stitch.check(svz(_.mean(xs), xs.length ? sum / xs.length : NaN) && svz(_.meanBy(xs, (x) => x), _.mean(xs)), "mean()/meanBy()");
  const x = xs.length && s.coin(0.7) ? s.pick(xs) : s.pick([0.5, -2.5, 1.005, 1e21, 123.456, -0, 5e-324, 1e300, -1e-7]);
  const y = xs.length && s.coin(0.7) ? s.pick(xs) : s.pick([1, -3, 0.1, 7, 1e-5]);
  stitch.check(svz(_.add(x, y), x + y) && svz(_.subtract(x, y), x - y) && svz(_.multiply(x, y), x * y) && svz(_.divide(x, y), x / y), "add/subtract/multiply/divide");
  if (Number.isFinite(x)) {
    const lo = _.floor(x, p), hi = _.ceil(x, p), r = _.round(x, p);
    stitch.log(`x=${x} p=${p} floor=${lo} ceil=${hi} round=${r}`);
    stitch.check(lo <= x && x <= hi, "floor(x, p) <= x <= ceil(x, p)");
    stitch.check(r === lo || r === hi, "round(x, p) is floor or ceil");
    if (p === 0) stitch.check(svz(lo, Math.floor(x)) && svz(hi, Math.ceil(x)), "floor/ceil with precision 0");
  }
  const [l, h] = x <= y ? [x, y] : [y, x];
  const c = _.clamp(x, l, h);
  if (l === l && h === h && x === x) stitch.check(c >= l && c <= h, "clamp() stays in bounds");
  // (inRange converts the bounds with toFinite: infinite bounds become +-Number.MAX_VALUE)
  if (Number.isFinite(l) && Number.isFinite(h)) stitch.check(_.inRange(x, l, h) === (x >= l && x < h), "inRange()");
  if (Number.isSafeInteger(l) && Number.isSafeInteger(h) && h - l < 2 ** 52) {
    const r = _.random(l, h, floating);
    stitch.check(r >= l && r <= h && (floating || Number.isInteger(r)), "random() stays in range");
  }
});
