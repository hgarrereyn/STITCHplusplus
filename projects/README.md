# Example projects

STITCH++ harnesses for real libraries: three per language, each pinned to an old release of the
library with known bugs so the fuzzer has something to find. Every project is a `stitch.toml`
(library source, version, build settings; pinned to STITCH++ 0.2.0), a `Dockerfile` that builds
the library, and a `harness/` of endpoints (plus seeds in `harness/data/`).

To run one:

```sh
cd projects/python/tomlkit
stitch build
stitch fuzz
```

| Language | Project | Library version | Endpoints | What the harness exercises |
|---|---|---|---|---|
| C/C++ | [cjson](cpp/cjson) | cJSON c859b25 | 65 | parse/print, tree editing, JSON Pointer and Patch (cJSON_Utils) |
| | [tinyxml2](cpp/tinyxml2) | tinyxml2 7.0.0 | 75 | parse/print, DOM editing, attributes and typed queries |
| | [libxml2](cpp/libxml2) | libxml2 v2.11.0 | 253 | XML/HTML parsing, push parser, reader/writer, tree editing, XPath, URIs |
| Python | [tomlkit](python/tomlkit) | tomlkit 0.13.2 | 16 | parse/dump round trips, editing tables, arrays and comments |
| | [dateutil](python/dateutil) | python-dateutil 2.9.0 | 17 | date parsing, `isoparse`, time zones, `rrule` |
| | [jinja2](python/jinja2) | Jinja2 3.0.0 | 9 | template compile and render, sync vs async rendering, native types, AST analysis |
| JavaScript | [lodash](javascript/lodash) | lodash 4.17.15 | 13 | path helpers (`set`, `get`, `unset`, `zipObjectDeep`), cloning; prototype pollution |
| | [semver](javascript/semver) | semver 7.5.1 | 11 | version and range parsing, comparison, range simplification |
| | [handlebars](javascript/handlebars) | handlebars 4.7.6 | 6 | compile and render (checked against `precompile`), partials, parse/print |
| Java | [jsoup](java/jsoup) | jsoup 1.15.2 | 11 | HTML parsing, selectors, DOM editing, `Cleaner` |
| | [snakeyaml](java/snakeyaml) | SnakeYAML 1.30 | 54 | load/dump round trips, dumper options, events and nodes |
| | [commons-compress](java/commons-compress) | Commons Compress 1.20 | 24 | archive and compressor readers and writers (zip, tar, cpio, 7z, lz4, ...) |
| Go | [yaml](go/yaml) | go-yaml v3 (496545a) | 10 | `Unmarshal`/`Marshal` round trips, node API |
| | [gjson](go/gjson) | gjson 1.6.4 | 7 | `Valid`, `Get`, `Parse` and path queries |
| | [expr](go/expr) | expr v1.15.0 | 6 | compile and run expressions against typed environments, printer |
| Rust | [smallvec](rust/smallvec) | smallvec 1.6.0 | 11 | vector operations (`insert_many`, `drain`, capacity changes, ...) under ASan |
| | [url](rust/url) | url 2.2.0 | 7 | parsing, setters, serialization round trips |
| | [regex](rust/regex) | regex 1.5.4 | 7 | compile, match, captures, engine agreement |
| Ruby | [rexml](ruby/rexml) | rexml 3.2.5 | 20 | DOM and stream parsing, entities, XPath, writing |
| | [addressable](ruby/addressable) | addressable 2.7.0 | 16 | URI parsing and normalization, URI templates, IDNA |
| | [mail](ruby/mail) | mail 2.7.1 | 9 | message parsing, headers, encodings, multipart |
| Swift | [swiftsoup](swift/swiftsoup) | SwiftSoup 2.3.2 | 19 | HTML parsing, selectors, DOM editing |
| | [yams](swift/yams) | Yams 4.0.6 | 10 | compose/load/dump round trips, serializer options, Codable, node editing |
| | [swift-syntax](swift/swift-syntax) | swift-syntax 509.0.0 | 6 | parse and print back, incremental reparse, formatting, operator folding, diagnostics |

The larger harnesses (libxml2, jinja2, handlebars, commons-compress, expr, regex, mail,
swift-syntax) take longer to build; the others build in a minute or two.
