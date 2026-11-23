# kramdown (gettalong/kramdown @ REL_2_3_0)

22 stubs in `harness/kd.rb`: producers `parse` (input kramdown/html/markdown + an option decoder),
`parse_default`, `parse_raw` (raw 300-byte text), `root`, `child`; converters `to_html`, `to_latex`,
`to_man`, `to_toc`, `to_hash_ast`, `to_remove_html_tags`, `to_kramdown`, `tree_to_kramdown`;
`warnings`; tree mutators `add_para`, `add_header`, `clear_children`; element accessors
`elem_inspect`; oracles `roundtrip_kramdown` (to_kramdown re-parses to the same HTML, whitespace
normalised), `html_deterministic` (two parses give the same HTML), `hash_ast_matches_tree`,
`to_html` twice equal. Decoders: `KdText` (headings with IDs, IALs, lists, tables, footnotes,
block/span HTML with `markdown=` attributes, smart-quote and entity text; 148 seeds from the
upstream testcases) and `KdOpts` (auto_ids, parse_block_html, parse_span_html, html_to_native,
smart_quotes, entity_output, header_offset, toc_levels, footnote_*, line_width, ...). GFM was
skipped (needs kramdown-parser-gfm). `Kramdown::Error` is the only declared bail.

Build: kramdown has no checked-in gemspec, so the Dockerfile copies the source, sets
`RUBYLIB=/src/lib` and `gem install rexml stringex rouge coderay` (stringex is needed for
`transliterated_header_ids`; without it `Gem::MissingSpecError`, a harness issue not a bug).

Campaigns: probe 60s; 600s (75k execs, 125/s, ~1.9k edges, 37 buckets after the oracle was fixed);
360s final (68k execs, 17 buckets). `stitch cov`: 95.6% lines / 88.2% branches of lib/.

## Library bugs (all checked with `stitch exec/try --rev REL_2_6_1`, the latest tag)

1. **Self-referencing IAL overflows the stack.** `Kramdown::Document.new("{:a: .x a}\n\npara\n{:a}\n")`
   -> SystemStackError in `update_attr_with_ial`. Still present at 2.6.1. `bugs/ial-self-reference-stackoverflow/`.
2. **Footnote that references itself: `to_latex` overflows, `to_kramdown` hangs.**
   `"[^1]: a[^1]\n\nx[^1]\n"`: `.to_latex` SystemStackError; `.to_kramdown` never returns (60s killed).
   Both present at 2.6.1. `bugs/footnote-self-reference-*`.
3. **Invalid numeric entity yields invalid UTF-8.** `"&#9999999;"` parses, `to_kramdown`/`to_html` of
   the code-block variant raise `ArgumentError: invalid byte sequence in UTF-8` (`String#strip`,
   `gsub`); a re-parse then raises RuntimeError "invalid characters". Present at 2.6.1.
   `bugs/numeric-entity-invalid-utf8/`.
4. **Control character in a nested list item**: `"* \x00\n    * \n"` -> NoMethodError `value` for nil
   in `parse_list` (list.rb:136). Present at 2.6.1. `bugs/list-nul-nomethoderror/` (verified with
   `stitch repro`).
5. **`<table markdown=...>` with stray/empty content** -> NoMethodError `type` for nil in
   `ElementConverter#convert_table`. Fixed in 9c40aa5798af ("Fix exception in HTML table parsing when
   table is empty", 2024-11-17; found by `stitch bisect --fixed`). Not reproducible at 2.6.1.
6. **`<td>`/`<th>` tables from HTML**: `Html#convert_td` NoMethodError (nil element), and
   `convert_th` missing in the Kramdown, Man and Latex converters (NoMethodError). Still at 2.6.1.
7. A small `line_width` (seen in c33, exact value not minimized) gives `RegexpError: upper is smaller than lower in repeat
   range /(.{1,0})( +|$\n?)/` in `Kramdown` converter `convert_p`. Still at 2.6.1.

Not bugs: `to_kramdown` is lossy for whitespace (soft-break indentation collapses), so the round trip
oracle only fires on odd whitespace/escapes and stays noisy on random text (c8); crashes with a
worker death ("crash (no sanitizer report)", no replay) came from load.

## Harness notes
- Removed the `template` option (reads files), and `Converter::Html.convert(child_element)` (needs
  context from the parent: indentation, ArgumentError `String#*`, harness bug).
- `hash_ast` leaves have no `:children` key; first oracle crashed on `nil.sum`.
