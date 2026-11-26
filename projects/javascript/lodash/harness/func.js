// lodash Function and Util functions: argument adapters (ary, rest, spread, flip, rearg, partial,
// curry, overArgs, bind...) checked against models of their documented argument handling; call
// counting (once, before, after, debounce, throttle); memoize and its MapCache (has/get/set/delete/
// clear/size) against a Map, with keys such as "__proto__", NaN, -0, null and objects; range,
// times, flow, over*, cond, iteratee, matches*, attempt, runInContext/mixin.
const stitch = require("stitch");
const { Borrow, New, OneOf, Range, Src, Val, Str } = stitch;
const _ = require("lodash");
const { Memo, ListVal, same, svz, show, prim } = require("./common");

const f3 = function (a, b, c) { return Array.from(arguments); };  // eslint-disable-line no-unused-vars
const TRANSFORMS = [(x) => typeof x, (x) => String(x), (x) => [x], (x) => (x == null ? 0 : 1)];
const PREDS = [(x) => Boolean(x), (x) => typeof x === "number", (x) => typeof x === "string", (x) => x == null];

function reorder(args, idx) {
  const out = args.slice(), n = Math.min(idx.length, args.length);
  for (let i = 0; i < n; i++) out[i] = Number.isInteger(idx[i]) && idx[i] >= 0 && idx[i] < args.length ? args[idx[i]] : undefined;
  return out;
}

stitch.endpoint("func_adapters", {
  op: OneOf("ary", "unary", "rest", "spread", "flip", "rearg", "partial", "partialRight", "curry", "curryRight", "overArgs", "wrap", "bind", "bindKey", "negate"),
  args: Val(ListVal, 512), n: Range(-2, 6), s: Src(32),
}, null, ({ op, args, n, s }) => {
  args = args.slice(0, 7);
  let got, want;
  switch (op) {
    case "ary": got = _.ary(f3, n)(...args); want = args.slice(0, Math.max(n, 0)); break;
    case "unary": got = _.unary(f3)(...args); want = args.slice(0, 1); break;
    case "rest": {
      const st = Math.max(n, 0);
      got = _.rest(f3, n)(...args);
      want = [...Array.from({ length: st }, (_x, i) => args[i]), args.slice(st)];
      break;
    }
    case "spread": {
      const st = Math.max(n, 0), x = args[st];
      got = _.spread(f3, n)(...args);
      want = args.slice(0, st);
      if (x) for (let i = 0; i < x.length; i++) want.push(x[i]);
      break;
    }
    case "flip": got = _.flip(f3)(...args); want = args.slice().reverse(); break;
    case "rearg": {
      const idx = Array.from({ length: s.range(0, 4) }, () => s.range(-1, 6));
      got = _.rearg(f3, idx)(...args);
      want = reorder(args, idx);
      break;
    }
    case "partial": case "partialRight": {
      const partials = Array.from({ length: s.range(0, 4) }, () => (s.coin(0.4) ? _ : prim(s)));
      const holders = partials.map((p, i) => (p === _ ? i : -1)).filter((i) => i >= 0);
      got = _[op](f3, ...partials)(...args);
      if (op === "partial") {
        let k = 0;
        want = partials.map((p) => (p === _ ? args[k++] : p)).concat(args.slice(holders.length));
      } else {
        const r = Math.max(args.length - holders.length, 0);
        want = args.slice(0, r).concat(partials);
        holders.forEach((h, j) => { want[r + h] = args[r + j]; });
      }
      break;
    }
    case "curry": case "curryRight": {
      while (args.length < 3) args.push(prim(s));
      // groups whose running total stays below the arity until the last one
      const groups = [];
      let i = 0;
      while (i < args.length) {
        const room = 2 - i;
        const size = room > 0 && s.coin(0.6) ? s.range(1, room) : args.length - i;
        groups.push(args.slice(i, i + size));
        i += size;
      }
      let fn = _[op](f3);
      for (const g of groups) fn = fn(...g);
      got = fn;
      want = op === "curry" ? args : groups.slice().reverse().flat(1);
      break;
    }
    case "overArgs": {
      const ts = Array.from({ length: s.range(0, 3) }, () => s.pick(TRANSFORMS));
      got = _.overArgs(f3, ts)(...args);
      want = args.map((x, i) => (i < ts.length ? ts[i](x) : x));
      break;
    }
    case "wrap": got = _.wrap(args[0], f3)(...args.slice(1)); want = [args[0], ...args.slice(1)]; break;
    case "bind": {
      const self = { self: true }, k = s.range(0, args.length);
      got = _.bind(function () { return [this, ...arguments]; }, self, ...args.slice(0, k))(...args.slice(k));
      want = [self, ...args];
      break;
    }
    case "bindKey": {
      const obj = { m() { return ["old"]; } }, k = s.range(0, args.length);
      const bound = _.bindKey(obj, "m", ...args.slice(0, k));
      obj.m = function () { return [this === obj, ...arguments]; };   // late bound
      got = bound(...args.slice(k));
      want = [true, ...args];
      break;
    }
    default: {
      const p = s.pick(PREDS);
      got = args.map(_.negate(p));
      want = args.map((x) => !p(x));
    }
  }
  if (!same(got, want)) stitch.log(`args ${show(args)}\ngot  ${show(got)}\nwant ${show(want)}`);
  stitch.check(same(got, want), `${op}() passes the arguments as documented`);
});

