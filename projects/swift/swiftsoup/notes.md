# SwiftSoup (scinfu/SwiftSoup @ 2.3.2)

A Swift port of jsoup (see `projects/java/jsoup`): 19 stubs: `parse`, `parse_fragment`,
`roundtrip`, `clean` (the 5 safelists, `preserveRelativeLinks`), `select`, `body`, `child`,
`text` (text, outerHtml, ownText, cssSelector), `set_attr`, `remove_attr`, `append`, `prepend`,
`wrap`, `unwrap`, `remove`, `set_text`, `empty`, `add_class`, `tag_name`. Objects: `Doc`, `Elem`.
Decoders: `Html` and `Selector` (the jsoup harness's vocabulary). A thrown `SwiftSoup.Exception` is
a bail; traps are bugs. Oracles: `parse(html())` is stable, `clean()` output has no script element,
event handler or `javascript:` URL and is stable when cleaned again, selected elements belong to
the document, a set attribute is present, `empty()` leaves no children.

Campaign: 15 min, 2 workers: 813k execs (845/s), 4.6k edges, 14 buckets.

## Library bugs

Checked against 2.14.4 (the latest release): all the crashes and the hang are fixed there.

Fixed between 2.3.2 and 2.14.4 (the same classes as jsoup's findings):
1. **`Element.wrap` force-unwraps nil** when the wrapper HTML has no element (c2; jsoup throws
   `IndexOutOfBoundsException` for the same call). Reproducer: `bugs/wrap-force-unwrap/`.
2. **`select` traps on malformed selectors**: "Range requires lowerBound <= upperBound" in
   `TokenQueue.consumeTo` from `QueryParser.consumeIndex` and `cssNthChild` (c8, c13; jsoup:
   `StringIndexOutOfBoundsException`). Also in `cssSelector()` (c12). Reproducer:
   `bugs/select-range-trap/`.
3. **`remove` and `unwrap` index out of range** in `Node.removeChild` after the parent's children
   changed (c1, c9, c10; jsoup: `IndexOutOfBoundsException` after `empty()`).
4. **`parse` hangs** on some malformed markup (timeout buckets c7, c11, c14). Reproducer:
   `bugs/parse-hang/` (stopped after 10 s).

Questionable, still at 2.14.4: `clean()` is not idempotent for some input (c4); a document's
`html()` is not stable after two parses when text follows `</head>` (c6); `attr()` with a key
like `<svg ` sets nothing (c3).

## Harness bugs found and fixed

- **The XSS oracle looked at the whole output string** (c5): cleaned *text* that mentions
  `javascript:` is harmless. It now parses the output and checks elements and attributes only.
