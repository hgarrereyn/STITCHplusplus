"use strict";
// Registry, manifest and program execution (a port of languages/cpp/runtime/src/runtime.cpp).
const fs = require("fs");
const os = require("os");
const path = require("path");
const util = require("util");
const instrument = require("./instrument");
const { Source } = require("./source");
const S = require("./spec");

const ST_OK = 1, ST_BAIL = 2, ST_SKIP_MISSING = 3, ST_SKIP_INVALID = 4;
const MAX_NODES = 256;
const STRINGS_SIZE = 65536;
const RUNTIME_DIR = __dirname;

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
class Finding extends Error {}

function fnv1a(buf) {
  let h = 2166136261;
  for (const c of buf) h = Math.imul(h ^ c, 16777619) >>> 0;
  return h || 1;
}

// ============================================================================ registry
class Endpoint {
  constructor(name, args, returns, fn, opts = {}) {
    if (typeof fn !== "function") throw new TypeError(`stub ${name}: the last argument must be a function`);
    this.name = name;
    this.fn = fn;
    this.bailsOn = opts.bailsOn || [];
    const site = callerFile();
    this.file = path.relative(process.env.STITCH_PROJECT || process.cwd(), site.file);
    this.line = site.line;
    this.args = []; // [name, "obj"|"param", spec]
    this.named = !Array.isArray(args);
    const entries = Array.isArray(args) ? args.map((a, i) => [paramNames(fn)[i] || `a${i}`, a]) : Object.entries(args || {});
    for (const [an, spec] of entries) {
      if (spec instanceof S.ObjSpec) this.args.push([an, "obj", spec]);
      else if (spec instanceof S.ObjectType) this.args.push([an, "obj", S.Borrow(spec)]);
      else if (spec instanceof S.Param) this.args.push([an, "param", spec]);
      else throw new TypeError(`stub ${name}: argument '${an}' has no STITCH++ type (use Borrow(T), Str(64), I32, ...)`);
    }
    this.multi = Array.isArray(returns);
    const outs = returns == null ? [] : this.multi ? returns : [returns];
    this.outputs = outs.map((r) => {
      if (r instanceof S.NewSpec) return [r.type, r.isOptional];
      if (r instanceof S.ObjectType) return [r, false];
      throw new TypeError(`stub ${name}: returns must be New(T), New(T).optional(), an array of those, or null`);
    });
  }
  get inputs() {
    return this.args.filter((a) => a[1] === "obj");
  }
  get params() {
    return this.args.filter((a) => a[1] === "param");
  }
  isExpected(e) {
    return this.bailsOn.some((b) => (typeof b === "function" && b.prototype ? e instanceof b : typeof b === "function" ? b(e) : false));
  }
}

function paramNames(fn) {
  const src = Function.prototype.toString.call(fn);
  const m = src.match(/^(?:async\s*)?(?:function\b[^(]*)?\(([^)]*)\)/) || src.match(/^(?:async\s+)?([\w$]+)\s*=>/);
  if (!m) return [];
  return m[1].split(",").map((s) => s.trim().replace(/=.*$/, "").trim()).filter(Boolean);
}

function callerFile() {
  const stack = new Error().stack.split("\n").slice(1);
  for (const l of stack) {
    const m = l.match(/\(?((?:file:\/\/)?\/[^():]+):(\d+):\d+\)?$/);
    if (m && !m[1].replace("file://", "").startsWith(RUNTIME_DIR)) return { file: m[1].replace("file://", ""), line: +m[2] };
  }
  return { file: "?", line: 0 };
}

