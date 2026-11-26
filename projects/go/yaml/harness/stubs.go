// go-yaml (v4): the classic API (Unmarshal/Marshal, Decoder/Encoder) and the v4 API (Load/Dump,
// Loader/Dumper, option sets, OptsYAML and the official plugins); Node trees: decode, load, edit,
// encode, dump; error values.
// Oracles: a dumped value loads back equal (Marshal, Dump, Dumper streams, Node.Encode/Dump);
// Load with WithAllDocuments agrees with a Loader; Node.SetString round trips; ShortTag/LongTag
// agree.
package harness

import (
	"bytes"
	"errors"
	"io"
	"reflect"
	"strings"
	"time"

	"go.yaml.in/yaml/v4"
	"go.yaml.in/yaml/v4/plugin/tabindent"
	"stitch"
)

// Loader and Dumper objects
type Loader struct {
	L *yaml.Loader
}

type Dumper struct {
	D     *yaml.Dumper
	Buf   *bytes.Buffer
	Vals  []any // values dumped, in order
	Nodes bool  // a Node was dumped (harness-edited nodes have no value to compare)
	Plain bool
}

var _ = func() bool {
	stitch.Object[*yaml.Node]("Node", nil)
	stitch.Object[*Loader]("Loader", nil)
	stitch.Object[*Dumper]("Dumper", nil)
	return true
}()

func init() {
	// tab-indent is the one official plugin not registered by default: register it so OptsYAML
	// and WithNamedPlugin can select it (RegisterPlugin is process-wide, so once per worker)
	stitch.Init(func() { _ = tabindent.Register() })
	stitch.Decoder("YamlText", yamlText)
	stitch.Decoder("LoadOpts", func(s *stitch.Source) Opts { return optsOf(s, true) })
	stitch.Decoder("DumpOpts", func(s *stitch.Source) Opts { return optsOf(s, false) })
	stitch.Decoder("OptsText", optsYAMLText)

	// classic API
	stitch.Endpoint("unmarshal_any", unmarshalAny, "text:max=2048,type=YamlText")
	stitch.Endpoint("unmarshal_node", unmarshalNode, "text:max=2048,type=YamlText")
	stitch.Endpoint("decode_stream", decodeStream, "text:max=2048,type=YamlText", "known:oneof=0|1")
	stitch.Endpoint("unmarshal_struct", unmarshalStruct, "text:max=1024,type=YamlText", "known:oneof=0|1")
	// v4 API
	stitch.Endpoint("load_any", loadAny, "text:max=2048,type=YamlText", "lo:val=LoadOpts,max=64", "do:val=DumpOpts,max=64")
	stitch.Endpoint("load_all", loadAll, "text:max=2048,type=YamlText", "lo:val=LoadOpts,max=64")
	stitch.Endpoint("load_node", loadNode, "text:max=2048,type=YamlText", "lo:val=LoadOpts,max=64")
	stitch.Endpoint("dump_all", dumpAll, "text:max=2048,type=YamlText", "do:val=DumpOpts,max=64")
	stitch.Endpoint("opts_yaml", optsYAML, "cfg:max=256,type=OptsText", "text:max=1024,type=YamlText")
	stitch.Endpoint("new_loader", newLoader, "text:max=2048,type=YamlText", "lo:val=LoadOpts,max=64")
	stitch.Endpoint("loader_next", loaderNext, "l", "out0:optional")
	stitch.Endpoint("new_dumper", newDumper, "do:val=DumpOpts,max=64")
	stitch.Endpoint("dumper_dump_value", dumperDumpValue, "d", "text:max=512,type=YamlText")
	stitch.Endpoint("dumper_dump_node", dumperDumpNode, "d", "n")
	stitch.Endpoint("dumper_close", dumperClose, "d:take")
	// nodes
	stitch.Endpoint("node_child", nodeChild, "n", "i:range=0..8")
	stitch.Endpoint("node_set", nodeSet, "n", "tag:range=0..5", "value:max=32", "style:oneof=0|1|2|4|8|16|32|64")
	stitch.Endpoint("node_set_string", nodeSetString, "n", "value:max=64")
	stitch.Endpoint("node_inspect", nodeInspect, "n")
	stitch.Endpoint("node_encode", nodeEncode, "n", "indent:range=0..8", "seq:oneof=0|1|2")
	stitch.Endpoint("node_decode", nodeDecode, "n")
	stitch.Endpoint("node_load", nodeLoad, "n", "lo:val=LoadOpts,max=64")
	stitch.Endpoint("node_from_value", nodeFromValue, "text:max=1024,type=YamlText")
	stitch.Endpoint("node_dump_value", nodeDumpValue, "text:max=1024,type=YamlText", "do:val=DumpOpts,max=64")
	stitch.Endpoint("new_mapping", newMapping)
	stitch.Endpoint("append_pair", appendPair, "m", "c", "key:max=16")
}

