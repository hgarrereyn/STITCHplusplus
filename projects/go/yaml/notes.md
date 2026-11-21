# go-yaml v3 (go-yaml/yaml @ 496545a, the v3 branch before v3.0.0)

10 stubs: `unmarshal_any`, `unmarshal_node`, `unmarshal_struct`, `decode_stream` (`Decoder` with
`KnownFields`), `new_mapping`, `append_pair`, `node_child`, `node_set`, `node_encode`,
`node_decode`. Object: `Node` (`*yaml.Node`). Decoder: `YamlText`. Oracle: `Marshal` of a decoded
value decodes to an equal value. Numbers compare by value (yaml.v3 writes the float 1.0 as `1`),
and maps compare as `map[any]any` (a `!!binary` key turns `map[string]any` into one). Node edits
keep trees well formed: no cycles, and mapping nodes get key/value pairs.

Campaign: 15 min, 2 workers: 9.0M execs (10k/s), 3291 of 4627 edges, 7 buckets.

## Library bugs

Checked against v3.0.1 (the last gopkg.in/yaml.v3 release) with `stitch exec <id> --rev v3.0.1`.

Still present in v3.0.1:
1. **`Marshal` writes keys that don't round-trip.** A key that starts with a tab and contains a
   newline (`"\t,\no"`) becomes a block scalar `? |-\n    \t,\n    o`, which the parser rejects
   ("found a tab character where an indentation space is expected"). A key of only newlines
   (`"\n\n\n"`) is written as `? |4+` and reads back with one newline fewer. Reproducer:
   `bugs/marshal-tab-key/`.

Fixed in v3.0.0:
- **CVE-2022-28948: nil pointer dereference in `yaml_parser_split_stem_comment`** (parserc.go:789)
  on a comment followed by nested block sequences, e.g. `#uOk$<c/c: {d: e}\n - \n   - \n...`. Reproducer:
  `bugs/cve-2022-28948/`.
- "internal error: attempted to parse unknown event (please report)" panics on malformed input
  such as `---\n!!str \xd8` (4 buckets).

## Harness notes

- The roundtrip oracle normalizes numbers and map types (see above). Without that, `0e9` (a float
  that is written as `0`) and `!!binary` keys were false positives.
