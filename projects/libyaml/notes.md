# libyaml (yaml/libyaml @ 840b65c, Debug build so library asserts are active)

33 endpoints in `parse/`, `emit/`, `document/`. Object types: `Parser` (owns its input string;
attribute `mode` enforces the documented "don't mix scan/parse/load" rule), `Emitter` (writes into a
string), `Event`, `Token`, `Document`. The emitter takes ownership of events and documents, even on
failure, so `emit`/`dump` use `Take<>`. Decoders: `YamlText` (block and flow collections, every
scalar style, anchors/aliases, tags, directives, multiple documents), `YamlAnchor`, `YamlTag`.
Seeds: libyaml's `examples/`.

## Findings

- **Questionable: `yaml_parser_load` asserts when called again after it failed.** After an error,
  `yaml_parser_parse` returns success with an empty `YAML_NO_EVENT` (it returns 1 when
  `parser->error` is set), and the loader asserts the event type (loader.c:100 / :189). Release
  builds continue on the empty event. Not memory corruption, but the API gives callers no
  well-defined way to detect this.

## Harness bugs found and fixed through triage (all `assert` aborts on API preconditions)

- `yaml_emitter_close` before open; `yaml_emitter_open` twice.
- `yaml_emitter_flush` before the STREAM-START event set the output encoding.
- NULL value pointers for zero-length scalars (`Bytes::data()` of an empty buffer).
- Reusing a parser after a load error (see above).

The 45-minute campaign after these fixes found no further crashes.
