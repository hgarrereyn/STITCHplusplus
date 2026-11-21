# JavaScript harnesses

Stubs are functions registered with `stitch.endpoint`. Node.js 22 runs in the image; the library
is installed into `/opt/node_modules` (on `NODE_PATH`).

## Project

```sh
stitch init yaml --lang javascript --git https://github.com/eemeli/yaml --rev v2.6.1
```

```toml
[project]
language = "javascript"

[javascript]
include = ["yaml"]          # package names of the code under test (instrumented while fuzzing)
```

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN npm ci --ignore-scripts && npm run build        # if the package needs building
RUN cd /opt && npm install /src
```

## Stubs

Every `harness/**/*.js` file is loaded (CommonJS; `.mjs` works too).

```js
const stitch = require("stitch");
const { Borrow, Take, New, Str, I32, OneOf, Src } = stitch;
const YAML = require("yaml");

const Doc = stitch.object("Doc");
const YamlText = stitch.datatype("YamlText");

//              name     arguments (names and types)          returns    function
stitch.endpoint("parse", { text: Str(2048, YamlText) }, New(Doc), ({ text }) => YAML.parseDocument(text));

stitch.endpoint("set", { doc: Borrow(Doc), key: Str(16), value: I32 }, null, ({ doc, key, value }) => {
  doc.set(key, value);
}, { bailsOn: [YAML.YAMLParseError, (e) => /not a collection/.test(e.message)] });
```

| Spec | Value |
|---|---|
| `Borrow(T)`, `Take(T)` | objects (`stitch.consume(x)` for a borrowed one the library took) |
| `New(T)`, `New(T).optional()`, `[New(A), New(B)]`, `null` | returns |
| `U8`..`U32`, `I8`..`I32`, `I64`, `U64` | number (`BigI64`/`BigU64`: BigInt) |
| `F32`, `F64`, `Bool` | number, boolean |
| `Range(lo, hi)`, `OneOf(...values)`, `Flags(...)` | constrained values (`OneOf` takes any values) |
| `Str(max, Tag)`, `Bytes(max, Tag)`, `File(max, Tag)` | string, Buffer, temp file path |
| `Val(Tag, max = 1024)` | the value `Tag`'s decoder returns (any JS value) |
| `Src(max)` | `stitch.Source` |

Stubs may be `async`; the runtime awaits them.

Decoders: `stitch.decoder(Tag, (s) => ...)` returns a string or Buffer (values for `Str`/`Bytes`
of that type) or any other value (for `Val(Tag)`); `s.draw(Tag)` builds on another decoder.
`stitch.Source`: `u8()`..`u64()`, `i8()`..`i64()`, `f32()`, `f64()`, `f64(lo, hi)` (finite, in
range), `range(lo, hi)`, `choice(n)`, `pick(arr)`, `coin(p)`, `more()`, `str(max)`, `bytes(max)`,
`fixed(n)`, `rest()`, `token()`, `draw(Tag)`.

## Bugs

- an exception not matched by `bailsOn` (classes or predicates), including `RangeError: Maximum
  call stack size exceeded`
- a change during a call to a built-in that every object reaches (prototype pollution):
  `Object.prototype`, its methods (`set(o, "toString.x", 1)` writes into
  `Object.prototype.toString`), `Array.prototype`, `Function.prototype` and `Object`. A property
  added, replaced or deleted is reported as `prototype pollution: Object.prototype.x was set` (or
  `was replaced` / `was deleted`), and the runtime restores the original
- an unhandled promise rejection
- `stitch.check(cond, "what")`; `stitch.bail` / `stitch.require` stop a call without a bug

## Coverage and comparisons

While fuzzing, files of the `include` packages are instrumented with a Babel plugin (counters on
branches, `===`/`switch` comparisons; `String.startsWith`/`includes` and `Set`/`Map` lookups are
traced). `stitch cov` uses V8 coverage and c8.

## Reproducers

`stitch write c1` writes `repro.js` (harness code + `main()` calling each stub with
`await stitch.call(n, "name", { args })`) and `stitch.js`. A plain minimized file is compared through
Node's uncaught error output.
