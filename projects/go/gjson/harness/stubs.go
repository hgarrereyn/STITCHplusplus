// gjson: query JSON with a path language (wildcards, array queries, modifiers).
// Oracles: Get(json, p) == Parse(json).Get(p); Valid agrees with encoding/json; @this returns
// the parsed document.
package harness

import (
	"encoding/json"
	"strings"

	"github.com/tidwall/gjson"
	"stitch"
)

func init() {
	stitch.Object[*gjson.Result]("Result", nil)
	stitch.Decoder("JsonText", jsonText)
	stitch.Decoder("PathText", pathText)
	stitch.Endpoint("parse", parse, "text:max=2048,type=JsonText")
	stitch.Endpoint("get", get, "text:max=2048,type=JsonText", "path:max=96,type=PathText")
	stitch.Endpoint("result_get", resultGet, "r", "path:max=96,type=PathText")
	stitch.Endpoint("get_many", getMany, "text:max=2048,type=JsonText", "a:max=64,type=PathText", "b:max=64,type=PathText")
	stitch.Endpoint("iterate", iterate, "r")
	stitch.Endpoint("valid", valid, "text:max=2048,type=JsonText")
	stitch.Endpoint("values", values, "r")
}

func jsonText(s *stitch.Source) string {
	var b strings.Builder
	var value func(d int)
	value = func(d int) {
		k := s.Choice(7)
		if d > 4 {
			k = s.Choice(4)
		}
		switch k {
		case 0:
			b.WriteString(stitch.Pick(s, "1", "-0", "1.5e3", "true", "false", "null", "12345678901234567890"))
		case 1:
			b.WriteString(`"` + strings.ReplaceAll(s.Str(8), `"`, `\"`) + `"`)
		case 2:
			b.WriteString(s.Token())
		case 3:
			b.WriteString(s.Str(4))
		case 4:
			b.WriteString("[")
			for i := 0; s.More() && i < 5; i++ {
				if i > 0 {
					b.WriteString(",")
				}
				value(d + 1)
			}
			b.WriteString("]")
		default:
			b.WriteString("{")
			for i := 0; s.More() && i < 5; i++ {
				if i > 0 {
					b.WriteString(",")
				}
				b.WriteString(`"` + stitch.Pick(s, "a", "b", "c.d", "*", "#", s.Str(3)) + `":`)
				value(d + 1)
			}
			b.WriteString("}")
		}
	}
	value(0)
	return b.String()
}

func pathText(s *stitch.Source) string {
	var parts []string
	for s.More() || len(parts) == 0 {
		switch s.Choice(5) {
		case 0:
			parts = append(parts, stitch.Pick(s, "a", "b", "c", "0", "1", "*", "?", "#"))
		case 1:
			parts = append(parts, "#("+stitch.Pick(s, "a", "b", "")+stitch.Pick(s, "==", "!=", "<", ">=", "%", "!%")+stitch.Pick(s, "1", `"x*"`, "true", "")+")"+stitch.Pick(s, "", "#"))
		case 2:
			parts = append(parts, "@"+stitch.Pick(s, "reverse", "ugly", "pretty", "this", "valid", "flatten", "join", "unknown"))
		default:
			parts = append(parts, s.Token())
		}
		if len(parts) > 6 {
			break
		}
	}
	return strings.Join(parts, stitch.Pick(s, ".", "|", "."))
}

func parse(text string) *gjson.Result {
	r := gjson.Parse(text)
	if gjson.Valid(text) {
		stitch.Check(gjson.Get(text, "@this").Raw == strings.TrimSpace(r.Raw), "@this returns the document")
	}
	return &r
}

func get(text, path string) *gjson.Result {
	r := gjson.Get(text, path)
	if gjson.Valid(text) { // results on invalid JSON are undefined (documented)
		p := gjson.Parse(text).Get(path)
		stitch.Check(r.Raw == p.Raw, "Get(json, path) == Parse(json).Get(path)")
	}
	return &r
}

func resultGet(r *gjson.Result, path string) *gjson.Result {
	x := r.Get(path)
	return &x
}

func getMany(text, a, b string) {
	rs := gjson.GetMany(text, a, b)
	stitch.Check(rs[0].Raw == gjson.Get(text, a).Raw && rs[1].Raw == gjson.Get(text, b).Raw, "GetMany agrees with Get")
}

func iterate(r *gjson.Result) {
	n := 0
	r.ForEach(func(k, v gjson.Result) bool {
		n++
		v.String()
		return n < 100
	})
	r.Array()
	r.Map()
}

func valid(text string) {
	stitch.Check(gjson.Valid(text) == json.Valid([]byte(text)), "Valid() agrees with encoding/json")
}

func values(r *gjson.Result) {
	r.Value()
	r.Int()
	r.Uint()
	r.Float()
	r.Bool()
	r.Time()
	r.Exists()
	r.Less(gjson.Parse("1"), true)
}
