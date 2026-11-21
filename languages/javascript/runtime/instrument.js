"use strict";
// Coverage map, comparison log and module hooks (main thread).
//
// Target files: packages listed in STITCH_JS_INCLUDE (comma separated names or paths) and the
// harness; without it, everything outside node: built-ins and the STITCH++ runtime. They are
// instrumented only while fuzzing (STITCH_FUZZING=1).
const fs = require("fs");
const path = require("path");
const Module = require("module");

const MAP_SIZE = 1 << 16; // scanned after every program: large enough for few collisions, no larger
const cov = new Uint8Array(MAP_SIZE);
const words = new Uint32Array(cov.buffer);
globalThis.__stitch_cov = cov;
const RUNTIME_DIR = __dirname;

let cmplog = false; // the engine traces this program
let inStub = false; // ...and a stub is running (not the runtime's own bookkeeping)
let busy = false; // recording uses Set/Map/String methods itself
let node = 0;
const cmps = [];
const seen = new Set();

// Promises created while a stub runs (v8 promise hook): only then can a program leave an
// unhandled rejection behind.
let promises = false;
let promiseHooks = true;
try {
  require("v8").promiseHooks.onInit(() => {
    if (inStub) promises = true;
  });
} catch (e) {
  promiseHooks = false; // no promise hooks: always check
}
function promisesSince() {
  const p = promises;
  promises = false;
  return p || !promiseHooks;
}

function beginExec() {
  cmps.length = 0;
  seen.clear();
}
function endExec() {
  cmplog = false;
}
function setNode(i) {
  node = i;
}
function setCmplog(on) {
  cmplog = on;
}
function setInStub(on) {
  inStub = on;
}

function edgesBytes() {
  const ids = [];
  for (let w = 0; w < words.length; w++) {
    if (words[w] === 0) continue;
    for (let k = 0; k < 4; k++) {
      const i = 4 * w + k;
      if (cov[i]) ids.push(i, cov[i]);
    }
    words[w] = 0;
  }
  const n = ids.length / 2;
  const b = Buffer.alloc(4 + 5 * n);
  b.writeUInt32LE(n, 0);
  for (let j = 0; j < n; j++) {
    b.writeUInt32LE(ids[2 * j], 4 + 5 * j);
    b.writeUInt8(ids[2 * j + 1], 8 + 5 * j);
  }
  return b;
}

function cmpsBytes() {
  const h = Buffer.alloc(4);
  h.writeUInt32LE(cmps.length, 0);
  return Buffer.concat([h, ...cmps]);
}

// ---------------------------------------------------------------- comparison records
function asBytes(v) {
  if (typeof v === "string") return Buffer.from(v, "utf8");
  if (Buffer.isBuffer(v) || v instanceof Uint8Array) return Buffer.from(v);
  return null;
}

function recInt(a, b) {
  const x = BigInt.asUintN(64, BigInt(a));
  const y = BigInt.asUintN(64, BigInt(b));
  const k = `${node}|i|${x}|${y}`;
  if (seen.has(k) || cmps.length >= 16384) return;
  seen.add(k);
  const r = Buffer.alloc(72);
  r.writeUInt16LE(node, 0);
  r.writeUInt8(0, 2);
  r.writeUInt8(8, 3);
  r.writeUInt8(8, 4);
  r.writeUInt8(8, 5);
  r.writeBigUInt64LE(x, 8);
  r.writeBigUInt64LE(y, 40);
  cmps.push(r);
}

function recBytes(a, b) {
  a = a.subarray(0, 32);
  b = b.subarray(0, 32);
  if (!a.length || !b.length) return;
  const k = `${node}|b|${a.toString("latin1")}|${b.toString("latin1")}`;
  if (seen.has(k) || cmps.length >= 16384) return;
  seen.add(k);
  const r = Buffer.alloc(72);
  r.writeUInt16LE(node, 0);
  r.writeUInt8(1, 2);
  r.writeUInt8(a.length, 4);
  r.writeUInt8(b.length, 5);
  a.copy(r, 8);
  b.copy(r, 40);
  cmps.push(r);
}

function record(a, b) {
  const outer = !busy;
  busy = true;
  try {
    const ta = typeof a, tb = typeof b;
    if ((ta === "number" || ta === "bigint") && (tb === "number" || tb === "bigint")) {
      if (Number.isInteger(Number(a)) && Number.isInteger(Number(b)) && a != b) recInt(a, b); // eslint-disable-line eqeqeq
      return;
    }
    const ba = asBytes(a), bb = asBytes(b);
    if (ba && bb && !ba.equals(bb)) recBytes(ba, bb);
  } catch (e) { /* never disturb the program */ } finally {
    if (outer) busy = false;
  }
}