stitch.endpoint("call_limits", { op: OneOf("once", "before", "after", "debounce", "throttle", "defer", "delay"), n: Range(-1, 5), k: Range(0, 6) }, null, ({ op, n, k }) => {
  let calls = 0;
  const g = (x) => { calls++; return [x, calls]; };
  const results = [];
  if (op === "defer" || op === "delay") {
    const id = op === "defer" ? _.defer(g, 1) : _.delay(g, 50, 1);
    clearTimeout(id);
    return stitch.check(calls === 0, `${op}() does not call synchronously`);
  }
  let fn;
  if (op === "once") fn = _.once(g);
  else if (op === "before") fn = _.before(n, g);
  else if (op === "after") fn = _.after(n, g);
  else if (op === "debounce") fn = _.debounce(g, 10000, { leading: true, trailing: false });
  else fn = _.throttle(g, 10000);
  for (let i = 1; i <= k; i++) results.push(fn(i));
  let want;
  if (op === "once") want = Math.min(k, 1);
  else if (op === "before") want = n >= 1 ? Math.min(k, n - 1) : 0;
  else if (op === "after") want = Array.from({ length: k }, (_x, i) => i + 1).filter((i) => i >= n).length;
  else want = Math.min(k, 1);
  stitch.check(calls === want, `${op}() calls the function ${want} times (got ${calls})`);
  if (op === "once" || op === "debounce" || op === "throttle") stitch.check(results.every((r) => r === results[0]), `${op}() returns the first result`);
  if (fn.cancel) {
    fn.cancel();
    stitch.check(fn.flush() === results[results.length - 1], `${op}.flush() after cancel() returns the last result`);
  }
});

// ---- memoize and its MapCache ----
const OBJ_A = { a: 1 }, OBJ_B = [1];
const KEYS = [0, -0, 1, "1", NaN, "NaN", null, "null", undefined, "undefined", true, "true", "__proto__", "constructor", "hasOwnProperty",
  "toString", "valueOf", "", "__data__", "size", Symbol.for("k"), Symbol.iterator, OBJ_A, OBJ_B, 2 ** 53, -1.5];
const KEY = Range(0, KEYS.length - 1);