func yamlText(s *stitch.Source) string {
	var b strings.Builder
	for s.More() {
		switch s.Choice(7) {
		case 0:
			b.WriteString(s.Token())
		case 1:
			b.WriteString(strings.Repeat(" ", s.Choice(6)) + stitch.Pick(s, "- ", "a: ", "? ", "&a ", "*a", "!!map ", "{", "[", "<<: ", "\t") + "\n")
		case 2:
			b.WriteString(stitch.Pick(s, "a: 1\n", "- x\n", "b: [1, 2]\n", "c: {d: e}\n", "---\n", "f: |\n  t\n", "<<: *a\n", "k: &a {x: 1}\n", "...\n"))
		case 3:
			b.WriteString(stitch.Pick(s, "%YAML 1.2\n---\n", "%TAG !e! tag:e.com,2000:\n---\n", "# c\n", "t: 2001-12-14t21:59:43.10-05:00\n", "n: .nan\n", "x: !!binary aGk=\n", "\ufeff"))
		default:
			b.WriteString(s.Str(8))
		}
	}
	return b.String()
}

// inspectErr exercises the error values the library returns (every method of LoadError,
// LoadErrors, TypeError, DumpError and Mark), then hands the error back so the stub bails.
func inspectErr(err error) error {
	if err == nil {
		return nil
	}
	_ = err.Error()
	var le *yaml.LoadError
	if errors.As(err, &le) {
		_ = le.Error()
		_ = le.Unwrap()
		_ = le.Mark.String() + le.Mark.ShortString() + le.Mark.RangeString(le.ContextMark)
		w := yaml.NewLoadError(le.Stage, le.Message, le.Mark, err)
		stitch.Check(errors.Is(w, err), "NewLoadError unwraps to its cause")
	}
	var les *yaml.LoadErrors
	if errors.As(err, &les) {
		_ = les.Error()
		var te *yaml.TypeError
		if les.As(&te) {
			_ = te.Error()
		}
		_ = les.Is(io.EOF)
	}
	var de *yaml.DumpError
	if errors.As(err, &de) {
		_ = de.Error()
		_ = de.Unwrap()
		w := yaml.NewDumpError(de.Stage, de.Message, err)
		stitch.Check(errors.Is(w, err), "NewDumpError unwraps to its cause")
	}
	return err
}

func roundtrip(v any) {
	out, err := yaml.Marshal(v)
	if err != nil {
		inspectErr(err)
		stitch.Bail("marshal error")
	}
	var again any
	if err := yaml.Unmarshal(out, &again); err != nil {
		stitch.Log("marshaled: %q error: %v", out, err)
		stitch.Check(false, "Marshal output unmarshals")
	}
	stitch.Check(reflect.DeepEqual(norm(v), norm(again)), "Marshal/Unmarshal roundtrip")
}

// dumpRoundtrip: Dump with options, then Load with the defaults, must give the same value.
func dumpRoundtrip(v any, do Opts) {
	out, err := yaml.Dump(v, do.List...)
	if err != nil {
		inspectErr(err)
		stitch.Bail("dump error")
	}
	if !do.Plain {
		return
	}
	var again any
	if err := yaml.Load(out, &again); err != nil {
		stitch.Log("opts %s dumped: %q error: %v", do, out, err)
		stitch.Check(false, "Dump output loads")
	}
	stitch.Log("opts %s dumped: %q", do, out)
	stitch.Check(reflect.DeepEqual(norm(v), norm(again)), "Dump/Load roundtrip")
}

// norm compares values the way YAML can preserve them: numbers by value (a float 1.0 is written
// as "1", which reads back as an int), NaN as a string, and every mapping as map[any]any (a
// !!binary key turns map[string]any into map[any]any).
func norm(v any) any {
	switch x := v.(type) {
	case float64:
		if x != x {
			return "NaN"
		}
		return x
	case int:
		return float64(x)
	case int64:
		return float64(x)
	case uint64:
		return float64(x)
	case time.Time:
		return x.UTC().Format(time.RFC3339Nano)
	case []any:
		out := make([]any, len(x))
		for i, e := range x {
			out[i] = norm(e)
		}
		return out
	case map[string]any:
		out := map[any]any{}
		for k, e := range x {
			out[k] = norm(e)
		}
		return out
	case map[any]any:
		out := map[any]any{}
		for k, e := range x {
			out[norm(k)] = norm(e)
		}
		return out
	}
	return v
}

