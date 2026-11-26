// lodash: deep get/set/merge utilities and the Object functions on plain objects.
// The runtime reports any property that a call adds to Object.prototype (prototype pollution).
// Other files: arrays.js (Array, Collection, Math), chain.js (lazy wrapper chains), lang.js (type
// checks, conversions, clone/isEqual), strings.js (String, template), func.js (Function, Util).
const stitch = require("stitch");
const { Borrow, New, Str, OneOf, Src } = stitch;
const _ = require("lodash");
const { Obj, PropPath, svz, same } = require("./common");

stitch.decoder(PropPath, (s) => {
  const segs = [];
  while (s.more() || segs.length === 0) {
    const seg = s.coin(0.6) ? s.token() : s.pick(["a", "b", "c", "0", "1", s.str(4)]);
    segs.push(segs.length && s.coin(0.2) ? `[${JSON.stringify(seg)}]` : seg);
    if (segs.length > 6) break;
  }
  return segs.reduce((p, x) => (p === "" ? x : x.startsWith("[") ? p + x : `${p}.${x}`), "");
});

const VALUES = [1, "v", null, true, [], {}, [1, 2], { k: 1 }, () => 1, Object.create(null)];
const value = (i) => {
  const v = VALUES[i];
  return typeof v === "object" && v !== null ? _.cloneDeep(v) : v;
};
const IDX = OneOf(...VALUES.map((_v, i) => i));

stitch.endpoint("new_obj", { kind: OneOf("object", "array", "nested") }, New(Obj), ({ kind }) =>
  kind === "array" ? [1, { a: 2 }] : kind === "nested" ? { a: { b: { c: 1 } }, d: [0, { e: 2 }] } : {});

// a numeric segment such as 777757777 makes a sparse array that long, which later calls walk
// element by element (memory exhaustion by request, not a bug)
const sane = (path) => stitch.require(!_.toPath(path).some((x) => /^\d{5,}$/.test(x)), "huge array index");

// assigning an array's length to a non-length value is a RangeError in JS itself
const badLength = (e) => e instanceof RangeError && /Invalid array length/.test(e.message);

stitch.endpoint("set", { o: Borrow(Obj), path: Str(64, PropPath), v: IDX }, null, ({ o, path, v }) => {
  sane(path);
  _.set(o, path, value(v));
}, { bailsOn: [badLength] });

stitch.endpoint("set_with", { o: Borrow(Obj), path: Str(64, PropPath), v: IDX }, null, ({ o, path, v }) => {
  sane(path);
  _.setWith(o, path, value(v), Object);
}, { bailsOn: [badLength] });

stitch.endpoint("update", { o: Borrow(Obj), path: Str(64, PropPath), withObject: stitch.Bool }, null, ({ o, path, withObject }) => {
  sane(path);
  const bump = (x) => (typeof x === "number" ? x + 1 : 0);
  if (withObject) _.updateWith(o, path, bump, Object);
  else _.update(o, path, bump);
}, { bailsOn: [badLength] });

stitch.endpoint("unset", { o: Borrow(Obj), path: Str(64, PropPath) }, null, ({ o, path }) => {
  _.unset(o, path);
});

stitch.endpoint("get_has", { o: Borrow(Obj), path: Str(64, PropPath) }, null, ({ o, path }) => {
  const v = _.get(o, path);
  const p = _.toPath(path);
  const parent = p.length > 1 ? _.get(o, p.slice(0, -1)) : o; // has() is true for a key holding undefined
  const holdsUndefined = parent != null && p.length > 0 && parent[p[p.length - 1]] === undefined;
  stitch.check(!_.has(o, path) || v !== undefined || holdsUndefined, "has() implies get() finds it");
});

stitch.endpoint("zip_object_deep", { p1: Str(64, PropPath), p2: Str(64, PropPath), v: IDX }, New(Obj), ({ p1, p2, v }) =>
  sane(p1) || sane(p2) || _.zipObjectDeep([p1, p2], [value(v), value(v)]), { bailsOn: [badLength] });

