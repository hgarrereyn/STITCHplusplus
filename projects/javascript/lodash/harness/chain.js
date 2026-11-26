// lodash wrapper chains: _(array) and _.chain(array) evaluate array sequences lazily (LazyWrapper:
// shortcut fusion of map/filter/take/drop/slice/reverse...). Oracle: a chain gives the same result
// as applying the eager functions one after the other, and plant/commit/next/iteration agree.
// Iteratees take one argument, so lodash may take the lazy path.
const stitch = require("stitch");
const { Borrow, Val } = stitch;
const _ = require("lodash");
const { List, same, show } = require("./common");

const ChainOps = stitch.datatype("ChainOps");

const MAPS = {
  double: (x) => (typeof x === "number" ? x * 2 : x),
  str: (x) => typeof x,
  wrap: (x) => [x],
  len: (x) => (x == null ? -1 : x.length),
};
const PREDS = {
  truthy: (x) => Boolean(x),
  num: (x) => typeof x === "number",
  small: (x) => typeof x === "number" && x < 5,
  str: (x) => typeof x === "string",
};
const sortKey = (x) => (typeof x === "number" && x === x ? x : -1);
const WHILE = ["filter", "reject", "takeWhile", "dropWhile", "takeRightWhile", "dropRightWhile"];
const TAKES = ["take", "takeRight", "drop", "dropRight"];
const NOARG = ["reverse", "initial", "tail", "compact", "toArray", "uniq", "flatten", "tap", "thru"];
const OTHER = ["sortBy", "push", "unshift", "sort", "splice", "concat", "invokeMap"];
const TERMINAL = ["head", "first", "last", "find", "findLast", "pop", "shift", "join", "size", "every", "some", "includes"];

stitch.decoder(ChainOps, (s) => {
  const ops = [];
  while (s.more() && ops.length < 8) {
    const c = s.choice(10);
    if (c < 3) ops.push(["map", s.pick(Object.keys(MAPS))]);
    else if (c < 5) ops.push([s.pick(WHILE), s.pick(Object.keys(PREDS))]);
    else if (c < 7) ops.push([s.pick(TAKES), s.coin(0.85) ? s.range(0, 12) : s.pick([undefined, -1, 199, 250, 1e9])]);
    else if (c === 7) ops.push(["slice", s.range(-5, 12), s.coin(0.3) ? undefined : s.range(-5, 30)]);
    else if (c === 8) ops.push([s.pick(NOARG)]);
    else ops.push([s.pick(OTHER), s.range(-2, 5), s.range(0, 3)]);
  }
  if (s.coin(0.35)) ops.push([s.pick(TERMINAL), s.pick(Object.keys(PREDS)), s.range(0, 3)]);
  return ops;
});

// the wrapper method call for one op
function wrapped(w, [name, a, b]) {
  if (name === "map") return w.map(MAPS[a]);
  if (WHILE.includes(name) || ["find", "findLast", "every", "some"].includes(name)) return w[name](PREDS[a]);
  if (TAKES.includes(name)) return w[name](a);
  if (name === "slice") return w.slice(a, b);
  if (name === "tap") return w.tap(() => {});
  if (name === "thru") return w.thru((v) => v);
  if (name === "sortBy") return w.sortBy(sortKey);
  if (name === "push" || name === "unshift" || name === "concat" || name === "includes") return w[name](a);
  if (name === "splice") return w.splice(a, b);
  if (name === "invokeMap") return w.invokeMap("toString");
  if (name === "join") return w.join(b === 0 ? undefined : "-");
  return w[name]();
}

// the same op on a plain array, with the standalone functions
function eager(v, [name, a, b]) {
  if (name === "map") return _.map(v, MAPS[a]);
  if (WHILE.includes(name) || ["find", "findLast", "every", "some"].includes(name)) return _[name](v, PREDS[a]);
  if (TAKES.includes(name)) return _[name](v, a);
  if (name === "slice") return _.slice(v, a, b);
  if (name === "tap" || name === "thru") return v;
  if (name === "sortBy") return _.sortBy(v, sortKey);
  if (name === "push") { v.push(a); return v; }
  if (name === "unshift") { v.unshift(a); return v; }
  if (name === "sort") return v.sort();
  if (name === "splice") return v.splice(a, b);
  if (name === "concat") return _.concat(v, a);
  if (name === "includes") return _.includes(v, a);
  if (name === "invokeMap") return _.invokeMap(v, "toString");
  if (name === "join") return _.join(v, b === 0 ? undefined : "-");
  if (name === "pop") return v.pop();
  if (name === "shift") return v.shift();
  if (name === "first") return _.head(v);
  return _[name](v);
}

const runEager = (arr, ops) => ops.reduce((v, op) => eager(v, op), arr.slice());
const build = (w, ops) => ops.reduce((x, op) => wrapped(x, op), w);

stitch.endpoint("chain", { a: Borrow(List), b: Borrow(List), ops: Val(ChainOps, 128) }, null, ({ a, b, ops }) => {
  const copy = a.slice();
  const want = runEager(a, ops);
  const terminal = ops.length > 0 && TERMINAL.includes(ops[ops.length - 1][0]);
  // implicit chaining: a terminal op returns the plain value
  const implicit = build(_(a.slice()), ops);
  const got = terminal ? implicit : implicit.value();
  if (!same(got, want)) stitch.log(`a ${show(a)}\nops ${show(ops)}\ngot  ${show(got)}\nwant ${show(want)}`);
  stitch.check(same(got, want), "_(a).ops().value() == the eager functions");
  // explicit chaining: everything stays wrapped until value()
  const explicit = build(_.chain(a.slice()), ops).value();
  stitch.check(same(explicit, want), "_.chain(a).ops().value() == the eager functions");
  if (!terminal) {
    stitch.check(same(build(_(a.slice()), ops).commit().value(), want), "commit().value() == value()");
    const it = [];
    for (const x of build(_(a.slice()), ops)) it.push(x);
    stitch.check(same(it, Array.isArray(want) ? want : _.toArray(want)), "iterating the wrapper yields value()");
    stitch.check(same(build(_(a.slice()), ops).toJSON(), want) && same(build(_(a.slice()), ops).valueOf(), want), "toJSON()/valueOf() == value()");
    const planted = build(_(a.slice()), ops).plant(b.slice()).value();
    if (!same(planted, runEager(b, ops))) stitch.log(`b ${show(b)}\nops ${show(ops)}\nplanted ${show(planted)}`);
    stitch.check(same(planted, runEager(b, ops)), "plant(b).value() == the eager functions on b");
  }
  stitch.check(same(a, copy), "chains on a copy leave the list alone");
});