func unmarshalAny(text string) error {
	var v any
	if err := yaml.Unmarshal([]byte(text), &v); err != nil {
		return inspectErr(err)
	}
	roundtrip(v)
	return nil
}

func unmarshalNode(text string) (*yaml.Node, error) {
	var n yaml.Node
	if err := yaml.Unmarshal([]byte(text), &n); err != nil {
		return nil, inspectErr(err)
	}
	return &n, nil
}

func decodeStream(text string, known int) error {
	d := yaml.NewDecoder(bytes.NewReader([]byte(text)))
	d.KnownFields(known == 1)
	for i := 0; i < 16; i++ {
		var n yaml.Node
		if err := d.Decode(&n); err != nil {
			if errors.Is(err, io.EOF) {
				return nil
			}
			return inspectErr(err)
		}
	}
	return nil
}

// Upper implements Unmarshaler/Marshaler (custom hooks during construction and representation)
type Upper string

func (u *Upper) UnmarshalYAML(n *yaml.Node) error {
	var s string
	if err := n.Decode(&s); err != nil {
		return err
	}
	*u = Upper(strings.ToUpper(s))
	return nil
}

func (u Upper) MarshalYAML() (any, error) { return strings.ToLower(string(u)), nil }

// Zeroish implements IsZeroer (consulted for omitempty)
type Zeroish struct{ V int }

func (z Zeroish) IsZero() bool { return z.V == 0 }

type Record struct {
	Name   string         `yaml:"name"`
	Tags   []string       `yaml:"tags,flow"`
	Attrs  map[string]int `yaml:"attrs,omitempty"`
	N      *int           `yaml:"n"`
	F      float32        `yaml:"f"`
	U      Upper          `yaml:"u"`
	Raw    yaml.Node      `yaml:"raw"`
	Z      Zeroish        `yaml:"z,omitempty"`
	When   time.Time      `yaml:"when,omitempty"`
	Dur    time.Duration  `yaml:"dur"`
	Inline struct {
		X int `yaml:"x"`
	} `yaml:",inline"`
	Rest map[string]any `yaml:",inline"`
}

func unmarshalStruct(text string, known int) error {
	var r Record
	var err error
	if known == 1 {
		err = yaml.Load([]byte(text), &r, yaml.WithKnownFields())
	} else {
		err = yaml.Unmarshal([]byte(text), &r)
	}
	if err != nil {
		return inspectErr(err)
	}
	_, err = yaml.Marshal(&r)
	return inspectErr(err)
}

func loadAny(text string, lo Opts, do Opts) error {
	var v any
	if err := yaml.Load([]byte(text), &v, lo.List...); err != nil {
		return inspectErr(err)
	}
	dumpRoundtrip(v, do)
	return nil
}

// loadAll: Load with WithAllDocuments gives the documents a Loader returns one by one
func loadAll(text string, lo Opts) error {
	var all []any
	if err := yaml.Load([]byte(text), &all, append(append([]yaml.Option{}, lo.List...), yaml.WithAllDocuments())...); err != nil {
		return inspectErr(err)
	}
	if lo.Stream {
		return nil
	}
	l, err := yaml.NewLoader(bytes.NewReader([]byte(text)), lo.List...)
	stitch.Check(err == nil, "NewLoader accepts options Load accepted")
	var docs []any
	for {
		var v any
		err := l.Load(&v)
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			stitch.Log("loader error: %v", err)
			stitch.Check(false, "a Loader reads the documents Load(WithAllDocuments) read")
		}
		docs = append(docs, v)
	}
	stitch.Log("all: %#v loader: %#v", all, docs)
	stitch.Check(len(all) == len(docs) && reflect.DeepEqual(norm(all), norm(docs)), "Load(WithAllDocuments) agrees with a Loader")
	return nil
}

func loadNode(text string, lo Opts) (*yaml.Node, error) {
	var n yaml.Node
	if err := yaml.Load([]byte(text), &n, lo.List...); err != nil {
		return nil, inspectErr(err)
	}
	return &n, nil
}

