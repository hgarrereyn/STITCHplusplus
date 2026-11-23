# Go harnesses

Stubs are ordinary Go functions registered with `stitch.Endpoint`. Their parameter and result
types are the signature; a spec string per parameter adds limits and data types. The harness is a
Go package that imports the library from `/src`. The fuzzer talks to a Go worker process.

## Project setup

```sh
stitch init gotoml --lang go --git https://github.com/BurntSushi/toml --rev v1.2.0
cd gotoml
```

`Dockerfile`: put the source at `/src` and download its modules.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && go mod download
```

`stitch.toml`:

```toml
[project]
name = "gotoml"
language = "go"

[source]
git = "https://github.com/BurntSushi/toml"
rev = "v1.2.0"

[go]
module = "github.com/BurntSushi/toml"        # import path of the library; it is replaced by /src
instrument = ["github.com/BurntSushi/toml"]  # packages to instrument (default: everything but the standard library)
# race = true                                # build with the race detector
# tags = ["purego"]                          # build tags

[fuzz]
timeout_ms = 2000
```

## Files

```
harness/
  objects.go           package harness: object types, data types, decoders, shared helpers
  parse.go             stubs
  tree.go
  data/TomlText/       seeds/, *.dict for the TomlText data type
```

All `.go` files go directly in `harness/` and use `package harness`. A `go.mod` is generated (or
write your own `harness/go.mod`).

## A complete example

```go
package harness

import (
	"bytes"
	"sort"
	"unicode/utf8"

	"github.com/BurntSushi/toml"
	"stitch"
)

type Doc struct{ M map[string]interface{} }

