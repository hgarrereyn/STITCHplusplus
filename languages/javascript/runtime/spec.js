"use strict";
// Signature vocabulary. A stub declares its arguments as an object of specs:
//   stitch.endpoint("set", { doc: Borrow(Doc), key: Str(32), value: I32 }, null, ({ doc, key, value }) => ...)
// Byte encodings match the C++ runtime so the engine mutates parameters the same way.

class ObjectType {
  constructor(name, drop) {
    this.name = name;
    this.drop = drop || null;
  }
}

class Datatype {
  constructor(name) {
    this.name = name;
    this.decoder = null;
  }
}

const nameOf = (x) => (x == null ? null : typeof x === "string" ? x : x.name);

class ObjSpec {
  constructor(type, mode) {
    if (!(type instanceof ObjectType)) throw new TypeError(`${mode}(...) takes an object type from stitch.object()`);
    this.type = type;
    this.mode = mode;
  }
}

class NewSpec {
  constructor(type, optional = false) {
    if (!(type instanceof ObjectType)) throw new TypeError("New(...) takes an object type from stitch.object()");
    this.type = type;
    this.isOptional = optional;
  }
  optional() {
    return new NewSpec(this.type, true);
  }
}

function leInt(raw, size, signed) {
  const b = Buffer.alloc(8);
  Buffer.from(raw).copy(b, 0, 0, Math.min(raw.length, size));
  let v = b.readBigUInt64LE(0);
  if (size < 8) v &= (1n << BigInt(8 * size)) - 1n;
  if (signed) v = BigInt.asIntN(8 * size, v);
  return v;
}

class Param {
  constructor(kind, ctype, size, extra = {}) {
    Object.assign(this, { kind, ctype, size, signed: false, lo: 0, hi: 0, values: [], datatype: null }, extra);
  }
  manifest(name) {
    const d = { kind: this.kind, type: this.ctype, size: this.size, signed: this.signed, datatype: this.datatype, name };
    if (this.kind === "range") Object.assign(d, { lo: Number(this.lo), hi: Number(this.hi) });
    if (this.values.length) d.values = this.values.map(Number);
    return d;
  }
}

class IntParam extends Param {
  constructor(size, signed, ctype, big = false) {
    super("int", ctype, size, { signed });
    this.big = big;
  }
  decode(raw) {
    const v = leInt(raw, this.size, this.signed);
    return this.big ? v : Number(v);
  }
}

class FloatParam extends Param {
  constructor(size) {
    super("float", size === 4 ? "F32" : "F64", size);
  }
  decode(raw) {
    const b = Buffer.alloc(8);
    Buffer.from(raw).copy(b, 0, 0, Math.min(raw.length, this.size));
    return this.size === 4 ? b.readFloatLE(0) : b.readDoubleLE(0);
  }
}

class BoolParam extends Param {
  constructor() {
    super("bool", "Bool", 1);
  }
  decode(raw) {
    return raw.length > 0 && (raw[0] & 1) === 1;
  }
}

class RangeParam extends Param {
  constructor(lo, hi) {
    if (lo > hi) throw new RangeError("Range(lo, hi) needs lo <= hi");
    super("range", "Range", 8, { signed: true, lo: BigInt(lo), hi: BigInt(hi) });
  }
  decode(raw) {
    const v = leInt(raw, 8, true);
    if (v >= this.lo && v <= this.hi) return Number(v);
    const span = BigInt.asUintN(64, this.hi - this.lo + 1n);
    const off = BigInt.asUintN(64, v - this.lo);
    return Number(this.lo + (span ? off % span : off));
  }
}

class OneOfParam extends Param {
  // any values; the engine sees their indices
  constructor(choices) {
    if (!choices.length) throw new RangeError("OneOf(...) needs at least one value");
    super("oneof", "OneOf", 8, { values: choices.map((_, i) => i) });
    this.choices = choices;
  }
  index(raw) {
    const v = leInt(raw, 8, true);
    const n = BigInt(this.choices.length);
    return Number(v >= 0n && v < n ? v : BigInt.asUintN(64, v) % n);
  }
  decode(raw) {
    return this.choices[this.index(raw)];
  }
}

class FlagsParam extends Param {
  constructor(flags) {
    super("flags", "Flags", 8, { values: flags.map(Number) });
    this.mask = flags.reduce((m, f) => m | BigInt(f), 0n);
  }
  decode(raw) {
    return Number(leInt(raw, 8, true) & this.mask);
  }
}

class BufParam extends Param {
  constructor(kind, size, datatype) {
    super(kind, { bytes: "Bytes", str: "Str", file: "File" }[kind], size, { datatype: nameOf(datatype) });
  }
  rawValue(raw, form, rt) {
    return rt.decodeParam(this.datatype, raw, form, this.size);
  }
  decode(raw, form, rt) {
    const b = this.rawValue(raw, form, rt);
    if (this.kind === "bytes") return b;
    if (this.kind === "str") return b.toString("utf8");
    return rt.tempFile(b);
  }
}

// Val(Tag, max): the value Tag's decoder builds (any JS value) from at most max choice bytes
class ValParam extends Param {
  constructor(datatype, size) {
    super("value", "Val", size, { datatype: nameOf(datatype) });
  }
  decode(raw, form, rt) {
    return rt.decodeValue(this.datatype, Buffer.from(raw).subarray(0, this.size), this.size);
  }
}

class SrcParam extends Param {
  constructor(size) {
    super("source", "Source", size);
  }
  decode(raw, form, rt) {
    return rt.newSource(Buffer.from(raw).subarray(0, this.size), this.size);
  }
}

const sized = (kind, def) => (a, b) => {
  let size = def;
  let dt = null;
  for (const x of [a, b]) {
    if (typeof x === "number") size = x;
    else if (x != null) dt = x;
  }
  return new BufParam(kind, size, dt);
};

module.exports = {
  ObjectType, Datatype, ObjSpec, NewSpec, Param, BufParam, SrcParam, ValParam, OneOfParam,
  Borrow: (t) => new ObjSpec(t, "borrow"),
  Take: (t) => new ObjSpec(t, "take"),
  New: (t) => new NewSpec(t),
  Str: sized("str", 256),
  Bytes: sized("bytes", 256),
  File: sized("file", 4096),
  Src: (size = 64) => new SrcParam(size),
  Val: (dt, size = 1024) => new ValParam(dt, size),
  Range: (lo, hi) => new RangeParam(lo, hi),
  OneOf: (...choices) => new OneOfParam(choices.length === 1 && Array.isArray(choices[0]) ? choices[0] : choices),
  Flags: (...flags) => new FlagsParam(flags.length === 1 && Array.isArray(flags[0]) ? flags[0] : flags),
  U8: new IntParam(1, false, "U8"),
  U16: new IntParam(2, false, "U16"),
  U32: new IntParam(4, false, "U32"),
  U64: new IntParam(8, false, "U64"),
  I8: new IntParam(1, true, "I8"),
  I16: new IntParam(2, true, "I16"),
  I32: new IntParam(4, true, "I32"),
  I64: new IntParam(8, true, "I64"),
  BigU64: new IntParam(8, false, "U64", true),
  BigI64: new IntParam(8, true, "I64", true),
  F32: new FloatParam(4),
  F64: new FloatParam(8),
  Bool: new BoolParam(),
};
