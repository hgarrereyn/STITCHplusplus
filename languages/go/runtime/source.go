package stitch

import (
	"encoding/binary"
	"fmt"
	"hash/fnv"
	"math"
	"runtime"
)

// Source provides decisions inside decoders and stubs, read from a fuzzer-owned choice buffer.
// Same encoding as the C++ runtime: little-endian fixed-width integers, 8-byte ranges,
// 1-byte coin/more, length-prefixed strings.
type Source struct {
	buf       []byte
	pos       int
	cap       int
	gen       bool
	rng       uint64
	dict      [][]byte
	spans     *[]uint32
	exhausted bool
	log       func(op string, v any, isBytes bool)
	// loop iterations for the engine: site, instance, start, end per More() iteration
	items    *[]uint32
	open     map[[2]uint64]*[3]uint32 // (site, depth) -> {instance, start+1, site}
	nextInst uint32
}

var interesting = []uint64{0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 512, 1000, 1024, 4096,
	0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff}

// NewSource replays a recorded choice buffer (standalone reproducers).
func NewSource(choices []byte) *Source {
	return newSource(choices, len(choices), false, 0, nil, nil)
}

func newSource(b []byte, cap int, gen bool, seed uint64, dict [][]byte, spans *[]uint32) *Source {
	if cap < len(b) {
		cap = len(b)
	}
	return &Source{buf: append([]byte(nil), b...), cap: cap, gen: gen, rng: seed, dict: dict, spans: spans}
}

func (s *Source) rnd() uint64 {
	s.rng += 0x9E3779B97F4A7C15
	z := s.rng
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
	z = (z ^ (z >> 27)) * 0x94D049BB133111EB
	return z ^ (z >> 31)
}

func (s *Source) unit() float64 { return float64(s.rnd()>>11) / 9007199254740992.0 }

func (s *Source) draw(n int, synth func() []byte) []byte {
	out := make([]byte, n)
	if s.pos+n <= len(s.buf) {
		copy(out, s.buf[s.pos:s.pos+n])
		s.pos += n
		return out
	}
	if s.gen && len(s.buf)+n <= s.cap {
		s.buf = s.buf[:s.pos]
		copy(out, synth())
		s.buf = append(s.buf, out...)
		s.pos += n
		return out
	}
	if s.pos < len(s.buf) {
		copy(out, s.buf[s.pos:])
	}
	s.pos = len(s.buf)
	s.exhausted = true
	return out
}

func le(b []byte) uint64 {
	var v uint64
	for i := len(b) - 1; i >= 0; i-- {
		v = v<<8 | uint64(b[i])
	}
	return v
}

func toLE(v uint64, n int) []byte {
	b := make([]byte, n)
	for i := 0; i < n; i++ {
		b[i] = byte(v)
		v >>= 8
	}
	return b
}

func (s *Source) interesting(width int) uint64 {
	mask := ^uint64(0)
	if width < 8 {
		mask = 1<<(8*width) - 1
	}
	switch s.rnd() % 4 {
	case 0:
		return mask
	case 1:
		return mask >> 1
	}
	return interesting[s.rnd()%uint64(len(interesting))] & mask
}

func (s *Source) uint(width int) uint64 {
	return le(s.draw(width, func() []byte {
		x := s.rnd()
		if s.unit() < 0.3 {
			x = s.interesting(width)
		}
		return toLE(x, width)
	}))
}

func lenPrefix(max int) int {
	if max < 256 {
		return 1
	}
	if max < 65536 {
		return 2
	}
	return 4
}

func (s *Source) synthLen(max int) int {
	u := s.unit()
	var l uint64
	switch {
	case u < 0.7:
		l = s.rnd() % 17
	case u < 0.95:
		l = s.rnd() % 65
	default:
		l = s.rnd() % uint64(max+1)
	}
	if int(l) > max {
		return max
	}
	return int(l)
}

func (s *Source) blob(max int, useDict bool) []byte {
	w := lenPrefix(max)
	p0 := s.pos
	out := s.blobInner(max, useDict, w)
	if s.spans != nil && p0+w <= len(s.buf) {
		*s.spans = append(*s.spans, uint32(p0), uint32(w), uint32(len(out)))
	}
	return out
}

