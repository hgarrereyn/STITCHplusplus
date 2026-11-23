"""stitch.Source: decisions inside decoders and stubs, read from a fuzzer-owned choice buffer.

Same encoding as the C++ runtime (languages/cpp/runtime/src/runtime.cpp), so the engine's
mutations (string spans, dictionary tokens) work the same way:
  integers  little endian, fixed width
  range     8 bytes; out-of-range values wrap into [lo, hi]
  coin(p)   1 byte b; true if b < p*256
  more()    1 byte; low bit
  str/bytes length prefix (1, 2 or 4 bytes, depending on max) + content
  f64(lo, hi) 8 bytes; the value itself when finite and in range, other bit patterns map into it
  fixed(n)  n bytes;  rest() every byte left
"""
import random
import struct
import sys
import zlib

_INTERESTING = [0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 512, 1000, 1024, 4096,
                0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff]


class Source:
    def __init__(self, buf=b"", cap=None, gen=False, seed=0, dict_tokens=None, spans=None, items=None):
        self._buf = bytearray(buf)
        self._pos = 0
        self._cap = max(cap if cap is not None else len(buf), len(buf))
        self._gen = gen
        self._rng = random.Random(seed) if gen else None
        self._dict = dict_tokens or []
        self._spans = spans
        self._exhausted = False
        self._log = None  # plan mode: callback(op, value, is_bytes)
        self._depth = 0
        # loop iterations for the engine: [site, instance, start, end] per more() iteration
        self._items = items
        self._open = {}
        self._next_inst = 0

    # ------------------------------------------------------------------ low level
    def _rnd(self):
        return self._rng.getrandbits(64)

    def _unit(self):
        return self._rng.random()

    def _draw(self, n, synth):
        buf = self._buf
        if self._pos + n <= len(buf):
            out = bytes(buf[self._pos:self._pos + n])
            self._pos += n
            return out
        if self._gen and len(buf) + n <= self._cap:
            del buf[self._pos:]
            fresh = bytes(synth())[:n].ljust(n, b"\0")
            buf += fresh
            self._pos += n
            return fresh
        out = bytes(buf[self._pos:]).ljust(n, b"\0")
        self._pos = len(buf)
        self._exhausted = True
        return out

    def _interesting(self, width):
        mask = (1 << (8 * width)) - 1
        r = self._rnd() % 4
        if r == 0:
            return mask
        if r == 1:
            return mask >> 1
        return _INTERESTING[self._rnd() % len(_INTERESTING)] & mask

    def _uint(self, width):
        def synth():
            x = self._interesting(width) if self._unit() < 0.3 else self._rnd()
            return (x & ((1 << (8 * width)) - 1)).to_bytes(width, "little")
        return int.from_bytes(self._draw(width, synth), "little")

    @staticmethod
    def _len_prefix(mx):
        return 1 if mx < 256 else 2 if mx < 65536 else 4

    def _synth_len(self, mx):
        u = self._unit()
        l = self._rnd() % 17 if u < 0.7 else self._rnd() % 65 if u < 0.95 else self._rnd() % (mx + 1)
        return min(l, mx)

    def _blob(self, mx, use_dict):
        w = self._len_prefix(mx)
        p0 = self._pos
        out = self._blob_inner(mx, use_dict, w)
        if self._spans is not None and p0 + w <= len(self._buf):
            self._spans += [p0, w, len(out)]
        return out

    def _blob_inner(self, mx, use_dict, w):
        content = []
        synthesized = [False]

        def synth():
            synthesized[0] = True
            if use_dict and self._dict and self._unit() < 0.5:
                c = bytes(self._dict[self._rnd() % len(self._dict)])[:mx]
            else:
                l = self._synth_len(mx)
                if self._unit() < 0.7:
                    c = bytes(32 + self._rnd() % 95 for _ in range(l))
                else:
                    c = bytes(self._rnd() & 0xff for _ in range(l))
            content.append(c)
            return len(c).to_bytes(8, "little")[:w]

        raw = int.from_bytes(self._draw(w, synth), "little")
        ln = raw if raw <= mx else raw % (mx + 1)
        if synthesized[0]:
            c = content[0][:ln]
            if len(self._buf) + ln <= self._cap:
                self._buf += c
                self._pos += ln
                return c
            self._exhausted = True
            return b""
        avail = max(0, len(self._buf) - self._pos)
        n = min(ln, avail)
        out = bytes(self._buf[self._pos:self._pos + n])
        self._pos += n
        if n < ln:
            self._exhausted = True
        return out

    def _range(self, lo, hi):
        if hi < lo:
            lo, hi = hi, lo
        span = (hi - lo + 1) & 0xFFFFFFFFFFFFFFFF

        def synth():
            u = self._unit()
            v = lo if u < 0.05 else hi if u < 0.1 else lo + (self._rnd() % span if span else self._rnd())
            return struct.pack("<q", ((v + (1 << 63)) % (1 << 64)) - (1 << 63))
        raw = struct.unpack("<q", self._draw(8, synth))[0]
        if lo <= raw <= hi:
            return raw
        off = (raw - lo) & 0xFFFFFFFFFFFFFFFF
        return lo + (off % span if span else off)

    def _logged(self, op, value, is_bytes=False):
        if self._log is not None and self._depth == 0:
            self._log(op, value, is_bytes)
        return value

    # ------------------------------------------------------------------ public draws
    def u8(self):
        return self._logged("u8()", self._uint(1))

    def u16(self):
        return self._logged("u16()", self._uint(2))

    def u32(self):
        return self._logged("u32()", self._uint(4))

    def u64(self):
        return self._logged("u64()", self._uint(8))

    def i8(self):
        return self._logged("i8()", self._signed(1))

    def i16(self):
        return self._logged("i16()", self._signed(2))

    def i32(self):
        return self._logged("i32()", self._signed(4))

    def i64(self):
        return self._logged("i64()", self._signed(8))

    def _signed(self, w):
        v = self._uint(w)
        return v - (1 << (8 * w)) if v >> (8 * w - 1) else v

    def f32(self, lo=None, hi=None):
        if lo is None:
            return self._logged("f32()", struct.unpack("<f", self._uint(4).to_bytes(4, "little"))[0])
        lo, hi = (lo, hi) if lo <= hi else (hi, lo)

        def synth():
            u = self._unit()
            v = lo if u < 0.08 else hi if u < 0.16 else lo + (hi - lo) * self._unit()
            return struct.pack("<f", v if lo <= v <= hi else lo)
        bits = int.from_bytes(self._draw(4, synth), "little")
        v = struct.unpack("<f", bits.to_bytes(4, "little"))[0]
        if not (v == v and v not in (float("inf"), float("-inf")) and lo <= v <= hi):
            p = (bits >> 8) / 16777216.0
            v = struct.unpack("<f", struct.pack("<f", min(hi, max(lo, lo + (hi - lo) * p))))[0]
        return self._logged(f"f32({lo}, {hi})", v)

    def f64(self, lo=None, hi=None):
        """Any double; with lo, hi: a finite double in [lo, hi]."""
        if lo is None:
            return self._logged("f64()", struct.unpack("<d", self._uint(8).to_bytes(8, "little"))[0])
        lo, hi = (float(lo), float(hi)) if lo <= hi else (float(hi), float(lo))

        def synth():
            u = self._unit()
            if u < 0.08:
                v = lo
            elif u < 0.16:
                v = hi
            elif u < 0.22 and lo <= 0 <= hi:
                v = 0.0
            else:
                v = lo + (hi - lo) * self._unit()
            return struct.pack("<d", v if lo <= v <= hi else lo)
        bits = int.from_bytes(self._draw(8, synth), "little")
        v = struct.unpack("<d", bits.to_bytes(8, "little"))[0]
        if not (v == v and abs(v) != float("inf") and lo <= v <= hi):
            p = (bits >> 11) / 9007199254740992.0
            w = hi - lo
            if w != float("inf"):
                v = lo + w * p
            elif p < 0.5:
                v = lo + (hi / 2 - lo / 2) * (2 * p)
            else:
                v = lo / 2 + hi / 2 + (hi / 2 - lo / 2) * (2 * p - 1)
            v = min(hi, max(lo, v))
        return self._logged(f"f64({lo}, {hi})", v)

    def fixed(self, n):
        """Exactly n bytes (zeros past the end of the input)."""
        def synth():
            pr = self._unit() < 0.5
            return bytes((32 + self._rnd() % 95) if pr else (self._rnd() & 0xff) for _ in range(n))
        return self._logged(f"fixed({n})", self._draw(n, synth) if n > 0 else b"", True)

    def rest(self):
        """Every byte that is left."""
        p0 = self._pos
        if self._gen and self._pos >= len(self._buf) and len(self._buf) < self._cap:
            room = self._cap - len(self._buf)
            l = min(self._synth_len(room), room)
            pr = self._unit() < 0.7
            del self._buf[self._pos:]
            self._buf += bytes((32 + self._rnd() % 95) if pr else (self._rnd() & 0xff) for _ in range(l))
        v = bytes(self._buf[min(self._pos, len(self._buf)):])
        self._pos = len(self._buf)
        if self._spans is not None:
            self._spans += [p0, 0, len(v)]  # no length prefix
        return self._logged("rest()", v, True)

    def draw(self, dt):
        """A value from data type dt's decoder, drawn from this source: decoders build on each other."""
        fn = getattr(dt, "decoder", None)
        if fn is None:
            raise TypeError(f"{dt!r} has no decoder")
        return fn(self)

    def range(self, lo, hi):
        return self._logged(f"range({lo}, {hi})", self._range(lo, hi))

    def choice(self, n):
        return self._logged(f"choice({n})", 0 if n <= 0 else self._range(0, n - 1))

    def pick(self, items):
        items = list(items)
        if not items:
            raise IndexError("pick() from an empty sequence")
        self._depth += 1
        try:
            i = self._range(0, len(items) - 1)
        finally:
            self._depth -= 1
        return self._logged(f"pick({len(items)} items) -> #{i}", items[i])

    def coin(self, p=0.5):
        thr = p * 256.0

        def synth():
            yes = self._unit() < p
            r = self._rnd()
            if yes:
                return bytes([int(r % int(min(256.0, thr))) if thr >= 1 else 0])
            return bytes([int(min(255.0, thr + (r % int(max(1.0, 256.0 - thr)))))])
        b = self._draw(1, synth)[0]
        # a full or exhausted buffer ends coin-driven loops, like more()
        return self._logged(f"coin({p})", b < thr and not self._exhausted)

    def more(self):
        at = min(self._pos, len(self._buf))
        if self._exhausted and not self._gen:
            v = False
        else:
            def synth():
                fill = len(self._buf) / self._cap if self._cap else 1.0
                p = 0.15 if fill > 0.5 else 0.65
                return bytes([(self._rnd() & 0xfe) | (1 if self._unit() < p else 0)])
            b = self._draw(1, synth)[0]
            v = bool(b & 1) and not self._exhausted
        if self._items is not None:
            f = sys._getframe(1)  # the loop: its source line, and the frame (recursion level)
            site = zlib.crc32(f"{f.f_code.co_filename}:{f.f_lineno}".encode())
            self._loop_item(site, id(f), at, v)
        return self._logged("more()", v)

    def _loop_item(self, site, frame, p, v):
        key = (site, frame)
        st = self._open.get(key)
        if st is None:
            st = self._open[key] = [self._next_inst, -1]
            self._next_inst += 1
        if st[1] >= 0:
            self._items += [site, st[0], st[1], p]
        if v:
            st[1] = p
        else:
            self._items += [site, st[0], p, p]
            del self._open[key]

    def _finish_items(self):
        n = len(self._buf)
        for (site, _), (inst, start) in self._open.items():
            if start >= 0:
                self._items += [site, inst, start, n]
            self._items += [site, inst, n, n]
        self._open = {}

    def bytes(self, max=64):
        return self._logged(f"bytes({max})", self._blob(max, False), True)

    def str(self, max=64):
        v = self._blob(max, False)
        self._logged(f"str({max})", v, True)
        return v.decode("utf-8", "replace")

    def token(self):
        v = self._blob(255, True)
        self._logged("token()", v, True)
        return v.decode("utf-8", "replace")

    # ------------------------------------------------------------------ state
    def position(self):
        return self._pos

    def remaining(self):
        return max(0, len(self._buf) - self._pos)

    def exhausted(self):
        return self._exhausted
