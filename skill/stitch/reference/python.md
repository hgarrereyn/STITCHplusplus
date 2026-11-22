# Python harnesses

Stubs are Python functions decorated with `@stitch.endpoint`. Their type annotations are the
signature. Python 3.12 runs in the image, and the library is installed into its environment
(`/opt/venv`). The fuzzer talks to a Python worker process.

## Project setup

```sh
stitch init tomlkit --lang python --git https://github.com/python-poetry/tomlkit --rev 0.13.2
cd tomlkit
```

`Dockerfile`: install the library with pip.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN pip install --no-cache-dir /src
```

`stitch.toml`:

```toml
[project]
name = "tomlkit"
language = "python"

[source]
git = "https://github.com/python-poetry/tomlkit"
rev = "0.13.2"

[python]
include = ["tomlkit"]     # import names of the code under test; empty: everything outside the standard library
# exclude = ["tomlkit._compat"]

[fuzz]
timeout_ms = 2000
```

`include` decides which modules get coverage and comparison tracing. Set it to the library's
import name.

## Files

```
harness/
  _common.py           shared object types, data types, decoders and helpers
  parse/parse.py       stubs; every harness/**/*.py file is imported as part of the `harness` package
  tree/edit.py
  data/TomlText/       seeds/, *.dict for the TomlText data type
```

Stub files import shared code relatively, without `__init__.py` files:

```python
# harness/tree/edit.py
from .._common import Doc, TomlText
```

Start the shared module's name with `_` (`_common.py`): `stitch write` pastes harness files in
sorted order, and the shared module must come before the files that use it.

## A complete example

```python
import stitch
import tomlkit
from stitch import Borrow, New, Str, I64, Src
from tomlkit.exceptions import TOMLKitError

Doc = stitch.object("Doc")                    # object type
TomlText = stitch.datatype("TomlText")        # data type: harness/data/TomlText/


@stitch.endpoint(bails_on=(TOMLKitError,))    # these exceptions are not bugs
def parse(text: Str[2048, TomlText]) -> New[Doc]:
    return tomlkit.parse(text)


@stitch.endpoint
def set_int(doc: Borrow[Doc], key: Str[16], value: I64):
    doc[key] = value


@stitch.endpoint
def remove(doc: Borrow[Doc], s: Src[8]):
    keys = list(doc.keys())
    stitch.require(keys, "no keys")
    doc.remove(s.pick(keys))                  # pick an existing key


@stitch.endpoint(bails_on=(TOMLKitError,))
def roundtrip(doc: Borrow[Doc]):
    text = tomlkit.dumps(doc)
    stitch.check(tomlkit.dumps(tomlkit.parse(text)) == text, "dumps/parse/dumps is stable")
```

## Objects

```python
Doc = stitch.object("Doc")
Conn = stitch.object("Conn", drop=lambda c: c.close())   # drop: frees objects left at the end
```

| Annotation | Meaning |
|---|---|
| `Borrow[T]` (or just `T`) | uses an object; it stays available |
| `Take[T]` | consumes the object, also when the stub bails |
| `stitch.consume(x)` | on a borrowed input: the library took ownership in this call |
| `-> New[T]` | returns a new object; `None` is a bail |
| `-> New[T] \| None` | optional output: `None` means no object, not a bail |
| `-> tuple[New[A], New[B] \| None]` | several outputs: return a tuple |
| no return annotation | the stub produces nothing |

```python
from stitch import Take

Table = stitch.object("Table")


@stitch.endpoint(bails_on=(TOMLKitError,))
def parse_first_table(text: Str[2048, TomlText]) -> tuple[New[Doc], New[Table] | None]:
    doc = tomlkit.parse(text)
    tables = [v for v in doc.values() if isinstance(v, tomlkit.items.Table)]
    return doc, (tables[0] if tables else None)


@stitch.endpoint
def append_table(doc: Borrow[Doc], name: Str[16], t: Take[Table]):
    stitch.require(name not in doc, "key exists")
    doc.append(name, t)                       # t now belongs to doc
```

## Fuzzed parameters

| Annotation | Value |
|---|---|
| `U8`..`U64`, `I8`..`I64`, `int` | int (`int` is `I64`) |
| `F32`, `F64`, `float` | float |
| `Bool`, `bool` | bool |
| `Range[lo, hi]` | int in [lo, hi] |
| `OneOf["a", None, 3]` | one of the values (any Python constants); `OneOf[VALUES]` with a list, tuple, range or set |
| `Flags[1, 2, 4]` | OR of a subset |
| `Str[max, Tag]`, `str` | str of at most `max` bytes (invalid UTF-8 replaced); `Tag` is an optional data type |
| `Bytes[max, Tag]`, `bytes` | bytes |
| `File[max, Tag]` | path of a temporary file holding the data |
| `Val[Tag, max]` | the value `Tag`'s decoder returns (any object); `max` defaults to 1024 |
| `Src[max]` | a `stitch.Source` for choices inside the stub |

```python
from stitch import Range, OneOf, File


@stitch.endpoint
def dumps_sorted(doc: Borrow[Doc], sort_keys: bool):
    tomlkit.dumps(doc, sort_keys=sort_keys)


@stitch.endpoint
def set_value(doc: Borrow[Doc], key: Str[16], n: Range[0, 8], kind: OneOf["int", "str", "float"]):
    doc[key] = {"int": n, "str": "x" * n, "float": n / 2}[kind]


@stitch.endpoint(bails_on=(TOMLKitError, UnicodeDecodeError))   # the file may not be UTF-8
def load_file(path: File[4096, TomlText]) -> New[Doc]:
    with open(path) as f:
        return tomlkit.load(f)
