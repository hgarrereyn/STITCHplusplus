# org.json (stleary/JSON-java @ 20230227)

12 stubs: `parseObject`, `parseArray`, `newObject`, edits (`put`, `remove`, `child`), JSON
pointers (`pointer`), `fromXml`/`toXml` (`XML`), `fromJsonML` (`JSONML`), `fromCookie` (`Cookie`)
and `cdl` (`CDL`). Objects: `JSONObject`, `JSONArray`. Decoders: `JsonText`, `XmlText`. Oracles: printed JSON
parses back equal, and `XML.toString` output parses as XML. `JSONException` is the documented
bail.

Campaign: 15 min, 2 workers: 1.6M execs (1.8k/s), 1735 edges, 3 buckets.

## Library bugs

1. **`JSONML.toJSONArray` throws `ClassCastException`** ("String cannot be cast to JSONArray",
   JSONML.java:257) on malformed markup such as `#QU>+v</x:y>`, instead of `JSONException`.
   Still in 20250517 (the latest release). Reproducer: `bugs/jsonml-classcast/`.
2. **`JSONML.toJSONArray` throws `NumberFormatException`** on a bad numeric character reference
   (`<a k="m">&#M;</a>`). Still in 20250517.
3. **`XML.toString` output that is not XML.** An object built from `Cookie.toJSONObject` has keys
   that `XML.toString` writes as element names without escaping. Not rechecked at the latest
   release.
