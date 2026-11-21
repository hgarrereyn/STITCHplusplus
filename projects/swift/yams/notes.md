# Yams (jpsim/Yams @ 4.0.6)

libyaml's C parser and emitter (compiled with the package, so instrumented and under ASan) and a
Swift layer. 10 stubs: `compose`, `load`, `load_all`, `roundtrip`, `serialize_options` (indent,
width, canonical, unicode, line breaks), `dump_load`, `codable` (YAMLEncoder / YAMLDecoder with a
struct of strings, ints, an optional double, an array and a dictionary), `decode_item`,
`subscript` (node accessors), `set_key`. Object: `Doc` (`Node`). Decoder: `YamlText`. Oracles:
`compose(serialize(node)) == node`, serialized nodes compose, Codable round trip.

Campaign: 15 min, 2 workers: 1.2M execs (1.3k/s), 4.4k edges, 5 buckets.

## Library bugs

Checked against 6.2.2 (the latest release): all 5 buckets still reproduce.

1. **`Yams.load` crashes on `{!!: [int, x]}`**: `Dictionary.private_construct_mapping`
   (Constructor.swift) force-unwraps nil when a mapping key is a node with an empty tag. Untrusted
   YAML can stop the process. Reproducer: `bugs/load-force-unwrap/`.
2. **A YAMLEncoder → YAMLDecoder round trip changes the value** (c2), for an item whose dictionary
   key contains U+FFFD; the field that changes was not narrowed down.
3. **Serialized nodes that do not compose back**: an empty scalar serializes to `"\n"`, which
   composes to nothing (c4, c5); a scalar `"�� !!str"` composes back to a different node
   (c3). These may be by design (Node equality includes the tag).
