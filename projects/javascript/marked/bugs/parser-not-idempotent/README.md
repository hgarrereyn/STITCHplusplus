# marked: check failed: parser(tokens) twice gives the same html in check_parser_idempotent

| | |
|---|---|
| Kind | `check failed: parser(tokens) twice gives the same html` |
| Location | – |
| Library source | [https://github.com/markedjs/marked](https://github.com/markedjs/marked) @ `e5171a996d8a` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `parser-not-idempotent` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 check_parser_idempotent(md=Str[9]"* [ ] \n\n*", f=34)
```

## Stubs used

What each call does (from `project/harness/`):

`check_parser_idempotent` · harness/stubs.js

```js
// Parser must not change its input: parsing the same tokens twice gives the same html.
stitch.endpoint("check_parser_idempotent", { md: Str(512, MarkdownText), f: NF }, null, ({ md, f }) => {
  const toks = marked.lexer(md, det(f));
  const a = marked.parser(toks, det(f));
  const b = marked.parser(toks, det(f));
  stitch.check(a === b, "parser(tokens) twice gives the same html");
});
```

## Report

```
[stitch] CHECK FAILED: parser(tokens) twice gives the same html (node 0)
    #0 0x0 in Endpoint.fn .stitch/build/app/harness/stubs.js:158
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH++ javascript toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