class Registry {
  constructor() {
    this.objects = [];
    this.datatypes = new Map();
    this.endpoints = [];
    this.byName = new Map();
    this.inits = [];
    this.dicts = new Map();
  }
  object(name, drop) {
    let o = this.objects.find((x) => x.name === name);
    if (!o) {
      o = new S.ObjectType(name, drop);
      this.objects.push(o);
    } else if (drop) o.drop = drop;
    return o;
  }
  datatype(name) {
    if (!this.datatypes.has(name)) this.datatypes.set(name, new S.Datatype(name));
    return this.datatypes.get(name);
  }
  add(ep) {
    if (this.byName.has(ep.name)) throw new Error(`duplicate stub name '${ep.name}'`);
    for (const [, , s] of ep.inputs) this._adopt(s.type);
    for (const [t] of ep.outputs) this._adopt(t);
    for (const [, , p] of ep.params) if (p.datatype) this.datatype(p.datatype);
    this.endpoints.push(ep);
    this.byName.set(ep.name, ep);
  }
  _adopt(t) {
    if (!this.objects.includes(t)) {
      if (this.objects.some((o) => o.name === t.name)) throw new Error(`two different object types are named '${t.name}'`);
      this.objects.push(t);
    }
  }
  dtList() {
    return [...this.datatypes.values()];
  }
  manifest() {
    return JSON.stringify({
      version: 2,
      objects: this.objects.map((o) => ({ name: o.name, drop: !!o.drop })),
      datatypes: this.dtList().map((d) => ({ name: d.name, decoder: !!d.decoder })),
      endpoints: this.endpoints.map((e) => ({
        name: e.name,
        file: e.file,
        inputs: e.inputs.map(([n, , s]) => ({ type: s.type.name, mode: s.mode, name: n })),
        params: e.params.map(([n, , p]) => p.manifest(n)),
        outputs: e.outputs.map(([t, o]) => ({ type: t.name, optional: o })),
      })),
    });
  }
}

const REG = new Registry();

// ============================================================================ execution
const MISSING = Symbol("missing");

function preview(b, n = 160) {
  let s = "";
  for (const c of b.subarray(0, n)) {
    s += c === 34 || c === 92 ? "\\" + String.fromCharCode(c) : c >= 32 && c < 127 ? String.fromCharCode(c)
      : c === 10 ? "\\n" : "\\x" + c.toString(16).padStart(2, "0");
  }
  return `"${s}"` + (b.length > n ? "..." : "");
}

function jsLiteral(v) {
  if (typeof v === "string") return JSON.stringify(v);
  if (typeof v === "bigint") return `${v}n`;
  if (typeof v === "number") return Object.is(v, -0) ? "-0" : String(v);
  if (v === undefined) return "undefined";
  if (v === null || typeof v === "boolean") return String(v);
  if (Buffer.isBuffer(v)) return `Buffer.from(${JSON.stringify(v.toString("hex"))}, "hex")`;
  try {
    const j = JSON.stringify(v);
    if (j !== undefined && util.isDeepStrictEqual(JSON.parse(j), v)) return j;
  } catch (e) { /* not JSON */ }
  return null;
}

function excKind(e) {
  if (e instanceof Error || (e && typeof e === "object" && e.constructor)) return e.constructor.name || "Error";
  return `thrown ${typeof e}`;
}

