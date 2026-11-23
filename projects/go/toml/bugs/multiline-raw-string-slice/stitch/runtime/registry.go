package stitch

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"strconv"
	"strings"
)

// Path is a file parameter: the path of a temporary file holding fuzzed bytes.
type Path string

type objType struct {
	name string
	typ  reflect.Type
	drop reflect.Value
}

type datatype struct {
	name    string
	decoder func(*Source) []byte // bytes for Str/Bytes params, or the value as text
	value   func(*Source) any    // the decoder's value (Val params, Draw)
	dict    [][]byte
}

type param struct {
	name     string
	typ      reflect.Type
	kind     string // int float bool range oneof flags bytes str file source
	ctype    string
	size     int
	signed   bool
	lo, hi   int64
	values   []int64
	choices  []string // oneof over strings: the engine sees indices
	mask     int64
	datatype string
}

type arg struct {
	name  string
	obj   *objType
	take  bool
	param *param
}

type stub struct {
	name      string
	file      string
	fn        reflect.Value
	args      []arg
	outs      []*objType
	optional  []bool
	errReturn bool
}

type registry struct {
	objects   []*objType
	byType    map[reflect.Type]*objType
	datatypes []*datatype
	stubs     []*stub
	byName    map[string]*stub
	inits     []func()
}

var reg = &registry{byType: map[reflect.Type]*objType{}, byName: map[string]*stub{}}

// Object declares an object type: values of type T that stubs produce and use. drop (may be nil)
// frees objects left over at the end of a program.
func Object[T any](name string, drop func(T)) {
	t := reflect.TypeOf((*T)(nil)).Elem()
	if o, ok := reg.byType[t]; ok {
		if drop != nil {
			o.drop = reflect.ValueOf(drop)
		}
		return
	}
	o := &objType{name: name, typ: t}
	if drop != nil {
		o.drop = reflect.ValueOf(drop)
	}
	reg.objects = append(reg.objects, o)
	reg.byType[t] = o
}

// Datatype declares a data type (harness/data/<name>/ holds seeds and *.dict tokens).
func Datatype(name string) {
	reg.datatype(name)
}

func (r *registry) datatype(name string) *datatype {
	for _, d := range r.datatypes {
		if d.name == name {
			return d
		}
	}
	d := &datatype{name: name}
	r.datatypes = append(r.datatypes, d)
	return d
}

// Decoder registers a decoder for a data type. A string or []byte result: values for string /
// []byte parameters of that type. Any other result: values for parameters with `val=<name>`.
func Decoder[R any](name string, fn func(*Source) R) {
	d := reg.datatype(name)
	d.value = func(s *Source) any { return fn(s) }
	d.decoder = func(s *Source) []byte {
		switch v := any(fn(s)).(type) {
		case string:
			return []byte(v)
		case []byte:
			return v
		default:
			return []byte(fmt.Sprintf("%+v", v))
		}
	}
}

// Value decodes a recorded Val parameter again (standalone reproducers).
func Value(name string, choices []byte) any {
	return reg.datatype(name).value(NewSource(choices))
}

// Init runs fn once per worker process, after the harness is loaded.
func Init(fn func()) {
	reg.inits = append(reg.inits, fn)
}

var (
	tSource = reflect.TypeOf((*Source)(nil))
	tPath   = reflect.TypeOf(Path(""))
	tBytes  = reflect.TypeOf([]byte(nil))
	tError  = reflect.TypeOf((*error)(nil)).Elem()
)