// dumpAll: a sequence dumped as a multi-document stream loads back as the same documents
func dumpAll(text string, do Opts) error {
	var x any
	if err := yaml.Unmarshal([]byte(text), &x); err != nil {
		return inspectErr(err)
	}
	v, ok := x.([]any)
	if !ok {
		v = []any{x}
	}
	// (a non-sequence is dumped as one document; an empty sequence fails with "expected STREAM-START")
	out, err := yaml.Dump(v, append(append([]yaml.Option{}, do.List...), yaml.WithAllDocuments())...)
	if err != nil {
		return inspectErr(err)
	}
	if !do.Plain {
		return nil
	}
	var back []any
	if err := yaml.Load(out, &back, yaml.WithAllDocuments()); err != nil {
		stitch.Log("dumped %q: %v", out, err)
		stitch.Check(false, "Dump(WithAllDocuments) output loads")
	}
	stitch.Log("dumped %q", out)
	stitch.Check(reflect.DeepEqual(norm(v), norm(back)), "Dump/Load(WithAllDocuments) roundtrip")
	return nil
}

func optsYAML(cfg string, text string) error {
	opt, err := yaml.OptsYAML(cfg)
	if err != nil {
		return inspectErr(err)
	}
	var v any
	if err := yaml.Load([]byte(text), &v, opt); err != nil {
		return inspectErr(err)
	}
	_, err = yaml.Dump(v, opt)
	return inspectErr(err)
}

func newLoader(text string, lo Opts) (*Loader, error) {
	l, err := yaml.NewLoader(bytes.NewReader([]byte(text)), lo.List...)
	if err != nil {
		return nil, inspectErr(err)
	}
	return &Loader{L: l}, nil
}

func loaderNext(l *Loader) *yaml.Node {
	var n yaml.Node
	err := l.L.Load(&n)
	if errors.Is(err, io.EOF) {
		return nil
	}
	if err != nil {
		inspectErr(err)
		stitch.Bail("load error")
	}
	if n.Kind == yaml.StreamNode && n.Stream != nil {
		_ = n.Stream.Encoding
		_ = n.Stream.Version
		_ = len(n.Stream.TagDirectives)
	}
	return &n
}

func newDumper(do Opts) (*Dumper, error) {
	var b bytes.Buffer
	d, err := yaml.NewDumper(&b, do.List...)
	if err != nil {
		return nil, inspectErr(err)
	}
	return &Dumper{D: d, Buf: &b, Plain: do.Plain}, nil
}

func dumperDumpValue(d *Dumper, text string) error {
	var v any
	if err := yaml.Unmarshal([]byte(text), &v); err != nil {
		return inspectErr(err)
	}
	if err := d.D.Dump(v); err != nil {
		d.Nodes = true // the stream may hold part of a document
		return inspectErr(err)
	}
	d.Vals = append(d.Vals, v)
	return nil
}

func dumperDumpNode(d *Dumper, n *yaml.Node) error {
	d.Nodes = true
	return inspectErr(d.D.Dump(n))
}

// dumperClose: a stream of dumped values loads back as the same documents
func dumperClose(d *Dumper) error {
	if err := d.D.Close(); err != nil {
		return inspectErr(err)
	}
	if d.Nodes || !d.Plain {
		return nil
	}
	out := d.Buf.Bytes()
	l, err := yaml.NewLoader(bytes.NewReader(out))
	stitch.Check(err == nil, "NewLoader with no options")
	var docs []any
	for {
		var v any
		err := l.Load(&v)
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			stitch.Log("stream %q: %v", out, err)
			stitch.Check(false, "a Dumper stream loads")
		}
		docs = append(docs, v)
	}
	stitch.Log("stream %q", out)
	stitch.Check(len(docs) == len(d.Vals), "a Dumper stream holds one document per Dump")
	stitch.Check(reflect.DeepEqual(norm(d.Vals), norm(docs)), "Dumper/Loader roundtrip")
	return nil
}

func nodeChild(n *yaml.Node, i int) *yaml.Node {
	if len(n.Content) == 0 {
		return nil
	}
	return n.Content[i%len(n.Content)]
}

// nodeSet edits a scalar's value and style (structural edits would build trees yaml never produces)
func nodeSet(n *yaml.Node, kind int, value string, style int) {
	stitch.Require(n.Kind == yaml.ScalarNode, "not a scalar")
	n.Value = value
	n.Style = yaml.Style(style)
	n.Tag = []string{"", "!!str", "!!int", "!!bool", "!!float", "!!null"}[kind]
}