```

## Bail, require, check

```python
stitch.bail("reason")                 # stop this call; not a bug
stitch.require(cond, "reason")        # bail unless cond
stitch.check(cond, "what")            # a bug if false: "check failed: what"
stitch.log("n=%d", n)                 # printed by `stitch exec --trace`


@stitch.init                          # runs once per worker process, after the harness is imported
def setup():
    sys.setrecursionlimit(5000)


with stitch.time_limit(0.05):         # bails ("time limit") instead of a timeout
    out = list(itertools.islice(rule, n))
```

These are bugs: any exception not listed in `bails_on` (`IndexError`, `KeyError`, `TypeError`,
`RecursionError`, `MemoryError` beyond 4 GB, ...), a failed `stitch.check`, and programs that
run past `timeout_ms`. `bails_on` takes exception classes; subclasses match too.

Use `stitch.time_limit` around calls that are slow by design (iterating an endless sequence), so
they bail instead of being reported as timeouts.

## Attributes

```python
stitch.set(doc, "edits", stitch.get(doc, "edits", 0) + 1)   # any value
stitch.get(doc, "edits", 0)          # with a default: a missing key is normal
stitch.get(doc, "edits")             # without: a missing key is reported by `stitch probe`
stitch.has(doc, "edits")
stitch.delete(doc, "edits")
stitch.global_set("mode", 1)         # program-wide; reset at the start of every program
stitch.global_get("mode", 0)
```

Keep state that `@stitch.init` builds in module-level variables, not in `global_set`.

Attributes set on a value before the stub returns it stay with the new object. They are keyed by
identity (`is`): if a stub returns an object that is already in the pool, `set` writes to the
existing object.

Links to other objects:

```python
@stitch.endpoint
def get_table(doc: Borrow[Doc], s: Src[8]) -> New[Table]:
    tables = [k for k, v in doc.items() if isinstance(v, tomlkit.items.Table)]
    stitch.require(tables, "no tables")
    t = doc[s.pick(tables)]
    stitch.set(t, "doc", doc)                # link the table to its document
    return t


@stitch.endpoint
def table_keys(t: Borrow[Table]):
    stitch.require(stitch.get_obj(t, "doc") is not None, "document is gone")   # None once consumed
    list(t.keys())
```

## Data types and decoders

Tag a `Str`, `Bytes` or `File` parameter with a data type and create `harness/data/<Name>/`
with `seeds/` (sample inputs) and `*.dict` files (one quoted token per line). A decoder builds
values from fuzzer choices:

```python
@stitch.decoder(TomlText)
def toml_text(s):                     # s: stitch.Source
    lines = []
    while s.more():
        lines.append(f"{s.token()} = {s.range(0, 99)}")
    return "\n".join(lines)
```

`stitch sample TomlText -n 5` prints values the decoder makes.

A decoder can return any object. Take it as `Val[Tag, max]`, and build decoders on each other
with `s.draw(Tag)`:

```python
from stitch import Val

Pair = stitch.datatype("Pair")
Pairs = stitch.datatype("Pairs")


@stitch.decoder(Pair)
def pair(s):
    key = s.token() if s.coin(0.3) else s.str(8)
    return key, (s.str(12) if s.coin() else f"{s.f64(0, 100):.2f}")


@stitch.decoder(Pairs)
def pairs(s):
    out = []
    while s.more():
        out.append(s.draw(Pair))
    return out


@stitch.endpoint(bails_on=(ValueError,))
def put_all(st: Borrow[Store], kvs: Val[Pairs, 256]):
    for k, v in kvs:
        st.put(k, v)
```

`stitch.Source` methods:

| Method | Returns |
|---|---|
| `u8()` .. `u64()`, `i8()` .. `i64()`, `f32()`, `f64()` | a number |
| `range(lo, hi)` | int in [lo, hi] |
| `choice(n)` | index in [0, n) |
| `pick(items)` | one element of the list |
| `coin(p=0.5)` | True with probability about p |
| `more()` | loop condition: `while s.more():` |
| `f64(lo, hi)`, `f32(lo, hi)` | finite float in [lo, hi] |
| `str(max)`, `bytes(max)` | text or bytes of at most `max` bytes |
| `fixed(n)` | exactly n bytes |
| `rest()` | every byte that is left |
| `token()` | often a token from the data type's `.dict` files; otherwise (and once mutated) any string, like `str`: check it when the input must be one of the tokens (or use `pick`) |
| `draw(Tag)` | a value from data type `Tag`'s decoder |

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/repro.py` (the harness code, then a `main()` that
calls each stub with its recorded arguments) and `stitch.py`, a stand-in for the runtime. Shrink
a copy to plain library code and check it after each edit:

```sh
cp .stitch/standalone/c1/repro.py .stitch/standalone/c1/min.py
stitch try .stitch/standalone/c1/min.py
```

A finished reproducer:

```python
import toml

toml.dumps({"\x00": 0})   # a key with a control character: _dump_str indexes past the end
```

```
SAME CRASH: .stitch/standalone/c1/min.py -> IndexError in _dump_str
  2 code lines, plain library code
```

For a failed `check`, raise an exception with the check's message in your own code:

```python
import toml

d = {"ip": "\u1000\x02"}
text = toml.dumps(d)   # 'ip = "\u1000x02"': after a non-ASCII char, \x02 loses its backslash
if toml.loads(text) != d:
    raise AssertionError("loads(dumps(d)) == d")
```

`stitch try` compares the uncaught exception and its innermost library frame with the original
crash. Details: `stitch guide reproducers`.

## Coverage

While fuzzing, the modules in `include` (and the harness) are imported through a hook that
records comparisons (`==`, `in`, `<`, ...), and `sys.monitoring` records lines and branches.
`stitch cov` uses coverage.py.
