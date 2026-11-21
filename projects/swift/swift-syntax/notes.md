# swift-syntax 509.0.0 (large project)

The Swift parser written in Swift. Built with ASan and SanitizerCoverage; the SwiftSyntax module is
large enough that the stitch.toml compiles it file by file (`swiftc_flags =
["-no-whole-module-optimization"]`, `build_jobs = 4`), a new stitch-build option added for it.

## Harness

- **Round trip.** Every parsed tree prints back to exactly its source, byte for byte (with errors and
  all), its length is the source's, and its tokens are contiguous and cover the file.
- **Incremental parsing.** An edit (offset, length, inserted text) is applied; the incremental
  reparse with the previous tree and lookahead ranges must equal a fresh parse of the edited text.
- **Formatting, folding, diagnostics.** BasicFormat on a tree without errors gives source without
  errors; folding operators with the standard table keeps the text; parse diagnostics point inside
  the file, a tree with errors gets error diagnostics, Fix-Its are well formed; syntax
  classification ranges are ordered.
- **Data type.** SwiftText: declarations, statements, expressions, closures, attributes, `#if`,
  regex literals, macros, seeded with 600 snippets from SwiftParserTest.

## Results

10 buckets. Checked at 604.0.0: still reproducing: the parser drops text so a tree does not print
back to its source (`@transpose(ofbreak: Float.-,uuu wrt: (0, 1))`), a parser trap "Range requires
lowerBound <= upperBound", a precondition in the diagnostics generator, and a diagnostic past the end
of the file. Fixed upstream: a force-unwrap trap in the parser, incremental reparse mismatches and a
precondition in it, formatting that introduces errors, missing diagnostics, deep-nesting timeouts.
