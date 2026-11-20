# zlib (madler/zlib @ 570720b, 1.3.1.2-audit)

45 endpoints in `deflate/`, `inflate/`, `util/`, `gz/`. `Deflater`/`Inflater` objects keep the
stream plus everything fed and produced. That enables the oracles:
- `inflate(deflate(x)) == x` (when wrappers and windows match and nothing primed the stream);
- `uncompress(compress2(x)) == x` and `compressBound`;
- `crc32_combine` / `crc32_combine_op` / `adler32_combine` agree with a direct checksum.

Decoders: `Plain` (compressible text with back-references) and `ZData`, which compresses fuzzed
plaintext with fuzzed parameters, calling zlib itself, then optionally corrupts it.

## Findings

No library bugs within the documented API contract. One noteworthy edge:

- `inflatePrime` bits are later "returned" to the input by `inflate_fast` (`in -= bits >> 3`) as if
  they came from `next_in`, so `next_in` moves before the caller's buffer (heap-buffer-overflow
  READ in `inflate`). zlib documents that `inflatePrime` must only be used with raw inflate, before
  the first `inflate()` call, so this is caller misuse; the stub now enforces that. A 20-minute
  campaign within the contract found nothing.

## Harness bugs found and fixed through triage

- **Timeout:** `gzseek` forward on a write handle writes zeros (documented), so an `INT_MAX`
  offset writes 2 GB. Offsets are now bounded.
- **Round-trip oracle false positive:** `inflatePrime` injects bits, and `inflateCopy` /
  `deflateCopy` stubs did not copy the "primed"/"dictionary" attributes to the copy. Copies now
  carry the attributes.
- **Use-after-free:** `deflateCopy` copies the internal pointer to the *source's* gzip header, and
  `deflateSetHeader` cannot re-point it after the stream finished. Header storage is now shared
  (`shared_ptr`) between a stream and its copies.
- **Hygiene:** the stubs never leave `next_in`/`next_out` pointing at stub buffers, and
  `deflateCopy`/`inflateCopy` re-point the gzip header at the copy's own storage (the library
  copies the pointer).
