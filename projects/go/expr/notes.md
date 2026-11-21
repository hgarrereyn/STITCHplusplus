# expr v1.15.0 (large project)

expr-lang/expr (module `github.com/antonmedv/expr` at this version; the Dockerfile renames later
versions back so the same harness builds at any revision).

## Harness

- **Environment.** The same names as expr's own fuzz test (`ok`, `f64`, `array`, `list`, `foo`,
  `add`, `greet`, ...) plus a map, a string, a nil pointer and a nil interface, as a typed struct
  (checker sees static types) or an untyped map; values fuzzed per run.
- **Bugs.** Panics in the parser, checker, compiler or optimizer. The VM recovers panics into errors,
  so a run error that is a Go runtime error or a reflect panic, other than the ones expr's own fuzz
  test lists as expected, is reported.
- **Oracles.** Optimized and unoptimized programs agree (the optimized one never fails where the
  other succeeds); a program runs the same twice; the checker's static type matches the value's
  kind; `Node.String()` prints an expression that parses and evaluates like the original.
- **Data type.** An expression grammar (operators, builtins, closures, pipes, `let`, optional
  chaining), seeded with 656 expressions from expr's fuzz corpus and its dictionary.

## Harness fixes during probing and triage

- **Output conversions** (`AsInt`, `AsFloat64`) are excluded from the type oracle and reused when
  recompiling a printed expression.
- **Map iteration order**: results of `keys`, `values`, `toPairs`, `groupBy` and `$env` compare as
  multisets.

## Results

8 buckets (8.7M executions). Checked at v1.17.8: fixed upstream: a nil dereference in the compiler,
`$env` typed as a map while evaluating to the struct, a printer that wrote raw newlines into string
literals, printed optimized ASTs that evaluate differently. Still reproducing: member access through
a nil struct pointer panics inside reflect instead of returning an error.