func (s *Source) blobInner(max int, useDict bool, w int) []byte {
	var content []byte
	synthesized := false
	raw := le(s.draw(w, func() []byte {
		synthesized = true
		if useDict && len(s.dict) > 0 && s.unit() < 0.5 {
			t := s.dict[s.rnd()%uint64(len(s.dict))]
			if len(t) > max {
				t = t[:max]
			}
			content = append([]byte(nil), t...)
		} else {
			l := s.synthLen(max)
			printable := s.unit() < 0.7
			content = make([]byte, l)
			for i := range content {
				if printable {
					content[i] = byte(32 + s.rnd()%95)
				} else {
					content[i] = byte(s.rnd())
				}
			}
		}
		return toLE(uint64(len(content)), w)
	}))
	ln := raw
	if ln > uint64(max) {
		ln %= uint64(max + 1)
	}
	if synthesized {
		if len(s.buf)+int(ln) <= s.cap {
			c := content[:ln]
			s.buf = append(s.buf, c...)
			s.pos += int(ln)
			return append([]byte(nil), c...)
		}
		s.exhausted = true
		return nil
	}
	avail := len(s.buf) - s.pos
	if avail < 0 {
		avail = 0
	}
	n := int(ln)
	if n > avail {
		n = avail
	}
	out := append([]byte(nil), s.buf[s.pos:s.pos+n]...)
	s.pos += n
	if n < int(ln) {
		s.exhausted = true
	}
	return out
}

func (s *Source) rangeImpl(lo, hi int64) int64 {
	if hi < lo {
		lo, hi = hi, lo
	}
	span := uint64(hi) - uint64(lo) + 1
	raw := int64(le(s.draw(8, func() []byte {
		u := s.unit()
		v := lo
		switch {
		case u < 0.05:
		case u < 0.1:
			v = hi
		default:
			if span != 0 {
				v = int64(uint64(lo) + s.rnd()%span)
			} else {
				v = int64(uint64(lo) + s.rnd())
			}
		}
		return toLE(uint64(v), 8)
	})))
	if raw >= lo && raw <= hi {
		return raw
	}
	off := uint64(raw) - uint64(lo)
	if span != 0 {
		off %= span
	}
	return int64(uint64(lo) + off)
}

// logged reports a draw in plan mode; op builds its label only then.
func (s *Source) logged(op func() string, v any, isBytes bool) {
	if s.log != nil {
		s.log(op(), v, isBytes)
	}
}

func label(s string) func() string { return func() string { return s } }

func (s *Source) U8() uint8   { v := uint8(s.uint(1)); s.logged(label("u8()"), v, false); return v }
func (s *Source) U16() uint16 { v := uint16(s.uint(2)); s.logged(label("u16()"), v, false); return v }
func (s *Source) U32() uint32 { v := uint32(s.uint(4)); s.logged(label("u32()"), v, false); return v }
func (s *Source) U64() uint64 { v := s.uint(8); s.logged(label("u64()"), v, false); return v }
func (s *Source) I8() int8    { v := int8(s.uint(1)); s.logged(label("i8()"), v, false); return v }
func (s *Source) I16() int16  { v := int16(s.uint(2)); s.logged(label("i16()"), v, false); return v }
func (s *Source) I32() int32  { v := int32(s.uint(4)); s.logged(label("i32()"), v, false); return v }
func (s *Source) I64() int64  { v := int64(s.uint(8)); s.logged(label("i64()"), v, false); return v }
func (s *Source) F32() float32 {
	v := math.Float32frombits(uint32(s.uint(4)))
	s.logged(label("f32()"), v, false)
	return v
}
func (s *Source) F64() float64 {
	v := math.Float64frombits(s.uint(8))
	s.logged(label("f64()"), v, false)
	return v
}

// Range returns an integer in [lo, hi].
func (s *Source) Range(lo, hi int64) int64 {
	v := s.rangeImpl(lo, hi)
	s.logged(func() string { return fmt.Sprintf("range(%d, %d)", lo, hi) }, v, false)
	return v
}

// Choice returns an index in [0, n).
func (s *Source) Choice(n int) int {
	v := 0
	if n > 0 {
		v = int(s.rangeImpl(0, int64(n-1)))
	}
	s.logged(func() string { return fmt.Sprintf("choice(%d)", n) }, v, false)
	return v
}

