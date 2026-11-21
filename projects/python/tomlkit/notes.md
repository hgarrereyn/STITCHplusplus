# tomlkit (python-poetry/tomlkit @ 0.13.2)

16 stubs: parse, document and table building (`add_table`, `table_set`, `table_add_table`,
`table_comment`, `new_array`, `array_append`, ...), `remove`, `roundtrip`. Objects: `Doc`, `Table`,
`Arr`. Decoders: `TomlText` (TOML assembled from keys, tables, arrays and values) and `TomlKey`.
Oracles: parse then print reproduces the input text, and every edited document prints text that
parses back to the same data.

Campaign: 15 min, 2 workers: 1.1M execs (1.3k/s), 2888 edges, 13 buckets.

## Library bugs

Checked against 0.15.2 (the latest release) with `stitch exec <id> --rev 0.15.2`.

Still present in 0.15.2:
1. **Comments are not escaped.** `table.comment("\x14")` prints `[""] # \x14`, and parsing that
   output fails with `InvalidControlChar`. Reproducer: `bugs/comment-control-char/`.
2. **An empty super-table disappears.** `doc.add("", table(is_super_table=True))` prints an empty
   string, so the key is lost.
3. **`doc.add` accepts a key that is already a super-table.** The output then has the same
   `["k"]` header twice and doesn't parse (`ParseError`).

Fixed between 0.13.2 and 0.15.2:
- `OverflowError` parsing dates with huge years (`dob = 01234567891979-05-27T07:32:00-08:00`), from
  `parse_rfc3339`.
- `[[""]]` (an array of tables with an empty name) printed differently from its input.
- Five printer bugs where the output doesn't parse: empty keys, invalid number text, keys or
  strings that need quoting, and an empty table name. In 0.15.2 the setters reject these with a
  `ValueError` instead.

## Harness notes

- Keys of existing tables come from a `Src` pick over `keys()`, not fuzzed strings. Otherwise
  lookups almost always miss.
- `OutOfOrderTableProxy` (a table whose parts are spread over the document) has no `comment()`,
  so the stub requires one.
