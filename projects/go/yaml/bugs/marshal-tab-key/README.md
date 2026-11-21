# go-yaml: check failed: Marshal output unmarshals in node_decode

| | |
|---|---|
| Kind | `check failed: Marshal output unmarshals` |
| Location | – |
| Library source | [https://github.com/go-yaml/yaml](https://github.com/go-yaml/yaml) @ `496545a6307b` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `marshal-tab-key` |

## Call sequence (5 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 new_mapping()
#1 unmarshal_node(text=Str[0,decoded]"")
#2 append_pair(m=o0.0, c=o1.0, key=Str[2]"\x09\n")
#3 node_child(n=o0.0, i=int(8))
#4 node_decode(n=o3.0)
```

## Stubs used

What each call does (from `project/harness/`):

`new_mapping` · harness/stubs.go

```go
func newMapping() *yaml.Node {
	return &yaml.Node{Kind: yaml.MappingNode}
}
```

`unmarshal_node` · harness/stubs.go

```go
func unmarshalNode(text string) (*yaml.Node, error) {
	var n yaml.Node
	if err := yaml.Unmarshal([]byte(text), &n); err != nil {
		return nil, err
	}
	return &n, nil
}
```

`append_pair` · harness/stubs.go

```go
func appendPair(m *yaml.Node, c *yaml.Node, key string) {
	stitch.Require(m.Kind == yaml.MappingNode, "not a mapping")
	stitch.Require(!contains(c, m), "would make a cycle")
	m.Content = append(m.Content, &yaml.Node{Kind: yaml.ScalarNode, Value: key}, c)
}
```

`node_child` · harness/stubs.go

```go
func nodeChild(n *yaml.Node, i int) *yaml.Node {
	if len(n.Content) == 0 {
		return nil
	}
	return n.Content[i%len(n.Content)]
}
```

`node_decode` · harness/stubs.go

```go
func nodeDecode(n *yaml.Node) error {
	var v any
	if err := n.Decode(&v); err != nil {
		return err
	}
	roundtrip(v)
	return nil
}
```

## Report

```
[stitch] CHECK FAILED: Marshal output unmarshals (node 4)
    #0 0x0 in stitchharness.roundtrip .stitch/build/app/mod/stubs.go:55
    #1 0x0 in stitchharness.nodeDecode .stitch/build/app/mod/stubs.go:176
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH go toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

