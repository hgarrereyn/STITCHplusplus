# Coverage

`stitch cov` replays a campaign's corpus with the language's own coverage tool and reports which
parts of the library the harness reaches.

```sh
stitch cov                     # the latest campaign's corpus
stitch cov probe --crashes     # another campaign, crashing programs included
stitch cov --rev v1.2.0        # the corpus against another library version
```

Output from the C key-value store of the tutorial:

```
coverage of 303 programs (298 ok, 5 crashed, 0 timed out), library at local

library:
  lines           90 / 94       95.7%
  functions        8 / 9        88.9%
  branches        35 / 38       92.1%

  file        lines  functions
  src/kv.c    95.7%      88.9%

never called: 1 library functions, 1 of them not static
  kv_get  src/kv.c:45

lcov: .stitch/cov/main/lcov.info
html: .stitch/cov/main/html/index.html
summary: .stitch/cov/main/summary.json
```

Languages whose tool has no branch data print `branches -`.

## What to do with it

1. **Never-called functions** need stubs. Write one for each that matters (public API, parsers,
   anything that takes input), then `stitch build`, `stitch probe` and fuzz again.
2. **Low line or branch coverage in a file** usually means a producer is missing (no stub makes the
   object those functions need) or a stub bails on most inputs. Read the probe report
   ([probe.md](probe.md)) for the stubs involved.
3. **Branches the corpus never takes** are in `html/index.html`. If a branch needs a specific input
   shape, add seeds or a decoder for that parameter's data type ([stubs.md](stubs.md)).
4. Stop when the remaining uncovered code is unreachable through the public API or not worth a
   stub (debug helpers, platform-specific paths).

## Files

`.stitch/cov/<campaign>/` holds:

| File | Contents |
|---|---|
| `lcov.info` | line and branch counts, for other tools |
| `html/index.html` | annotated source |
| `summary.json` | per-file numbers and uncovered functions |
| `report.txt` | the text report above |

## Tools by language

| Language | `stitch cov` uses |
|---|---|
| C/C++, Objective-C | clang source-based coverage, no sanitizers (memory bugs that need ASan to crash run to completion) |
| Python | coverage.py |
| JavaScript | V8 coverage and c8 |
| Java | JaCoCo |
| Go | `-cover` (lines and functions) |
| Rust | `-Cinstrument-coverage` (lines and functions) |
| Ruby | Ruby's `Coverage` (lines and branches) |
| Swift | Swift source-based coverage and llvm-cov, no sanitizers |
