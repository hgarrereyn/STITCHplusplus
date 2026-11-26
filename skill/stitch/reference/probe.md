# Reading the probe report

`stitch probe` runs one worker for a short time and reports stubs that are not working.
`stitch status` prints the same report for a campaign. Run the probe after every harness change
and compare.

## Example

```
probe: 20s, 42.8k execs (2.1k/s), 733/3298 edges, corpus 461, calls bailing 13% (programs with a bail 66%), 0 crash/timeout programs saved
never succeeds (1, 20+ calls and none ok so far): the fuzzer picks these less and less
  item_string  [600 calls] "root is gone" x600; read missing attr 'root'
depends on where an input came from (the stub that made or last used that object; the fuzzer learns these pairs. Act only when the low one should work: a different kind of object behind one type gets its own type):
  add_item(obj): ok 2% when obj last came from parse, 100% when from new_object
  delete_key(obj): ok 0% when obj last came from parse, 79% when from print_opts
often skipped because an input object was missing (1): its producer bailed or returned no object
  item_string  skipped 13797x, ran 600x
```

Here `item_string` reads an attribute `root` that its producer never sets. `add_item` only works
on JSON objects, and `parse` often returns arrays or numbers, which is expected.

## What to do

| Line | Action |
|---|---|
| never succeeds | Read its bail reasons. Usual causes: a missing producer, an attribute nobody sets (`read missing attr 'x'`), or something missing in the environment (a server, a file). Fix the stub or add a producer. |
| mostly bails | Replace rejected values with `Range`/`OneOf`. Bails from parse errors on fuzzed input are fine. |
| depends on where an input came from | Usually fine: the fuzzer learns these pairs. Act only if the stub should accept both. If one object type holds different kinds of values, split it into types with one producer each. |
| often skipped | A producer of one of its inputs usually fails. Fix that producer. |
| never called | No stub produces one of its inputs, or the run was too short. |
| calls bailing above 50% | Most runs are wasted. Fix the stubs that bail most. |
| crash or timeout programs saved | Triage them: `stitch crashes probe`. |

## Done when

- no stub under "never succeeds" or "often skipped" that should work
- calls bailing below 50%
- harness bugs among the probe's crashes are fixed

Then start the campaign: `stitch fuzz --jobs 4`.
