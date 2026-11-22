# JavaScript harnesses

Stubs are functions registered with `stitch.endpoint(name, args, returns, fn)`. The `args` object
is the signature. Node.js 22 runs in the image; the library is installed in `/opt/node_modules`
(on `NODE_PATH`). The fuzzer talks to a Node worker process.

## Project setup

```sh
stitch init qs --lang javascript --git https://github.com/ljharb/qs --rev v6.9.0
cd qs
```

`Dockerfile`: install the package where the harness can `require` it.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN npm install --omit=dev --ignore-scripts
RUN cd /opt && npm install /src
```

A package that needs a build step (TypeScript, a bundler): run
`RUN npm install --ignore-scripts && npm run build` before installing it into `/opt`.

`stitch.toml`:

```toml
[project]
name = "qs"
language = "javascript"

[source]
git = "https://github.com/ljharb/qs"
rev = "v6.9.0"

[javascript]
include = ["qs"]          # npm package names of the code under test; empty: every package
# node_options = ["--max-old-space-size=4096"]

[fuzz]
timeout_ms = 2000
```

`include` decides which packages get coverage and comparison tracing. Set it to the library's
package name.

## Files

```
harness/
  common.js            shared object types, data types and helpers
  parse/parse.js       stubs; every .js, .cjs and .mjs file under harness/ is loaded
  stringify/stringify.js
  data/QueryText/      seeds/, *.dict for the QueryText data type (not loaded)
```

Stub files share definitions with `require`:

```js
// harness/common.js
const stitch = require("stitch");
module.exports = {
  Obj: stitch.object("Obj"),
  QueryText: stitch.datatype("QueryText"),
};

// harness/parse/parse.js
const { Obj, QueryText } = require("../common");
```

## A complete example

```js
const stitch = require("stitch");
const { Borrow, New, Str, Src, OneOf } = stitch;
const qs = require("qs");

const Obj = stitch.object("Obj");                       // object type
const QueryText = stitch.datatype("QueryText");         // data type: harness/data/QueryText/

//              name     arguments (names and types)                  returns   function
stitch.endpoint("parse", { text: Str(1024, QueryText), depth: OneOf(0, 1, 5) }, New(Obj),
  ({ text, depth }) => qs.parse(text, { depth }));

stitch.endpoint("set_key", { o: Borrow(Obj), key: Str(16), value: Str(16) }, null, ({ o, key, value }) => {
  o[key] = value;
});

stitch.endpoint("remove_key", { o: Borrow(Obj), s: Src(8) }, null, ({ o, s }) => {
  const keys = Object.keys(o);
  stitch.require(keys.length > 0, "no keys");
  delete o[s.pick(keys)];                               // pick an existing key
});

stitch.endpoint("roundtrip", { o: Borrow(Obj) }, null, ({ o }) => {
  // only flat string values with plain keys: nested keys, depth limits and prototype names
  // ("constructor") legitimately change in a round trip
  const flat = Object.fromEntries(Object.entries(o).filter(([k, v]) =>
    typeof v === "string" && /^[a-z0-9]+$/i.test(k) && !(k in Object.prototype)));
  const back = qs.parse(qs.stringify(flat));
  stitch.check(JSON.stringify(back) === JSON.stringify(flat), "flat strings survive stringify/parse");
});
```

The function receives one object with the arguments by name.

## Objects

```js
const Obj = stitch.object("Obj");
const Conn = stitch.object("Conn", (c) => c.close());   // drop: runs on objects left at the end
```

| Spec | Meaning |
|---|---|
| `Borrow(T)` | uses an object; it stays available |
| `Take(T)` | consumes the object |
| `stitch.consume(x)` | on a borrowed input: the library took ownership in this call |
| returns `New(T)` | the function returns a new object; `null` or `undefined` is a bail |
| returns `New(T).optional()` | optional output: `null` means no object, not a bail |
| returns `[New(A), New(B).optional()]` | several outputs: return an array |
| returns `null` | the stub produces nothing |

```js
const Arr = stitch.object("Arr");