// Coin is true with probability about p.
func (s *Source) Coin(p float64) bool {
	thr := p * 256.0
	b := s.draw(1, func() []byte {
		yes := s.unit() < p
		r := s.rnd()
		if yes {
			if thr >= 1 {
				return []byte{byte(r % uint64(math.Min(256.0, thr)))}
			}
			return []byte{0}
		}
		return []byte{byte(math.Min(255.0, thr+float64(r%uint64(math.Max(1.0, 256.0-thr)))))}
	})[0]
	// a full or exhausted buffer ends coin-driven loops, like More()
	v := float64(b) < thr && !s.exhausted
	s.logged(func() string { return fmt.Sprintf("coin(%g)", p) }, v, false)
	return v
}

// More is a loop condition: `for s.More() { ... }`.
func (s *Source) More() bool {
	at := s.pos
	if at > len(s.buf) {
		at = len(s.buf)
	}
	if s.exhausted && !s.gen {
		s.loopItem(at, false)
		s.logged(label("more()"), false, false)
		return false
	}
	b := s.draw(1, func() []byte {
		fill := 1.0
		if s.cap > 0 {
			fill = float64(len(s.buf)) / float64(s.cap)
		}
		p := 0.65
		if fill > 0.5 {
			p = 0.15
		}
		bit := uint64(0)
		if s.unit() < p {
			bit = 1
		}
		return []byte{byte(s.rnd()&0xfe | bit)}
	})[0]
	v := b&1 == 1 && !s.exhausted
	s.loopItem(at, v)
	s.logged(label("more()"), v, false)
	return v
}

// loopItem records one More() for the engine: the loop is its call site plus the stack depth
// (recursion level).
func (s *Source) loopItem(p int, v bool) {
	if s.items == nil {
		return
	}
	var pcs [1]uintptr
	runtime.Callers(3, pcs[:])
	h := fnv.New32a()
	fmt.Fprintf(h, "%x", pcs[0])
	site := h.Sum32()
	big := make([]uintptr, 1024)
	depth := runtime.Callers(0, big)
	if s.open == nil {
		s.open = map[[2]uint64]*[3]uint32{}
	}
	key := [2]uint64{uint64(site), uint64(depth)}
	st := s.open[key]
	if st == nil {
		st = &[3]uint32{s.nextInst, 0, site}
		s.nextInst++
		s.open[key] = st
	}
	if st[1] > 0 {
		*s.items = append(*s.items, site, st[0], st[1]-1, uint32(p))
	}
	if v {
		st[1] = uint32(p) + 1
	} else {
		*s.items = append(*s.items, site, st[0], uint32(p), uint32(p))
		delete(s.open, key)
	}
}

func (s *Source) finishItems() {
	n := uint32(len(s.buf))
	for _, st := range s.open {
		if st[1] > 0 {
			*s.items = append(*s.items, st[2], st[0], st[1]-1, n)
		}
		*s.items = append(*s.items, st[2], st[0], n, n)
	}
	s.open = nil
}

// F64Range returns a finite float64 in [lo, hi]: the 8 bytes are the value itself when it is in range.
func (s *Source) F64Range(lo, hi float64) float64 {
	if !(lo <= hi) {
		lo, hi = hi, lo
	}
	bits := le(s.draw(8, func() []byte {
		u := s.unit()
		x := lo + (hi-lo)*s.unit()
		switch {
		case u < 0.08:
			x = lo
		case u < 0.16:
			x = hi
		case u < 0.22 && lo <= 0 && 0 <= hi:
			x = 0
		}
		if !(x >= lo && x <= hi) {
			x = lo
		}
		return toLE(math.Float64bits(x), 8)
	}))
	v := math.Float64frombits(bits)
	if !(!math.IsNaN(v) && !math.IsInf(v, 0) && v >= lo && v <= hi) {
		p := float64(bits>>11) / 9007199254740992.0
		w := hi - lo
		if !math.IsInf(w, 0) {
			v = lo + w*p
		} else if p < 0.5 {
			v = lo + (hi/2-lo/2)*(2*p)
		} else {
			v = lo/2 + hi/2 + (hi/2-lo/2)*(2*p-1)
		}
		v = math.Min(hi, math.Max(lo, v))
	}
	s.logged(func() string { return fmt.Sprintf("f64(%g, %g)", lo, hi) }, v, false)
	return v
}

