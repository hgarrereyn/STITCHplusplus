# org-json: ClassCastException in JSONML.toJSONArray

| | |
|---|---|
| Kind | `ClassCastException` |
| Location | `JSONML.toJSONArray JSONML.java:257`, `Stubs.fromJsonML (stub)` |
| Library source | [https://github.com/stleary/JSON-java](https://github.com/stleary/JSON-java) @ `47fb49b6a871` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `jsonml-classcast` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 fromJsonML(text=Str[4,decoded]"</a>")
```

## Stubs used

What each call does (from `project/harness/`):

`fromJsonML` · harness/Stubs.java

```java
    @Endpoint(bailsOn = {JSONException.class})
    public static JSONArray fromJsonML(@Str(max = 4096, type = "XmlText") String text) {
        JSONArray a = JSONML.toJSONArray(text);
        JSONML.toString(a);
        return a;
    }
```

## Report

```
[stitch] EXCEPTION in fromJsonML: ClassCastException: class java.lang.String cannot be cast to class org.json.JSONArray (java.lang.String is in module java.base of loader 'bootstrap'; org.json.JSONArray is in unnamed module of loader 'app')
    #0 0x0 in JSONML.toJSONArray /org/json/JSONML.java:257
    #1 0x0 in Stubs.fromJsonML harness/Stubs.java:101
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH java toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

