"use strict";
// stitch.Source: decisions inside decoders and stubs, read from a fuzzer-owned choice buffer.
// Same encoding as the C++ runtime (languages/cpp/runtime/src/runtime.cpp): little-endian fixed
// width integers, 8-byte ranges, 1-byte coin/more, length-prefixed strings, f64(lo, hi) as the value
// itself when finite and in range.

// FNV-1a: a stable 32-bit id for a loop's source location
function site32(text) {
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) h = Math.imul(h ^ text.charCodeAt(i), 0x01000193) >>> 0;
  return h >>> 0;
}

// The loop calling more(): its source location and the stack depth (recursion level).
function loopCaller() {
  const limit = Error.stackTraceLimit;
  const prep = Error.prepareStackTrace;
  Error.stackTraceLimit = Infinity;
  Error.prepareStackTrace = (_, cs) => cs;
  const holder = {};
  Error.captureStackTrace(holder, loopCaller);
  const cs = holder.stack;
  Error.stackTraceLimit = limit;
  Error.prepareStackTrace = prep;
  const c = cs[1]; // [0] is Source.more
  return { site: site32(c ? `${c.getFileName()}:${c.getLineNumber()}:${c.getColumnNumber()}` : "?"), depth: cs.length };
}

const INTERESTING = [0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 512, 1000, 1024, 4096,
  0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff].map(BigInt);
const M64 = (1n << 64n) - 1n;

function rng(seed) {
  // splitmix64 on BigInt: deterministic per seed
  let s = BigInt.asUintN(64, BigInt(seed));
  return () => {
    s = BigInt.asUintN(64, s + 0x9E3779B97F4A7C15n);
    let z = s;
    z = BigInt.asUintN(64, (z ^ (z >> 30n)) * 0xBF58476D1CE4E5B9n);
    z = BigInt.asUintN(64, (z ^ (z >> 27n)) * 0x94D049BB133111EBn);
    return z ^ (z >> 31n);
  };
}

function leRead(bytes) {
  let v = 0n;
  for (let i = bytes.length - 1; i >= 0; i--) v = (v << 8n) | BigInt(bytes[i]);
  return v;
}

function leWrite(v, n) {
  const out = Buffer.alloc(n);
  let x = BigInt.asUintN(64, v);
  for (let i = 0; i < n; i++) {
    out[i] = Number(x & 0xffn);
    x >>= 8n;
  }
  return out;
}

class Source {
  constructor(buf = Buffer.alloc(0), opts = {}) {
    this._buf = Buffer.from(buf);
    this._len = this._buf.length;
    this._pos = 0;
    this._cap = Math.max(opts.cap ?? this._len, this._len);
    this._gen = !!opts.gen;
    this._rnd = this._gen ? rng(opts.seed ?? 0) : null;
    this._dict = opts.dict || [];
    this._spans = opts.spans || null;
    this._items = opts.items || null; // loop iterations for the engine: site, instance, start, end
    this._open = new Map();
    this._nextInst = 0;
    this._exhausted = false;
    this._log = null; // plan mode: (op, value, isBytes) => void
  }

  // ---------------------------------------------------------------- low level
  _unit() {
    return Number(this._rnd() >> 11n) / 9007199254740992;
  }

  _append(bytes) {
    const need = this._len + bytes.length;
    if (need > this._buf.length) {
      const nb = Buffer.alloc(Math.max(need, this._buf.length * 2 + 16));
      this._buf.copy(nb, 0, 0, this._len);
      this._buf = nb;
    }
    Buffer.from(bytes).copy(this._buf, this._len);
    this._len += bytes.length;
  }

  _draw(n, synth) {
    if (this._pos + n <= this._len) {
      const out = Buffer.from(this._buf.subarray(this._pos, this._pos + n));
      this._pos += n;
      return out;
    }
    if (this._gen && this._len + n <= this._cap) {
      this._len = this._pos;
      const fresh = Buffer.alloc(n);
      Buffer.from(synth()).copy(fresh, 0, 0, n);
      this._append(fresh);
      this._pos += n;
      return fresh;
    }
    const out = Buffer.alloc(n);
    if (this._pos < this._len) this._buf.copy(out, 0, this._pos, this._len);
    this._pos = this._len;
    this._exhausted = true;
    return out;
  }

  _interesting(width) {
    const mask = width >= 8 ? M64 : (1n << BigInt(8 * width)) - 1n;
    const r = this._rnd() % 4n;
    if (r === 0n) return mask;
    if (r === 1n) return mask >> 1n;
    return INTERESTING[Number(this._rnd() % BigInt(INTERESTING.length))] & mask;
  }

  _uint(width) {
    return leRead(this._draw(width, () => leWrite(this._unit() < 0.3 ? this._interesting(width) : this._rnd(), width)));
  }

  static _lenPrefix(max) {
    return max < 256 ? 1 : max < 65536 ? 2 : 4;
  }

