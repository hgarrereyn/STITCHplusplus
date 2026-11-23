# pulldown-cmark v0.8.0 (Rust)

Library: CommonMark pull parser. Pinned `v0.8.0` (`default-features = false`, dep path `/src`).
Latest checked: 0.13.6 (crates.io; newer revs are a workspace, so `stitch exec --rev` does not build).

## Stubs (harness/stubs.rs, 14)
- Object `Doc` = owned text + Options (Parser borrows text, so it is rebuilt per stub).
- Producers: `new_doc` (Markdown data type + option flags), `new_doc_raw` (plain bytes).
- Mutators: `set_options`, `append_text`, `insert_text`.
- Consumers / oracles: `render_html` (twice, equal), `render_write_html` (push_html == write_html),
  `offsets_valid` (offset iter ranges ordered, in bounds, on char boundaries, Start/End balanced),
  `offsets_match_plain`, `events_to_html_roundtrip`, `render_with_broken_links` (callback modes),
  `broken_link_offsets`, `heading_levels`, `render_idempotent_html` (render HTML output as markdown).
- Data: `data/Markdown/` dict + 2 seeds, decoder mixing tokens.

## Campaigns
- probe 60 s: 45.6k execs, 2 crashes. main 600 s `--jobs 1`: 294k execs (490/s), 5200 edges, corpus 3482, 2 crashes.
- `stitch cov`: library lines 96.7%, functions 94.4%. No timeouts / quadratic blowups found.

## Library bugs (v0.8.0)
1. Panic in `Parser::scan_inline_html` (parse.rs:2762, slice start out of range) on `"><a\r"` with default options.
   Fixed at latest (0.13.6, verified). Repro: `bugs/scan-inline-html-oob/`.
2. `into_offset_iter` yields an event with `range.start > range.end` for tables: `"|| |\n-|-"` with ENABLE_TABLES.
   Fixed at latest (verified). Repro: `bugs/table-offset-inverted/min.rs`.

## Harness notes
- Dropped a "no NUL in text" oracle: 0.8.0 and 0.13.6 both keep U+0000 (not a crash, spec says U+FFFD).
- Removed a `Parser::clone` stub: Parser is not Clone in 0.8.0.
- Heading is `Tag::Heading(u32)` in 0.8; the harness does not compile against 0.13 (API changed), so latest checks are done with plain cargo programs.
- Not done: bisect.
