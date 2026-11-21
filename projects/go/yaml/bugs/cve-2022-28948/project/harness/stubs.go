// go-yaml (yaml.v3): decode into values and Node trees, edit nodes, encode.
// Oracle: Marshal of a decoded value decodes to an equal value.
package harness

import (
	"bytes"
	"errors"
	"io"
	"reflect"
	"strings"

	"gopkg.in/yaml.v3"
	"stitch"
)

func init() {
	stitch.Object[*yaml.Node]("Node", nil)
	stitch.Decoder("YamlText", yamlText)
	stitch.Endpoint("unmarshal_any", unmarshalAny, "text:max=2048,type=YamlText")
	stitch.Endpoint("unmarshal_node", unmarshalNode, "text:max=2048,type=YamlText")
	stitch.Endpoint("decode_stream", decodeStream, "text:max=2048,type=YamlText", "known:oneof=0|1")
	stitch.Endpoint("unmarshal_struct", unmarshalStruct, "text:max=1024,type=YamlText")
	stitch.Endpoint("node_child", nodeChild, "n", "i:range=0..8")
	stitch.Endpoint("node_set", nodeSet, "n", "tag:range=0..5", "value:max=32", "style:oneof=0|1|2|4|8|16|32|64")
	stitch.Endpoint("node_encode", nodeEncode, "n", "indent:range=0..8")
	stitch.Endpoint("node_decode", nodeDecode, "n")
	stitch.Endpoint("new_mapping", newMapping)
	stitch.Endpoint("append_pair", appendPair, "m", "c", "key:max=16")
}

func yamlText(s *stitch.Source) string {
	var b strings.Builder
	for s.More() {
		switch s.Choice(6) {
		case 0:
			b.WriteString(s.Token())
		case 1:
			b.WriteString(strings.Repeat(" ", s.Choice(6)) + stitch.Pick(s, "- ", "a: ", "? ", "&a ", "*a", "!!map ", "{", "[") + "\n")
		case 2:
			b.WriteString(stitch.Pick(s, "a: 1\n", "- x\n", "b: [1, 2]\n", "c: {d: e}\n", "---\n", "f: |\n  t\n"))
		default:
			b.WriteString(s.Str(8))
		}
	}
	return b.String()
}

func roundtrip(v any) {
	out, err := yaml.Marshal(v)
	if err != nil {
		stitch.Bail("marshal error")
	}
	var again any
	if err := yaml.Unmarshal(out, &again); err != nil {
		stitch.Check(false, "Marshal output unmarshals")
	}
	stitch.Check(reflect.DeepEqual(norm(v), norm(again)), "Marshal/Unmarshal roundtrip")
}

// norm compares values the way YAML can preserve them: numbers by value (yaml.v3 writes the float
// 1.0 as "1", which reads back as an int), NaN as a string, and every mapping as map[any]any (a
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
		return err
	}
	roundtrip(v)
	return nil
}

func unmarshalNode(text string) (*yaml.Node, error) {
	var n yaml.Node
	if err := yaml.Unmarshal([]byte(text), &n); err != nil {
		return nil, err
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
			return err
		}
	}
	return nil
}


func unmarshalStruct(text string) error {
	var r struct {
		Name  string         `yaml:"name"`
		Tags  []string       `yaml:"tags,flow"`
		Attrs map[string]int `yaml:"attrs,omitempty"`
		N     *int           `yaml:"n"`
		F     float32        `yaml:"f"`
	}
	if err := yaml.Unmarshal([]byte(text), &r); err != nil {
		return err
	}
	_, err := yaml.Marshal(&r)
	return err
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

func nodeEncode(n *yaml.Node, indent int) error {
	var b bytes.Buffer
	e := yaml.NewEncoder(&b)
	e.SetIndent(indent)
	if err := e.Encode(n); err != nil {
		return err
	}
	return e.Close()
}

func nodeDecode(n *yaml.Node) error {
	var v any
	if err := n.Decode(&v); err != nil {
		return err
	}
	roundtrip(v)
	return nil
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
