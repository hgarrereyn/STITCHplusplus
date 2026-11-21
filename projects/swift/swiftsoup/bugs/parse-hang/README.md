# swiftsoup: timeout in parse

| | |
|---|---|
| Kind | `timeout` |
| Location | – |
| Library source | [https://github.com/scinfu/SwiftSoup](https://github.com/scinfu/SwiftSoup) @ `774dc9c72130` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `parse-hang` |

## Call sequence (4 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse_fragment(html=Str[263,decoded]"<title>vbscript:< dtp=\"E4*3tcK1w}\" Uh},ELfHMj|Hn|]rOPw;8_l1&C4d=Ov_:h:qVM#/WLyOR`f<%zm=yA}NVh <*6a4Vo-VYr_]/CB%%oRjI=@w_tR0sW-:Q4}g0ahv,clFq8t,oUQDX.;lbA/`>lk3+"...)
#1 select(doc=o0.0, q=Str[2,decoded]"td")
#2 roundtrip(doc=o0.0)
#3 parse(html=Str[52]"pa\x1b\xeb\xbf\xbd;#########\xef\xbf\xbd\x11\xef\xbf\xbd`rea><!DOCTYPE html><!DOCTYPE")
```

## Stubs used

What each call does (from `project/harness/`):

`parse_fragment` · harness/stubs.swift

```swift
let parseFragment = endpoint("parse_fragment") { (html: Str<2048, Html>) throws -> New<Doc> in
    New(try SwiftSoup.parseBodyFragment(html.value))
}
```

`select` · harness/stubs.swift

```swift
let select = endpoint("select") { (doc: Borrow<Doc>, q: Str<64, Selector>) throws -> New<Elem>? in
    let els = try doc.value.select(q.value)
    for e in els.array() {
        try check(e.ownerDocument() === doc.value, "selected elements belong to the document")
    }
    return els.first().map { New($0) }
}
```

`roundtrip` · harness/stubs.swift

```swift
// html() parses again and prints the same
let roundtrip = endpoint("roundtrip") { (doc: Borrow<Doc>) throws in
    let a = try doc.value.html()
    let b = try SwiftSoup.parse(a).html()
    log("html: \(a.debugDescription)")
    try check(try SwiftSoup.parse(b).html() == b, "parse(html()) is stable")
}
```

`parse` · harness/stubs.swift

```swift
let parse = endpoint("parse") { (html: Str<2048, Html>) throws -> New<Doc> in
    New(try SwiftSoup.parse(html.value))
}
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH swift toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

