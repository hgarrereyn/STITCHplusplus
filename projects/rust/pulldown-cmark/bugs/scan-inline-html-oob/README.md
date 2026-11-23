# pulldown-cmark: range start index N out of range for slice of length N in pulldown_cmark::parse::Parser::scan_inline_html

| | |
|---|---|
| Kind | `range start index N out of range for slice of length N` |
| Location | `pulldown_cmark::parse::Parser::scan_inline_html parse.rs:2762`, `pulldown_cmark::parse::Parser::handle_inline_pass1::{{closure}} parse.rs:2158`, `pulldown_cmark::parse::Parser::handle_inline_pass1 parse.rs:2157` |
| Library source | [https://github.com/pulldown-cmark/pulldown-cmark](https://github.com/pulldown-cmark/pulldown-cmark) @ `d4bf0872b14f` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `pulldown-cmark-c2` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 new_doc_raw(text=Str[4]"><a\x0d")
#1 render_html(d=o0.0)
```

## Stubs used

What each call does (from `project/harness/`):

`new_doc_raw` · harness/stubs.rs

```rust
    fn new_doc_raw(text: Str<256>) -> New<Doc> {
        New::new(DocT { text: text.to_string(), opts: Options::empty() })
    }
```

`render_html` · harness/stubs.rs

```rust
    fn render_html(d: Borrow<Doc>) {
        let a = render(&d.text, d.opts);
        let b = render(&d.text, d.opts);
        check(a == b, "render is deterministic");
    }
```

## Report

```
[stitch] EXCEPTION in render_html: range start index N out of range for slice of length N: range start index 4 out of range for slice of length 3
    #0 0x0 in pulldown_cmark::parse::Parser::scan_inline_html /src/src/parse.rs:2762
    #1 0x0 in pulldown_cmark::parse::Parser::handle_inline_pass1::{{closure}} /src/src/parse.rs:2158
    #2 0x0 in pulldown_cmark::parse::Parser::handle_inline_pass1 /src/src/parse.rs:2157
    #3 0x0 in pulldown_cmark::parse::Parser::handle_inline /src/src/parse.rs:2111
    #4 0x0 in <pulldown_cmark::parse::Parser as core::iter::traits::iterator::Iterator>::next /src/src/parse.rs:3046
    #5 0x0 in pulldown_cmark::html::HtmlWriter<I,W>::run /src/src/html.rs:87
    #6 0x0 in pulldown_cmark::html::push_html /src/src/html.rs:419
    #7 0x0 in stitch_harness::stubs::render harness/stubs.rs:35
    #8 0x0 in stitch_harness::stubs::render_html harness/stubs.rs:93
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH++ rust toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

