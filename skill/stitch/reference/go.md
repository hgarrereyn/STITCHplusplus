# Go harnesses

Stubs are Go functions registered in `init()`. The library source is at `/src`; the harness is a
module that requires it.

## Project

```sh
stitch init yaml --lang go --git https://github.com/go-yaml/yaml --rev v3.0.1
```

```toml
[project]
language = "go"

[go]
module = "gopkg.in/yaml.v3"              # import path of the library (replaced by /src)
instrument = ["gopkg.in/yaml.v3"]        # packages to instrument (default: all but std)
# race = true                            # build with the race detector
```

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && go mod download
```

## Stubs

`harness/*.go` (package `harness`) becomes the module `stitchharness`; a `go.mod` is generated
(or use your own in `harness/go.mod`).

```go
package harness

import (
	"gopkg.in/yaml.v3"
	"stitch"
)

func init() {
	stitch.Object[*yaml.Node]("Node", nil)
	stitch.Endpoint("parse", parse, "text:max=2048,type=YamlText")
	stitch.Endpoint("child", child, "n", "i:range=0..8")
}

func parse(text string) (*yaml.Node, error) {   // a non-nil error is a bail
	var n yaml.Node
	if err := yaml.Unmarshal([]byte(text), &n); err != nil {
		return nil, err
	}
	return &n, nil
}

func child(n *yaml.Node, i int) *yaml.Node {     // nil: no object (a bail)
	if len(n.Content) == 0 {
		return nil
	}
	return n.Content[i%len(n.Content)]
}
```

Parameters of registered object types are objects; results of object types are new objects (a
trailing `error` is a bail). Fuzzed parameters: `string`, `[]byte`, ints, floats, `bool`,
`stitch.Path` (temp file), `*stitch.Source`. One spec string per parameter, in order:

| Spec | Meaning |
|---|---|
| `"text:max=4096,type=YamlText"` | size limit and data type |
| `"n:range=0..9"` | integer in [0, 9] |
| `"mode:oneof=1\|2\|4"` | one of the values (strings for a string parameter) |
| `"flags:flags=1\|2\|4"` | OR of a subset |
| `"pts:val=Polygon,max=512"` | the value of the `Polygon` decoder (any Go type) |
| `"node:take"` | the call consumes this object |
| `"out1:optional"` | result 1 may be nil |

Also `stitch.Decoder(name, fn)` (`fn` returns a string / `[]byte`, or any type for `val=`; build on
another with `stitch.Draw[T](s, name)`), Source extras `s.F64Range(lo, hi)`, `s.F32Range(lo, hi)`,
`s.Fixed(n)`, `s.Rest()`, `stitch.Init(fn)`, `stitch.Bail/Require/Check/Log/Consume`,
`stitch.Set/Get/GetInt/GetObj`, and `stitch.Pick(s, items...)` on a Source.

## Bugs

Panics (index out of range, nil dereference, ...), fatal runtime errors (stack overflow, concurrent
map writes; the worker dies and triage reads Go's traceback), data races with `race = true`, and
failed `stitch.Check`.

## Coverage and comparisons

The worker is built with `-gcflags=<pkgs>=-d=libfuzzer`, the instrumentation `go test -fuzz` uses:
8-bit edge counters and comparison hooks. `stitch cov` builds with `-cover`.

## Reproducers

`stitch write c1` writes `repro.go` (`stitch.Call(n, "name", args...)` per call). `stitch try`
builds any `.go` file as a command inside the harness module; a plain minimized file is compared
through Go's panic output.
