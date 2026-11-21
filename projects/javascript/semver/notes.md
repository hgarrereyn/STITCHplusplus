# semver (npm/node-semver @ v7.5.1)

11 stubs: `parse`, `coerce`, `inc`, `compare`, `range`, `valid_range`, `satisfies`, `min_version`,
`subset`, `max_satisfying`, `simplify`. Objects: `Ver` (`SemVer`), `Rng` (`Range`). Decoders:
`VersionText` and `RangeText` (comparators, hyphen ranges, X-ranges, `||`; rarely a run of up to
20000 separators, put there to stress the range regexes). Each object is parsed with one of the
four option sets (`loose`, `includePrerelease`). Oracles: `parse(format(v)) == v`, `compare` is
antisymmetric and agrees with `eq`/`gt`, a version that satisfies a range is `>= minVersion`,
`subset(a, b)` means every version in `a` is in `b`, `simplifyRange` keeps the membership of the
listed versions, and `inc` goes up.

Campaign: 15 min, 2 workers: 48.5k execs (54/s; slow regex matches take up much of the time),
731 edges, 4 buckets. `timeout_ms = 500`.

## Library bugs

1. **CVE-2022-25883: ReDoS in `Range`.** Ranges with long runs of spaces or tabs take seconds to
   minutes to parse (the `\s*` patterns in the comparator regexes backtrack). Found as `timeout`
   buckets in `range` and `valid_range`; a minimized program parses a ~9k-character range and is
   still running after 60 s. Fixed in 7.5.2 (the buckets don't reproduce at 7.7.3). Reproducer:
   `bugs/cve-2022-25883-redos/` (stopped after 10 s).
2. **`minVersion` returns `null` for a satisfiable range** (still in 7.7.3). With `{loose: true,
   includePrerelease: true}`, `~>\t0.9.0\t<9.0.0\t^\t0.1.2\t|| zF)ud3  >=1.0.0 <2.0.0-0` is
   satisfied by `1.0.0`, but `minVersion` gives `null`.
3. **`simplifyRange` returns `*` when no listed version satisfies the range** (still in 7.7.3).
   `simplifyRange(["1.0.0", "99.2.1-0"], "~>10.0.1+build <=0.10.100000")` is `""`, which every
   version satisfies, so `1.0.0` changes from "not in the range" to "in the range".

## Harness bugs found and fixed

- **Options must be passed with every call.** A `Range` keeps the options it was parsed with, but
  `satisfies(v, r)`, `minVersion(r)`, `subset(a, b)`, ... without options parse `r.raw` again
  strictly. A loose range then throws `TypeError: Invalid comparator` or changes meaning. Every
  call now passes `r.options`, and `subset` requires both ranges to have the same options.
  This removed 4 buckets (a `TypeError` in `subset`, and `simplify`, `subset` and `intersects`
  check failures).
- **`inc(prerelease)` with a different identifier can go down** (`1.2.3-beta` with `alpha` gives
  `1.2.3-alpha.0`). That is semver's behaviour, so the check skips identifier changes.
