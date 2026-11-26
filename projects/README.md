# Example projects

STITCH++ harnesses for real libraries, three per language. Each library is pinned to a recent
upstream commit, and each harness exercises most of the library's public API. Every project is a
`stitch.toml` (library source and commit, build settings; pinned to STITCH++ 0.2.0), a
`Dockerfile` that builds the library, and a `harness/` of endpoints (plus seeds in
`harness/data/`).

To run one:

```sh
cd projects/python/tomlkit
stitch build
stitch fuzz
```

`stitch build --rev <commit>` builds a project against another version of the library.

| Language | Project | Library at | Endpoints | What the harness exercises |
|---|---|---|---|---|
| C/C++ | [cjson](cpp/cjson) | cJSON master (1.7.19+) | 67 | every parse variant, print/parse round trips, tree building and editing, JSON Pointer, Patch and Merge Patch |
| | [tinyxml2](cpp/tinyxml2) | tinyxml2 master (11.0.0+) | 108 | parsing from memory and files, DOM navigation and editing, handles, typed attributes, XMLPrinter, deep copy, XMLUtil |
| | [libxml2](cpp/libxml2) | libxml2 master (2.16 dev) | 307 | XML/HTML parsing through every entry point (push, reader, SAX2), tree editing, serializers and c14n, XPath, XInclude, DTD/RelaxNG/XSD/Schematron, xmlTextWriter, URIs |
| Python | [tomlkit](python/tomlkit) | tomlkit 0.15.3 | 76 | parse/edit/print of documents, tables, arrays, arrays of tables, items and keys; text-preserving round trips |
| | [dateutil](python/dateutil) | python-dateutil master (4.0.0+) | 50 | `parse` and `isoparse`, relativedelta arithmetic, rrule/rruleset/rrulestr, every tz class including fuzzed TZif and iCal input |
| | [jinja2](python/jinja2) | Jinja2 main (3.2.0.dev) | 28 | templates rendered sync, async, streamed, native, sandboxed and i18n, all loaders and bytecode caches, every filter and test |
| JavaScript | [lodash](javascript/lodash) | lodash main (4.18.1+) | 51 | nearly all of lodash 4: deep paths and merges (prototype pollution), array/set ops, lazy chains, type checks, clone/isEqual, strings and `_.template` |
| | [semver](javascript/semver) | semver 7.8.6 | 23 | versions, comparators and ranges: parse/coerce/inc, compare and sort, satisfies, minVersion, subset, intersects, simplifyRange |
| | [handlebars](javascript/handlebars) | handlebars 4.7.10 | 12 | compile/precompile/render with helpers, partials and decorators, parser, visitors and compilers; code-injection canary |
| Java | [jsoup](java/jsoup) | jsoup master (1.24.1+) | 69 | HTML/XML parsing (strings, bytes, streaming), DOM traversal and editing, CSS/XPath selection, entities, W3C DOM, `Cleaner` and safelists |
| | [snakeyaml](java/snakeyaml) | SnakeYAML master (2.7+) | 86 | load/compose/parse/scan with Loader and Dumper options, the emitter and serializer, constructors, JavaBeans, comments |
| | [commons-compress](java/commons-compress) | Commons Compress master (1.29 dev) | 66 | every archive format and compressor, read and write, ZIP extra fields, random-access readers, metadata round trips, path-traversal checks |
| Go | [yaml](go/yaml) | go-yaml v4 main (yaml/go-yaml) | 26 | `Unmarshal`/`Marshal`, `Load`/`Dump`, Loader/Dumper with every option and plugin, the node API; round trips |
| | [gjson](go/gjson) | gjson 1.20.7 | 18 | `Get`/`Parse`/`GetMany`, path syntax (wildcards, queries, modifiers), `Result` accessors, `Valid`, `Escape` |
| | [expr](go/expr) | expr master (1.18.1+) | 14 | compile and run expressions against typed and map environments with every compile option, `Eval`, the printer |
| Rust | [smallvec](rust/smallvec) | smallvec 1.16.6 | 35 | every vector operation against a `Vec` model, iterators, `drain_filter`, bincode decoding; ASan |
| | [url](rust/url) | url 2.5.9 | 17 | WHATWG parsing and joining, every setter, path/query editing, `make_relative`, form encoding; `check_invariants()` |
| | [regex](rust/regex) | regex master (1.13.2+) | 32 | regex, regex-syntax and regex-automata: matching APIs and every engine cross-checked, AST/HIR round trips, DFA serialization |
| Ruby | [rexml](ruby/rexml) | rexml master (3.4.4+) | 63 | DOM, pull, stream and SAX2 parsing, writing and formatters, XPath, tree edits, DOCTYPE and entities, RELAX NG |
| | [addressable](ruby/addressable) | addressable 2.10.0 | 40 | URI parsing, every component setter, normalize/join/route/omit, encoding helpers, IDNA, RFC 6570 templates |
| | [mail](ruby/mail) | mail 2.9.1 | 41 | parsing, building and encoding messages, header fields, parts and attachments, delivery-status reports, encodings |
| Swift | [swiftsoup](swift/swiftsoup) | SwiftSoup 2.14.4 | 55 | HTML/XML parsing, CSS selectors, tree edits, cleaner and whitelists, output settings, entities |
| | [yams](swift/yams) | Yams main (6.2.2+) | 23 | load/compose, serialize/dump with every emitter option, Node editing, Codable |
| | [swift-syntax](swift/swift-syntax) | swift-syntax main | 23 | parser and incremental reparse, operator folding, formatting, diagnostics and fix-its, syntax builder, rewriters, SwiftIfConfig, lexical lookup, refactoring |
