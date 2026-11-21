# swiftsoup: Range requires lowerBound <= upperBound in String.indexOf

| | |
|---|---|
| Kind | `Range requires lowerBound <= upperBound` |
| Location | `String.indexOf String.swift:65:32`, `TokenQueue.consumeTo TokenQueue.swift:194:28`, `TokenQueue.chompTo TokenQueue.swift:257:20` |
| Library source | [https://github.com/scinfu/SwiftSoup](https://github.com/scinfu/SwiftSoup) @ `774dc9c72130` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `select-range-trap` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse(html=Str[18,decoded]"javascript:http://")
#1 select(doc=o0.0, q=Str[8,decoded]"uf8|:lt(")
```

## Stubs used

What each call does (from `project/harness/`):

`parse` · harness/stubs.swift

```swift
let parse = endpoint("parse") { (html: Str<2048, Html>) throws -> New<Doc> in
    New(try SwiftSoup.parse(html.value))
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

## Report

```
==13==ERROR: AddressSanitizer: TRAP on unknown address 0xaaaac7fd7910 (pc 0xaaaac7fd7910 bp 0xfffff1cf3750 sp 0xfffff1cf3730 T0)
    #0 0xaaaac7fd7910 in Swift runtime failure: Range requires lowerBound <= upperBound /<compiler-generated>
    #1 0xaaaac7fd7910 in String.indexOf(_:_:) /src/Sources/String.swift:65:32
    #2 0xaaaac7ffb22c in TokenQueue.consumeTo(_:) /src/Sources/TokenQueue.swift:194:28
    #3 0xaaaac7ffc3f8 in TokenQueue.chompTo(_:) /src/Sources/TokenQueue.swift:257:20
    #4 0xaaaac7fcc394 in QueryParser.consumeIndex() /src/Sources/QueryParser.swift:275:33
    #5 0xaaaac7fc90b4 in QueryParser.indexLessThan() /src/Sources/QueryParser.swift:221:50
    #6 0xaaaac7fc2ec4 in QueryParser.findElements() /src/Sources/QueryParser.swift:145:218
    #7 0xaaaac7fc0050 in QueryParser.parse() /src/Sources/QueryParser.swift:64:21
    #8 0xaaaac7f0620c in static QueryParser.parse(_:) /src/Sources/QueryParser.swift:38:22
    #9 0xaaaac7f0620c in CssSelector.init(_:_:) /src/Sources/CssSelector.swift:86:42
    #10 0xaaaac7f200a8 in CssSelector.__allocating_init(_:_:) /src/Sources/CssSelector.swift
    #11 0xaaaac7f200a8 in static CssSelector.select(_:_:) /src/Sources/CssSelector.swift:105:20
    #12 0xaaaac7f200a8 in Element.select(_:) /src/Sources/Element.swift:277:32
    #13 0xaaaac804ed6c in closure #1 in variable initialization expression of select .stitch/build/app/pkg/Sources/worker/stubs.swift:95:29
    #14 0xaaaac7eb8d4c in closure #1 in endpoint<A, B, C>(_:file:_:) /stitch/swift/Stitch/Sources/Stitch/Endpoints.swift:76:85
    #15 0xaaaac7eb63c8 in partial apply for closure #1 in endpoint<A, B, C>(_:file:_:) Endpoints.swift.o
    #16 0xaaaac7eb8fec in partial apply for closure #1 in endpoint<A, B, C>(_:file:_:) Endpoints.swift.o
    #17 0xaaaac7ed7428 in thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #18 0xaaaac7ede95c in partial apply for thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #19 0xaaaac7ed6a48 in closure #1 in static Runtime.setup() /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:475:42
    #20 0xaaaac805cde8 in void (anonymous namespace)::invoke<4>(stitch::detail::Ctx&) /stitch/swift/bridge/bridge.cpp:47:25
    #25 0xaaaac7ed7be0 in stitchMain(argNames:_:) /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:543:12
    #26 0xaaaac8048b94 in worker_main .stitch/build/app/pkg/Sources/worker/main.swift:7:6

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH swift toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

