# gjson (tidwall/gjson @ v1.6.4)

7 stubs: `parse`, `get`, `result_get`, `get_many`, `iterate` (`ForEach`), `valid`, `values`. Object:
`Result`. Decoders: `JsonText` (JSON with awkward keys such as `#`, `*`, `c.d` and escapes) and
`PathText` (gjson paths: wildcards, `#(...)` queries, modifiers such as `@this`,
`@pretty`, `@reverse`, `@flatten`). Oracles: `Get(json, p) == Parse(json).Get(p)` and `@this` returns the document (on
valid JSON only; results on invalid JSON are undefined), and `Valid` agrees with `encoding/json`.

Campaign: 15 min, 2 workers: 5.9M execs (6.6k/s), 1264 of 1490 edges, 3 buckets.

## Library bugs (all fixed by v1.20.5)

1. **`Valid` accepts a lone `-` as a number** (`{"a":-}`). Reproducer: `bugs/valid-lone-minus/`
   (`./run.sh`, and `./run.sh --standalone` for a 12-line program).
2. **`Parse` of a top-level string with an escaped quote drops the closing quote** from `Raw`:
   `Parse("\"XM\\\"ZI~ \"").Raw` is `"XM\"ZI~ ` (so `@this` disagrees with the document).
3. **`Get(json, "#")` and `Parse(json).Get("#")` disagree** on a top-level string containing `[`
   and an escaped quote (`"1"` vs empty): the string scanner stops at the escaped quote.
