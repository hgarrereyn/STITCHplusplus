# yaml (eemeli/yaml @ v2.6.1)

12 stubs: `parse_document`, `parse_all`, `parse_value`, `new_document`, edits (`set_in`,
`delete_in`, `get_in`, `node_ops`, `create_alias`, `visit`), `print_roundtrip` and
`stringify_roundtrip` (values built from a `Src`). Objects: `Doc`, `Node`. Decoder: `YamlText`
(nested block and flow collections, complex keys, anchors and aliases, merge keys, block scalars,
`!!str` tags, document markers). Oracles: an error-free document prints to text that parses with no
errors to the same JS value, and `stringify(v)` parses back to `v`. Both stubs log the printed
text, so `stitch exec --trace` shows it.

Campaign: 15 min, 2 workers: 1.8M execs (2.1k/s), 2800 edges, 6 buckets.

## Library bugs (all still present in v2.9.1, the latest release)

1. **Block scalars with a leading space and a non-default `indent`.** `stringify(" ? x:\n",
   {indent: 3})` writes `|1\n ? x:\n`. The indentation indicator is relative to the content, so the
   value reads back as `"? x:\n"` and the leading space is lost. With a second line (`" x:\n2"`)
   the output doesn't parse at all: `|1-\n x:\n2\n`, "Unexpected scalar at node end".
   Reproducer: `bugs/stringify-block-scalar/`.
2. **Anchors with control characters.** The parser accepts `&\ba ...` and `*\0`, then
   `toString()` throws "Anchor must not contain whitespace or control characters" (2 buckets).
3. **YAML 1.1 complex key with a nested sequence.** `? k:\n  - \n    - a\n  - a\n  - a\n` prints as
   `? k:\n    \n    - - a\n ...`, which has a different value.
4. **A comment followed by a lone CR** (`#\r `) prints as `\r#\n\nnull\n`, which doesn't parse
   ("Unexpected scalar token").

## Harness bugs found and fixed

- Errors the API documents for invalid requests ("Expected YAML collection", "Unresolved alias",
  "Excessive alias count", ...) are bails, declared with a predicate in `bailsOn`.
- `visit` with "replace" never ended: the visitor visits the replacement nodes too. Replacements
  are now marked and not replaced again.
- Reprinting uses the same `version`/`merge` options the document was parsed with.
- `lineWidth` is 0 or at least 20 (the documented minimum).
- A tag-setting stub was removed: tags made unparseable output by design.
