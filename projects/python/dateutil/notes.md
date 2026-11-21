# python-dateutil (dateutil/dateutil @ 2.9.0, the latest release)

17 stubs over the parser (`parse`, fuzzy parsing, `isoparse`), `relativedelta`, `rrule`/`rrulestr`
and time zones (`tzstr`, `tzoffset`, `gettz`). Objects: `Dt`, `Delta`, `Rule`, `Zone`. Decoders:
`DateText` and `RRuleText`. Oracles: `isoparse(isoformat(d)) == d`, recurrence occurrences come in
order and `between`/`after` agree with iteration, and `relativedelta` arithmetic round-trips.
Rules that would iterate for too long are capped with `stitch.time_limit(0.05)`.

Campaign: 15 min, 2 workers: 102k execs (113/s; `rrule` iteration dominates), 3497 edges, 14 buckets.

## Library bugs (all in 2.9.0)

1. **`tzstr("GMT")` raises `TypeError`** (`unsupported operand type(s) for *=: 'NoneType' and
   'int'`, tz.py:1092). A bare abbreviation with no offset leaves the offset as `None`.
   Reproducer: `bugs/tzstr-gmt/`.
2. **The parser accepts UTC offsets of 24 hours or more.** `parse(..., fuzzy=True)` builds a
   `tzoffset` that `datetime` rejects later, so the `ValueError` ("offset must be a timedelta
   strictly between ...") appears in whichever call first uses the datetime: `isoformat`,
   `rrule.after`, `rrule.between`. That's 7 buckets with one cause.
3. **`isoformat()` output that `isoparse` rejects.** Offsets with seconds (`+05:30:15`) are written
   by `datetime.isoformat` but refused by `isoparse` ("Time zone offset must be 1, 3, 5 or 6
   characters").
4. **`rrule` with `INTERVAL=0`** divides by zero in `_iter` (rrule.py:934, 951; 5 buckets). In some
   frequencies it also yields occurrences out of order instead of failing.

## Harness notes

- `rrulestr` text comes from a decoder (`FREQ`, `BYxxx` parts, `COUNT`/`UNTIL`). Rules are capped
  by a time limit, and `COUNT` keeps iteration short.
