# BurntSushi/toml v1.0.0

24 stubs. Producers: `decode` (into `map[string]interface{}`, object `Doc`), `decode_meta` (Doc + `Meta`
= `toml.MetaData`), `decode_prim` (`Prim` = `toml.Primitive` + Meta), `new_encoder` (`Enc` with Indent),
`lookup` (sub-table). Mutators: `doc_set_int/str/float/time/array/tables/mixed`, `doc_nest`.
Consumers: `decode_reader` (DecodeReader vs NewDecoder vs Decode agree), `decode_struct`,
`decode_struct_meta` (Keys/IsDefined/Type/Undecoded on a typed struct with time.Time, slices, nested
tables, TextUnmarshaler `Level`, Duration, pointer, map), `prim_decode` (PrimitiveDecode into 4 types),
`meta_keys`, `meta_undecoded`, `encode_doc`, `encode_typed`. Oracles: `roundtrip` (decode(encode(x)) == x
with NaN/time/`[]map` normalisation, Indent "" or tab or 2 spaces), `roundtrip_typed`, `encode_stable`
(encode(decode(encode(decode(x)))) text-equal), "encoder output decodes".
Data type `TomlText` (2 seeds + dict).

Campaign (main, 1 job, 600 s requested): about 440k execs at ~1.1k/s, 1461/2214 edges, 6 buckets.
The machine was heavily loaded (load 20) so throughput is not representative.

## Library bugs
1. **Lexer panic `slice bounds out of range` in `lexMultilineRawString` / `lexMultilineString`**
   (lex.go:115) on a multiline string ending in an incomplete UTF-8 sequence: `n=''''''\xae`.
   Reproducer `bugs/multiline-raw-string-slice/`. `stitch bisect --fixed --fixed-at v1.5.0` finds the fix:
   891d2617ddbd "Don't error out if a multiline string ends with an incomplete UTF-8 sequence" (2022-04-04).
   Fixed in v1.5.0 (returns a normal "invalid UTF-8 byte" error).
2. **Encoder panics on arrays mixing scalars and tables** (`eTable - unhandled reflect.Value Kind`,
   encode.go:329) when a `[]interface{}` holds a table and other values (v1.0.0). v1.5.0 encodes
   `k = [1, {"" = 2}]` without error.
3. **Decoder drops elements of a mixed array**: `toml.Decode("k = [1, {\"\" = 2}]")` gives
   `k = []map{{"": 2}}` (the 1 vanishes), so decode(encode(x)) != x. Still present in v1.5.0 (checked with
   `stitch try --rev v1.5.0`).
4. Quirk (not fixed, left as a harness precondition): `MetaData.IsDefined` is false for keys below an
   array of tables (`[[p]]\nn=1` -> `Keys()` has `p.n`, `IsDefined("p","n")` false), v1.0.0 and v1.5.0.

## Harness notes
- Encoder objects accumulate output in their buffer; `encode_*` reset the buffer first, otherwise a
  second Encode on the same Enc gives text that legitimately fails to decode (duplicate keys).
- `okStrings` must also cover `[]map[string]interface{}` (invalid UTF-8 keys inside arrays of tables
  make Encode emit text the decoder rejects); `Indent` is limited to whitespace (arbitrary text such as "x"
  yields undecodable output by design).
- Time values are normalised with `.UTC()` and compared by `Equal`; nil vs empty slices are folded.
