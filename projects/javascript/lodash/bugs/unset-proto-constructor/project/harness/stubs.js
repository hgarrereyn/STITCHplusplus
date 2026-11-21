// lodash: deep get/set/merge utilities on plain objects.
// The runtime reports any property that a call adds to Object.prototype (prototype pollution).
const stitch = require("stitch");
const { Borrow, Take, New, Str, I32, OneOf, Bool, Src } = stitch;
const _ = require("lodash");

const Obj = stitch.object("Obj");
const PropPath = stitch.datatype("PropPath");

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

stitch.endpoint("update", { o: Borrow(Obj), path: Str(64, PropPath) }, null, ({ o, path }) => {
  sane(path);
  _.update(o, path, (x) => (typeof x === "number" ? x + 1 : 0));
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

stitch.endpoint("merge", { a: Borrow(Obj), b: Borrow(Obj), op: OneOf("merge", "mergeWith", "defaultsDeep", "assign", "defaults", "assignIn") },
  null, ({ a, b, op }) => {
    if (op === "mergeWith") _.mergeWith(a, b, (x, y) => (Array.isArray(x) ? x.concat(y) : undefined));
    else _[op](a, b);
  }, { bailsOn: [badLength] });

stitch.endpoint("parse_json_into", { o: Borrow(Obj), text: Str(128), op: OneOf("merge", "defaultsDeep", "assign") }, null,
  ({ o, text, op }) => {
    let src;
    try {
      src = JSON.parse(text);
    } catch (e) {
      stitch.bail("not JSON");
    }
    _[op](o, src);
  }, { bailsOn: [badLength] });

stitch.endpoint("clone", { o: Borrow(Obj), deep: Bool }, New(Obj), ({ o, deep }) => {
  const c = deep ? _.cloneDeep(o) : _.clone(o);
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
  // a path starting at an inherited built-in ("toString.x") writes into that shared object: the
  // runtime reports it as prototype pollution after the call
  const inherited = p[0] in {};
  const o = _.set({}, p, 1);
  stitch.check(_.get(o, p) === 1 || inherited || p.some((x) => ["__proto__", "constructor", "prototype"].includes(x)), "get finds what set wrote");
});