  _synthLen(max) {
    const u = this._unit();
    const l = u < 0.7 ? this._rnd() % 17n : u < 0.95 ? this._rnd() % 65n : this._rnd() % BigInt(max + 1);
    return Math.min(Number(l), max);
  }

  _blob(max, useDict) {
    const w = Source._lenPrefix(max);
    const p0 = this._pos;
    const out = this._blobInner(max, useDict, w);
    if (this._spans && p0 + w <= this._len) this._spans.push(p0, w, out.length);
    return out;
  }

  _blobInner(max, useDict, w) {
    let content = null;
    const raw = leRead(this._draw(w, () => {
      if (useDict && this._dict.length && this._unit() < 0.5) {
        content = Buffer.from(this._dict[Number(this._rnd() % BigInt(this._dict.length))]).subarray(0, max);
      } else {
        const l = this._synthLen(max);
        const printable = this._unit() < 0.7;
        content = Buffer.alloc(l);
        for (let i = 0; i < l; i++) content[i] = printable ? 32 + Number(this._rnd() % 95n) : Number(this._rnd() & 0xffn);
      }
      return leWrite(BigInt(content.length), w);
    }));
    let len = Number(raw);
    if (len > max) len = len % (max + 1);
    if (content !== null) {
      const c = content.subarray(0, len);
      if (this._len + len <= this._cap) {
        this._append(c);
        this._pos += len;
        return Buffer.from(c);
      }
      this._exhausted = true;
      return Buffer.alloc(0);
    }
    const n = Math.min(len, Math.max(0, this._len - this._pos));
    const out = Buffer.from(this._buf.subarray(this._pos, this._pos + n));
    this._pos += n;
    if (n < len) this._exhausted = true;
    return out;
  }

  _range(lo, hi) {
    lo = BigInt(lo);
    hi = BigInt(hi);
    if (hi < lo) [lo, hi] = [hi, lo];
    const span = BigInt.asUintN(64, hi - lo + 1n);
    const raw = BigInt.asIntN(64, leRead(this._draw(8, () => {
      const u = this._unit();
      const v = u < 0.05 ? lo : u < 0.1 ? hi : lo + (span ? this._rnd() % span : this._rnd());
      return leWrite(v, 8);
    })));
    if (raw >= lo && raw <= hi) return raw;
    const off = BigInt.asUintN(64, raw - lo);
    return lo + (span ? off % span : off);
  }

  _logged(op, v, isBytes = false) {
    if (this._log) this._log(op, v, isBytes);
    return v;
  }

