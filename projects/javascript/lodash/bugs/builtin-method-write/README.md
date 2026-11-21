# lodash: prototype pollution in set

| | |
|---|---|
| Kind | `prototype pollution` |
| Location | – |
| Library source | [https://github.com/lodash/lodash](https://github.com/lodash/lodash) @ `ddfd9b11a012` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `builtin-method-write` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 new_obj(kind="object")
#1 set(o=o0.0, path=Str[10,decoded]"toString.a", v=9)
```

## Stubs used

What each call does (from `project/harness/`):

`new_obj` · harness/stubs.js

```js
stitch.endpoint("new_obj", { kind: OneOf("object", "array", "nested") }, New(Obj), ({ kind }) =>
  kind === "array" ? [1, { a: 2 }] : kind === "nested" ? { a: { b: { c: 1 } }, d: [0, { e: 2 }] } : {});
```

`set` · harness/stubs.js

```js
stitch.endpoint("set", { o: Borrow(Obj), path: Str(64, PropPath), v: IDX }, null, ({ o, path, v }) => {
  sane(path);
  _.set(o, path, value(v));
}, { bailsOn: [badLength] });
```

## Report

```
[stitch] EXCEPTION in set: prototype pollution: Object.prototype.toString.a was set
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH javascript toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

