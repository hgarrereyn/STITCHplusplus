# marked (markedjs/marked @ v4.0.9)

21 stubs. Producers: `parse`, `parse_inline`, `parse_repeated` (html strings), `lex`, `lex_static`
(block tokens, object `Tokens`), `lex_inline` (`InlineTokens`). Consumers: `parser`,
`parser_static`, `parser_with_renderer` (custom `Renderer.strong`), `parser_inline`, `walk_tokens`,
`walk_rewrite_render` (walkTokens that edits text tokens), `custom_renderer` (heading / link+image+code
returning false / paragraph+html+text), `slugger`. Oracles (`check_*`): `parse == parser(lexer)`,
lexer determinism, lexer `raw` fields concatenate to the input (inline and block), silent mode returns
a string, `parseInline == Parser.parseInline(Lexer.lexInline)`, `parser(tokens)` twice is the same.
Options are a `Flags` bitmask (gfm, breaks, pedantic, smartLists, smartypants, headerIds, mangle).
Data type `MarkdownText` (dict of markdown punctuation and control chars, 4 seeds, no decoder).

Campaigns (jobs 1, 10 min, load average 10-25 on the machine): `main` 7.3k execs (10-14/s),
`two` 17k execs (27/s, after adding control chars to the dict), 660 edges, 81.8% line coverage of
`lib/marked.cjs` (`stitch cov two`; never called: `use`, `setOptions`, `resolveUrl`, `changeDefaults`).

## Library bugs

1. **`Parser.parse` mutates the token tree** (`bugs/parser-not-idempotent/`, verified repro).
   `marked.parser(toks)` called twice on a loose task list (`- [ ] a\n\n- [x] b\n`) renders the
   checkbox twice the second time, because the parser unshifts `{type:"text", text: checkbox}` into
   `item.tokens`. Fixed at v18.2.1 (bisect: first fixed commit 7b192315b286 "only create tokens
   inside tokenizers", #3755; bisect verdicts for the mid versions were non-monotone, see friction).
2. **Lexer `TypeError: Cannot read properties of undefined (reading 'trim')`** in `splitCells`
   for a table followed by a line holding only `\x0b` (`marked.lexer("| a |\n|---|\n\x0b")`,
   `bugs/table-ctrl/min.js`). 45 hits in campaign `two`. Not reproducible at v18.2.1.
3. **Superlinear (roughly cubic) inline emphasis parsing**: `"*a **b _".repeat(2000)` (16 KB) takes
   4.7 s in `parseInline` (1000 repeats 0.6 s); `"*a ".repeat(2000)` 0.6 s. Buckets `timeout in
   parse_repeated` in both campaigns; the replay of a timeout bucket does not always reproduce under
   load. Not checked at v18.2.1 (the timeout bucket was not minimized to a deterministic repro).

Quirks that are not bugs but cost me a few buckets (harness handles them):
- `marked.lexer/parser(x, opts)` do not merge `opts` with the defaults, unlike `marked.parse`;
  `marked.parser(marked.lexer(md,{headerIds:true}), {headerIds:true})` yields `id="undefinedt"` and
  `class="undefinedjs"` (`headerPrefix`, `langPrefix` undefined). Harness starts from `marked.getDefaults()`.
- The lexer drops link reference definitions (no token, kept in `tokens.links`), list `raw` is
  trimmed plus `\n`, and table cells have no `type`/`raw`, so the raw-concatenation oracle skips
  inputs with definitions, requires exact equality only without lists, and otherwise compares modulo whitespace.
- `mangle` (default true) uses `Math.random`, so every determinism oracle turns it off.

## Harness notes

- Dockerfile: the init template's `npm install --omit=dev` fails (ERESOLVE on dev peer deps) and
  is unnecessary (no runtime deps; v4 commits `lib/`); removed. For `--rev` at newer tags it builds
  `lib/` if missing and copies `lib/marked.umd.js` to `lib/marked.cjs`, because marked >= v5 is
  ESM-only and Node 22.11 cannot `require()` it; `stubs.js` falls back to that path.
- Input split by object type (`Tokens` vs `InlineTokens`): feeding inline tokens to `Parser.parse`
  throws, which `stitch probe` showed as `ok 4% after lex_inline vs 100% after lex_static`.
- Parser stubs `structuredClone` the tokens (keeping `.links`) so bug 1 only shows in its own oracle.