stitch.endpoint("memo_new", { resolver: OneOf("first", "second", "joined") }, New(Memo), ({ resolver }) => {
  const state = { calls: 0 };
  const f = (x, y) => { state.calls++; return { x, y, n: state.calls }; };
  const r = { first: undefined, second: (x, y) => y, joined: (x, y) => `${typeof x}:${String(x)}|${typeof y}` }[resolver];
  const m = _.memoize(f, r);
  stitch.set(m, "state", state);
  stitch.set(m, "model", new Map());
  stitch.set(m, "resolver", resolver);
  return m;
});

const cacheKey = (resolver, x, y) => (resolver === "first" ? x : resolver === "second" ? y : `${typeof x}:${String(x)}|${typeof y}`);

stitch.endpoint("memo_call", { m: Borrow(Memo), x: KEY, y: KEY }, null, ({ m, x, y }) => {
  const state = stitch.get(m, "state"), model = stitch.get(m, "model");
  const key = cacheKey(stitch.get(m, "resolver"), KEYS[x], KEYS[y]);
  const before = state.calls, hit = model.has(key);
  const r = m(KEYS[x], KEYS[y]);
  if (hit) {
    stitch.check(state.calls === before && r === model.get(key), "memoized call returns the cached result");
  } else {
    stitch.check(state.calls === before + 1, "a new key calls the function");
    model.set(key, r);
  }
  stitch.check(m.cache.size === model.size, "cache size counts the keys");
});

stitch.endpoint("memo_cache", { m: Borrow(Memo), op: OneOf("has", "get", "set", "delete", "clear"), x: KEY, v: Range(0, 3) }, null, ({ m, op, x, v }) => {
  const model = stitch.get(m, "model"), key = KEYS[x], c = m.cache;
  const value = [undefined, null, "v", { v }][v];
  if (op === "has") stitch.check(c.has(key) === model.has(key), "MapCache#has agrees with a Map");
  else if (op === "get") stitch.check(c.get(key) === model.get(key), "MapCache#get agrees with a Map");
  else if (op === "set") { stitch.check(c.set(key, value) === c, "MapCache#set returns the cache"); model.set(key, value); }
  else if (op === "delete") stitch.check(c.delete(key) === model.delete(key), "MapCache#delete agrees with a Map");
  else { c.clear(); model.clear(); }
  stitch.check(c.size === model.size, `MapCache size agrees with a Map after ${op}`);
  stitch.check(c.has(key) === model.has(key) && c.get(key) === model.get(key), `MapCache agrees with a Map after ${op}`);
});

// ---- Util ----
stitch.endpoint("ranges", { start: Range(-5, 5), end: Range(-5, 25), step: OneOf(undefined, 0, 1, -1, 2, 0.5, -0.5, 3, 0.1), two: stitch.Bool }, null, ({ start, end, step, two }) => {
  const r = two ? _.range(start, end, step) : _.range(end);
  const rr = two ? _.rangeRight(start, end, step) : _.rangeRight(end);
  stitch.check(same(rr, r.slice().reverse()), "rangeRight() == reverse(range())");
  const [a, b] = two ? [start, end] : [0, end];
  const st = two && step !== undefined ? step : (a < b ? 1 : -1);
  stitch.check(r.length === Math.max(Math.ceil((b - a) / (st || 1)), 0), "range() length is ceil((end - start) / step)");
  if (r.length) stitch.check(r[0] === a, "range() starts at start");
  if (!two && end >= 0) stitch.check(same(_.times(end, (i) => i), r), "times(n, identity) == range(n)");
});