// Endpoint registers a stub. fn's parameters are object types (registered with Object) or
// fuzzed values; its results are objects, optionally followed by an error (a non-nil error is a
// bail). specs name the parameters in order, with options after a colon:
//
//	"text:max=4096,type=YamlText"   string / []byte: size limit and data type
//	"n:range=0..9"                  integer in [0, 9]
//	"mode:oneof=1|2|4"              one of the values (ints, or strings for a string parameter)
//	"flags:flags=1|2|4"             OR of a subset
//	"node:take"                     the call consumes this object
//	"src:max=64"                    *stitch.Source size
//	"out1:optional"                 result 1 may be nil
func Endpoint(name string, fn any, specs ...string) {
	v := reflect.ValueOf(fn)
	if v.Kind() != reflect.Func {
		panic(fmt.Sprintf("stitch.Endpoint(%q): fn must be a function", name))
	}
	if _, dup := reg.byName[name]; dup {
		panic(fmt.Sprintf("stitch.Endpoint: duplicate stub name %q", name))
	}
	t := v.Type()
	s := &stub{name: name, fn: v}
	_, file, _, _ := runtime.Caller(1)
	s.file = harnessRel(file)
	opts := map[string]map[string]string{}
	names := []string{}
	for _, sp := range specs {
		n, o, _ := strings.Cut(sp, ":")
		m := map[string]string{}
		if o != "" {
			for _, kv := range strings.Split(o, ",") {
				k, val, _ := strings.Cut(strings.TrimSpace(kv), "=")
				m[k] = val
			}
		}
		if strings.HasPrefix(n, "out") && len(n) > 3 && n[3] >= '0' && n[3] <= '9' {
			opts[n] = m
			continue
		}
		names = append(names, n)
		opts["#"+strconv.Itoa(len(names)-1)] = m
	}
	for i := 0; i < t.NumIn(); i++ {
		pt := t.In(i)
		an := fmt.Sprintf("a%d", i)
		if i < len(names) && names[i] != "" {
			an = names[i]
		}
		o := opts["#"+strconv.Itoa(i)]
		if ob, ok := reg.byType[pt]; ok {
			_, take := o["take"]
			s.args = append(s.args, arg{name: an, obj: ob, take: take})
			continue
		}
		p, err := newParam(an, pt, o)
		if err != nil {
			panic(fmt.Sprintf("stitch.Endpoint(%q), parameter %s: %v", name, an, err))
		}
		if p.datatype != "" {
			reg.datatype(p.datatype)
		}
		s.args = append(s.args, arg{name: an, param: p})
	}
	for i := 0; i < t.NumOut(); i++ {
		ot := t.Out(i)
		if i == t.NumOut()-1 && ot == tError {
			s.errReturn = true
			continue
		}
		ob, ok := reg.byType[ot]
		if !ok {
			panic(fmt.Sprintf("stitch.Endpoint(%q): result %d has type %v, which is not an object type (stitch.Object)", name, i, ot))
		}
		s.outs = append(s.outs, ob)
		_, opt := opts["out"+strconv.Itoa(i)]["optional"]
		s.optional = append(s.optional, opt)
	}
	reg.stubs = append(reg.stubs, s)
	reg.byName[name] = s
}

func harnessRel(file string) string {
	if i := strings.LastIndex(file, "/harness/"); i >= 0 {
		return file[i+1:]
	}
	return "harness/" + filepath.Base(file)
}

func newParam(name string, t reflect.Type, o map[string]string) (*param, error) {
	p := &param{name: name, typ: t}
	atoi := func(k string, def int) int {
		if v, ok := o[k]; ok {
			n, err := strconv.Atoi(v)
			if err == nil {
				return n
			}
		}
		return def
	}
	p.datatype = o["type"]
	if v, ok := o["val"]; ok {
		p.kind, p.ctype, p.size, p.datatype = "value", "Val", atoi("max", 1024), v
		return p, nil
	}
	switch {
	case t == tSource:
		p.kind, p.ctype, p.size = "source", "Source", atoi("max", 64)
	case t == tPath:
		p.kind, p.ctype, p.size = "file", "File", atoi("max", 4096)
	case t == tBytes:
		p.kind, p.ctype, p.size = "bytes", "Bytes", atoi("max", 256)
	case t.Kind() == reflect.String:
		if v, ok := o["oneof"]; ok {
			p.kind, p.ctype, p.size = "oneof", "OneOf", 8
			p.choices = strings.Split(v, "|")
			for i := range p.choices {
				p.values = append(p.values, int64(i))
			}
		} else {
			p.kind, p.ctype, p.size = "str", "Str", atoi("max", 256)
		}
	case t.Kind() == reflect.Bool:
		p.kind, p.ctype, p.size = "bool", "Bool", 1
	case t.Kind() == reflect.Float32 || t.Kind() == reflect.Float64:
		p.kind, p.size = "float", int(t.Size())
		p.ctype = map[int]string{4: "F32", 8: "F64"}[p.size]
	case t.Kind() >= reflect.Int && t.Kind() <= reflect.Uintptr:
		p.size = int(t.Size())
		p.signed = t.Kind() <= reflect.Int64
		if v, ok := o["range"]; ok {
			a, b, _ := strings.Cut(v, "..")
			lo, e1 := strconv.ParseInt(a, 0, 64)
			hi, e2 := strconv.ParseInt(b, 0, 64)
			if e1 != nil || e2 != nil || lo > hi {
				return nil, fmt.Errorf("bad range %q (want lo..hi)", v)
			}
			p.kind, p.ctype, p.size, p.signed, p.lo, p.hi = "range", "Range", 8, true, lo, hi
		} else if v, ok := o["oneof"]; ok {
			p.kind, p.ctype, p.size = "oneof", "OneOf", 8
			for _, x := range strings.Split(v, "|") {
				n, err := strconv.ParseInt(x, 0, 64)
				if err != nil {
					return nil, fmt.Errorf("bad oneof value %q", x)
				}
				p.values = append(p.values, n)
			}
		} else if v, ok := o["flags"]; ok {
			p.kind, p.ctype, p.size = "flags", "Flags", 8
			for _, x := range strings.Split(v, "|") {
				n, err := strconv.ParseInt(x, 0, 64)
				if err != nil {
					return nil, fmt.Errorf("bad flag %q", x)
				}
				p.values = append(p.values, n)
				p.mask |= n
			}
		} else {
			p.kind = "int"
			p.ctype = map[bool]string{true: "I", false: "U"}[p.signed] + strconv.Itoa(8*p.size)
		}
	default:
		return nil, fmt.Errorf("unsupported type %v (register object types with stitch.Object)", t)
	}
	return p, nil
}