stitch.endpoint("from_pairs_set", { path: Str(64, PropPath), v: IDX }, New(Obj), ({ path, v }) =>
  sane(path) || _.set({}, _.toPath(path), value(v)), { bailsOn: [badLength] });

stitch.endpoint("merge", { a: Borrow(Obj), b: Borrow(Obj), op: OneOf("merge", "mergeWith", "defaultsDeep", "assign", "defaults", "assignIn", "assignWith", "assignInWith", "extend", "extendWith") },
  null, ({ a, b, op }) => {
    if (op === "mergeWith") _.mergeWith(a, b, (x, y) => (Array.isArray(x) ? x.concat(y) : undefined));
    else _[op](a, b);
  }, { bailsOn: [badLength] });

// JSON documents with the keys that matter for merging (__proto__, constructor, prototype)
const JsonText = stitch.datatype("JsonText");
stitch.decoder(JsonText, (s) => {
  const KEYS = ["__proto__", "constructor", "prototype", "a", "b", "0", "length", "toString", "polluted"];
  const val = (d) => {
    const c = s.choice(d < 3 ? 6 : 3);
    if (c === 0) return s.pick([1, 0, -1, true, false, null, "x", ""]);
    if (c === 1) return s.str(4);
    if (c === 2) return s.range(-3, 3);
    if (c === 3) return Array.from({ length: s.choice(3) }, () => val(d + 1));
    const kv = Array.from({ length: s.range(1, 3) }, () => `${JSON.stringify(s.coin(0.8) ? s.pick(KEYS) : s.str(4))}:${JSON.stringify(val(d + 1))}`);
    return JSON.parse(`{${kv.join(",")}}`);
  };
  const text = JSON.stringify(val(0));
  return s.coin(0.05) ? text + s.str(4) : text;
});

stitch.endpoint("parse_json_into", { o: Borrow(Obj), text: Str(512, JsonText), op: OneOf("merge", "defaultsDeep", "assign", "mergeWith", "assignIn", "defaults") }, null,
  ({ o, text, op }) => {
    let src;
    try {
      src = JSON.parse(text);
    } catch (e) {
      stitch.bail("not JSON");
    }
    _[op](o, src);
  }, { bailsOn: [badLength] });

stitch.endpoint("clone", { o: Borrow(Obj), op: OneOf("clone", "cloneDeep", "cloneWith", "cloneDeepWith", "toPlainObject") }, New(Obj), ({ o, op }) => {
  // a customizer that returns undefined leaves the cloning to lodash
  const c = op.endsWith("With") ? _[op](o, () => undefined) : _[op](o);
  if (op === "toPlainObject") {
    stitch.check(_.isPlainObject(c) && same(_.keysIn(c).sort(), _.uniq(_.keysIn(o)).sort()), "toPlainObject() copies the enumerable keys");
    return c;
  }
  stitch.check(_.isEqual(c, o), "clone isEqual original");
  return c;
});

stitch.endpoint("pick_omit", { o: Borrow(Obj), path: Str(64, PropPath), op: OneOf("pick", "omit", "at", "invoke", "result") },
  New(Obj), ({ o, path, op }) => {
    if (op === "invoke") {
      const f = _.get(o, path);
      stitch.require(f == null || typeof f === "function", "invoke: not a method"); // calling a non-function is the caller's error
    }
    const r = op === "invoke" ? _.invoke(o, path) : _[op](o, [path]);
    return r !== null && typeof r === "object" ? r : { r };
  }, { bailsOn: [badLength] });