globalThis.__stitch_cmp = function (a, b, op) {
  let r;
  switch (op) {
    case 0: r = a === b; break;
    case 1: r = a !== b; break;
    case 2: r = a == b; break; // eslint-disable-line eqeqeq
    case 3: r = a != b; break; // eslint-disable-line eqeqeq
    case 4: r = a < b; break;
    case 5: r = a <= b; break;
    case 6: r = a > b; break;
    default: r = a >= b;
  }
  if (cmplog && inStub) record(a, b);
  return r;
};

globalThis.__stitch_sw = function (d, cases) {
  if (cmplog && inStub) for (const c of cases) if (c !== d) record(d, c);
  return d;
};

// String / Set / Map lookups: compared operands of keyword tables and prefix checks
function hookBuiltins() {
  const wrap = (proto, name, fn) => {
    const orig = proto[name];
    Object.defineProperty(proto, name, {
      value: function (...args) {
        if (cmplog && inStub && !busy) {
          busy = true;
          try {
            fn(this, args);
          } catch (e) { /* ignore */ } finally {
            busy = false;
          }
        }
        return orig.apply(this, args);
      },
      writable: true, configurable: true, enumerable: false,
    });
  };
  for (const m of ["startsWith", "endsWith", "includes", "indexOf", "lastIndexOf"]) {
    wrap(String.prototype, m, (s, [x]) => { if (typeof x === "string") record(String(s), x); });
  }
  const keys = (c, k) => {
    if (typeof k !== "string" && typeof k !== "number") return;
    if (c.size > 256) return;
    let n = 0;
    for (const x of c.keys()) {
      if (typeof x === typeof k && x !== k) record(k, x);
      if (++n >= 64) break;
    }
  };
  wrap(Set.prototype, "has", (c, [k]) => keys(c, k));
  wrap(Map.prototype, "has", (c, [k]) => keys(c, k));
  wrap(Map.prototype, "get", (c, [k]) => keys(c, k));
  wrap(Array.prototype, "includes", (a, [k]) => {
    if (typeof k === "string" && a.length <= 256) for (const x of a.slice(0, 64)) if (typeof x === "string") record(k, x);
  });
}

// ---------------------------------------------------------------- module hooks
function targets(project) {
  const inc = (process.env.STITCH_JS_INCLUDE || "").split(",").map((s) => s.trim()).filter(Boolean);
  const prefixes = [];
  for (const name of inc) {
    if (name.startsWith("/")) {
      prefixes.push(fs.realpathSync(name));
      continue;
    }
    for (const base of [project, process.cwd(), "/opt", "/src"]) {
      try {
        const pj = require.resolve(`${name}/package.json`, { paths: [base] });
        prefixes.push(fs.realpathSync(path.dirname(pj)));
        break;
      } catch (e) {
        try {
          prefixes.push(fs.realpathSync(path.dirname(require.resolve(name, { paths: [base] }))));
          break;
        } catch (e2) { /* next */ }
      }
    }
  }
  return { prefixes, all: inc.length === 0, harness: path.join(project, "harness") };
}

function wanted(file, t) {
  let f;
  try {
    f = fs.realpathSync(file);
  } catch (e) {
    return false;
  }
  if (f.startsWith(RUNTIME_DIR) || f.includes("/@babel/")) return false;
  if (f.startsWith(t.harness)) return true;
  if (t.all) return true;
  return t.prefixes.some((p) => f === p || f.startsWith(p + "/"));
}

function install(project) {
  const { transform } = require("./transform");
  const t = targets(project);
  for (const ext of [".js", ".cjs"]) {
    const orig = Module._extensions[ext] || Module._extensions[".js"];
    Module._extensions[ext] = function (module, filename) {
      if (!wanted(filename, t)) return orig(module, filename);
      let src = fs.readFileSync(filename, "utf8");
      src = transform(src, filename);
      try {
        return module._compile(src, filename);
      } catch (e) {
        if (e && e.code === "ERR_REQUIRE_ESM") throw e;
        throw e;
      }
    };
  }
  try {
    const { register } = require("module");
    const { pathToFileURL } = require("url");
    register("./loader.mjs", pathToFileURL(__filename), { data: t });
  } catch (e) {
    if (process.env.STITCH_DEBUG) process.stderr.write(`[stitch] ESM hooks unavailable: ${e.message}\n`);
  }
  hookBuiltins();
}

module.exports = { MAP_SIZE, beginExec, endExec, setNode, setCmplog, setInStub, edgesBytes, cmpsBytes, install, wanted, promisesSince };
