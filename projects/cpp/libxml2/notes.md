# libxml2 v2.11.0 (large project)

GNOME libxml2, built from source with CMake as a static library (no Python, zlib, LZMA, ICU, HTTP,
FTP or threads) and ASan. 27 stubs over documents (`Doc`, freed with `xmlFreeDoc`) and element
views (`Node`, never freed: unlinked subtrees leak by design so every handle stays valid).

## Harness

- **Parsing.** `parse` (xmlReadMemory with 13 fuzzed options, never DTD loading or XInclude from
  files), `parse_html`, `push_parse` (chunked push parser vs one-shot parser: same verdict, same
  document), `reader` (xmlTextReader walk with subtree expansion). An external-entity loader that
  refuses everything keeps the fuzzer off the file system and network.
- **Tree editing.** `root`/`child`/`parent`/`xpath` produce element views; `new_child`, `set_prop`,
  `unset_prop`, `add_content`, `rename_node`, `move_node` (same document, never into its own
  subtree), `unlink_node`, `copy_doc` (+ XInclude on the copy), `inspect`.
- **Oracles.** A document parsed without RECOVER serializes to XML that parses, and serialization is
  a fixed point after one reparse; `xmlGetNoNsProp` reads back what `xmlSetProp` wrote; a saved URI
  parses and saves the same; `unescape(escape(s)) == s`; libxml2's UTF-8 helpers agree with a
  strict decoder; C14N and DTD validation must not crash.
- **Data types.** XmlText (prolog, internal DTD with entities / attribute defaults / content models,
  namespaces, CDATA, references), HtmlText, XPathExpr, UriText, RegexText, XmlName, seeded from
  libxml2's own `test/` corpus and its fuzz dictionaries.

## Harness fixes during probing and triage

- **Text with non-XML characters.** Mutation stubs first bailed on most inputs; they now keep only
  XML characters, using a strict UTF-8 decoder of the harness (libxml2 2.11's own `xmlGetUTF8Char`
  accepted continuation bytes as lead bytes, which was itself a finding).
- **Serialization fixed point.** Trees built in memory hold things parsing never produces (empty
  text nodes), so the oracle compares the first and second reparse, not the in-memory tree.
- **C string truncation.** Inputs with NUL bytes (URIs, push parser) bail.
- **xmlGetProp ignores namespaces.** It returned `xml:space` for `space`: the oracle uses
  `xmlGetNoNsProp`.

## Results

8 crash buckets in 30 minutes (2 workers). Checked at v2.15.4: 4 fixed upstream (push parser
encoding verdict, UTF-8 decoding, regexp compile blowup, a serializer that wrote unparsable XML),
2 URI round-trip bugs still reproduce, 1 slow XPath by design, 1 harness bug (fixed).
