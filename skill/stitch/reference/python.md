# Python harnesses

Stubs are Python functions. Their annotations are the spec. Python 3.12 runs in the image; the
library is installed into its environment (`/opt/venv`).

## Project

```sh
stitch init tomlkit --lang python --git https://github.com/python-poetry/tomlkit --rev 0.13.2
```

```toml
# stitch.toml
[project]
language = "python"

[python]
include = ["tomlkit"]     # import names of the code under test: coverage and comparison tracing
```

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN pip install --no-cache-dir /src
```

## Stubs

Every `harness/**/*.py` file is imported as part of the `harness` package.

```python
import stitch
import tomlkit
from stitch import Borrow, Take, New, Str, I64, OneOf, Src
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
```

| Annotation | Value |
|---|---|
| `Borrow[T]`, `T` | an object; it stays available. `stitch.consume(x)` if the library took it |
| `Take[T]` | an object the call consumes |
| `-> New[T]` | returns an object; `None` is a bail |
| `-> New[T] \| None` | optional output |
| `-> tuple[New[A], New[B] \| None]` | several outputs |
| `U8`..`U64`, `I8`..`I64`, `int` | int (`int` is `I64`) |
| `F32`, `F64`, `float` | float |
| `Bool`, `bool` | bool |
| `Range[lo, hi]` | int in [lo, hi] |
| `OneOf["a", None, 3]` | one of the values (any Python constants) |
| `Flags[1, 2, 4]` | OR of a subset |
| `Str[max, Tag]`, `str` | str (UTF-8, invalid bytes replaced) |
| `Bytes[max, Tag]`, `bytes` | bytes |
| `File[max, Tag]` | path of a temporary file |
| `Src[max]` | `stitch.Source` |

## Bugs

Any exception a stub does not list in `bails_on` is a bug (`IndexError`, `KeyError`,
`RecursionError`, `MemoryError` beyond 4 GB, ...), and so is a failed `stitch.check(cond, "what")`.
`stitch.bail("reason")` and `stitch.require(cond, "reason")` stop the call without a bug.

Calls that are slow by design (iteration without an end) can be bounded:

```python
with stitch.time_limit(0.05):      # bails ("time limit") instead of a timeout
    out = list(itertools.islice(rule, n))
```

## Decoders and attributes

```python
@stitch.decoder(TomlText)
def toml_text(s):                  # s: stitch.Source
    lines = []
    while s.more():
        lines.append(f"{s.token()} = {s.range(0, 99)}")
    return "\n".join(lines)

stitch.set(doc, "count", 1)
stitch.get(doc, "count", 0)
stitch.get_obj(doc, "parent")      # a linked object, or None
```

## Coverage and comparisons

While fuzzing, modules in `include` (and the harness) are compiled through an import hook that
traces `==`, `!=`, `in`, `<`..., and `sys.monitoring` records lines and branches. A location
reports the first time the worker reaches it (events are re-armed after a program that ended in
a finding, and every 256 programs): that is what the fuzzer needs to see new coverage, at a
fraction of the cost. `STITCH_PY_COVERAGE=full` re-arms them for every program. `stitch cov` uses
coverage.py.

## Reproducers

`stitch write c1` writes `repro.py`: the harness code followed by `main()`, which calls each stub
with its recorded arguments, and `stitch.py`, a stand-in for the runtime. Shrink it to plain
library code; `stitch try min.py` compares a plain Python traceback with the original crash.
