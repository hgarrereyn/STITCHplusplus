package harness

import (
	"bytes"
	"math"
	"reflect"
	"sort"
	"strings"
	"time"
	"unicode/utf8"

	"github.com/BurntSushi/toml"
	"stitch"
)

type Doc struct{ M map[string]interface{} }
type Meta struct{ MD toml.MetaData }
type Prim struct{ P toml.Primitive }
type Enc struct {
	E   *toml.Encoder
	Buf *bytes.Buffer
}

type Inner struct {
	Name string
	N    int
	Tags []string
}
type Level string

func (l *Level) UnmarshalText(b []byte) error {
	if len(b) > 8 {
		return stitchErr("too long")
	}
	*l = Level(strings.ToUpper(string(b)))
	return nil
}
func (l Level) MarshalText() ([]byte, error) { return []byte(strings.ToLower(string(l))), nil }

type stitchErr string

func (e stitchErr) Error() string { return string(e) }

type Typed struct {
	Title    string
	N        int64
	F        float64
	B        bool
	D        time.Time
	Arr      []int
	Owner    Inner
	Products []Inner
	Lvl      Level
	Dur      time.Duration
	Any      interface{}
	Opt      *Inner
	M        map[string]int
}

var _ = func() bool {
	stitch.Object[*Doc]("Doc", nil)
	stitch.Object[*Meta]("Meta", nil)
	stitch.Object[*Prim]("Prim", nil)
	stitch.Object[*Enc]("Enc", func(e *Enc) { e.Buf.Reset() })
	return true
}()

func init() {
	stitch.Init(func() { time.Local = time.UTC })
	stitch.Endpoint("decode", decode, "text:max=4096,type=TomlText")
	stitch.Endpoint("decode_meta", decodeMeta, "text:max=4096,type=TomlText", "out0:optional", "out1:optional")
	stitch.Endpoint("decode_reader", decodeReader, "text:max=4096,type=TomlText")
	stitch.Endpoint("decode_struct", decodeStruct, "text:max=4096,type=TomlText")
	stitch.Endpoint("decode_struct_meta", decodeStructMeta, "text:max=4096,type=TomlText")
	stitch.Endpoint("decode_prim", decodePrim, "text:max=4096,type=TomlText", "s")
	stitch.Endpoint("prim_decode", primDecode, "p", "which:range=0..3")
	stitch.Endpoint("meta_keys", metaKeys, "m", "s")
	stitch.Endpoint("meta_undecoded", metaUndecoded, "m")
	stitch.Endpoint("doc_set_int", docSetInt, "d", "key:max=12", "v:range=-100000..100000")
	stitch.Endpoint("doc_set_str", docSetStr, "d", "key:max=12", "v:max=24")
	stitch.Endpoint("doc_set_float", docSetFloat, "d", "key:max=12", "v")
	stitch.Endpoint("doc_set_time", docSetTime, "d", "key:max=12", "secs:range=-62135596800..253402300799")
	stitch.Endpoint("doc_set_array", docSetArr, "d", "key:max=12", "s")
	stitch.Endpoint("doc_set_tables", docSetTables, "d", "key:max=12", "s")
	stitch.Endpoint("doc_set_mixed", docSetMixed, "d", "key:max=12", "s")
	stitch.Endpoint("doc_nest", docNest, "d", "key:max=12")
	stitch.Endpoint("lookup", lookup, "d", "s", "out0:optional")
	stitch.Endpoint("new_encoder", newEncoder, "indent:oneof=|  |\t|    ")
	stitch.Endpoint("encode_doc", encodeDoc, "e", "d")
	stitch.Endpoint("encode_typed", encodeTyped, "e", "text:max=2048,type=TomlText")
	stitch.Endpoint("roundtrip", roundtrip, "d", "indent:oneof=|  |\t")
	stitch.Endpoint("roundtrip_typed", roundtripTyped, "text:max=2048,type=TomlText")
	stitch.Endpoint("encode_stable", encodeStable, "text:max=2048,type=TomlText")
}

