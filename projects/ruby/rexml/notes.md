# rexml (ruby/rexml @ v3.2.5)

20 stubs: `parse`, `new_doc`, `roundtrip`, `pretty`, `root`, `xpath`, `xpath_first`,
`add_element`, `add_attribute`, `add_text`, `set_text`, `cdata`, `comment`, `delete_child`,
`children`, `deep_clone`, `text_of`, `pull` (pull parser), `stream` (stream listener), `sax2`.
Objects: `Doc`, `Elem`. Decoders: `XmlText` (XML declarations, DOCTYPEs with entities that refer to
each other, ATTLIST, elements, attributes, entity and character references, CDATA, comments,
PIs) and `XPathExpr` (paths, axes, predicates, functions). Oracles: a written document parses and
writes the same text again; XPath results belong to the document; attributes and text read back;
`deep_clone` writes the same; the stream parser ends every tag it starts.

Campaign: 15 min, 2 workers: 1.2M execs (1.4k/s), 1.2k edges, 51 buckets.

## Library bugs

Checked against v3.4.4 (the latest release) with `stitch exec <id> --rev v3.4.4`: 24 buckets still
reproduce, 27 were fixed since v3.2.5.

Still present in v3.4.4:
1. **A recursive entity overflows the stack when text is read.** With
   `<!ENTITY e0 "&e0;&e0;">`, `Element#text` and `Text#value` recurse through `Entity#value` until
   `SystemStackError` (c36). The parsers reject this recursion since 3.3.3; the DOM accessors do
   not. Reproducer: `bugs/entity-recursion-text/`.
2. **An out-of-range character reference in text raises `RangeError`.** `elem.text = "&#1111111111;"`
   fails in `Text.expand` with "pack(U): value out of range" instead of REXML's "Illegal character"
   error (c46). Reproducer: `bugs/text-charref-rangeerror/`.
3. **`deep_clone` fails on text added through the API.** `add_text(".>b<")` is accepted (and written
   escaped), but cloning the element revalidates it as raw text: `RuntimeError: Illegal character
   "<" in raw string` (c31).
4. **A malformed DOCTYPE crashes the stream parser.** An entity value containing `<` followed by
   `]>` and more text makes `BaseParser#pull_event` call `[]` on nil (c15).
5. **Malformed XPath expressions raise internal errors** instead of `REXML::ParseException` (16
   buckets): `NoMethodError` for `/or/*/-last("`, `TypeError` for `..|9`, `ArgumentError` for
   `contains(.a'a')`, in `XPathParser#Predicate`, `#FunctionCall`, `#PrimaryExpr` and
   `REXML::Functions`. Reproducer: `bugs/xpath-nomethoderror/`.

Fixed since v3.2.5 (first release without the bucket, checked at each security release):
- v3.2.7 (9 buckets): `ArgumentError` and `NoMethodError` in the pull, stream and SAX2 parsers on
  malformed input; text that `write` produces but `parse` rejects.
- v3.3.2 (4) and v3.3.3 (9): more parser crashes, and **stack overflows from self-referencing
  entities in the pull and SAX2 parsers** (`<!ENTITY e0 "&e0;&e0;">`, c18, c19), fixed in the
  release that fixed entity expansion in those parsers (CVE-2024-41946).
- v3.3.6 (1): a stack overflow in `Document.new` from a self-referencing parameter entity (c41).
- v3.4.2 (1), v3.4.3 / v3.4.4 (3): an XPath `RuntimeError`, a `write` crash on an entity
  declaration, and two more XPath errors.

No timeout buckets: the decoder did not produce the long runs of `<` or whitespace behind the
2024 denial-of-service CVEs.

## Harness bugs found and fixed

- **XPath results include the Document** (`/` returns it; a `Document` is an `Element`), and adding
  an element to it raised "attempted adding second root element". Documents are filtered out.
- **REXML reports characters XML does not allow with `RuntimeError`** ("Illegal character ...").
  `add_attribute`, `add_text` and `set_text` declare it as a bail.
- **Read-back checks ignored normalization**: attribute values and text with `&` (entity
  references), `\r` or invalid UTF-8 do not read back verbatim (c20, c22). The checks skip them.
- **Prefixed names without a namespace declaration**: `new_doc("x:y")` writes `<x:y/>`, which REXML
  then refuses to read (c25). Elements built through the API use unprefixed names.
