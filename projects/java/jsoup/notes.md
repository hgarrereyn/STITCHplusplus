# jsoup (jhy/jsoup @ jsoup-1.15.2)

11 stubs: `parse`, `parseFragment`, `parseXml`, `body`, `child`, `select`, `edit` (`attr`, `html`,
`before`, `wrap`, ...), `structure` (`empty`, `unwrap`, `remove`; takes the element), `render`
(pretty, indent, outline), `clean` and `cleanDocument` (the Safelists `none` to `relaxed`, with
and without `preserveRelativeLinks`). Objects: `Document`, `Element`. Decoders: `Html` (tags from
a list that includes `script`, `iframe`, `svg`, `math`, `template`, `noscript`, with `href`,
`src`, `onclick`, `srcdoc`, `style` attributes) and `Css`. The `Html` dictionary
(`harness/data/Html/h.dict`) has XSS tokens, including `<a href="java\tscript:x">`. Oracles: `clean()` output
contains no script, event handler or `javascript:` URL; cleaning is idempotent; and `isValid()`
agrees with `clean()`.

Campaign: 15 min, 2 workers: 2.5M execs (2.8k/s), 6596 edges, 9 buckets.

## Library bugs

Checked against 1.21.1 (the latest release) with `stitch exec <id> --rev jsoup-1.21.1`.

Still present in 1.21.1:
1. **`select` throws `StringIndexOutOfBoundsException`** on a malformed attribute selector
   (`[C-60Nsj9v<a href&amp;dtable"...]`), in `Evaluator$AttributeKeyPair.<init>`. Invalid queries
   should raise `SelectorParseException`. Reproducer: `bugs/select-attribute-sioobe/`.
2. **`Element.wrap("</math>")` throws `IndexOutOfBoundsException`** (`Collections$EmptyList.get`
   in `Node.wrap`). A wrapper that parses to no element should be refused with an
   `IllegalArgumentException` instead.
3. **An attribute key made of control characters is accepted.** `attr("\x11\x02", v)` succeeds,
   then iterating the attributes (`Cleaner`) fails: "String must not be empty" (1.15.2), a
   `ValidationException` (1.21.1).

Fixed between 1.15.2 and 1.21.1:
- **CVE-2022-36033: XSS through `Cleaner` with `preserveRelativeLinks(true)`.**
  `clean("<a href=\"java\tscript:x\">", base, Safelist.basic().preserveRelativeLinks(true))` keeps
  the link. Browsers strip the tab, so it runs as `javascript:x`. Fixed in 1.15.3. The
  dictionary supplied the payload; the fuzzer found the Safelist and `preserveRelativeLinks`
  combination that lets it through. Reproducer: `bugs/cve-2022-36033/`.
- `IndexOutOfBoundsException` in `before`, `unwrap` and `remove` on an element whose parent was
  emptied with `empty()` (it kept a stale parent and sibling index), or whose siblings were
  replaced with `html()`.
- `clean()` was not idempotent for some inputs with control characters and broken entities.

## Harness notes

- `select` declares `Selector.SelectorParseException` as a bail (an `IllegalStateException` in
  1.15, not the `IllegalArgumentException` it was first declared as).
