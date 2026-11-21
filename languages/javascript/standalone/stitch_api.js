// ---------------------------------------------------------------------------------------------
// Standalone stand-in for the STITCH++ runtime (written next to repro.js by `stitch write`).
// The harness code runs unchanged: stitch.endpoint records the stubs, stitch.bail/check and
// attributes work without the fuzzer, and Source replays the recorded choices.
//   await stitch.call(n, name, args)        a call that succeeded in the original run
//   await stitch.callBails(n, name, args)   a call that bailed in the original run
// STITCH_REPRO_TRACE=1 prints one line per call.
// ---------------------------------------------------------------------------------------------
class Bail extends Error {
  constructor(reason = "bail") {
    super(reason);
    this.reason = reason;
  }
}
class CheckFailed extends Error {
  constructor(what) {
    super(what);
    this.what = what;
  }
}
const stubs = new Map();
const attrs = new WeakMap();
const globals = new Map();
const cur = [-1, "?"];
const TRACE = !!process.env.STITCH_REPRO_TRACE;
const spec = () => ({ optional() { return this; } });
const named = (name) => ({ name });

function attrMap(o) {
  if (!attrs.has(o)) attrs.set(o, new Map());
  return attrs.get(o);
}

function diverged(why) {
  process.stderr.write(`\n[repro] DIVERGED at call #${cur[0]} ${cur[1]}: ${why}\n`);
  process.exit(99);
}

function invoke(name, args) {
  const s = stubs.get(name);
  if (!s) throw new Error(`unknown stub ${name}`);
  return s.named ? s.fn(args) : s.fn(...args);
}

function expected(name, e) {
  const s = stubs.get(name);
  return (s.bailsOn || []).some((b) => (typeof b === "function" && b.prototype ? e instanceof b : typeof b === "function" ? b(e) : false));
}

module.exports = {
  Source,
  Bail,
  endpoint(name, args, returns, fn, opts = {}) {
    stubs.set(name, { fn, named: !Array.isArray(args), bailsOn: opts.bailsOn || [] });
  },
  object: named,
  datatype: named,
  decoder: (dt, fn) => fn,
  init: (fn) => fn(),
  bail(reason = "bail") {
    throw new Bail(reason);
  },
  require(cond, reason = "requirement failed") {
    if (!cond) throw new Bail(reason);
  },
  check(cond, what = "check") {
    if (!cond) throw new CheckFailed(what);
  },
  log: (...a) => {
    if (TRACE) process.stderr.write(`[repro]     log: ${a.map(String).join(" ")}\n`);
  },
  consume() {},
  set: (o, k, v) => attrMap(o).set(k, v),
  get: (o, k, def) => (attrMap(o).has(k) ? attrMap(o).get(k) : def),
  getObj: (o, k) => (attrMap(o).has(k) ? attrMap(o).get(k) : null),
  has: (o, k) => attrMap(o).has(k),
  del: (o, k) => attrMap(o).delete(k),
  globalSet: (k, v) => globals.set(k, v),
  globalGet: (k, def) => (globals.has(k) ? globals.get(k) : def),
  file(data) {
    const p = require("path").join(require("os").tmpdir(), `stitch-repro-${process.pid}-${Date.now()}`);
    require("fs").writeFileSync(p, data);
    return p;
  },
  async call(n, name, args) {
    cur[0] = n;
    cur[1] = name;
    let r;
    try {
      r = await invoke(name, args);
    } catch (e) {
      if (e instanceof Bail) diverged(`bailed: "${e.reason}" (it succeeded in the original run)`);
      throw e;
    }
    if (TRACE) process.stderr.write(`[repro] #${n} ${name} -> ok\n`);
    return r;
  },
  async callBails(n, name, args) {
    cur[0] = n;
    cur[1] = name;
    try {
      await invoke(name, args);
    } catch (e) {
      if (!(e instanceof Bail) && !expected(name, e)) throw e;
    }
    if (TRACE) process.stderr.write(`[repro] #${n} ${name} -> bail\n`);
  },
  // run the program; report a crash in the same format as the fuzzer
  run(main) {
    // shared built-ins every object reaches (as the fuzzer's runtime checks): added, replaced or
    // deleted properties
    const watched = [["Object.prototype", Object.prototype], ["Array.prototype", Array.prototype],
      ["Function.prototype", Function.prototype], ["Object", Object]].concat(Object.getOwnPropertyNames(Object.prototype)
      .map((k) => [`Object.prototype.${k}`, Object.getOwnPropertyDescriptor(Object.prototype, k).value])
      .filter(([, v]) => typeof v === "function" && v !== Object));
    const base = watched.map(([n, o]) => [n, o, new Map(Reflect.ownKeys(o).map((k) => [k, Object.getOwnPropertyDescriptor(o, k)]))]);
    const same = (a, b) => a && b && a.value === b.value && a.get === b.get && a.set === b.set;
    Promise.resolve().then(main).then(() => {
      for (const [n, o, props] of base) {
        const added = Reflect.ownKeys(o).find((k) => !props.has(k));
        const changed = [...props].find(([k, d]) => !same(Object.getOwnPropertyDescriptor(o, k), d));
        const what = added !== undefined ? `${n}.${String(added)} was set`
          : changed ? `${n}.${String(changed[0])} was ${Object.prototype.hasOwnProperty.call(o, changed[0]) ? "replaced" : "deleted"}` : null;
        if (what) {
          process.stderr.write(`\n[stitch] EXCEPTION in ${cur[1]}: prototype pollution: ${what}\n`);
          process.exit(1);
        }
      }
    }, (e) => {
      if (e instanceof CheckFailed) {
        process.stderr.write(`\n[stitch] CHECK FAILED: ${e.what} (node ${cur[0]})\n`);
        process.exit(1);
      }
      const kind = e && e.constructor ? e.constructor.name : `thrown ${typeof e}`;
      const msg = String(e && e.message !== undefined ? e.message : e).replace(/\n/g, " ").slice(0, 500);
      process.stderr.write(`\n[stitch] EXCEPTION in ${cur[1]}: ${kind}: ${msg}\n`);
      let k = 0;
      for (const l of String((e && e.stack) || "").split("\n")) {
        const m = l.match(/^\s+at (?:(.*?) \()?(.+?):(\d+):\d+\)?$/);
        if (!m || m[2].startsWith("node:") || m[2] === __filename) continue;
        process.stderr.write(`    #${k++} 0x0 in ${m[1] || `anonymous@${require("path").basename(m[2])}`} ${m[2]}:${m[3]}\n`);
      }
      process.exit(1);
    });
  },
  Borrow: spec, Take: spec, New: spec, Str: spec, Bytes: spec, File: spec, Src: spec, Range: spec,
  OneOf: spec, Flags: spec, Bool: spec(), U8: spec(), U16: spec(), U32: spec(), U64: spec(), I8: spec(),
  I16: spec(), I32: spec(), I64: spec(), BigU64: spec(), BigI64: spec(), F32: spec(), F64: spec(),
};
