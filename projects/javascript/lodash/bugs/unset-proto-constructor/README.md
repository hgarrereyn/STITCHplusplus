# lodash: prototype pollution in unset

| | |
|---|---|
| Kind | `prototype pollution` |
| Location | – |
| Library source | [https://github.com/lodash/lodash](https://github.com/lodash/lodash) @ `ddfd9b11a012` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `unset-proto-constructor` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 from_pairs_set(path=Str[1,decoded]"a", v=7)
#1 unset(o=o0.0, path=Str[21,decoded]"__proto__.constructor")
```

## Stubs used

What each call does (from `project/harness/`):

`from_pairs_set` · harness/stubs.js

```js
stitch.endpoint("from_pairs_set", { path: Str(64, PropPath), v: IDX }, New(Obj), ({ path, v }) =>
  sane(path) || _.set({}, _.toPath(path), value(v)), { bailsOn: [badLength] });
```

`unset` · harness/stubs.js

```js
stitch.endpoint("unset", { o: Borrow(Obj), path: Str(64, PropPath) }, null, ({ o, path }) => {
  _.unset(o, path);
});
```

## Report

```
[stitch] EXCEPTION in unset: prototype pollution: Object.prototype.constructor was deleted
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH javascript toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

