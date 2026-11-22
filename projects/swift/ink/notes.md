# Ink (JohnSundell/Ink) 0.5.0

Pure-Swift Markdown parser. Pinned 0.5.0; latest is 0.6.0 (checked with `stitch exec --rev 0.6.0`).
Note: the SwiftPM product is `Ink` (capital I); `stitch init` wrote `ink`.

## Stubs (14)
- Objects: `Parser` (MarkdownParser, a struct), `Doc` (Markdown).
- Producers: `new_parser`, `new_parser_with(target, kind)`, `parse(parser, md)`, `parse_plain(raw bytes)`.
- Mutators: `add_modifier(parser, target, kind)` (all 13 `Modifier.Target`s; kinds: identity, wrap, uppercase, echo markdown, empty), `set_title`, `set_metadata`.
- Consumers/oracles: `html_from` (html(from:) == parse().html), `deterministic` (two parses agree on html, metadata, title), `title_consistent` (title != nil implies `<h1` in html), `metadata_consistent` (metadata implies leading `-`, keys appear in input), `doc_html`, `doc_title`, `reparse_html`.
- Data type `MdText`: decoder emitting markdown constructs (front matter, headings, lists, tables, code fences, links, images, raw html, link definitions), seeds + dict in harness/data/MdText.

## Campaigns
- probe 60s (took 179s wall): 7.8k execs, 1585 edges, 8 crash buckets.
- main 600s: 54k execs, ~1620 of 11k edges (Ink is small; rest is Swift runtime), 9 buckets.
- main resume 400s with guards for known bugs: 38k execs, 1666 edges, 5 buckets (all known bugs).
- Not done: `stitch bisect` (hangs make each step 60 s).

## Library bugs (all still present in 0.6.0 unless noted)
1. Trap "String index is out of bounds" in `Metadata.read` (Reader.currentCharacter at end of input) when front matter never closes: `MarkdownParser().parse("---\n\n")`. Reproduces on 0.5.0 and 0.6.0. Repro: bugs/metadata-unterminated-trap (verified), bugs/metadata-unterminated-trap/min.swift.
2. Infinite loop in `HTML.read`: `MarkdownParser().html(from: "<p")` never returns (unterminated tag; `try? readHTMLElement()` fails without advancing, `continue`s forever). Also "<P", "x<P61". 0.6.0: still hangs. bugs/html-unterminated-hang/min.swift.
3. Exponential time on repeated "[": `"x" + 22 * "["` takes minutes (14 is instant). 0.5.0 only checked. bugs/nested-brackets-hang/min.swift.
4. Nondeterministic output: with a `.metadataKeys` modifier that maps several keys to the same string (e.g. returns ""), `Metadata.applyingModifiers` mutates the dictionary while iterating, so which value wins depends on per-process hash order. `parse(x) == parse(x)` fails in-process and across runs. (0.5.0; code unchanged in 0.6.0 as far as I read.)

## Harness notes
- Harness guards (`guardKnown`) bail on inputs that trigger bugs 1 and 2, and the metadataKeys collision modifiers are replaced; the guards are heuristic and leak (campaign still hit 1-3).
- Harness bugs fixed: metadata value check was wrong (continuation lines get appended with a space) -> keys only; `"".contains("")` is false in Swift, so skip empty keys.
- A mismatching `Str<..., Plain>` raw stub (`parse_plain`) finds the hangs faster than the structured decoder.
