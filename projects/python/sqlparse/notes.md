# sqlparse (0.4.3)

Latest release checked: 0.6.1.

## Stubs (35)

- Producers: `parse_one`, `parse_last`, `parsestream_one`, `get_sublist`, `get_token`, `tok_next`, `tok_first` (objects: `Stmt`, `Tok`; data type `SqlText` with seeds, `sql.dict` and a decoder).
- Whole-text oracles: `parse_check_lossless` (str(parse(x)) == x modulo trailing whitespace), `split_check` (pieces join back modulo whitespace), `split_vs_parse`, `split_idempotent`.
- `format` options: `fmt_case`, `fmt_strip_comments`, `fmt_strip_whitespace`, `fmt_reindent` (indent_width, wrap_after, comma_first, ...), `fmt_reindent_aligned`, `fmt_misc` (output_format, truncate_strings), `fmt_all`, `fmt_bad_options` (error path: always SQLParseError, bails), idempotence checks `fmt_reindent_idem`, `fmt_case_idem`.
- Token tree: `stmt_get_type`, `stmt_roundtrip`, `tree_consistency` (parent links), `stmt_flatten_types`, `tok_prev`, `tok_index_lookup`, `tok_matching`, `ident_names`, `ident_deep` (Identifier/Where/Comparison/Function/Case), `tok_methods`, `tok_edit` (insert whitespace tokens), `tok_group_ops`, `stmt_str_eq_after_format`.

## Campaigns (1 worker)

probe (60 s), main (600 s, 57k execs, 1935 edges), run2 (600 s, 37k execs), run3 (600 s, 84k execs, 1944 edges). Coverage (run2, `stitch cov`): never called: `get_token_at_offset`, `has_alias`, `is_child_of`, `Comment.is_multiline`, `RightMarginFilter` (right_margin option), `group_tzcasts`, `OutputFilter._process`, CLI. No stubs for these yet.

## Library findings

- `format(strip_whitespace=True)` on `"(AS)"` raises IndexError in `StripWhitespaceFilter._stripws_parenthesis` (others.py:90, `tlist.tokens[1]` on a parenthesis group with one token). Fixed in 0.6.1 (`stitch bisect --fixed`: first fixed commit 3512a9d86912 "Fix formatter crashes on malformed grouped input"). Repro: `bugs/strip-whitespace-empty-paren/`.
- `format(reindent=True)` is not idempotent: `"create table foo (id int primary key, name varchar(10)); drop table foo;"` gains an extra blank line between statements on each pass. Still present in 0.6.1.
- `format(strip_whitespace=True)` is not idempotent: `"TEZK_6G }n;jS`TC0#   , X"` -> `...# , X` -> `...#, X`. Still present in 0.6.1.
- `format(keyword_case=...)` is not idempotent when text contains `;` followed by whitespace (statement re-splitting strips whitespace): `"; $$; $$"`. Fixed (no crash) in 0.6.1. Probably by design, harness now removes `;`.
- Trailing whitespace-only text is dropped by `parse` (`str(parse(" ")) != " "`): documented behavior; oracle is rstripped.

## Harness notes

- `split(strip_semicolon=)` and `format(..., truncate_strings=1)` do not exist / are invalid in 0.4.3 (truncate_strings needs > 1, indent_width >= 1).
- `token_prev(idx)` / `token_next(idx)` require a valid index: pick it with `Src.choice(len(tokens))`.
- `tok_edit` inserts only whitespace tokens, otherwise `str(tree)`-based oracles after editing are meaningless; the reparse oracle strips outer whitespace.
- `utils.remove_quotes("")` raises IndexError but is an internal helper, so it is not harnessed.