stitch.endpoint("to_path_roundtrip", { path: Str(64, PropPath) }, null, ({ path }) => {
  const p = _.toPath(path);
  stitch.require(p.length > 0, "empty path"); // set() with no path writes nothing
  sane(path);
  // a path through an inherited built-in ("toString.x", "a.valueOf.length") writes into that
  // shared object (the runtime reports it as prototype pollution after the call), or fails on a
  // read-only property
  const o = _.set({}, p, 1);
  let inherited = false;
  for (let i = 0, cur = o; i < p.length && cur != null; cur = cur[p[i++]]) {
    const box = Object(cur);
    if (p[i] in box && !Object.prototype.hasOwnProperty.call(box, p[i])) inherited = true;
  }
  stitch.check(_.get(o, p) === 1 || inherited || p.some((x) => ["__proto__", "constructor", "prototype"].includes(x)), "get finds what set wrote");
});

// ---- Object functions: keys, iteration, inversion, mapping, partitioning ----

const isNum = (x) => typeof x === "number";
// no array in v carries non-index own properties (set() on an array can add them)
const plainArrays = (v, seen = new Set()) => {
  if (v === null || typeof v !== "object" || seen.has(v)) return true;
  seen.add(v);
  if (Array.isArray(v) && Object.keys(v).some((k) => !/^(?:0|[1-9]\d*)$/.test(k))) return false;
  return Object.keys(v).every((k) => plainArrays(v[k], seen));
};
// path keys that set/pickBy refuse to write (the prototype-pollution guards)
const GUARDED = ["__proto__", "constructor", "prototype"];

stitch.endpoint("keys_values", { o: Borrow(Obj) }, null, ({ o }) => {
  const k = _.keys(o), v = _.values(o), p = _.toPairs(o);
  if (!_.isArrayLike(o)) stitch.check(same(k, Object.keys(o)), "keys() == Object.keys() for a non-array-like object");
  stitch.check(v.length === k.length && v.every((x, i) => svz(x, o[k[i]])), "values() follows keys()");
  stitch.check(p.length === k.length && p.every(([a, b], i) => a === k[i] && svz(b, v[i])), "toPairs() follows keys()");
  stitch.check(same(_.entries(o), p), "entries() == toPairs()");
  const ki = _.keysIn(o), vi = _.valuesIn(o);
  stitch.check(k.every((x) => ki.includes(x)), "keysIn() includes keys()");
  stitch.check(vi.length === ki.length && same(_.toPairsIn(o).map((e) => e[0]), ki), "valuesIn()/toPairsIn() follow keysIn()");
  stitch.check(_.findKey(o, isNum) === k.find((x) => isNum(o[x])), "findKey() is the first matching key");
  stitch.check(_.findLastKey(o, isNum) === k.slice().reverse().find((x) => isNum(o[x])), "findLastKey() is the last matching key");
  const seen = (fn, obj) => { const out = []; fn(obj, (_v, key) => { out.push(String(key)); }); return out; };
  stitch.check(same(seen(_.forOwn, o), k), "forOwn() visits keys() in order");
  stitch.check(same(seen(_.forOwnRight, o), k.slice().reverse()), "forOwnRight() visits keys() backwards");
  stitch.check(same(seen(_.forIn, o), ki), "forIn() visits keysIn() in order");
  stitch.check(same(seen(_.forInRight, o), ki.slice().reverse()), "forInRight() visits keysIn() backwards");
  stitch.check(same(_.functions(o), k.filter((x) => typeof o[x] === "function")), "functions() lists the function-valued keys");
  stitch.check(same(_.functionsIn(o), ki.filter((x) => typeof o[x] === "function")), "functionsIn() lists the function-valued keysIn");
  stitch.check(_.size(o) === (_.isArrayLike(o) ? o.length : k.length), "size() counts keys");
});

const keyOf = (v) => (v != null && typeof v.toString !== "function" ? Object.prototype.toString.call(v) : String(v));
// a value whose string conversion throws (an array holding a null-prototype object) is JS's own TypeError
const unconvertible = (e) => e instanceof TypeError && /Cannot convert object to primitive/.test(e.message);

