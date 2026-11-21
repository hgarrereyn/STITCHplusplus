# yaml: check failed: stringify/parse roundtrip in stringify_roundtrip

| | |
|---|---|
| Kind | `check failed: stringify/parse roundtrip` |
| Location | – |
| Library source | [https://github.com/eemeli/yaml](https://github.com/eemeli/yaml) @ `aa1898ae6160` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `stringify-block-scalar` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 stringify_roundtrip(s=Src[15]7f6105ffffff050a20203f20783a0a, indent=3, lineWidth=21)
```

## Stubs used

What each call does (from `project/harness/`):

`stringify_roundtrip` · harness/stubs.js

```js
// lineWidth: 0 (no folding) or at least the documented minimum content width (20)
stitch.endpoint("stringify_roundtrip", { s: Src(64), indent: Range(1, 8), lineWidth: OneOf(0, 20, 21, 40, 80) }, null, ({ s, indent, lineWidth }) => {
  const value = (d) => {
    const k = d > 2 ? s.choice(4) : s.choice(6);
    if (k === 0) return s.i32();
    if (k === 1) return s.str(10);
    if (k === 2) return s.pick([true, false, null, 1.5, -0, "", "null", "yes", "~", "0x10", "#", "- a", ": b"]);
    if (k === 3) return s.pick(["", " lead", "trail ", "a\nb", "\t", "'", '"']);
    if (k === 4) return Array.from({ length: s.range(0, 3) }, () => value(d + 1));
    const o = {};
    for (let i = s.range(0, 3); i > 0; i--) o[s.pick(["a", "b", "", " k", "1", "null"])] = value(d + 1);
    return o;
  };
  const v = value(0);
  const text = YAML.stringify(v, { indent, lineWidth });
  stitch.log("value:", JSON.stringify(v), "\n  text:", JSON.stringify(text));
  const again = YAML.parse(text);
  stitch.log("parsed:", JSON.stringify(again));
  stitch.check(isDeepStrictEqual(again, JSON.parse(JSON.stringify(v))) || isDeepStrictEqual(again, v), "stringify/parse roundtrip");
});
```

## Report

```
[stitch] CHECK FAILED: stringify/parse roundtrip (node 0)
    #0 0x0 in Endpoint.fn .stitch/build/app/harness/stubs.js:158
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH javascript toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