func sortedKeys(m map[string]interface{}) []string {
	ks := make([]string, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	return ks
}

func decode(text string) (*Doc, error) {
	d := &Doc{M: map[string]interface{}{}}
	if _, err := toml.Decode(text, &d.M); err != nil {
		return nil, err
	}
	return d, nil
}

func decodeMeta(text string) (*Doc, *Meta) {
	d := &Doc{M: map[string]interface{}{}}
	md, err := toml.Decode(text, &d.M)
	stitch.Require(err == nil, "parse error")
	m := &Meta{MD: md}
	return d, m
}

func decodeReader(text string) {
	d := map[string]interface{}{}
	_, err := toml.DecodeReader(strings.NewReader(text), &d)
	d2 := map[string]interface{}{}
	_, err2 := toml.NewDecoder(strings.NewReader(text)).Decode(&d2)
	stitch.Check((err == nil) == (err2 == nil), "DecodeReader and NewDecoder agree")
	if err == nil {
		stitch.Check(eq(d, d2), "DecodeReader and NewDecoder give same value")
	}
	d3 := map[string]interface{}{}
	_, err3 := toml.Decode(text, &d3)
	stitch.Check((err == nil) == (err3 == nil), "Decode and DecodeReader agree")
}

func decodeStruct(text string) {
	var t Typed
	_, err := toml.Decode(text, &t)
	stitch.Require(err == nil, "decode error")
}

func decodeStructMeta(text string) {
	var t Typed
	md, err := toml.Decode(text, &t)
	stitch.Require(err == nil, "decode error")
	for _, k := range md.Keys() {
		if len(k) == 1 {
			stitch.Check(md.IsDefined(k...), "every top key of Keys() is defined")
		}
		_ = md.Type(k...)
	}
	_ = md.Undecoded()
	_ = md.IsDefined()
	_ = md.Type()
}

func decodePrim(text string, s *stitch.Source) (*Prim, *Meta) {
	var raw map[string]toml.Primitive
	md, err := toml.Decode(text, &raw)
	stitch.Require(err == nil, "parse error")
	ks := make([]string, 0)
	for k := range raw {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	stitch.Require(len(ks) > 0, "no keys")
	p := &Prim{P: raw[stitch.Pick(s, ks...)]}
	return p, &Meta{MD: md}
}

func primDecode(p *Prim, which int) {
	m := &Meta{}
	_ = m
	switch which {
	case 0:
		var v interface{}
		_ = toml.PrimitiveDecode(p.P, &v)
	case 1:
		var v Inner
		_ = toml.PrimitiveDecode(p.P, &v)
	case 2:
		var v []int
		_ = toml.PrimitiveDecode(p.P, &v)
	case 3:
		var v time.Time
		_ = toml.PrimitiveDecode(p.P, &v)
	}
}

func metaKeys(m *Meta, s *stitch.Source) {
	ks := m.MD.Keys()
	stitch.Require(len(ks) > 0, "no keys")
	k := ks[s.Choice(len(ks))]
	if len(k) == 1 {
		stitch.Check(m.MD.IsDefined(k...), "top key from Keys() is defined")
	}
	_ = m.MD.Type(k...)
	_ = k.String()
}

func metaUndecoded(m *Meta) {
	u := m.MD.Undecoded()
	for _, k := range u {
		if len(k) == 1 {
			stitch.Check(m.MD.IsDefined(k...), "undecoded key is defined")
		}
	}
}

func docSetInt(d *Doc, key string, v int64) { d.M[key] = v }
func docSetStr(d *Doc, key string, v string) {
	stitch.Require(utf8.ValidString(key) && utf8.ValidString(v), "invalid utf8")
	d.M[key] = v
}
func docSetFloat(d *Doc, key string, s *stitch.Source) { d.M[key] = s.F64() }
func docSetTime(d *Doc, key string, secs int64) {
	d.M[key] = time.Unix(secs, 0).UTC()
}
func docSetArr(d *Doc, key string, s *stitch.Source) {
	var a []interface{}
	for s.More() {
		switch s.Choice(3) {
		case 0:
			a = append(a, s.I64())
		case 1:
			a = append(a, s.Str(6))
		default:
			a = append(a, []interface{}{s.I64()})
		}
	}
	d.M[key] = a
}

// docSetTables: array of tables only (the mixed array/table case is its own stub).
func docSetTables(d *Doc, key string, s *stitch.Source) {
	var a []map[string]interface{}
	for s.More() {
		a = append(a, map[string]interface{}{s.Str(4): s.I64()})
	}
	stitch.Require(len(a) > 0, "empty")
	d.M[key] = a
}

func docSetMixed(d *Doc, key string, s *stitch.Source) {
	a := []interface{}{s.I64(), map[string]interface{}{s.Str(4): s.I64()}}
	d.M[key] = a
}

func docNest(d *Doc, key string) {
	d.M[key] = map[string]interface{}{"x": int64(1)}
}

func lookup(d *Doc, s *stitch.Source) *Doc {
	ks := sortedKeys(d.M)
	stitch.Require(len(ks) > 0, "empty")
	if m, ok := d.M[stitch.Pick(s, ks...)].(map[string]interface{}); ok {
		return &Doc{M: m}
	}
	stitch.Bail("not table")
	return nil
}

func newEncoder(indent string) *Enc {
	b := &bytes.Buffer{}
	e := toml.NewEncoder(b)
	e.Indent = indent
	return &Enc{E: e, Buf: b}
}

func okStrings(v interface{}) bool {
	switch x := v.(type) {
	case string:
		return utf8.ValidString(x)
	case map[string]interface{}:
		for k, e := range x {
			if !utf8.ValidString(k) || !okStrings(e) {
				return false
			}
		}
	case []interface{}:
		for _, e := range x {
			if !okStrings(e) {
				return false
			}
		}
	case []map[string]interface{}:
		for _, e := range x {
			if !okStrings(e) {
				return false
			}
		}
	}
	return true
}

func encodeDoc(e *Enc, d *Doc) {
	stitch.Require(okStrings(d.M), "invalid utf8")
	e.Buf.Reset()
	err := e.E.Encode(d.M)
	stitch.Require(err == nil, "encode error")
	back := map[string]interface{}{}
	_, err = toml.Decode(e.Buf.String(), &back)
	if err != nil {
		stitch.Log("err=%v\nout=%q", err, e.Buf.String())
	}
	stitch.Check(err == nil, "encoder output decodes")
}

func encodeTyped(e *Enc, text string) {
	var t Typed
	_, err := toml.Decode(text, &t)
	stitch.Require(err == nil, "decode error")
	e.Buf.Reset()
	err = e.E.Encode(&t)
	stitch.Require(err == nil, "encode error")
	var t2 Typed
	_, err = toml.Decode(e.Buf.String(), &t2)
	if err != nil {
		stitch.Log("err=%v\nout=%q", err, e.Buf.String())
	}
	stitch.Check(err == nil, "typed encoder output decodes")
}

// eq compares with NaN equal to NaN and times via Equal.
func eq(a, b interface{}) bool {
	switch x := a.(type) {
	case float64:
		y, ok := b.(float64)
		return ok && (x == y || (math.IsNaN(x) && math.IsNaN(y)))
	case time.Time:
		y, ok := b.(time.Time)
		return ok && x.Equal(y)
	case map[string]interface{}:
		y, ok := b.(map[string]interface{})
		if !ok || len(x) != len(y) {
			return false
		}
		for k, v := range x {
			w, ok := y[k]
			if !ok || !eq(v, w) {
				return false
			}
		}
		return true
	case []interface{}:
		y, ok := b.([]interface{})
		if !ok || len(x) != len(y) {
			return false
		}
		for i := range x {
			if !eq(x[i], y[i]) {
				return false
			}
		}
		return true
	case []map[string]interface{}:
		y, ok := b.([]map[string]interface{})
		if !ok || len(x) != len(y) {
			return false
		}
		for i := range x {
			if !eq(x[i], y[i]) {
				return false
			}
		}
		return true
	}
	return reflect.DeepEqual(a, b)
}

func roundtrip(d *Doc, indent string) {
	stitch.Require(okStrings(d.M), "invalid utf8")
	var buf bytes.Buffer
	enc := toml.NewEncoder(&buf)
	enc.Indent = indent
	stitch.Require(enc.Encode(d.M) == nil, "encode error")
	back := map[string]interface{}{}
	_, err := toml.Decode(buf.String(), &back)
	if err != nil {
		stitch.Log("err=%v\nout=%q", err, buf.String())
	}
	stitch.Check(err == nil, "encoder output decodes")
	if !eq(norm(d.M), norm(back)) {
		stitch.Log("in=%#v\nout=%q\nback=%#v", d.M, buf.String(), back)
	}
	stitch.Check(eq(norm(d.M), norm(back)), "decode(encode(x)) == x")
}

// norm folds []map and []interface{} of tables together, and int types.
func norm(v interface{}) interface{} {
	switch x := v.(type) {
	case map[string]interface{}:
		o := map[string]interface{}{}
		for k, e := range x {
			o[k] = norm(e)
		}
		return o
	case []map[string]interface{}:
		o := make([]interface{}, len(x))
		for i, e := range x {
			o[i] = norm(e)
		}
		return o
	case []interface{}:
		o := make([]interface{}, len(x))
		for i, e := range x {
			o[i] = norm(e)
		}
		return o
	case time.Time:
		return x.UTC()
	}
	return v
}

func roundtripTyped(text string) {
	var t Typed
	_, err := toml.Decode(text, &t)
	stitch.Require(err == nil, "decode error")
	var buf bytes.Buffer
	stitch.Require(toml.NewEncoder(&buf).Encode(&t) == nil, "encode error")
	var t2 Typed
	_, err = toml.Decode(buf.String(), &t2)
	stitch.Check(err == nil, "typed output decodes")
	stitch.Require(!math.IsNaN(t.F), "nan")
	if err == nil {
		t.D, t2.D = t.D.UTC(), t2.D.UTC()
		stitch.Check(reflect.DeepEqual(t.Title, t2.Title) && t.N == t2.N && t.B == t2.B && t.F == t2.F && t.D.Equal(t2.D) && reflect.DeepEqual(t.Arr, t2.Arr) && t.Lvl == t2.Lvl, "typed roundtrip fields equal")
	}
}

func encodeStable(text string) {
	d := map[string]interface{}{}
	_, err := toml.Decode(text, &d)
	stitch.Require(err == nil, "parse error")
	var b1 bytes.Buffer
	stitch.Require(toml.NewEncoder(&b1).Encode(d) == nil, "encode error")
	d2 := map[string]interface{}{}
	_, err = toml.Decode(b1.String(), &d2)
	stitch.Check(err == nil, "encoder output decodes")
	var b2 bytes.Buffer
	stitch.Require(toml.NewEncoder(&b2).Encode(d2) == nil, "encode2 error")
	stitch.Check(b1.String() == b2.String(), "encode is stable across a round trip")
}
