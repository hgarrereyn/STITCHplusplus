# intervaltree (chaimleib/intervaltree @ 3.1.0, the latest release)

16 stubs: `new_tree`, `from_tuples`, `copy`, `clear`, `addi`, `remove`, `remove_overlap`,
`remove_envelop`, `chop`, `slice_at`, `merge_overlaps`, `merge_equals`, `split_overlaps`, `query`,
`bounds`, `set_op` (union, difference, intersection, symmetric difference). Object: `Tree`. Interval bounds are small
(`Range[-40, 40]`, lengths 0 to 20) so intervals overlap often. Oracles: the tree's own `verify()`
after every change, and a plain list of intervals that mirrors every change (`stitch.set(t,
"model", ...)`). Queries must match a brute-force search over the list.

Campaign: 15 min, 2 workers: 2.4M execs (2.6k/s), 855 edges, no crashes.

## Findings

None. The first campaign's only bucket, "slice does not drop intervals", was a harness bug.
`slice(p)` splits `[25,45)` into `[25,40)` and `[40,45)`. When those already exist, the tree (a
set) keeps one copy of each, so its size can shrink. The check now compares `slice` with an exact
model: every interval containing the point is replaced by its two halves.

## Harness notes

- Null intervals (`begin == end`) are ignored by `add`; the model skips them too (`iv.is_null()`).
- `chop` is checked through the queries that follow it rather than with its own model.