// two outputs, each optional: an object value or an array value
stitch.endpoint("get_value", { o: Borrow(Obj), s: Src(8) }, [New(Obj).optional(), New(Arr).optional()], ({ o, s }) => {
  const keys = Object.keys(o).sort();
  stitch.require(keys.length > 0, "no keys");
  const v = o[s.pick(keys)];
  if (Array.isArray(v)) return [null, v];
  if (v && typeof v === "object") return [v, null];
  stitch.bail("not an object or array");
});

const contains = (root, x) => root === x ||
  (root !== null && typeof root === "object" && Object.values(root).some((v) => contains(v, x)));

stitch.endpoint("adopt", { o: Borrow(Obj), key: Str(16), child: Take(Obj) }, null, ({ o, key, child }) => {
  stitch.require(!contains(child, o), "would create a cycle");   // o may be a value inside child
  o[key] = child;                                        // child now belongs to o
});
```

## Fuzzed parameters

| Spec | Value |
|---|---|
| `U8`..`U32`, `I8`..`I32`, `I64`, `U64` | number |
| `BigI64`, `BigU64` | BigInt |
| `F32`, `F64`, `Bool` | number, boolean |
| `Range(lo, hi)` | integer in [lo, hi] |
| `OneOf(...values)` | one of the values (any JS values) |
| `Flags(1, 2, 4)` | OR of a subset |
| `Str(max, Tag)` | string of at most `max` bytes; `Tag` is an optional data type |
| `Bytes(max, Tag)` | Buffer |
| `File(max, Tag)` | path of a temporary file holding the data |
| `Val(Tag, max = 1024)` | the value `Tag`'s decoder returns (any JS value) |
| `Src(max)` | a `stitch.Source` for choices inside the stub |

```js
const { Range, Flags, Bytes, Bool } = stitch;

stitch.endpoint("stringify", { o: Borrow(Obj), format: OneOf("indices", "brackets", "repeat", "comma"),
                               opts: Flags(1, 2, 4) }, null, ({ o, format, opts }) => {
  qs.stringify(o, { arrayFormat: format, encode: !(opts & 1), allowDots: !!(opts & 2), skipNulls: !!(opts & 4) });
});

stitch.endpoint("parse_buffer", { b: Bytes(256), limit: Range(1, 20), dots: Bool }, New(Obj), ({ b, limit, dots }) =>
  qs.parse(b.toString("latin1"), { parameterLimit: limit, allowDots: dots }));
```

## Bail, require, check

```js
stitch.bail("reason");                 // stop this call; not a bug
stitch.require(cond, "reason");        // bail unless cond
stitch.check(cond, "what");            // a bug if false: "check failed: what"
stitch.log("n =", n);                  // printed by `stitch exec --trace`
stitch.init(() => { process.env.TZ = "UTC"; });   // once per worker process
```

Exceptions that mean "invalid input" go in `bailsOn`, as classes or arrow-function predicates:

```js
stitch.endpoint("parse_strict", { text: Str(256) }, New(Obj), ({ text }) => qs.parse(text, { strictNullHandling: true }),
  { bailsOn: [RangeError, (e) => /Parameter limit/.test(e.message)] });
