# sqlparse: IndexError in StripWhitespaceFilter._stripws_parenthesis

| | |
|---|---|
| Kind | `IndexError` |
| Location | `StripWhitespaceFilter._stripws_parenthesis others.py:90`, `StripWhitespaceFilter._stripws others.py:63`, `StripWhitespaceFilter.process others.py:98` |
| Library source | [https://github.com/andialbrecht/sqlparse](https://github.com/andialbrecht/sqlparse) @ `fba15d34a017` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `sqlparse-c11` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 fmt_strip_whitespace(text=Str[4]"(AS)")
```

## Stubs used

What each call does (from `project/harness/`):

`fmt_strip_whitespace` · harness/stubs.py

```python
@stitch.endpoint
def fmt_strip_whitespace(text: Str[1024, SqlText]):
    out = sqlparse.format(text, strip_whitespace=True)
    stitch.check(sqlparse.format(out, strip_whitespace=True) == out, "strip_whitespace idempotent")
```

## Report

```
[stitch] EXCEPTION in fmt_strip_whitespace: IndexError: list index out of range
    #0 0x0 in StripWhitespaceFilter._stripws_parenthesis /opt/venv/lib/python3.12/site-packages/sqlparse/filters/others.py:90
    #1 0x0 in StripWhitespaceFilter._stripws /opt/venv/lib/python3.12/site-packages/sqlparse/filters/others.py:63
    #2 0x0 in StripWhitespaceFilter.process /opt/venv/lib/python3.12/site-packages/sqlparse/filters/others.py:98
    #3 0x0 in StripWhitespaceFilter.process /opt/venv/lib/python3.12/site-packages/sqlparse/filters/others.py:97
    #4 0x0 in FilterStack.run /opt/venv/lib/python3.12/site-packages/sqlparse/engine/filter_stack.py:39
    #5 0x0 in format /opt/venv/lib/python3.12/site-packages/sqlparse/__init__.py:59
    #6 0x0 in fmt_strip_whitespace .stitch/build/app/harness/stubs.py:82
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH++ python toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

