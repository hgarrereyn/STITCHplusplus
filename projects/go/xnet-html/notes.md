# golang.org/x/net/html @ v0.12.0

9 stubs: `parse`, `parse_fragment`, `tokenize`, `child`, `render_roundtrip`, `render`, `set_attr`,
`append_text`, `escape`. Object: `Node`. Decoder: `Html` (open and close tags from a list that includes
`svg`, `math`, `mtext`, `foreignObject`, `template`, `table`, `style`, `script`, `plaintext`, with
attributes, entities, comments, CDATA and doctype).
Oracle: for a tree the parser built, rendering it and parsing the output again gives the same
svg/math (foreign content) subtrees. Whole trees need not round-trip (the HTML spec's adoption
agency and foster parenting), and the spec says `<plaintext>` cannot. Edited trees are checked
for crashes only.

Campaign: 15 min, 2 workers: 6.1M execs (6.7k/s), 1809 of 2460 edges, 1 bucket.

## Library bug

**CVE-2023-3978: text inside foreign content is rendered unescaped.** A `<style>` (or other
raw-text element name) inside `<svg>` or `<math>` is an ordinary element there, but `Render`
wrote its text without escaping. Parsing the output then gives a different tree, which is an XSS
vector when rendered HTML is trusted. The bucket ("svg/math content survives render + parse")
goes away at v0.13.0, the fixed release. Reproducer: `bugs/cve-2023-3978/`.

## Harness notes

- The `Html` dictionary (`harness/data/Html/h.dict`) has tokens for foreign content and raw-text
  elements (`<svg `, `<math>`, `<style>`, `<plaintext>`, `<xmp>`, ...), so the fuzzer reaches the
  CVE's element combinations quickly.
