# addressable (sporkmonger/addressable @ addressable-2.7.0)

16 stubs: `parse`, `heuristic_parse`, `roundtrip`, `normalize`, `join`, `route`, `set_part`,
`query_values`, `omit`, `display`, `encode_component`, `form`, `template`, `expand_extract`,
`extract`, `partial_expand`. Objects: `Uri`, `Tmpl`. Decoders: `UriText` (schemes, userinfo, IP
literals, IDNA hosts, ports, dot segments, percent escapes, queries, fragments) and `TemplateText`
(RFC 6570 expressions with every operator and modifier). Oracles: `parse(to_s) == uri`,
`normalize` is idempotent and a normalized URI parses back to itself, `base.join(base.route_to(u))
== u`, a part set reads back after `to_s`/`parse`, `query_values=` reads back,
`unencode(encode(s)) == s`, form encode/decode round trip, a template matches its own expansion.

Campaign: 15 min, 2 workers: 1.1M execs (1.3k/s), 700 edges, 12 buckets.

## Library bugs

Checked against addressable-2.9.2 (the latest release): 8 buckets still reproduce, 4 were fixed.

Still present in 2.9.2:
1. **`Template#partial_expand` (and matching) raise `RegexpError`.** The template's literal text is
   put into a regular expression without escaping, so a template containing `\5` (or other regex
   syntax) makes "numbered backref/call is not allowed" (c2). Reproducer:
   `bugs/template-regexperror/`.
2. **`normalize` raises `ArgumentError` ("invalid byte sequence in UTF-8")** on a host with a
   percent-escape that decodes to invalid UTF-8, e.g. `https://localhost%41...%FF` from
   `heuristic_parse` (c1). Reproducer: `bugs/normalize-invalid-utf8/`.
3. **`normalize` is not idempotent.** `ftp://aW@example.com%41` normalizes to
   `ftp://aW@example.comA/`, which normalizes to something else again (c6). Reproducer:
   `bugs/normalize-idempotent/`.
4. **A normalized URI does not parse back to itself** for authorities like `file:///:%C3%25B:@...`
   (c12).

Questionable (the library's behavior may be intended): a path set with a `?` in it reads back
differently (c3), malformed nested templates do not match their own expansion (c4), `route_to`
between `mailto:` URIs that differ only in the path returns `""` (c5), and `==` is false for a
URI and its reparsed copy with a host of `.` (c7).

Fixed since 2.7.0: four crashes of `display_uri` (`ArgumentError`, `RangeError`,
`Addressable::IDNA::PunycodeBigOutput`) on hosts whose IDNA conversion fails (c8-c11); 2.8.0
replaced the IDNA implementation.

The campaign did not find CVE-2021-32740 (slow template matching): `expand_extract` matches short
expansions only (values of at most 8 bytes).

## Harness bugs found and fixed

- `encode_component` asked for a `SEGMENT` character class, which 2.7.0 does not have.
- `form` built pairs from `"".split("=")` (an empty array) and passed `nil` keys.
