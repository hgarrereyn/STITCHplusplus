# SnakeYAML (bitbucket snakeyaml/snakeyaml @ snakeyaml-1.30)

54 stubs, 9 object types (`Yaml` always built on `SafeConstructor`, `Data` = Java values loaded or built,
`List`/`Map`/`Set` builders, `DataList`, `Node`, `SeqNode`, `MapNode`). One data type `YamlText` (structured
decoder: block/flow collections, block scalars, anchors/aliases, tags, directives, quoted escapes, multi-docs).

Stubs: `yaml_default`, `yaml_options` (LoaderOptions + DumperOptions: flow/scalar style, indent, indicator
indent, width, unicode, canonical, split lines, pretty flow, explicit start/end, line break, non-printable
style, max aliases, max simple key); `load`, `load_all`, `load_as_map`, `load_bytes`, `compose`, `compose_all`,
`parse_events` (balance oracle), `scan_tokens`, `parser_comments`; builders `data_*` / `*_add` / `*_put` /
`*_as_data`; `dump`, `dump_all`, `dump_writer`, `dump_as_map` (also `dumpAs`), `represent`, `serialize_events`,
`scalar_*`/`seq_node`/`map_node`/`seq_append`/`map_add` (node trees, can be made recursive); `load_deep`,
`load_alias_bomb`. Oracles: `dump_roundtrip` (load(dump(x)) == x), `dump_all_roundtrip`, `load_dump_load`,
`canonical_dump`, `serialize_roundtrip` / `compose_serialize` (compose(serialize(node)) equals node, also
canonical), `dump_string_roundtrip`, `dump_writer` (dump(x, writer) == dump(x), anchors normalised).
Documented bails: `YAMLException` (all parse/compose/emit errors).

Build: the pom targets Java 7, so the Dockerfile compiles `src/main/java` with `javac --release 8` and jars it
(same trick as org-json). The harness compiles against 1.30 and 2.6 (`SafeConstructor(new LoaderOptions())`,
scanner/parser built by reflection), so `stitch exec --rev snakeyaml-2.6` works.

Campaigns (1 worker, shared loaded machine, so execs/s is low): main 600 s 1.1M execs; run2 600 s 32k execs
(deep inputs are slow: 20 KB nested input takes ~2 s); run3 600 s 9.3k execs; 5.6k edges of the instrumented
packages; `stitch cov`: 67% lines of org.yaml.snakeyaml (Emitter 87%, Scanner 94%, Parser 93%,
SafeConstructor 77%; `Constructor` 0% because only SafeConstructor is harnessed on purpose).

## Library bugs (1.30; status at 2.6 = latest tag)

1. **Emitter output is not loadable with large `indent` / `indicatorIndent`.** `DumperOptions` with indent 10,
   indicator indent 7, block style: `{b: [x, {z: 1}]}` dumps as text that `load` rejects
   (ParserException "expected <block end>, but found '<block mapping start>'"). Also `!!binary` inside a list
   at indent 10 / indicator 5 is dumped with the literal block content at column 0. Still in 2.6.
   Reproducer: `bugs/indent-indicator-unloadable/` (verified by `stitch repro`, `Min.java` is the plain-library file).
2. **`dump(String)` is not injective for NEL / BOM.** `"x\u0085"` (and `"\u0085"`) is dumped as a literal block
   containing the raw NEL, which loads back as `"x\n"`; the one-character string `"﻿"` is dumped as a plain
   scalar `﻿` and loads back as `null` (BOM stripped). Still in 2.6 (`dump_string_roundtrip`).
3. **`compose(serialize(node))` differs from `node`** for some represented trees (run3 c14: `represent` of
   loaded data, then `serialize_roundtrip`): tag or value changes. Still in 2.6 (run3 c14). Not minimised.
4. **Constructor leaks with non-YAMLException**: `load` throws `IllegalArgumentException` (bad `!!binary` base64),
   `NumberFormatException` (`!!float`/`!!int` with bad text, `1_`, `0b`), `ClassCastException` (`!!set`,
   `!!seq`, `!!map` on a scalar), `StringIndexOutOfBoundsException` (`!!float` with empty text). Still in 2.6
   (Base64/NumberFormat ones checked).
5. **StackOverflowError on `load` of deeply nested flow/block/`?` input** (~3-10 KB `[[[[...x]]]]`, 20 KB
   makes `dump` of the result overflow too); each overflow site gives a different crash bucket (11 buckets for
   one bug). CVE-2022-25857 class. Fixed by commit fc300780da21 "Restrict nested depth for collections to avoid DoS attacks" (2022-04-28, found with `stitch bisect --fixed`, ~10 min); 2.6: no crash.
6. **Loaded lone surrogate (`"\ud800"`) cannot be dumped**: `YAMLException: invalid string value has occurred`
   from `represent`/`dump` (load/dump asymmetry). Harness bails on it.
7. **Pre-1583 dates** (`0000-01-01`) do not round trip (also canonical): calendar skew. Harness ignores dates
   before 1583.
8. API quirk, not a crash: `new Yaml(constructor, new Representer(), dumperOptions)` silently ignores
   `NonPrintableStyle` / other options read by `Representer(DumperOptions)` (use `new Representer(d)`).
   `DumperOptions` anchor generator is shared state: successive `dump()` calls number anchors id001, id002...
   (so dump output is not deterministic; harness normalises).
9. Slow: load time of nested flow input grows super-linearly (4000 levels 1.2 s, 50000 levels 6.5 s); program
   timeouts (3 s) under load.

## Harness notes

- `String` cannot be returned from a stub: oracles are `void` stubs doing the whole dump/load comparison.
- Never put `YamlText` decoder edits between a campaign and its triage: saved .stp files store decoder bytes
  and stop reproducing.
- Constructor-level leaks are bails (`constructor leak`, found by stack frame `*Constructor*`) in every stub
  except plain `load`, so they are reported once and do not hide oracle failures.
- Known-bad value classes are filtered from the generic oracles (`NEL/BOM string`, non-printable strings with
  BINARY style, lone surrogates, pre-1583 dates, empty scalar node serializes to nothing) and kept testable in
  dedicated stubs.
- Not harnessed on purpose: `new Yaml()` with the unsafe default `Constructor` (arbitrary class instantiation,
  possible network access through gadget classes).