function excFrames(e) {
  const out = [];
  const stack = e && typeof e.stack === "string" ? e.stack.split("\n") : [];
  for (const l of stack) {
    const m = l.match(/^\s+at (?:(.*?) \()?(.+?):(\d+):\d+\)?$/);
    if (!m) continue;
    let file = m[2].replace(/^file:\/\//, "");
    if (file.startsWith(RUNTIME_DIR) || file.startsWith("node:") || !file.startsWith("/")) continue;
    out.push([m[1] || `anonymous@${path.basename(file)}`, file, +m[3]]);
  }
  return out;
}

class Runtime {
  constructor(reg = REG) {
    this.reg = reg;
    this.trace = false;
    this.plan = null;
    this.inExec = false;
    this.curNode = -1;
    this.pool = [];
    this.objOf = new Map();
    this.globals = new Map();
    this.pending = [];
    this.curInSlots = [];
    this.curConsumed = 0;
    this.lastMiss = 0;
    this.gen = 1;
    this.tmpFiles = [];
    this.excBail = process.env.STITCH_EXCEPTIONS === "bail";
    this.planSrcNext = 0;
    this.protoBaseline = null;
    this.rejection = null;
    this.resetResults(0);
  }

  // ---------------------------------------------------------------- results
  resetResults(n) {
    this.nNodes = n;
    this.current = -1;
    this.crashPc = 0n;
    this.res = Array.from({ length: n }, () => ({ status: 0, nOut: 0, outMask: 0, reason: 0, miss: 0, consumed: 0 }));
    this.strings = [];
    this.strLen = 0;
    this.seen = new Set();
  }
  intern(s) {
    let b = Buffer.from(String(s), "utf8");
    const h = fnv1a(b);
    if (this.seen.has(h)) return h;
    b = b.subarray(0, 512);
    if (this.strLen + 6 + b.length <= STRINGS_SIZE) {
      const head = Buffer.alloc(6);
      head.writeUInt32LE(h, 0);
      head.writeUInt16LE(b.length, 4);
      this.strings.push(head, b);
      this.strLen += 6 + b.length;
      this.seen.add(h);
    }
    return h;
  }
  resultsBytes() {
    const head = Buffer.alloc(16);
    head.writeUInt32LE(this.nNodes, 0);
    head.writeInt32LE(this.current, 4);
    head.writeBigUInt64LE(BigInt.asUintN(64, this.crashPc), 8);
    const nodes = Buffer.alloc(16 * this.nNodes);
    this.res.forEach((r, i) => {
      const o = 16 * i;
      nodes.writeUInt8(r.status, o);
      nodes.writeUInt8(r.nOut, o + 1);
      nodes.writeUInt16LE(r.outMask, o + 2);
      nodes.writeUInt32LE(r.reason >>> 0, o + 4);
      nodes.writeUInt32LE(r.miss >>> 0, o + 8);
      nodes.writeUInt32LE(r.consumed >>> 0, o + 12);
    });
    const sl = Buffer.alloc(4);
    sl.writeUInt32LE(this.strLen, 0);
    return Buffer.concat([head, nodes, sl, ...this.strings, instrument.edgesBytes(), instrument.cmpsBytes()]);
  }

  // ---------------------------------------------------------------- objects + attributes
  live(i) {
    return i >= 0 && i < this.pool.length && this.pool[i].alive ? this.pool[i] : null;
  }
  slotOf(obj) {
    for (let i = this.pool.length - 1; i >= 0; i--) if (this.pool[i].alive && this.pool[i].value === obj) return i;
    return -1;
  }
  noteMiss(key) {
    if (this.inExec) this.lastMiss = this.intern(key);
  }
  _wrap(v) {
    if (v !== null && (typeof v === "object" || typeof v === "function")) {
      const i = this.slotOf(v);
      if (i >= 0) return { __ref: i, gen: this.pool[i].gen };
    }
    return v;
  }
  _unwrap(v) {
    if (v && typeof v === "object" && "__ref" in v) {
      const s = this.live(v.__ref);
      return s && s.gen === v.gen ? s.value : null;
    }
    return v;
  }
  attrSet(obj, key, value) {
    const i = this.slotOf(obj);
    if (i >= 0) this.pool[i].attrs.set(key, this._wrap(value));
    else this.pending.push([obj, key, this._wrap(value)]);
  }
  attrGet(obj, key, def = MISSING) {
    const i = this.slotOf(obj);
    if (i >= 0 && this.pool[i].attrs.has(key)) return this._unwrap(this.pool[i].attrs.get(key));
    if (i < 0) for (let k = this.pending.length - 1; k >= 0; k--) if (this.pending[k][0] === obj && this.pending[k][1] === key) return this._unwrap(this.pending[k][2]);
    if (def === MISSING) {
      this.noteMiss(key);
      return undefined;
    }
    return def;
  }
  attrHas(obj, key) {
    const i = this.slotOf(obj);
    return i >= 0 && this.pool[i].attrs.has(key);
  }
  attrDel(obj, key) {
    const i = this.slotOf(obj);
    if (i >= 0) this.pool[i].attrs.delete(key);
  }
  consume(obj) {
    const i = this.slotOf(obj);
    this.curInSlots.forEach((s, k) => {
      if (s === i) this.curConsumed |= 1 << k;
    });
  }

  // ---------------------------------------------------------------- params
  tempFile(data) {
    const dir = process.env.STITCH_TMPDIR || os.tmpdir();
    const p = path.join(dir, `stitch-${process.pid}-${this.tmpFiles.length}-${Date.now()}`);
    fs.writeFileSync(p, data);
    this.tmpFiles.push(p);
    return p;
  }
  newSource(buf, cap) {
    const s = new Source(buf, { cap });
    if (this.plan && this.inExec && this.curNode >= 0) {
      const sid = this.planSrcNext++;
      const node = this.curNode;
      s._log = (op, v, isBytes) => this.planDraw(node, sid, op, v, isBytes);
    }
    return s;
  }
  decodeParam(dtname, raw, form, max) {
    const dt = dtname ? this.reg.datatypes.get(dtname) : null;
    if (form === 1 && dt && dt.decoder) return this.runDecoder(dt, new Source(raw, { cap: Math.max(raw.length, max) }), max);
    return Buffer.from(raw).subarray(0, max);
  }
  decodeValue(dtname, raw, max) {
    const dt = this.reg.datatypes.get(dtname);
    if (!dt || !dt.decoder) throw new TypeError(`Val(${dtname}): the data type has no decoder`);
    return dt.decoder(new Source(raw, { cap: max }));
  }
  runDecoder(dt, src, max) {
    let v;
    try {
      v = dt.decoder(src);
    } catch (e) {
      v = Buffer.alloc(0);
    }
    if (typeof v === "string") v = Buffer.from(v, "utf8");
    else if (!Buffer.isBuffer(v)) v = v instanceof Uint8Array ? Buffer.from(v) : Buffer.from(util.inspect(v, { depth: 6, breakLength: Infinity }));
    return max === undefined ? v : v.subarray(0, max);
  }
  describe(p, raw, form) {
    if (p instanceof S.BufParam) {
      const v = p.rawValue(raw, form, this);
      return `${p.ctype}[${v.length}${form === 1 ? ",decoded" : ""}]${preview(v)}`;
    }
    if (p instanceof S.SrcParam) return `Src[${raw.length}]${Buffer.from(raw).subarray(0, 48).toString("hex")}${raw.length > 48 ? "..." : ""}`;
    if (p instanceof S.ValParam) return `Val<${p.datatype}>[${raw.length} choices] ${util.inspect(p.decode(raw, form, this), { depth: 4, breakLength: Infinity }).slice(0, 300)}`;
    const v = p.decode(raw, form, this);
    return jsLiteral(v) ?? util.inspect(v);
  }

  // ---------------------------------------------------------------- plan mode
  planLine(d) {
    if (this.plan !== null) fs.writeSync(this.plan, JSON.stringify(d, (k, v) => (typeof v === "bigint" ? `${v}` : v)) + "\n");
  }
  planDraw(node, src, op, v, isBytes) {
    const d = { t: "draw", node, src, op };
    if (isBytes) d.hex = Buffer.from(v).toString("hex");
    else {
      d.v = String(v);
      d.js = jsLiteral(v);
    }
    this.planLine(d);
  }
  planParam(p, raw, form) {
    const d = { kind: p.kind, size: p.size };
    if (p instanceof S.BufParam) {
      d.decoded = form === 1;
      d.hex = p.rawValue(raw, form, this).toString("hex");
    } else if (p instanceof S.SrcParam || p instanceof S.ValParam) {
      d.hex = Buffer.from(raw).subarray(0, p.size).toString("hex");
      if (p instanceof S.ValParam) d.v = util.inspect(p.decode(raw, form, this), { depth: 4, breakLength: Infinity }).slice(0, 300);
    } else {
      const v = p.decode(raw, form, this);
      d.signed = p.signed;
      d.v = String(v);
      d.js = jsLiteral(v);
      if (p instanceof S.OneOfParam) d.index = p.index(raw);
    }
    if (p.datatype) d.datatype = p.datatype;
    return d;
  }

  // ---------------------------------------------------------------- program
  static parse(data) {
    try {
      const b = Buffer.from(data);
      if (b.toString("latin1", 0, 4) !== "STCH") return null;
      let off = 4;
      const nn = b.readUInt32LE(off);
      off += 4;
      if (nn > 100000) return null;
      const names = [];
      for (let i = 0; i < nn; i++) {
        const l = b.readUInt16LE(off);
        off += 2;
        names.push(b.toString("utf8", off, off + l));
        off += l;
      }
      const n = b.readUInt32LE(off);
      off += 4;
      if (n > MAX_NODES) return null;
      const nodes = [];
      for (let i = 0; i < n; i++) {
        const ni = b.readUInt32LE(off);
        const nin = b.readUInt8(off + 4);
        off += 5;
        const ins = [];
        for (let k = 0; k < nin; k++) {
          ins.push([b.readUInt32LE(off), b.readUInt8(off + 4)]);
          off += 5;
        }
        const np = b.readUInt8(off);
        off += 1;
        const ps = [];
        for (let k = 0; k < np; k++) {
          const form = b.readUInt8(off);
          const l = b.readUInt32LE(off + 1);
          off += 5;
          if (l > 16 << 20 || off + l > b.length) return null;
          ps.push([form, b.subarray(off, off + l)]);
          off += l;
        }
        nodes.push([ni < names.length ? names[ni] : "?", ins, ps]);
      }
      return nodes;
    } catch (e) {
      return null;
    }
  }

  report(ep, err, what) {
    const w = (s) => process.stderr.write(s);
    let frames;
    if (what !== undefined) {
      this.crashPc = (1n << 63n) | BigInt(fnv1a(Buffer.from(what)));
      w(`\n[stitch] CHECK FAILED: ${what} (node ${this.curNode})\n`);
      frames = excFrames(err);
    } else {
      const kind = err && err.__stitchKind ? err.__stitchKind : excKind(err);
      const msg = String(err && err.message !== undefined ? err.message : err).replace(/\n/g, " ").slice(0, 500);
      frames = err && err.__stitchFrames ? err.__stitchFrames : excFrames(err);
      const proj = (process.env.STITCH_PROJECT || "/project") + "/harness";
      const top = frames.find(([, f]) => !f.startsWith(proj));
      const idx = Math.max(0, this.reg.endpoints.findIndex((e) => e.name === ep));
      this.crashPc = (1n << 63n) | (BigInt(idx) << 32n) | BigInt(fnv1a(Buffer.from(`${kind}|${top ? `${top[1]}:${top[2]}` : ""}`)));
      w(`\n[stitch] EXCEPTION in ${ep}: ${kind}: ${msg}\n`);
    }
    frames.slice(0, 24).forEach(([fn, file, line], k) => w(`    #${k} 0x0 in ${fn} ${file}:${line}\n`));
    w("\n");
  }

  // A JS-specific oracle: properties added during a call to the shared built-ins every object
  // reaches: Object.prototype, its methods (`set(o, "toString.x", 1)` writes into
  // Object.prototype.toString), Array.prototype, Function.prototype and Object.
  static watched() {
    const out = [["Object.prototype", Object.prototype], ["Array.prototype", Array.prototype],
      ["Function.prototype", Function.prototype], ["Object", Object]];
    for (const k of Object.getOwnPropertyNames(Object.prototype)) {
      const d = Object.getOwnPropertyDescriptor(Object.prototype, k);
      if (d && typeof d.value === "function" && d.value !== Object) out.push([`Object.prototype.${k}`, d.value]);
    }
    return out;
  }

  snapshotBuiltins() {
    this.protoBaseline = Runtime.watched().map(([name, o]) => {
      const props = new Map();
      for (const k of Reflect.ownKeys(o)) props.set(k, Object.getOwnPropertyDescriptor(o, k));
      // fast check after each call: data properties compared by value, accessors by descriptor
      const data = [], accessors = [];
      for (const [k, d] of props) ("value" in d ? data : accessors).push([k, d]);
      return [name, o, props, data, accessors];
    });
  }

  // True when every watched built-in looks unchanged (no allocation for data properties).
  builtinsIntact() {
    for (const [, o, props, data, accessors] of this.protoBaseline) {
      if (Reflect.ownKeys(o).length !== props.size) return false;
      for (let i = 0; i < data.length; i++) {
        const [k, d] = data[i];
        if (o[k] !== d.value) return false; // (a deleted key changes the key count or its value)
      }
      for (let i = 0; i < accessors.length; i++) {
        const [k, d] = accessors[i];
        const v = Object.getOwnPropertyDescriptor(o, k);
        if (!v || v.get !== d.get || v.set !== d.set) return false;
      }
    }
    return true;
  }

  // Undo every change to the watched built-ins (so the runtime keeps working) and report the
  // first one: a property added, replaced (`set(o, "__proto__.valueOf", 7)`) or deleted.
  checkPollution(ep) {
    if (!this.protoBaseline || this.builtinsIntact()) return;
    const changes = [];
    const same = (a, b) => a && b && a.value === b.value && a.get === b.get && a.set === b.set;
    for (const [name, o, props] of this.protoBaseline) {
      for (const k of Reflect.ownKeys(o)) {
        if (props.has(k)) continue;
        try {
          delete o[k];
        } catch (e) { /* non-configurable: leave it */ }
        changes.push(`${name}.${String(k)} was set`);
      }
      for (const [k, d] of props) {
        const now = Object.getOwnPropertyDescriptor(o, k);
        if (same(now, d)) continue;
        changes.push(`${name}.${String(k)} was ${now ? "replaced" : "deleted"}`);
        try {
          Object.defineProperty(o, k, d);
        } catch (e) { /* frozen meanwhile: leave it */ }
      }
    }
    if (!changes.length) return;
    const e = new Error(changes[0] + (changes.length > 1 ? ` (+${changes.length - 1} more)` : ""));
    e.__stitchKind = "prototype pollution";
    e.__stitchFrames = [];
    this.report(ep, e);
    throw new Finding();
  }

  async run(data, trace = false) {
    const nodes = Runtime.parse(data);
    if (nodes === null) {
      this.resetResults(0);
      if (trace) process.stderr.write("[stitch] invalid program\n");
      return "invalid";
    }
    this.resetResults(nodes.length);
    this.pool = [];
    this.objOf = new Map();
    this.globals = new Map();
    this.trace = trace;
    this.inExec = true;
    instrument.beginExec();
    try {
      return await this._runNodes(nodes);
    } catch (e) {
      if (e instanceof Finding) return "finding";
      throw e;
    } finally {
      this.inExec = false;
      this.curNode = -1;
      instrument.endExec();
      for (const f of this.tmpFiles) try { fs.unlinkSync(f); } catch (e) { /* gone */ }
      this.tmpFiles = [];
    }
  }

  async _runNodes(nodes) {
    const err = (s) => process.stderr.write(s);
    const trace = this.trace;
    for (let i = 0; i < nodes.length; i++) {
      const [name, ins, ps] = nodes[i];
      this.current = i;
      this.curNode = i;
      instrument.setNode(i);
      const r = this.res[i];
      const e = this.reg.byName.get(name);
      if (!e) {
        r.status = ST_SKIP_INVALID;
        this.planLine({ t: "skip", node: i, why: "unknown endpoint" });
        if (trace) err(`[stitch] #${i} ${name}: unknown endpoint, skipped\n`);
        continue;
      }
      r.nOut = e.outputs.length;
      const inputs = e.inputs;
      const params = e.params;
      if (ins.length !== inputs.length || ps.length !== params.length) {
        r.status = ST_SKIP_INVALID;
        this.planLine({ t: "skip", node: i, why: "signature mismatch" });
        if (trace) err(`[stitch] #${i} ${name}: signature mismatch, skipped\n`);
        continue;
      }
      const inSlot = [];
      let missing = false;
      for (let k = 0; k < ins.length; k++) {
        const si = this.objOf.get(`${ins[k][0]}.${ins[k][1]}`) ?? -1;
        const s = this.live(si);
        if (!s || s.type !== inputs[k][2].type || inSlot.includes(si)) {
          missing = true;
          break;
        }
        inSlot.push(si);
      }
      if (missing) {
        r.status = ST_SKIP_MISSING;
        this.planLine({ t: "skip", node: i, why: "input not available" });
        if (trace) err(`[stitch] #${i} ${name}: input not available, skipped\n`);
        continue;
      }
      if (trace) {
        let oi = 0, pi = 0;
        const parts = e.args.map(([an, kind, spec]) => {
          if (kind === "obj") {
            const [src, out] = ins[oi++];
            return `${an}=${spec.mode === "take" ? "take " : ""}o${src}.${out}`;
          }
          const [form, raw] = ps[pi++];
          return `${an}=${this.describe(spec, raw, form)}`;
        });
        err(`[stitch] #${i} ${name}(${parts.join(", ")})\n`);
      }
      if (this.plan !== null) {
        this.planCall(i, e, ins, ps);
        this.planSrcNext = 0;
      }
      this.curInSlots = inSlot;
      this.curConsumed = 0;
      this.lastMiss = 0;
      this.pending = [];
      let bailReason = null;
      let result;
      try {
        let oi = 0, pi = 0;
        const vals = e.args.map(([, kind, spec]) => {
          if (kind === "obj") return this.pool[inSlot[oi++]].value;
          const [form, raw] = ps[pi++];
          return spec.decode(raw, form, this);
        });
        let named = null;
        if (e.named) {
          named = {};
          e.args.forEach(([an], k) => { named[an] = vals[k]; });
        }
        instrument.setInStub(true);
        try {
          result = named ? e.fn(named) : e.fn(...vals);
          if (result && typeof result.then === "function") result = await result;
        } finally {
          instrument.setInStub(false);
          this.checkPollution(name); // first: a polluted built-in can break everything after it
        }
        r.status = ST_OK;
      } catch (ex) {
        if (ex instanceof Bail) {
          r.status = ST_BAIL;
          bailReason = ex.reason;
        } else if (ex instanceof Finding) {
          throw ex;
        } else if (ex instanceof CheckFailed) {
          this.report(name, ex, ex.what);
          throw new Finding();
        } else if (e.isExpected(ex)) {
          r.status = ST_BAIL;
          bailReason = `threw ${excKind(ex)}`;
        } else if (this.excBail) {
          r.status = ST_BAIL;
          bailReason = "exception";
        } else {
          this.report(name, ex);
          throw new Finding();
        }
      }
      inSlot.forEach((si, k) => {
        const take = inputs[k][2].mode === "take";
        if (take || this.curConsumed & (1 << k)) {
          this.pool[si].alive = false;
          this.pool[si].value = null;
          if (!take) r.consumed |= 1 << k;
        }
      });
      if (r.status === ST_OK && e.outputs.length) {
        const vals = e.multi ? result : [result];
        if (!Array.isArray(vals) || vals.length !== e.outputs.length) {
          r.status = ST_BAIL;
          bailReason = `returned ${typeof result}, expected ${e.outputs.length} outputs`;
        } else {
          vals.forEach((v, k) => {
            if (v === null || v === undefined) return;
            const s = { type: e.outputs[k][0], value: v, alive: true, gen: this.gen++, node: i, out: k, attrs: new Map() };
            for (const [o, key, val] of this.pending) if (o === v) s.attrs.set(key, val);
            this.pool.push(s);
            this.objOf.set(`${i}.${k}`, this.pool.length - 1);
            r.outMask |= 1 << k;
          });
          if (e.outputs.some(([, opt], k) => !opt && !(r.outMask & (1 << k)))) {
            r.status = ST_BAIL;
            bailReason = "returned null";
          }
        }
      }
      if (r.status === ST_BAIL) {
        r.reason = this.intern(bailReason || "bail");
        r.miss = this.lastMiss;
      }
      if (this.plan !== null) {
        const d = {
          t: "done", node: i, status: r.status === ST_OK ? "ok" : "bail",
          outs: e.outputs.map((_, k) => k).filter((k) => r.outMask & (1 << k)),
          consumed: inSlot.map((_, k) => k).filter((k) => r.consumed & (1 << k)),
        };
        if (r.status === ST_BAIL) d.reason = bailReason || "bail";
        this.planLine(d);
      }
      if (trace) {
        if (r.status === ST_OK) {
          const outs = e.outputs.map((_, k) => k).filter((k) => r.outMask & (1 << k)).map((k) => ` o${i}.${k}`).join("");
          err(`[stitch]     -> ok${outs ? " produced" + outs : ""}\n`);
        } else err(`[stitch]     -> bail "${bailReason || "bail"}"\n`);
      }
    }
    // async work the stubs started: an unhandled rejection is a bug. Node reports rejections
    // only from the event loop, so take one turn of it, but only when a stub created a promise
    // (a turn costs more than many small programs).
    if (this.rejection === null && instrument.promisesSince()) await new Promise((res) => setImmediate(res));
    if (this.rejection !== null) {
      const rj = this.rejection;
      this.rejection = null;
      this.report(nodes[Math.max(0, this.current)][0], rj);
      throw new Finding();
    }
    this.current = nodes.length;
    this.curNode = nodes.length;
    instrument.setNode(nodes.length);
    const live = this.pool.filter((s) => s.alive && s.type.drop).reverse();
    if (this.plan !== null) this.planLine({ t: "drops", objs: live.map((s) => [s.node, s.out]) });
    for (const s of live) {
      try {
        const r = s.type.drop(s.value);
        if (r && typeof r.then === "function") await r.catch(() => {});
      } catch (e) { /* ignore */ }
    }
    this.pool = [];
    this.current = -1;
    this.planLine({ t: "end" });
    return "ok";
  }

  planCall(i, e, ins, ps) {
    let oi = 0, pi = 0;
    const args = e.args.map(([an, kind, spec]) => {
      if (kind === "obj") {
        const [src, out] = ins[oi++];
        return { obj: [src, out], type: spec.type.name, take: spec.mode === "take", name: an };
      }
      const [form, raw] = ps[pi++];
      return { ...this.planParam(spec, raw, form), name: an };
    });
    this.planLine({ t: "call", node: i, ep: e.name, args, outs: e.outputs.map(([t]) => t.name), multi: e.multi, named: e.named });
  }
}

const RT = new Runtime();
process.on("unhandledRejection", (e) => {
  if (RT.inExec) RT.rejection = e;
});

module.exports = { REG, RT, Bail, CheckFailed, Finding, Endpoint, MISSING, Runtime };
