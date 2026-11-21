# jsoup: StringIndexOutOfBoundsException in String.checkBoundsBeginEnd

| | |
|---|---|
| Kind | `StringIndexOutOfBoundsException` |
| Location | `String.checkBoundsBeginEnd String.java:4855`, `String.substring String.java:2823`, `Evaluator$AttributeKeyPair.<init> Evaluator.java:320` |
| Library source | [https://github.com/jhy/jsoup](https://github.com/jhy/jsoup) @ `d9566b534048` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `select-attribute-sioobe` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse(html=Str[0,decoded]"", base="file:/x")
#1 select(doc=o0.0, css=Str[48,decoded]"[C-60Nsj9v<a href&amp;dtable\"\x04\x1bowl=\"]c-9csqc\"]c-", i=2)
```

## Stubs used

What each call does (from `project/harness/`):

`parse` · harness/Stubs.java

```java
    @Endpoint
    public static Document parse(@Str(max = 4096, type = "Html") String html, @OneOf(strings = {"", "http://example.com/", "file:/x"}) String base) {
        return Jsoup.parse(html, base);
    }
```

`select` · harness/Stubs.java

```java
    // an invalid query throws Selector.SelectorParseException (an IllegalStateException in 1.15)
    @Endpoint(optional = true, bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static Element select(Document doc, @Str(max = 64, type = "Css") String css, @Range(lo = 0, hi = 8) int i) {
        Elements els = doc.select(css);
        return els.size() > 0 ? els.get(i % els.size()) : null;
    }
```

## Report

```
[stitch] EXCEPTION in select: StringIndexOutOfBoundsException: Range [1, 0) out of bounds for length 1
    #0 0x0 in String.checkBoundsBeginEnd /java/lang/String.java:4855
    #1 0x0 in String.substring /java/lang/String.java:2823
    #2 0x0 in Evaluator$AttributeKeyPair.<init> /org/jsoup/select/Evaluator.java:320
    #3 0x0 in Evaluator$AttributeKeyPair.<init> /org/jsoup/select/Evaluator.java:309
    #4 0x0 in Evaluator$AttributeWithValue.<init> /org/jsoup/select/Evaluator.java:182
    #5 0x0 in QueryParser.byAttribute /org/jsoup/select/QueryParser.java:278
    #6 0x0 in QueryParser.findElements /org/jsoup/select/QueryParser.java:168
    #7 0x0 in QueryParser.parse /org/jsoup/select/QueryParser.java:62
    #8 0x0 in QueryParser.parse /org/jsoup/select/QueryParser.java:45
    #9 0x0 in Selector.select /org/jsoup/select/Selector.java:98
    #10 0x0 in Element.select /org/jsoup/nodes/Element.java:422
    #11 0x0 in Stubs.select harness/Stubs.java:74
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH java toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