// F32Range returns a finite float32 in [lo, hi].
func (s *Source) F32Range(lo, hi float32) float32 {
	if !(lo <= hi) {
		lo, hi = hi, lo
	}
	bits := uint32(le(s.draw(4, func() []byte {
		u := s.unit()
		x := float32(float64(lo) + (float64(hi)-float64(lo))*s.unit())
		if u < 0.08 {
			x = lo
		} else if u < 0.16 {
			x = hi
		}
		if !(x >= lo && x <= hi) {
			x = lo
		}
		return toLE(uint64(math.Float32bits(x)), 4)
	})))
	v := math.Float32frombits(bits)
	if !(!math.IsNaN(float64(v)) && !math.IsInf(float64(v), 0) && v >= lo && v <= hi) {
		p := float64(bits>>8) / 16777216.0
		v = float32(math.Min(float64(hi), math.Max(float64(lo), float64(lo)+(float64(hi)-float64(lo))*p)))
	}
	s.logged(func() string { return fmt.Sprintf("f32(%g, %g)", lo, hi) }, v, false)
	return v
}

// Fixed returns exactly n bytes (zeros past the end of the input).
func (s *Source) Fixed(n int) []byte {
	if n <= 0 {
		return []byte{}
	}
	v := s.draw(n, func() []byte {
		pr := s.unit() < 0.5
		b := make([]byte, n)
		for i := range b {
			if pr {
				b[i] = byte(32 + s.rnd()%95)
			} else {
				b[i] = byte(s.rnd())
			}
		}
		return b
	})
	s.logged(func() string { return fmt.Sprintf("fixed(%d)", n) }, v, true)
	return v
}

// Rest returns every byte that is left.
func (s *Source) Rest() []byte {
	p0 := s.pos
	if s.gen && s.pos >= len(s.buf) && len(s.buf) < s.cap {
		room := s.cap - len(s.buf)
		l := s.synthLen(room)
		if l > room {
			l = room
		}
		pr := s.unit() < 0.7
		s.buf = s.buf[:s.pos]
		for i := 0; i < l; i++ {
			if pr {
				s.buf = append(s.buf, byte(32+s.rnd()%95))
			} else {
				s.buf = append(s.buf, byte(s.rnd()))
			}
		}
	}
	start := s.pos
	if start > len(s.buf) {
		start = len(s.buf)
	}
	v := append([]byte(nil), s.buf[start:]...)
	s.pos = len(s.buf)
	if s.spans != nil {
		*s.spans = append(*s.spans, uint32(p0), 0, uint32(len(v))) // no length prefix
	}
	s.logged(label("rest()"), v, true)
	return v
}

// Draw returns a value of data type `name`, drawn from s by its decoder: decoders build on each other.
func Draw[T any](s *Source, name string) T {
	d := reg.datatype(name)
	if d.value == nil {
		panic(fmt.Sprintf("stitch.Draw: data type %q has no decoder", name))
	}
	return d.value(s).(T)
}

func (s *Source) Bytes(max int) []byte {
	v := s.blob(max, false)
	s.logged(func() string { return fmt.Sprintf("bytes(%d)", max) }, v, true)
	return v
}

func (s *Source) Str(max int) string {
	v := s.blob(max, false)
	s.logged(func() string { return fmt.Sprintf("str(%d)", max) }, v, true)
	return string(v)
}

// Token returns a token from the data type's dictionaries.
func (s *Source) Token() string {
	v := s.blob(255, true)
	s.logged(label("token()"), v, true)
	return string(v)
}

func (s *Source) Position() int  { return s.pos }
func (s *Source) Exhausted() bool { return s.exhausted }

// Pick returns one of items.
func Pick[T any](s *Source, items ...T) T {
	if len(items) == 0 {
		panic("stitch.Pick from an empty list")
	}
	i := int(s.rangeImpl(0, int64(len(items)-1)))
	s.logged(func() string { return fmt.Sprintf("pick(%d items) -> #%d", len(items), i) }, i, false)
	return items[i]
}

var _ = binary.LittleEndian
