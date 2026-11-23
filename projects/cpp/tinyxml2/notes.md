# tinyxml2 (leethomason/tinyxml2 @ 7.0.0, 397f225)

75 endpoints in `doc/`, `tree/`, `attr/`, `print/`, `check/`. Object types: `Doc` (an owning
`XMLDocument*`), `Node` (any `XMLNode*`: element, text, comment, declaration, unknown, or the
document itself via `doc_node`) and `Printer` (an `XMLPrinter*`). Data types: `XmlText` (decoder:
well-formed documents with entities, CDATA, comments, PIs, optional declaration/DOCTYPE; plus 4
seeds and a dict) and `XmlName` (decoder: valid names, some with a `prefix:`).

Stubs: `new_doc`, `parse`, `parse_len`, `parse_bytes`, `load_file`, `parse_into`, `clear_doc`,
`delete_doc`, `doc_node`, `root_element`; `new_element/text/comment/declaration/unknown`,
`insert_end/first/after`, `delete_child`, `delete_children`, `set_value`, `deep_clone`,
`shallow_clone`, navigation (`first_child`, `next_sibling_element`, `child_by_existing_name`, ...);
`set_attr_*` for str/int/uint/int64/bool/double/float, `delete_attr`, `query_existing_attr`,
`query_attr_any`, `set_text_*`, `query_text`, `typed_defaults`, `set_cdata`; printing
(`roundtrip_doc`, `roundtrip_node`, `parse_print_stable`, `clone_doc`, `save_load`, the manual
`XMLPrinter` steps and `printer_script`); `check_tree` (parent/prev/next/last invariants and a
visitor count), `check_error_state`, `check_root`.

Oracles: typed Set then Query returns the same value, `GetText`/`Attribute` return what was
stored, `Insert*` return the node and place it, `DeepClone` is `ShallowEqual` and prints the same,
print -> parse -> print is a fixed point (also through `SaveFile`/`LoadFile`), manual printer output
parses, tree links are consistent.

## Library bugs (all still present at 11.0.0, the latest release)

1. **`XMLNode::InsertAfterChild(a, b)` crashes when `b` is `a`'s next sibling and the last child**
   (SEGV, null write at tinyxml2.cpp:931 in 7.0.0, :1015 in 11.0.0). `InsertChildPreamble` unlinks
   `b`, leaving `a->_next == nullptr`; the next line writes `afterThis->_next->_prev`. The
   `afterThis->_next == 0` shortcut is checked *before* the unlink and does not cover this case.
   Bucket: `insert_after`. Reproducer: `bugs/insert-after-last-child/` (`./run.sh`), 13-line
   standalone file in `bugs/insert-after-last-child/standalone/min.cpp`.
2. **Float serialization is not exact**: `SetAttribute(name, float)`, `SetText(float)` and
   `XMLPrinter::PushAttribute/PushText(float)` use `%.8g`; a float needs 9 digits. `-109.729324f` is
   stored as `"-109.72932"` and queries back as `-109.729317`. Doubles (`%.17g`) are fine. Bucket
   "float attribute round-trips exactly" (harness now only checks 2e-6 relative error).
3. **`QueryIntAttribute` / `QueryUnsignedAttribute` / `QueryIntText` accept out-of-range numbers**
   (`sscanf %d`): `v="8536928700102491035"` returns `XML_SUCCESS` with the value 309476251. Only the
   int vs int64 consistency check (limited to values that fit) found this; the harness no longer
   treats it as a bug.
4. **`Parse` accepts a stray closing tag**: `Parse("</a>")` and `Parse("text</c>junk")` return
   `XML_SUCCESS` with an empty / text-only document; `Parse("hello")` is an error. Printing that text
   node gives output that does not parse (`XML_ERROR_PARSING_TEXT`).
5. **Empty text node prints `<f></f>`, which reparses as `<f/>`** (`SetText("")`), so print -> parse
   -> print is not a fixed point. Harness bails on empty text nodes.
6. **Pretty printing mixed content is not idempotent in 7.0.0** (`<o>x<a/>y</o>` gains 4 spaces of
   indentation in the text on every print -> parse round trip). Fixed in 11.0.0 (not in the table
   of still-open bugs).

## Harness bugs found and fixed through triage

- `set_value` on the document node: `Value()` is NULL (strcmp SEGV). Now bails.
- `XMLPrinter::OpenElement` keeps the name pointer until `CloseElement`; passing a fuzzer-owned
  string gave a heap-use-after-free. Names are kept alive in a pool.
- Oracles compared output of documents with different settings: the reparsed/cloned document must
  use the same `processEntities`/whitespace mode (`XMLPrinter` takes the setting from the
  document). With `processEntities=false` values are printed raw, so quotes/`<` break the output
  (documented); roundtrip oracles require `processEntities=true`.
- Invalid XML that the harness itself built: empty/odd names (names are validated), duplicate
  attributes (`printer_script` uses distinct names), CDATA containing `]]>`, comments containing
  `--`, unknown/declaration nodes with `>`/`?>` or placed inside elements or after the root, text
  with CR (parser normalizes CR/LF), top-level text.
- Cross-type "int and double agree" checks: `sscanf` accepts `"3.5hello"` as int 3 and double 3.5.
- `Source::token()` returns arbitrary bytes, not dictionary entries: names are built with explicit
  lists instead.

## Stale node handling

Nodes belong to their document. `Clear`, `Parse`, `DeleteChild(ren)` free nodes, so each document has
an attribute `tok` (from a program-wide counter) and nodes carry the `tok` of the document state they
were made in. Freeing calls draw a new `tok`, so every older `Node` bails with "stale node". Deleting
a document (`delete_doc` takes it) kills its nodes through `get_obj`-style checks (`GetDocument()` pointer
compare + `tok`).

## Campaign notes

- Three 600 s runs of one campaign (`main`, resumed, `--jobs 1`), 157k + 361k + 441k execs, 2.5-2.6k of
  10.3k edges; library line coverage 82.5% (`stitch cov`, 2230 programs), `tinyxml2.cpp` 87%.
  Never-called functions are the `SetBoolSerialization`/`Int64Attribute`-style helpers (added
  `typed_defaults`, `set_text_uint` afterwards).
- About a third of calls bail (stale nodes after a freeing call, navigation with no sibling, `parse_bytes`
  on random bytes: 97% parse errors but it is the only way to hit weird parse paths).
- Manual printer steps (`printer_attr`, `printer_text`...) mostly bail because the step they need must be the
  immediately preceding call on that printer; `printer_script` drives a whole balanced session from one
  `Src` and does the useful work.
- `stitch bisect` on bug 1 finds no good revision: it is present in every tag.
