// gjson: query JSON with a path language (wildcards, array queries, multipaths, literals, stock and
// custom modifiers); string and []byte entry points; Result accessors and iterators; Result.Path(s);
// Escape, AppendJSONString, ForEachLine, Valid.
// Oracles: Get(json, p) == Parse(json).Get(p); the []byte functions agree with the string ones;
// Valid agrees with encoding/json; @this returns the parsed document; Result.Path/Paths of a
// simple-path result query back to the same value; Escape(key) finds the key; AppendJSONString
// decodes back to the string; the iterators agree with ForEach; Less is antisymmetric; paths
// without modifiers do not depend on DisableModifiers.
package harness

import (
	"encoding/json"
	"regexp"
	"strings"
	"unicode/utf8"

	"github.com/tidwall/gjson"
	"stitch"
)

var _ = func() bool {
	stitch.Object[*gjson.Result]("Result", nil)
	return true
}()

// stitchMod is a user modifier: "@stitch:path" runs a nested Get on its input (re-entrant use of
// the library from inside a modifier)
func stitchMod(json, arg string) string {
	r := gjson.Get(json, arg)
	if !r.Exists() {
		return ""
	}
	return r.Raw
}

func init() {
	stitch.Init(func() { gjson.AddModifier("stitch", stitchMod) })
	stitch.Decoder("JsonText", jsonText)
	stitch.Decoder("PathText", pathText)
	stitch.Endpoint("parse", parse, "text:max=2048,type=JsonText")
	stitch.Endpoint("parse_bytes", parseBytes, "text:max=2048,type=JsonText")
	stitch.Endpoint("get", get, "text:max=2048,type=JsonText", "path:max=96,type=PathText")
	stitch.Endpoint("get_bytes", getBytes, "text:max=2048,type=JsonText", "path:max=96,type=PathText")
	stitch.Endpoint("get_nomod", getNoMod, "text:max=2048,type=JsonText", "path:max=96,type=PathText")
	stitch.Endpoint("result_get", resultGet, "r", "path:max=96,type=PathText")
	stitch.Endpoint("get_many", getMany, "text:max=2048,type=JsonText", "a:max=64,type=PathText", "b:max=64,type=PathText")
	stitch.Endpoint("get_many_bytes", getManyBytes, "text:max=2048,type=JsonText", "a:max=64,type=PathText", "b:max=64,type=PathText")
	stitch.Endpoint("result_path", resultPath, "text:max=2048,type=JsonText", "path:max=96,type=PathText")
	stitch.Endpoint("iterate", iterate, "r")
	stitch.Endpoint("iterators", iterators, "r")
	stitch.Endpoint("valid", valid, "text:max=2048,type=JsonText")
	stitch.Endpoint("values", values, "r")
	stitch.Endpoint("less", less, "a", "b", "cs")
	stitch.Endpoint("for_each_line", forEachLine, "a:max=512,type=JsonText", "b:max=512,type=JsonText")
	stitch.Endpoint("escape_key", escapeKey, "key:max=32", "text:max=512,type=JsonText")
	stitch.Endpoint("append_json_string", appendJSONString, "s:max=128", "html")
	stitch.Endpoint("modifier_exists", modifierExists, "name:oneof=pretty|ugly|reverse|this|flatten|join|valid|keys|values|tostr|fromstr|group|dig|stitch|nope")
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
			b.WriteString(stitch.Pick(s, "1", "-0", "1.5e3", "true", "false", "null", "12345678901234567890", "00", "\"01\"", "-1e-400"))
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
				b.WriteString(`"` + stitch.Pick(s, "a", "b", "c.d", "*", "#", "@x", `e\"f`, `a`, s.Str(3)) + `":`)
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
		switch s.Choice(7) {
		case 0:
			parts = append(parts, stitch.Pick(s, "a", "b", "c", "0", "1", "*", "?", "#", "a*", "c\\.d", "-1"))
		case 1:
			parts = append(parts, "#("+stitch.Pick(s, "a", "b", "", "..")+stitch.Pick(s, "==", "!=", "<", ">=", "%", "!%", "=")+stitch.Pick(s, "1", `"x*"`, "true", "", "~true", "~null")+")"+stitch.Pick(s, "", "#"))
		case 2:
			parts = append(parts, "@"+stitch.Pick(s, "reverse", "ugly", "pretty", "this", "valid", "flatten", "join", "keys", "values", "tostr", "fromstr", "group", "dig:a",
				"stitch:a", "stitch:0", `pretty:{"sortKeys":true,"indent":"\t"}`, `flatten:{"deep":true}`, `join:{"preserve":true}`, "unknown"))
		case 3:
			parts = append(parts, stitch.Pick(s, "[a,b]", "{a,b.c}", `{"k":a,b}`, "[a,#]", "!true", "!{\"x\":1}", "!\"s\""))
		case 4:
			parts = append(parts, "..")
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

func parseBytes(text string) *gjson.Result {
	r := gjson.ParseBytes([]byte(text))
	stitch.Check(r.Raw == gjson.Parse(text).Raw, "ParseBytes == Parse")
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

func getBytes(text, path string) *gjson.Result {
	r := gjson.GetBytes([]byte(text), path)
	stitch.Check(r.Raw == gjson.Get(text, path).Raw, "GetBytes == Get")
	return &r
}

// getNoMod: with DisableModifiers set, a path that names no modifier queries the same
func getNoMod(text, path string) {
	gjson.DisableModifiers = true
	r := gjson.Get(text, path)
	gjson.DisableModifiers = false
	if !strings.Contains(path, "@") {
		stitch.Check(r.Raw == gjson.Get(text, path).Raw, "a path without modifiers does not depend on DisableModifiers")
	}
}

func resultGet(r *gjson.Result, path string) *gjson.Result {
	x := r.Get(path)
	return &x
}

func getMany(text, a, b string) {
	rs := gjson.GetMany(text, a, b)
	stitch.Check(rs[0].Raw == gjson.Get(text, a).Raw && rs[1].Raw == gjson.Get(text, b).Raw, "GetMany agrees with Get")
}

func getManyBytes(text, a, b string) {
	rs := gjson.GetManyBytes([]byte(text), a, b)
	stitch.Check(rs[0].Raw == gjson.Get(text, a).Raw && rs[1].Raw == gjson.Get(text, b).Raw, "GetManyBytes agrees with Get")
}

// simple paths: keys, indexes, escapes, wildcards and '#' (no modifiers, multipaths, literals,
// pipes or nested queries: Path documents "" for those, so they are not checked)
var simplePath = regexp.MustCompile(`^([A-Za-z0-9_\-*?]|\\.)+(\.([A-Za-z0-9_\-*?#]|\\.)+)*$`)

// resultPath: Result.Path and Result.Paths give paths that query back to the same values
func resultPath(text, path string) {
	r := gjson.Get(text, path)
	p := r.Path(text)
	ps := r.Paths(text)
	if !gjson.Valid(text) || !simplePath.MatchString(path) {
		return
	}
	stitch.Log("path %q -> %q, paths %q", path, p, ps)
	if p != "" && r.Exists() {
		stitch.Check(gjson.Get(text, p).Raw == r.Raw, "Result.Path queries back to the result")
	}
	if ps != nil {
		elems := r.Array()
		stitch.Check(len(ps) == len(elems), "Result.Paths has one path per element")
		for i, q := range ps {
			if q != "" {
				stitch.Check(gjson.Get(text, q).Raw == elems[i].Raw, "Result.Paths query back to the elements")
			}
		}
	}
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

// iterators: All, Keys and Values yield what ForEach yields; the Is* predicates match Type/Raw
func iterators(r *gjson.Result) {
	var ks, vs []string
	r.ForEach(func(k, v gjson.Result) bool {
		ks = append(ks, k.Raw)
		vs = append(vs, v.Raw)
		return len(ks) < 64
	})
	var ak, av, kk, vv []string
	r.All()(func(k, v gjson.Result) bool {
		ak = append(ak, k.Raw)
		av = append(av, v.Raw)
		return len(ak) < 64
	})
	r.Keys()(func(k gjson.Result) bool {
		kk = append(kk, k.Raw)
		return len(kk) < 64
	})
	r.Values()(func(v gjson.Result) bool {
		vv = append(vv, v.Raw)
		return len(vv) < 64
	})
	stitch.Check(strings.Join(ak, "\x00") == strings.Join(ks, "\x00") && strings.Join(av, "\x00") == strings.Join(vs, "\x00"), "All agrees with ForEach")
	stitch.Check(strings.Join(kk, "\x00") == strings.Join(ks, "\x00"), "Keys agrees with ForEach")
	stitch.Check(strings.Join(vv, "\x00") == strings.Join(vs, "\x00"), "Values agrees with ForEach")
	stitch.Check(r.IsObject() == (r.Type == gjson.JSON && len(r.Raw) > 0 && r.Raw[0] == '{'), "IsObject")
	stitch.Check(r.IsArray() == (r.Type == gjson.JSON && len(r.Raw) > 0 && r.Raw[0] == '['), "IsArray")
	stitch.Check(r.IsBool() == (r.Type == gjson.True || r.Type == gjson.False), "IsBool")
	_ = r.Type.String()
}

func valid(text string) {
	stitch.Check(gjson.Valid(text) == json.Valid([]byte(text)), "Valid() agrees with encoding/json")
	stitch.Check(gjson.ValidBytes([]byte(text)) == gjson.Valid(text), "ValidBytes == Valid")
}

func values(r *gjson.Result) {
	r.Value()
	r.Int()
	r.Uint()
	r.Float()
	r.Bool()
	r.Time()
	r.Exists()
	r.String()
	r.Less(gjson.Parse("1"), true)
}

func less(a, b *gjson.Result, cs bool) {
	stitch.Check(!(a.Less(*b, cs) && b.Less(*a, cs)), "Less is antisymmetric")
}

// forEachLine: two JSON documents on two lines are read back as two results
func forEachLine(a, b string) {
	var lines []string
	gjson.ForEachLine(a+"\n"+b, func(line gjson.Result) bool {
		lines = append(lines, line.Raw)
		return len(lines) < 16
	})
	if gjson.Valid(a) && gjson.Valid(b) && !strings.ContainsAny(a+b, "\n") {
		stitch.Log("lines %q", lines)
		stitch.Check(len(lines) == 2 && lines[0] == strings.TrimSpace(a) && lines[1] == strings.TrimSpace(b), "ForEachLine yields each line")
	}
}

// escapeKey: Escape(key) is a path that finds key in an object (also below an array index)
func escapeKey(key, text string) {
	stitch.Require(key != "" && utf8.ValidString(key), "empty or invalid UTF-8 key")
	k, _ := json.Marshal(key)
	doc := `{` + string(k) + `:1}`
	p := gjson.Escape(key)
	if gjson.Valid(text) {
		// the object after another value in an array
		doc = `[` + text + `,{` + string(k) + `:1}]`
		p = "1." + p
	}
	stitch.Log("doc %q path %q", doc, p)
	stitch.Check(gjson.Get(doc, p).Raw == "1", "Escape(key) finds the key")
}

// appendJSONString: the output is one JSON string that decodes to the input (invalid UTF-8 bytes
// become U+FFFD), with or without HTML escaping
func appendJSONString(s string, html bool) {
	gjson.SetEscapeHTML(html)
	out := gjson.AppendJSONString([]byte("x"), s)
	gjson.SetEscapeHTML(true)
	stitch.Check(len(out) > 0 && out[0] == 'x', "AppendJSONString appends")
	var back string
	err := json.Unmarshal(out[1:], &back)
	stitch.Log("out %q", out)
	stitch.Check(err == nil, "AppendJSONString output is a JSON string")
	stitch.Check(back == string([]rune(s)), "AppendJSONString round trips")
	if html {
		stitch.Check(!strings.ContainsAny(string(out[1:]), "<>&"), "SetEscapeHTML(true) escapes <>&")
	}
}

func modifierExists(name string) {
	ok := gjson.ModifierExists(name, nil)
	stitch.Check(ok == (name != "nope"), "ModifierExists knows the stock and added modifiers")
}