func sortedKeys(m map[string]interface{}) []string {
	ks := make([]string, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	return ks
}

// Object types are registered in a package-level variable: Go runs these before every init(),
// whatever the file order.
var _ = func() bool {
	stitch.Object[*Doc]("Doc", nil)
	return true
}()

func init() {
	stitch.Endpoint("parse", parse, "text:max=4096,type=TomlText")
	stitch.Endpoint("set_int", setInt, "d", "key:max=16", "v:range=-1000..1000")
	stitch.Endpoint("remove", remove, "d", "s")
	stitch.Endpoint("roundtrip", roundtrip, "d")
}

func parse(text string) (*Doc, error) {          // a non-nil error is a bail
	d := &Doc{M: map[string]interface{}{}}
	if _, err := toml.Decode(text, &d.M); err != nil {
		return nil, err
	}
	return d, nil
}

func setInt(d *Doc, key string, v int64) {
	d.M[key] = v
}

func remove(d *Doc, s *stitch.Source) {
	keys := sortedKeys(d.M)                      // sorted: map order is random
	stitch.Require(len(keys) > 0, "no keys")
	delete(d.M, stitch.Pick(s, keys...))         // pick an existing key
}

func roundtrip(d *Doc) {
	for k := range d.M {
		stitch.Require(utf8.ValidString(k), "invalid UTF-8 key")   // TOML cannot encode it
	}
	var buf bytes.Buffer
	stitch.Require(toml.NewEncoder(&buf).Encode(d.M) == nil, "encode failed")
	back := map[string]interface{}{}
	_, err := toml.Decode(buf.String(), &back)
	stitch.Check(err == nil, "encoder output decodes")   // a bug if false
}
```

## Objects

```go
stitch.Object[*Doc]("Doc", nil)
stitch.Object[*Enc]("Enc", func(e *Enc) { e.Buf.Reset() })   // drop: frees objects left at the end
```

Register object types before the endpoints that use them, in a package-level `var _ = func()
bool { ...; return true }()` as above. With `init()` in several files, Go runs them in file-name
order, and an endpoint registered before its object type fails to build with "unsupported type
*harness.Doc (register object types with stitch.Object)".

A parameter of a registered object type is an object input; a result of one is a new object.

| Spec or code | Meaning |
|---|---|
| a parameter of an object type | uses the object; it stays available |
| `"e:take"` | the call consumes this object |
| `stitch.Consume(x)` | on a borrowed input: the library took ownership in this call |
| a result of an object type | a new object; `nil` is a bail |
| `"out0:optional"` | result 0 may be `nil`: no object, not a bail (results count from 0) |
| a trailing `error` result | a non-nil error is a bail |

Result specs go after the parameter specs:

```go
stitch.Endpoint("parse_meta", parseMeta, "text:max=4096,type=TomlText")
func parseMeta(text string) (*Doc, *Meta, error) { ... }      // two outputs

stitch.Endpoint("lookup", lookup, "d", "s", "out0:optional", "out1:optional")
func lookup(d *Doc, s *stitch.Source) (*Doc, *Arr) {          // a table or an array
	ks := sortedKeys(d.M)
	stitch.Require(len(ks) > 0, "empty table")
	switch x := d.M[stitch.Pick(s, ks...)].(type) {
	case map[string]interface{}:
		return &Doc{M: x}, nil
	case []interface{}:
		return nil, &Arr{A: x}
	}
	stitch.Bail("not a table or array")
	return nil, nil
}

stitch.Endpoint("enc_finish", encFinish, "e:take")
func encFinish(e *Enc) (*Doc, error) { ... }
```

## Fuzzed parameters

One spec string per parameter, in order: the parameter's name, then options after a colon.

| Parameter type | Value |
|---|---|
| `string` | text (`max=`, `type=`); raw bytes, so it can be invalid UTF-8 |
| `[]byte` | bytes (`max=`, `type=`) |
| `int`, `int8`..`int64`, `uint`..`uint64` | integer |
| `float32`, `float64`, `bool` | float, bool |
| `stitch.Path` | path of a temporary file holding the data (`max=`, `type=`); a `string` type |
| `*stitch.Source` | choices inside the stub (`max=`) |
| any type with `val=` | the value a decoder returns |

| Spec | Meaning |
|---|---|
| `"text:max=4096,type=TomlText"` | at most 4096 bytes, data type TomlText |
| `"n:range=0..9"` | integer in [0, 9] |
| `"mode:oneof=1\|2\|4"` | one of the values; strings for a `string` parameter (`"kind:oneof=str\|float\|empty"`) |
| `"opts:flags=1\|2\|4"` | OR of a subset |
| `"es:val=Entries,max=512"` | the value of the `Entries` decoder |
| `"s:max=64"` | a `*stitch.Source` with 64 bytes of choices |

```go
stitch.Endpoint("parse_file", parseFile, "p:max=4096,type=TomlText")
func parseFile(p stitch.Path) (*Doc, error) {
	d := &Doc{M: map[string]interface{}{}}
	if _, err := toml.DecodeFile(string(p), &d.M); err != nil {
		return nil, err
	}
	return d, nil
}

stitch.Endpoint("new_encoder", newEncoder, "indent:oneof=|  |\t|    ")   // "" is a value too
stitch.Endpoint("encode", encode, "d", "opts:flags=1|2|4")
```

## Bail, require, check

```go
stitch.Bail("reason")                 // stop this call; not a bug
stitch.Require(cond, "reason")        // bail unless cond
stitch.Check(cond, "what")            // a bug if false: "check failed: what"
stitch.Log("out=%q", out)             // printf-style; printed by `stitch exec --trace`
stitch.Init(func() { time.Local = time.UTC })   // once per worker process, before any stub
```

These are bugs: panics (index out of range, nil dereference, ...), fatal runtime errors (stack
overflow, concurrent map writes), data races with `race = true`, a failed `stitch.Check`, and
programs that run past `timeout_ms`.

## Attributes

```go
stitch.Set(d, "depth", stitch.GetInt(d, "depth", 0)+1)   // any value
stitch.GetInt(d, "depth", 0)            // int64 with a default
stitch.GetString(d, "name", "")
v, ok := stitch.Get(d, "depth")         // any; a miss is reported by `stitch probe`
stitch.Has(d, "depth")
stitch.Del(d, "depth")
stitch.GlobalSet("mode", 1)             // program-wide; reset for every program
v, ok := stitch.GlobalGet("mode")
```

Links to other objects:

```go
func parseMeta(text string) (*Doc, *Meta, error) {
	d := &Doc{M: map[string]interface{}{}}
	md, err := toml.Decode(text, &d.M)
	if err != nil {
		return nil, nil, err
	}
	m := &Meta{MD: md}
	stitch.Set(m, "doc", d)                // link the Meta to the Doc it describes
	return d, m, nil
}

func metaDoc(m *Meta) {
	d, ok := stitch.GetObj[*Doc](m, "doc") // ok is false once the Doc was consumed
	stitch.Require(ok, "doc is gone")
	_ = d
}
```

## Data types and decoders

Tag a `string`, `[]byte` or `stitch.Path` parameter with `type=Name` and create
`harness/data/<Name>/` with `seeds/` (sample inputs) and `*.dict` files (one quoted token per
line). A decoder builds values from fuzzer choices:

```go
stitch.Decoder("TomlText", func(s *stitch.Source) string {
	var b strings.Builder
	for s.More() {
		switch s.Choice(3) {
		case 0:
			fmt.Fprintf(&b, "[%s]\n", s.Token())
		case 1:
			fmt.Fprintf(&b, "%s = %d\n", s.Token(), s.Range(-1000, 1000))
		default:
			fmt.Fprintf(&b, "%s = %q\n", s.Token(), s.Str(16))
		}
	}
	return b.String()
})
```

`stitch sample TomlText -n 5` prints values the decoder makes.

A decoder can return any type. Take it with a `val=` spec, and build decoders on each other with
`stitch.Draw[T](s, name)`:

```go
type Entry struct {
	Key string
	Val interface{}
}

stitch.Decoder("GoValue", func(s *stitch.Source) interface{} { return goValue(s, 0) })
stitch.Decoder("Entries", func(s *stitch.Source) []Entry {
	var es []Entry
	for s.More() {
		es = append(es, Entry{Key: s.Str(8), Val: stitch.Draw[interface{}](s, "GoValue")})
	}
	return es
})

stitch.Endpoint("from_entries", fromEntries, "es:val=Entries,max=512")
func fromEntries(es []Entry) *Doc { ... }
```

`*stitch.Source` methods:

| Method | Returns |
|---|---|
| `U8()` .. `U64()`, `I8()` .. `I64()`, `F32()`, `F64()` | a number |
| `Range(lo, hi int64) int64` | integer in [lo, hi] |
| `Choice(n int) int` | index in [0, n) |
| `stitch.Pick(s, items...)` | one of the items |
| `Coin(p float64) bool` | true with probability about p |
| `More() bool` | loop condition: `for s.More() { ... }` |
| `F64Range(lo, hi)`, `F32Range(lo, hi)` | finite float in [lo, hi] |
| `Str(max)`, `Bytes(max)` | text or bytes of at most `max` bytes |
| `Fixed(n)` | exactly n bytes |
| `Rest()` | every byte that is left |
| `Token()` | often a token from the data type's `.dict` files; otherwise (and once mutated) any string, like `str`: check it when the input must be one of the tokens (or use `pick`) |
| `stitch.Draw[T](s, name)` | a value from data type `name`'s decoder |

Choices must replay the same way: sort map keys before `stitch.Pick`.

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/repro.go`, a program built inside the harness
module that calls each stub with its recorded arguments:

```go
		r0 := stitch.Call(0, "parse", "k = [1, {\"\" = 2}]")
		doc0 := r0[0]
		stitch.Call(1, "roundtrip", doc0)
```

Shrink a copy to plain library code (it can drop the `stitch` import) and check it after each
edit:

```sh
cp .stitch/standalone/c1/repro.go .stitch/standalone/c1/min.go
stitch try .stitch/standalone/c1/min.go
```

A finished reproducer for a failed check:

```go
package main

import (
	"bytes"
	"fmt"

	"github.com/BurntSushi/toml"
)

func main() {
	var first bytes.Buffer
	toml.NewEncoder(&first).Encode(map[string]interface{}{"k": []interface{}{int64(1), map[string]interface{}{"": int64(2)}}})
	// first: k = [1, {"" = 2}]
	back := map[string]interface{}{}
	toml.Decode(first.String(), &back) // bug: back["k"] is []map{{"": 2}}; the 1 is dropped
	var second bytes.Buffer
	toml.NewEncoder(&second).Encode(back)
	if first.String() != second.String() {
		panic(fmt.Sprintf("encode is stable across a round trip: %q != %q", first.String(), second.String()))
	}
}
```

```
SAME CRASH: .stitch/standalone/c2/min.go -> check failed: encode is stable across a round trip
  17 code lines, plain library code
```

`stitch try FILE` with no crash id runs any `.go` file in the harness module and reports what it
does; add `--show` to see its output. Details: `stitch guide reproducers`.

## Coverage

While fuzzing, the `instrument` packages are built with the instrumentation `go test -fuzz` uses.
`stitch cov` builds with `-cover` and reports lines and functions.