stitch.endpoint("invert", { o: Borrow(Obj) }, New(Obj), ({ o }) => {
  const k = _.keys(o);
  const inv = _.invert(o);
  for (const r of Object.keys(inv)) stitch.check(keyOf(o[inv[r]]) === r, "invert(): o[inv[r]] converts back to r");
  const by = _.invertBy(o);
  const n = Object.keys(by).reduce((t, r) => t + by[r].length, 0);
  stitch.check(n === k.length, "invertBy() puts every key in exactly one group");
  for (const r of Object.keys(by)) for (const key of by[r]) stitch.check(keyOf(o[key]) === r, "invertBy() groups keys by value");
  return inv;
}, { bailsOn: [unconvertible] });

stitch.endpoint("map_partition", { o: Borrow(Obj), pred: OneOf("number", "object", "truthy") }, New(Obj), ({ o, pred }) => {
  const p = { number: isNum, object: (x) => x !== null && typeof x === "object", truthy: Boolean }[pred];
  const k = _.keys(o);
  const mv = _.mapValues(o, (x) => x);
  stitch.check(same(_.keys(mv).sort(), k.slice().sort()) && k.every((x) => svz(mv[x], o[x])), "mapValues(identity) keeps keys and values");
  const mk = _.mapKeys(o, (_v, key) => key);
  stitch.check(same(_.keys(mk).sort(), k.slice().sort()), "mapKeys(identity) keeps keys");
  const picked = _.keys(_.pickBy(o, p)), omitted = _.keys(_.omitBy(o, p));
  stitch.check(!picked.some((x) => omitted.includes(x)), "pickBy() and omitBy() are disjoint");
  const all = _.keysIn(o).filter((x) => !GUARDED.includes(x));
  stitch.check(same(_.uniq([...picked, ...omitted]).sort(), _.uniq(all).sort()), "pickBy() and omitBy() partition the keys");
  const visited = _.transform(o, (acc, _v, key) => { acc.push(String(key)); }, []);
  stitch.check(same(visited, Array.isArray(o) ? Array.from({ length: o.length }, (_x, i) => String(i)) : k), "transform() visits every key");
  return mv;
});

stitch.endpoint("has_in", { o: Borrow(Obj), path: Str(64, PropPath) }, null, ({ o, path }) => {
  if (_.has(o, path)) stitch.check(_.hasIn(o, path), "has() implies hasIn()");
});

stitch.endpoint("create", { proto: Borrow(Obj), props: Borrow(Obj) }, New(Obj), ({ proto, props }) => {
  const r = _.create(proto, props);
  stitch.check(Object.getPrototypeOf(r) === proto, "create() sets the prototype");
  for (const k of _.keys(props)) stitch.check(Object.prototype.hasOwnProperty.call(r, k) && svz(r[k], props[k]), "create() assigns the own properties");
  return r;
});

stitch.endpoint("match", { o: Borrow(Obj), src: Borrow(Obj) }, null, ({ o, src }) => {
  const m = _.isMatch(o, src);
  // matches() deep-clones its source first (a clone drops an array's non-index properties)
  stitch.check(_.matches(src)(o) === _.isMatch(o, _.cloneDeep(src)), "matches(src)(o) == isMatch(o, cloneDeep(src))");
  stitch.check(_.isMatchWith(o, src, () => undefined) === m, "isMatchWith(undefined customizer) == isMatch");
  stitch.check(_.isMatch(o, o) && _.isMatch(o, {}), "isMatch(o, o) and isMatch(o, {})");
  // (isEqual compares an array's elements only, isMatch also its other own properties)
  if (_.isEqual(o, src) && plainArrays(src)) stitch.check(m, "isEqual implies isMatch");
  const ks = _.keys(src).filter((k) => !GUARDED.includes(k));
  const conf = _.conformsTo(o, _.fromPairs(ks.map((k) => [k, isNum])));
  stitch.check(conf === ks.every((k) => k in Object(o) && isNum(o[k])), "conformsTo() checks every predicate");
  stitch.check(_.conforms(_.fromPairs(ks.map((k) => [k, isNum])))(o) === conf, "conforms(src)(o) == conformsTo(o, src)");
});
