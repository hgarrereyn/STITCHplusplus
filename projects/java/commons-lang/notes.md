# commons-lang3 (apache/commons-lang @ rel/commons-lang-3.12.0)

14 stubs: `StrBuilder` (mirrored by a `java.lang.StringBuilder` model: `newBuilder`, `append`,
`insert`, `delete`, `deleteAll`, `replaceAll`, `setLength`, `trimReverse`, `search`),
`StringUtils.split`/`join` (`splitJoin`), `StringUtils.abbreviate`, `WordUtils.wrap` (`wrapping`),
and `NumberUtils` (`createNumber`, `isCreatable`, which also calls `isParsable`, `toInt`,
`toDouble`). Object:
`StrBuilder`. Decoder: `NumText` (signs, hex and octal prefixes, exponents, type suffixes).
Oracles: the builder matches its model after every edit; `isCreatable(s)` is true exactly when
`createNumber(s)` parses.

Campaign: 15 min, 2 workers: 3.6M execs (4.0k/s), 1104 edges, 4 buckets.

## Library bugs

Checked against 3.17.0 (the latest release):

1. **`createNumber` and `isCreatable` disagree** (still in 3.17.0). `createNumber("07f")` returns
   7.0 but `isCreatable("07f")` is false. `isCreatable("292233E2036854775807")` is true but
   `createNumber` throws ("Too many nonzero exponent digits"). Reproducer:
   `bugs/createnumber-iscreatable/`.
2. **`createNumber` throws `StringIndexOutOfBoundsException`** instead of `NumberFormatException`
   on some malformed input (`kE\vle3.51`). Fixed by 3.17.0.
3. **`StrBuilder.contains("")` is false on an empty builder** (`"".contains("")` is true). Fixed by
   3.17.0.

## Harness notes

- The model for `reverse` is character-wise (`StrBuilder` reverses UTF-16 units, unlike
  `StringBuilder`, which keeps surrogate pairs).
