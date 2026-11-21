# regex 1.5.4 (large project)

rust-lang/regex with regex-syntax 0.6 from the same checkout, built with ASan, overflow checks and
debug assertions.

## Harness

- **Engines against each other.** Each compiled regex is built twice: normally and with a zero-size
  lazy DFA cache, which forces the fallback engines. `find`, `captures`, `find_at` and `find_iter`
  must agree; the `bytes::Regex` must agree with the `str` one on UTF-8 text.
- **APIs against each other.** `is_match` == `find().is_some()` == `shortest_match().is_some()`;
  group 0 of `captures` is the `find` match; `find_iter` matches are ordered, non-overlapping, on
  char boundaries, with no two empty matches at one position; `split` and `replace_all` agree with
  the match count; `RegexSet` agrees with `is_match`.
- **Printers.** The AST printer round-trips and is a fixed point; the HIR printer gives a regex that
  matches like the original; `escape(s)` matches exactly `s`.
- **Limits.** size_limit 1 MB and nest_limit 64 so blowups by design bail.

## Harness fixes during triage

- **Printed HIR over the size limit.** Printing spells Unicode classes out range by range; that now
  bails instead of failing the oracle.

## Results

7 buckets. All library buckets are fixed in 1.13.2: the compile-time blowup on repeated empty
sub-expressions (CVE-2022-24713, fixed in 1.5.5), and engine disagreements, `find_at` context bugs
and empty matches splitting UTF-8 characters, fixed by the 1.9 rewrite.
