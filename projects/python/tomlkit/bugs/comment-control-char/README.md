# tomlkit: check failed: printed document parses (InvalidControlChar) in roundtrip

| | |
|---|---|
| Kind | `check failed: printed document parses (InvalidControlChar)` |
| Location | – |
| Library source | [https://github.com/python-poetry/tomlkit](https://github.com/python-poetry/tomlkit) @ `e6e5d3803ba2` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `comment-control-char` |

## Call sequence (4 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse(text=Str[0,decoded]"")
#1 add_table(doc=o0.0, key=Str[0]"", super_table=false)
#2 table_comment(t=o1.0, text=Str[1]"\x14")
#3 roundtrip(doc=o0.0)
```

## Stubs used

What each call does (from `project/harness/`):

`parse` · harness/stubs.py

```python
@stitch.endpoint(bails_on=(TOMLKitError,))
def parse(text: Str[2048, TomlText]) -> New[Doc]:
    doc = tomlkit.parse(text)
    stitch.check(doc.as_string() == text, "parse/print preserves the text")
    return doc
```

`add_table` · harness/stubs.py

```python
@stitch.endpoint(bails_on=(TOMLKitError,))
def add_table(doc: Borrow[Doc], key: Str[16, TomlKey], super_table: Bool) -> New[Table]:
    doc.add(key, tomlkit.table(is_super_table=super_table))
    return doc[key]
```

`table_comment` · harness/stubs.py

```python
@stitch.endpoint(bails_on=(TOMLKitError,))
def table_comment(t: Borrow[Table], text: Str[16]):
    stitch.require(hasattr(t, "comment"), "no comment() on this table type")
    t.comment(text)
```

`roundtrip` · harness/stubs.py

```python
@stitch.endpoint
def roundtrip(doc: Borrow[Doc]):
    text = doc.as_string()
    stitch.log("printed: %r", text)
    try:
        again = tomlkit.parse(text)
    except TOMLKitError as e:
        stitch.check(False, f"printed document parses ({type(e).__name__})")
        return
    stitch.check(same(again.unwrap(), doc.unwrap()), "printed document has the same data")
```

## Report

```
[stitch] CHECK FAILED: printed document parses (InvalidControlChar) (node 3)
    #0 0x0 in roundtrip .stitch/build/app/harness/stubs.py:209
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH python toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

