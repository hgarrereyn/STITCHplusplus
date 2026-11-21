# Apache Commons Compress 1.20 (large project)

Compiled with `javac --release 8` (the 1.20 pom targets Java 7 and pulls OSGi test tooling). Optional
codecs (xz, brotli, zstd-jni) and, for newer releases, commons-io / lang3 / codec / asm come from
Maven Central, so the same Dockerfile builds 1.20 and 1.28.0.

## Harness

- **Readers.** One stub per format with its own seed corpus from the project's test resources:
  zip (stream and ZipFile), tar, 7z (with passwords), ar, arj, cpio, dump, and the bzip2, gzip, xz,
  lzma, lz4 (block/framed), snappy, Z, deflate/deflate64 and brotli decompressors, plus format
  detection through the factories. Output is capped at 4 MB (bigger results bail).
- **Bugs.** Readers must fail with IOException (or ArchiveException / CompressorException) on bad
  input: any other exception, an OutOfMemoryError or a hang is a bug.
- **Writers.** `writer` (zip, tar, cpio, ar, jar with modes and encodings) → `add_entry` →
  `finish` → `read_back` / `zip_file_back`: what the library writes reads back with the same names
  and contents; `compress_roundtrip` for every compressor.

## Harness fixes during probing

- **Documented refusals.** tar's RuntimeExceptions for too-long names / too-big numbers bail.
- **Read back with the writer's encoding**, and an empty ar archive (ar writes nothing without
  entries) is skipped.

## Results

59 buckets in 30 minutes, the most of any project. Checked at 1.28.0: 46 fixed upstream (the 1.21
hardening release that answered OSS-Fuzz reports: NPEs, NegativeArraySize, OOMs from declared sizes,
an infinite loop in dump, ...). Still reproducing: BlockLZ4CompressorOutputStream crashing on empty
input, cpio accepting an entry named `TRAILER!!!`, cpio non-ASCII names, ZipFile round trip,
IllegalArgumentExceptions in the cpio and Z readers, and a new ArithmeticException in 1.28.0's
NTFS time conversion.