func (r *registry) manifest() string {
	type in struct {
		Type string `json:"type"`
		Mode string `json:"mode"`
		Name string `json:"name"`
	}
	type out struct {
		Type     string `json:"type"`
		Optional bool   `json:"optional"`
	}
	type pm struct {
		Kind     string  `json:"kind"`
		Type     string  `json:"type"`
		Size     int     `json:"size"`
		Signed   bool    `json:"signed"`
		Lo       *int64  `json:"lo,omitempty"`
		Hi       *int64  `json:"hi,omitempty"`
		Values   []int64 `json:"values,omitempty"`
		Datatype *string `json:"datatype"`
		Name     string  `json:"name"`
	}
	type ep struct {
		Name    string `json:"name"`
		File    string `json:"file"`
		Inputs  []in   `json:"inputs"`
		Params  []pm   `json:"params"`
		Outputs []out  `json:"outputs"`
	}
	type obj struct {
		Name string `json:"name"`
		Drop bool   `json:"drop"`
	}
	type dt struct {
		Name    string `json:"name"`
		Decoder bool   `json:"decoder"`
	}
	m := struct {
		Version   int   `json:"version"`
		Objects   []obj `json:"objects"`
		Datatypes []dt  `json:"datatypes"`
		Endpoints []ep  `json:"endpoints"`
	}{Version: 2, Objects: []obj{}, Datatypes: []dt{}, Endpoints: []ep{}}
	for _, o := range r.objects {
		m.Objects = append(m.Objects, obj{o.name, o.drop.IsValid()})
	}
	for _, d := range r.datatypes {
		m.Datatypes = append(m.Datatypes, dt{d.name, d.decoder != nil})
	}
	for _, s := range r.stubs {
		e := ep{Name: s.name, File: s.file, Inputs: []in{}, Params: []pm{}, Outputs: []out{}}
		for _, a := range s.args {
			if a.obj != nil {
				mode := "borrow"
				if a.take {
					mode = "take"
				}
				e.Inputs = append(e.Inputs, in{a.obj.name, mode, a.name})
				continue
			}
			p := a.param
			q := pm{Kind: p.kind, Type: p.ctype, Size: p.size, Signed: p.signed, Values: p.values, Name: p.name}
			if p.kind == "range" {
				lo, hi := p.lo, p.hi
				q.Lo, q.Hi = &lo, &hi
			}
			if p.datatype != "" {
				d := p.datatype
				q.Datatype = &d
			}
			e.Params = append(e.Params, q)
		}
		for i, o := range s.outs {
			e.Outputs = append(e.Outputs, out{o.name, s.optional[i]})
		}
		m.Endpoints = append(m.Endpoints, e)
	}
	b, _ := json.Marshal(m)
	return string(b)
}

var _ = os.Getenv