```

A predicate must be an arrow function: a `function` expression is treated as a class.

Stubs can be `async`; the runtime awaits them.

These are bugs:

- an exception not matched by `bailsOn`, including `RangeError: Maximum call stack size exceeded`
- prototype pollution: a change to `Object.prototype`, its methods (`Object.prototype.toString.x`),
  `Array.prototype`, `Function.prototype` or `Object` during a call. It is reported as
  `prototype pollution: Object.prototype.x was set` (or `was replaced`, `was deleted`), and the
  runtime restores the original.
- an unhandled promise rejection
- a failed `stitch.check`, and programs that run past `timeout_ms`

## Attributes

```js
stitch.set(o, "edits", stitch.get(o, "edits", 0) + 1);   // any value
stitch.get(o, "edits", 0);             // with a default: a missing key is normal
stitch.get(o, "edits");                // without: a missing key is reported by `stitch probe`
stitch.has(o, "edits");
stitch.del(o, "edits");
stitch.globalSet("mode", 1);           // program-wide; reset for every program
stitch.globalGet("mode", 0);
```

Links to other objects:

```js
stitch.endpoint("child", { o: Borrow(Obj), s: Src(8) }, New(Obj), ({ o, s }) => {
  const keys = Object.keys(o).filter((k) => o[k] && typeof o[k] === "object").sort();
  stitch.require(keys.length > 0, "no object values");
  const c = o[s.pick(keys)];
  stitch.set(c, "parent", o);                            // link the child to its parent
  return c;
});

stitch.endpoint("child_roundtrip", { c: Borrow(Obj) }, null, ({ c }) => {
  if (stitch.has(c, "parent")) stitch.require(stitch.getObj(c, "parent") !== null, "parent is gone");
  qs.stringify(c);
});
```

## Data types and decoders

Tag a `Str`, `Bytes` or `File` parameter with a data type and create `harness/data/<Name>/` with
`seeds/` (sample inputs) and `*.dict` files (one quoted token per line). A decoder builds values
from fuzzer choices:

```js
stitch.decoder(QueryText, (s) => {
  const parts = [];
  while (s.more()) {
    const key = s.coin(0.5) ? s.token() : s.str(6);
    parts.push(s.coin(0.2) ? key : `${key}=${s.str(8)}`);
  }
  return parts.join("&");
});
```

`stitch sample QueryText -n 5` prints values the decoder makes.

A decoder that returns a string or Buffer serves `Str`/`Bytes` parameters. Any other value is
taken with `Val(Tag)`, and decoders build on each other with `s.draw(Tag)`:

```js
const Pair = stitch.datatype("Pair");
const Pairs = stitch.datatype("Pairs");

stitch.decoder(Pair, (s) => [s.coin(0.5) ? s.token() : s.str(6), s.str(8)]);
stitch.decoder(Pairs, (s) => {
  const out = [];
  while (s.more()) out.push(s.draw(Pair));
  return out;
});

stitch.endpoint("from_pairs", { kvs: stitch.Val(Pairs, 256) }, New(Obj), ({ kvs }) =>
  qs.parse(kvs.map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`).join("&")));
```

`stitch.Source` methods:

| Method | Returns |
|---|---|
| `u8()` .. `u64()`, `i8()` .. `i64()`, `f32()`, `f64()` | a number |
| `range(lo, hi)` | integer in [lo, hi] |
| `choice(n)` | index in [0, n) |
| `pick(array)` | one element |
| `coin(p = 0.5)` | true with probability about p |
| `more()` | loop condition: `while (s.more()) { ... }` |
| `f64(lo, hi)`, `f32(lo, hi)` | finite float in [lo, hi] |
| `str(max)`, `bytes(max)` | text or bytes of at most `max` bytes |
| `fixed(n)` | exactly n bytes |
| `rest()` | every byte that is left |
| `token()` | a token from the data type's `.dict` files |
| `draw(Tag)` | a value from data type `Tag`'s decoder |

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/repro.js` (the harness code, then a `main()` that
calls each stub with `await stitch.call(n, "name", { args })`) and `stitch.js`, a stand-in for the
runtime. Shrink a copy to plain library code and check it after each edit:

```sh
cp .stitch/standalone/c1/repro.js .stitch/standalone/c1/min.js
stitch try .stitch/standalone/c1/min.js
```

End with `throw new Error("X")` for a failed check. `stitch try` compares Node's uncaught error
with the original crash. Details: `stitch guide reproducers`.

## Coverage

While fuzzing, the files of the `include` packages are instrumented with a Babel plugin (branch
counters and comparison tracing). `stitch cov` uses V8 coverage and c8.
