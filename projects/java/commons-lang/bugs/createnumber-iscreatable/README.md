# commons-lang: check failed: isCreatable() true for what createNumber() parses in createNumber

| | |
|---|---|
| Kind | `check failed: isCreatable() true for what createNumber() parses` |
| Location | – |
| Library source | [https://github.com/apache/commons-lang](https://github.com/apache/commons-lang) @ `105a350b154e` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `createnumber-iscreatable` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 createNumber(text=Str[3,decoded]"07f")
```

## Stubs used

What each call does (from `project/harness/`):

`createNumber` · harness/Stubs.java

```java
    @Endpoint(bailsOn = {NumberFormatException.class})
    public static void createNumber(@Str(max = 48, type = "NumText") String text) {
        Number n = NumberUtils.createNumber(text);
        Stitch.require(n != null, "null for null input");
        Stitch.check(NumberUtils.isCreatable(text), "isCreatable() true for what createNumber() parses");
    }
```

## Report

```
[stitch] CHECK FAILED: isCreatable() true for what createNumber() parses (node 0)
    #0 0x0 in Stubs.createNumber harness/Stubs.java:145
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH java toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

