"use strict";
// STITCH harness API for JavaScript (Node.js).
//
//   const stitch = require("stitch");
//   const { Borrow, New, Str, I32 } = stitch;
//   const Doc = stitch.object("Doc");
//
//   stitch.endpoint("parse", { text: Str(4096) }, New(Doc), ({ text }) => YAML.parseDocument(text),
//                   { bailsOn: [YAML.YAMLParseError] });
//   stitch.endpoint("set", { doc: Borrow(Doc), key: Str(32), value: I32 }, null, ({ doc, key, value }) => {
//     doc.set(key, value);
//   });
//
// An exception a stub does not list in bailsOn is a bug, and so is a property added to
// Object.prototype. Reference: `stitch guide javascript`.
const { REG, RT, Bail, CheckFailed, Endpoint, MISSING } = require("./runtime");
const { Source } = require("./source");
const S = require("./spec");

function endpoint(name, args, returns, fn, opts) {
  REG.add(new Endpoint(name, args, returns, fn, opts || {}));
}

module.exports = {
  endpoint,
  object: (name, drop) => REG.object(name, drop),
  datatype: (name) => REG.datatype(typeof name === "string" ? name : name.name),
  decoder: (dt, fn) => {
    REG.datatype(typeof dt === "string" ? dt : dt.name).decoder = fn;
    return fn;
  },
  init: (fn) => REG.inits.push(fn),
  bail: (reason = "bail") => {
    throw new Bail(reason);
  },
  require: (cond, reason = "requirement failed") => {
    if (!cond) throw new Bail(reason);
  },
  check: (cond, what = "check") => {
    if (!cond) throw new CheckFailed(what);
  },
  log: (...a) => {
    if (RT.trace) process.stderr.write(`[stitch]     log: ${a.map(String).join(" ")}\n`);
  },
  consume: (obj) => RT.consume(obj),
  set: (obj, key, value) => RT.attrSet(obj, key, value),
  get: (obj, key, def = MISSING) => RT.attrGet(obj, key, def),
  getObj: (obj, key) => RT.attrGet(obj, key, null),
  has: (obj, key) => RT.attrHas(obj, key),
  del: (obj, key) => RT.attrDel(obj, key),
  globalSet: (key, value) => RT.globals.set(key, value),
  globalGet: (key, def = MISSING) => {
    if (RT.globals.has(key)) return RT.globals.get(key);
    if (def === MISSING) {
      RT.noteMiss(key);
      return undefined;
    }
    return def;
  },
  Source,
  Bail,
  Borrow: S.Borrow, Take: S.Take, New: S.New,
  Str: S.Str, Bytes: S.Bytes, File: S.File, Src: S.Src,
  Range: S.Range, OneOf: S.OneOf, Flags: S.Flags, Bool: S.Bool,
  U8: S.U8, U16: S.U16, U32: S.U32, U64: S.U64, I8: S.I8, I16: S.I16, I32: S.I32, I64: S.I64,
  BigU64: S.BigU64, BigI64: S.BigI64, F32: S.F32, F64: S.F64,
};