  // ---------------------------------------------------------------- public draws
  u8() { return this._logged("u8()", Number(this._uint(1))); }
  u16() { return this._logged("u16()", Number(this._uint(2))); }
  u32() { return this._logged("u32()", Number(this._uint(4))); }
  u64() { return this._logged("u64()", this._uint(8)); }
  i8() { return this._logged("i8()", Number(BigInt.asIntN(8, this._uint(1)))); }
  i16() { return this._logged("i16()", Number(BigInt.asIntN(16, this._uint(2)))); }
  i32() { return this._logged("i32()", Number(BigInt.asIntN(32, this._uint(4)))); }
  i64() { return this._logged("i64()", BigInt.asIntN(64, this._uint(8))); }
  range(lo, hi) { return this._logged(`range(${lo}, ${hi})`, Number(this._range(lo, hi))); }
  choice(n) { return this._logged(`choice(${n})`, n <= 0 ? 0 : Number(this._range(0, n - 1))); }
  pick(items) {
    if (!items.length) throw new RangeError("pick() from an empty list");
    const i = Number(this._range(0, items.length - 1));
    return this._logged(`pick(${items.length} items) -> #${i}`, items[i]);
  }
  coin(p = 0.5) {
    const thr = p * 256;
    const b = this._draw(1, () => {
      const yes = this._unit() < p;
      const r = this._rnd();
      if (yes) return [thr >= 1 ? Number(r % BigInt(Math.floor(Math.min(256, thr)))) : 0];
      return [Math.floor(Math.min(255, thr + Number(r % BigInt(Math.floor(Math.max(1, 256 - thr))))))];
    })[0];
    // a full or exhausted buffer ends coin-driven loops, like more()
    return this._logged(`coin(${p})`, b < thr && !this._exhausted);
  }
  more() {
    const at = Math.min(this._pos, this._len);
    let v = false;
    if (!(this._exhausted && !this._gen)) {
      const b = this._draw(1, () => {
        const fill = this._cap ? this._len / this._cap : 1;
        const p = fill > 0.5 ? 0.15 : 0.65;
        return [Number(this._rnd() & 0xfen) | (this._unit() < p ? 1 : 0)];
      })[0];
      v = !!(b & 1) && !this._exhausted;
    }
    if (this._items) {
      const { site, depth } = loopCaller();
      this._loopItem(site, depth, at, v);
    }
    return this._logged("more()", v);
  }
  _loopItem(site, frame, p, v) {
    const key = `${site}:${frame}`;
    let st = this._open.get(key);
    if (!st) {
      st = { site, inst: this._nextInst++, start: -1 };
      this._open.set(key, st);
    }
    if (st.start >= 0) this._items.push(site, st.inst, st.start, p);
    if (v) st.start = p;
    else {
      this._items.push(site, st.inst, p, p);
      this._open.delete(key);
    }
  }
  _finishItems() {
    for (const st of this._open.values()) {
      if (st.start >= 0) this._items.push(st.site, st.inst, st.start, this._len);
      this._items.push(st.site, st.inst, this._len, this._len);
    }
    this._open.clear();
  }
  // f64(): any double. f64(lo, hi): a finite double in [lo, hi].
  f64(lo, hi) {
    if (lo === undefined) return this._logged("f64()", leWrite(this._uint(8), 8).readDoubleLE(0));
    if (!(lo <= hi)) [lo, hi] = [hi, lo];
    const bits = leRead(this._draw(8, () => {
      const u = this._unit();
      let v = u < 0.08 ? lo : u < 0.16 ? hi : u < 0.22 && lo <= 0 && 0 <= hi ? 0 : lo + (hi - lo) * this._unit();
      if (!(v >= lo && v <= hi)) v = lo;
      const b = Buffer.alloc(8);
      b.writeDoubleLE(v, 0);
      return b;
    }));
    let v = leWrite(bits, 8).readDoubleLE(0);
    if (!(Number.isFinite(v) && v >= lo && v <= hi)) {
      const p = Number(bits >> 11n) / 9007199254740992;
      const w = hi - lo;
      v = Number.isFinite(w) ? lo + w * p : p < 0.5 ? lo + (hi / 2 - lo / 2) * (2 * p) : lo / 2 + hi / 2 + (hi / 2 - lo / 2) * (2 * p - 1);
      v = Math.min(hi, Math.max(lo, v));
    }
    return this._logged(`f64(${lo}, ${hi})`, v);
  }
  f32(lo, hi) {
    if (lo === undefined) return this._logged("f32()", leWrite(this._uint(4), 4).readFloatLE(0));
    if (!(lo <= hi)) [lo, hi] = [hi, lo];
    const bits = leRead(this._draw(4, () => {
      const u = this._unit();
      let v = u < 0.08 ? lo : u < 0.16 ? hi : lo + (hi - lo) * this._unit();
      if (!(v >= lo && v <= hi)) v = lo;
      const b = Buffer.alloc(4);
      b.writeFloatLE(v, 0);
      return b;
    }));
    let v = leWrite(bits, 4).readFloatLE(0);
    if (!(Number.isFinite(v) && v >= lo && v <= hi)) v = Math.fround(Math.min(hi, Math.max(lo, lo + (hi - lo) * (Number(bits >> 8n) / 16777216))));
    return this._logged(`f32(${lo}, ${hi})`, v);
  }
  // exactly n bytes (zeros past the end of the input)
  fixed(n) {
    if (n <= 0) return this._logged(`fixed(${n})`, Buffer.alloc(0), true);
    const v = this._draw(n, () => {
      const pr = this._unit() < 0.5;
      const b = Buffer.alloc(n);
      for (let i = 0; i < n; i++) b[i] = pr ? 32 + Number(this._rnd() % 95n) : Number(this._rnd() & 0xffn);
      return b;
    });
    return this._logged(`fixed(${n})`, v, true);
  }
  // every byte that is left
  rest() {
    const p0 = this._pos;
    if (this._gen && this._pos >= this._len && this._len < this._cap) {
      const room = this._cap - this._len;
      const l = Math.min(this._synthLen(room), room);
      const pr = this._unit() < 0.7;
      const b = Buffer.alloc(l);
      for (let i = 0; i < l; i++) b[i] = pr ? 32 + Number(this._rnd() % 95n) : Number(this._rnd() & 0xffn);
      this._len = this._pos;
      this._append(b);
    }
    const v = Buffer.from(this._buf.subarray(Math.min(this._pos, this._len), this._len));
    this._pos = this._len;
    if (this._spans) this._spans.push(p0, 0, v.length); // no length prefix
    return this._logged("rest()", v, true);
  }
  // a value from data type dt's decoder, drawn from this source: decoders build on each other
  draw(dt) {
    if (!dt || typeof dt.decoder !== "function") throw new TypeError(`${dt && dt.name} has no decoder`);
    return dt.decoder(this);
  }
  bytes(max = 64) { return this._logged(`bytes(${max})`, this._blob(max, false), true); }
  str(max = 64) {
    const v = this._blob(max, false);
    this._logged(`str(${max})`, v, true);
    return v.toString("utf8");
  }
  token() {
    const v = this._blob(255, true);
    this._logged("token()", v, true);
    return v.toString("utf8");
  }

  position() { return this._pos; }
  remaining() { return Math.max(0, this._len - this._pos); }
  exhausted() { return this._exhausted; }
  // the choice buffer after generation
  buffer() { return Buffer.from(this._buf.subarray(0, this._len)); }
}

module.exports = { Source };