// nodeSetString: SetString makes a scalar that decodes to the same string (base64 !!binary for
// invalid UTF-8), also after a Marshal/Unmarshal round trip
func nodeSetString(n *yaml.Node, value string) {
	stitch.Require(n.Kind == yaml.ScalarNode, "not a scalar")
	n.SetString(value)
	n.Style &^= yaml.TaggedStyle | yaml.FlowStyle
	var got string
	err := n.Decode(&got)
	stitch.Check(err == nil && got == value, "a SetString node decodes to its string")
	out, err := yaml.Marshal(map[string]*yaml.Node{"k": n})
	stitch.Require(err == nil, "marshal error")
	var back map[string]string
	err = yaml.Unmarshal(out, &back)
	stitch.Log("marshaled %q", out)
	stitch.Check(err == nil && back["k"] == value, "a SetString node round trips")
}

// nodeInspect: the accessors over the whole tree; the long tag is the short tag expanded
func nodeInspect(n *yaml.Node) {
	var walk func(x *yaml.Node, d int)
	walk = func(x *yaml.Node, d int) {
		if x == nil || d > 64 {
			return
		}
		short, long := x.ShortTag(), x.LongTag()
		if strings.HasPrefix(short, "!!") {
			stitch.Check(long == "tag:yaml.org,2002:"+short[2:], "LongTag expands ShortTag")
		}
		_ = x.IsZero()
		for _, c := range x.Content {
			walk(c, d+1)
		}
	}
	walk(n, 0)
	stitch.Check((&yaml.Node{}).IsZero(), "a zero Node is zero")
}

func nodeEncode(n *yaml.Node, indent int, seq int) error {
	var b bytes.Buffer
	e := yaml.NewEncoder(&b)
	e.SetIndent(indent)
	switch seq {
	case 1:
		e.CompactSeqIndent()
	case 2:
		e.DefaultSeqIndent()
	}
	if err := e.Encode(n); err != nil {
		return inspectErr(err)
	}
	return inspectErr(e.Close())
}

func nodeDecode(n *yaml.Node) error {
	var v any
	if err := n.Decode(&v); err != nil {
		return inspectErr(err)
	}
	roundtrip(v)
	return nil
}

func nodeLoad(n *yaml.Node, lo Opts) error {
	var v any
	if err := n.Load(&v, lo.List...); err != nil {
		return inspectErr(err)
	}
	roundtrip(v)
	return nil
}

// nodeFromValue: Node.Encode of a value decodes back to the value
func nodeFromValue(text string) (*yaml.Node, error) {
	var v any
	if err := yaml.Unmarshal([]byte(text), &v); err != nil {
		return nil, inspectErr(err)
	}
	var n yaml.Node
	if err := n.Encode(v); err != nil {
		return nil, inspectErr(err)
	}
	var back any
	err := n.Decode(&back)
	stitch.Check(err == nil, "a Node.Encode node decodes")
	stitch.Check(reflect.DeepEqual(norm(v), norm(back)), "Node.Encode/Decode roundtrip")
	return &n, nil
}

// nodeDumpValue: Node.Dump of a value loads back to the value
func nodeDumpValue(text string, do Opts) (*yaml.Node, error) {
	var v any
	if err := yaml.Unmarshal([]byte(text), &v); err != nil {
		return nil, inspectErr(err)
	}
	var n yaml.Node
	if err := n.Dump(v, do.List...); err != nil {
		return nil, inspectErr(err)
	}
	if do.Plain {
		var back any
		err := n.Load(&back)
		stitch.Check(err == nil, "a Node.Dump node loads")
		stitch.Check(reflect.DeepEqual(norm(v), norm(back)), "Node.Dump/Load roundtrip")
	}
	return &n, nil
}

func newMapping() *yaml.Node {
	return &yaml.Node{Kind: yaml.MappingNode}
}

func contains(n, x *yaml.Node) bool {
	if n == x {
		return true
	}
	for _, c := range n.Content {
		if contains(c, x) {
			return true
		}
	}
	return false
}

func appendPair(m *yaml.Node, c *yaml.Node, key string) {
	stitch.Require(m.Kind == yaml.MappingNode, "not a mapping")
	stitch.Require(!contains(c, m), "would make a cycle")
	m.Content = append(m.Content, &yaml.Node{Kind: yaml.ScalarNode, Value: key}, c)
}