stitch.endpoint("util", { args: Val(ListVal, 512), n: Range(-3, 6), text: Str(64), s: Src(32) }, null, ({ args, n, text, s }) => {
  args = args.slice(0, 6);
  const x = args.length ? args[0] : prim(s);
  const [t1, t2] = [s.pick(TRANSFORMS), s.pick(TRANSFORMS)];
  stitch.check(same(_.flow([t1, t2])(x), t2(t1(x))) && same(_.flowRight([t1, t2])(x), t1(t2(x))), "flow/flowRight compose");
  stitch.check(same(_.over(TRANSFORMS)(x), TRANSFORMS.map((t) => t(x))), "over() calls every function");
  stitch.check(_.overEvery(PREDS.slice(0, 2))(x) === PREDS.slice(0, 2).every((p) => p(x)) && _.overSome(PREDS)(x) === PREDS.some((p) => p(x)), "overEvery/overSome");
  const pairs = PREDS.map((p, i) => [p, () => i]);
  const hit = PREDS.findIndex((p) => p(x));
  stitch.check(_.cond(pairs)(x) === (hit < 0 ? undefined : hit), "cond() returns the first matching branch");
  stitch.check(svz(_.constant(x)(), x) && svz(_.identity(x), x) && _.noop(x) === undefined, "constant/identity/noop");
  stitch.check(same(_.stubArray(), []) && _.stubArray() !== _.stubArray() && _.stubString() === "" && _.stubTrue() && !_.stubFalse() && _.isEqual(_.stubObject(), {}), "stub*()");
  stitch.check(svz(_.defaultTo(x, "d"), x == null || x !== x ? "d" : x), "defaultTo()");
  stitch.check(svz(_.nthArg(n)(...args), args.at(n)), "nthArg(n) picks the nth argument");
  stitch.check(svz(_.property(n)(args), _.get(args, n)) && svz(_.propertyOf(args)(n), _.get(args, n)), "property/propertyOf == get");
  stitch.check(_.iteratee(t1) === t1 && svz(_.iteratee("length")(args), args.length), "iteratee(function/property)");
  const src = { k: x };
  const objs = args.filter((a) => a !== null && typeof a === "object");
  for (const o of objs) {
    stitch.check(_.iteratee(src)(o) === _.isMatch(o, src) && _.matches(src)(o) === _.isMatch(o, src), "iteratee(object) == matches == isMatch");
    stitch.check(_.iteratee(["k", x])(o) === _.matchesProperty("k", x)(o), "iteratee([path, value]) == matchesProperty");
    if (_.isMatch(o, src)) stitch.check(_.matchesProperty("k", x)(o), "isMatch(o, {k: x}) implies matchesProperty('k', x)");
  }
  if (x != null) stitch.check(_.method("toString")(x) === x.toString() && _.methodOf(x)("toString") === x.toString(), "method/methodOf invoke");
  const t0 = Date.now(), now = _.now();
  stitch.check(now >= t0 && now <= Date.now(), "now() is the current time");
  const id1 = _.uniqueId(text), id2 = _.uniqueId(text);
  stitch.check(id1 !== id2 && id1.startsWith(text) && id2.startsWith(text), "uniqueId() is unique and prefixed");
  let parsed = true;
  try { JSON.parse(text); } catch (e) { parsed = false; }
  stitch.check(_.isError(_.attempt(JSON.parse, text)) === !parsed, "attempt() returns the error iff the function throws");
});

stitch.endpoint("context", { name: OneOf("triple", "map", "__proto__", "constructor", "chain", "x y") }, null, ({ name }) => {
  const ctx = _.runInContext();
  stitch.check(ctx !== _ && ctx.VERSION === _.VERSION, "runInContext() makes a new lodash");
  ctx.mixin({ [name]: (v) => v * 3 });
  if (!["__proto__"].includes(name)) stitch.check(ctx[name](2) === 6, "mixin() adds a function");
  if (name === "triple") stitch.check(_.triple === undefined && ctx([2]).map((v) => v + 1).value()[0] === 3, "mixin() on a context leaves _ alone");
  stitch.check(ctx.noConflict() === ctx, "noConflict() returns the context");
  const obj = { tag: "o", m() { return this && this.tag; } };
  _.mixin(obj, { extra: () => 1 });
  stitch.check(obj.extra() === 1, "mixin(object, source) adds to the object");
  _.bindAll(obj, ["m"]);
  const m = obj.m;
  stitch.check(m() === "o", "bindAll() binds methods to the object");
});
