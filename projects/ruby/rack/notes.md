# rack (rack/rack @ 2.2.3)

13 stubs over the request-parsing utilities: `parse_nested`, `nested_roundtrip`, `parse_query`,
`escape`, `unescape`, `byte_ranges`, `cookies`, `set_cookie`, `media_type`, `q_values`,
`multipart` (`Rack::Multipart.parse_multipart` on a mock request), `request_headers` (host, port,
ip, forwarded headers, Accept-Encoding), `clean_path`. Object: `Params`. Decoders: `QueryText`
(nested keys, separators), `HeaderText` (byte ranges, q-values, content types, long runs of
spaces) and `MultipartBody` (parts with names, filenames, `filename*`, content types). Oracles:
`parse(build(params)) == params`, `unescape(escape(s)) == s`, byte ranges lie inside the body,
`clean_path_info` removes `..`. `timeout_ms = 1000`, so slow parsing becomes a timeout bucket.

Campaign: 15 min, 2 workers: 2.4M execs (2.6k/s), 420 edges, 14 buckets.

## Library bugs

Checked against v2.2.26 (the latest 2.2 release): 13 buckets still reproduce. The main ones were
also run against v3.2.9 (the latest release), where the harness still loads.

Still present in v3.2.9:
1. **`Rack::MediaType.params` and `.type` raise `NoMethodError` on a `Content-Type` of `;` or ``**
   (`each_with_object` / `downcase!` for nil; c2, c9, c14). `Request#media_type_params` calls them
   with the request's header, so such a header turns into an exception in the application.
   Reproducer: `bugs/media-type-params/`.
2. **`Utils.best_q_match` raises `NoMethodError`** on an Accept header entry without a `/` (e.g.
   `,*oL42_?L38p+h::,multipart/`): `Mime.match?` splits nil (c4). Reproducer: `bugs/best-q-match/`.
3. **The multipart parser raises `NoMethodError` on a part with an empty `Content-Type:` header**
   (`strip!` for nil in `tag_multipart_encoding`; c5, c7, c12). Reproducer:
   `bugs/multipart-empty-content-type/`.

Still present in v2.2.26, fixed in 3.x:
- An unknown `charset` in a multipart part raises `ArgumentError` from `Encoding.find` (c10, c11).
- An empty forwarded host makes `Request#host` call `start_with?` on nil (c6).
- Invalid UTF-8 in a multipart field name raises `ArgumentError` in `normalize_params` (c13).

Fixed in v2.2.26: `MediaType.params` on another malformed header (c3).

By design: `parse_query` raises `ArgumentError` on an invalid `%`-escape (c1; `parse_nested_query`
raises `InvalidParameterError`), and keys containing `[` or `]` do not survive
`build_nested_query` (c8).

No timeout buckets: none of the slow-regex CVEs fixed in 2.2.x (byte ranges, media types,
multipart headers) showed up at 1 s; the decoders produce runs of up to 40 spaces, which is not
enough for those regexes.

## Harness bugs found and fixed

- `Rack::QueryParser::ParamsTooDeepError` does not exist in 2.2 (it is a `RangeError` there).
