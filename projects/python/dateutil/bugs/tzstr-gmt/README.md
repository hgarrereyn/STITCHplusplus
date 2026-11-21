# dateutil: TypeError in tzstr.__init__

| | |
|---|---|
| Kind | `TypeError` |
| Location | `tzstr.__init__ tz.py:1092`, `_TzFactory.instance _factories.py:22`, `_TzStrFactory.__call__ _factories.py:69` |
| Library source | [https://github.com/dateutil/dateutil](https://github.com/dateutil/dateutil) @ `db9d018944c4` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `tzstr-gmt` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 tzstr(text=Str[3]"GMT")
```

## Stubs used

What each call does (from `project/harness/`):

`tzstr` · harness/stubs.py

```python
@stitch.endpoint(bails_on=(ValueError,))
def tzstr(text: Str[48]) -> New[Zone]:
    return tz.tzstr(text)
```

## Report

```
[stitch] EXCEPTION in tzstr: TypeError: unsupported operand type(s) for *=: 'NoneType' and 'int'
    #0 0x0 in tzstr.__init__ /opt/venv/lib/python3.12/site-packages/dateutil/tz/tz.py:1092
    #1 0x0 in _TzFactory.instance /opt/venv/lib/python3.12/site-packages/dateutil/tz/_factories.py:22
    #2 0x0 in _TzStrFactory.__call__ /opt/venv/lib/python3.12/site-packages/dateutil/tz/_factories.py:69
    #3 0x0 in tzstr .stitch/build/app/harness/stubs.py:176
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH python toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

