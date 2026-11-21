# swiftsoup: Unexpectedly found nil while unwrapping an Optional value in Element.wrap

| | |
|---|---|
| Kind | `Unexpectedly found nil while unwrapping an Optional value` |
| Location | `Element.wrap Element.swift`, `closure #1 in variable initialization expression of wrap stubs.swift:137:18`, `partial apply for closure #1 in endpoint<A, B> ` |
| Library source | [https://github.com/scinfu/SwiftSoup](https://github.com/scinfu/SwiftSoup) @ `774dc9c72130` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `wrap-force-unwrap` |

## Call sequence (5 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse(html=Str[22,decoded]"<textarea value=\"d\x00\"/>")
#1 body(doc=o0.0)
#2 child(el=o1.0, s=Src[8]7e21f489df8ecc97)
#3 set_attr(el=o2.0, key=Str[13]"\xabT%\xdc\xb9\xbe\x80\xf9N4\xd8SL", value=Str[32]"\x15U\xb7\xfa\x9bB\x9eS0\xac\xb3\x14e\xacg*g\x99\xca\xda-\xf9\xe3g~\xf5X\xe9h\xdck\xd9")
#4 wrap(el=o2.0, html=Str[8,decoded]"</style>")
```

## Stubs used

What each call does (from `project/harness/`):

`parse` · harness/stubs.swift

```swift
let parse = endpoint("parse") { (html: Str<2048, Html>) throws -> New<Doc> in
    New(try SwiftSoup.parse(html.value))
}
```

`body` · harness/stubs.swift

```swift
let body = endpoint("body") { (doc: Borrow<Doc>) throws -> New<Elem>? in
    doc.value.body().map { New($0) }
}
```

`child` · harness/stubs.swift

```swift
let child = endpoint("child") { (el: Borrow<Elem>, s: Choices<8>) throws -> New<Elem>? in
    let kids = el.value.children().array()
    return kids.isEmpty ? nil : New(s.source.pick(kids))
}
```

`set_attr` · harness/stubs.swift

```swift
let setAttr = endpoint("set_attr") { (el: Borrow<Elem>, key: Str<16, Plain>, value: Str<32, Plain>) throws in
    try el.value.attr(key.value, value.value)
    try check(el.value.hasAttr(key.value) || key.value.isEmpty, "a set attribute is present")
}
```

`wrap` · harness/stubs.swift

```swift
let wrap = endpoint("wrap") { (el: Borrow<Elem>, html: Str<128, Html>) throws in
    try el.value.wrap(html.value)
}
```

## Report

```
==12==ERROR: AddressSanitizer: TRAP on unknown address 0xaaaacd2dc4c8 (pc 0xaaaacd2dc4c8 bp 0xffffd8497bb0 sp 0xffffd8497bb0 T0)
    #0 0xaaaacd2dc4c8 in Swift runtime failure: Unexpectedly found nil while unwrapping an Optional value /src/Sources/Element.swift
    #1 0xaaaacd2dc4c8 in Element.wrap(_:) /src/Sources/Element.swift
    #2 0xaaaacd411400 in closure #1 in variable initialization expression of wrap .stitch/build/app/pkg/Sources/worker/stubs.swift:137:18
    #3 0xaaaacd2798d8 in closure #1 in endpoint<A, B>(_:file:_:) /stitch/swift/Stitch/Sources/Stitch/Endpoints.swift:84:81
    #4 0xaaaacd27633c in partial apply for closure #1 in endpoint<A, B>(_:file:_:) Endpoints.swift.o
    #5 0xaaaacd279aa8 in partial apply for closure #1 in endpoint<A, B>(_:file:_:) Endpoints.swift.o
    #6 0xaaaacd297428 in thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #7 0xaaaacd29e95c in partial apply for thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #8 0xaaaacd296a48 in closure #1 in static Runtime.setup() /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:475:42
    #9 0xaaaacd41d428 in void (anonymous namespace)::invoke<12>(stitch::detail::Ctx&) /stitch/swift/bridge/bridge.cpp:47:25
    #14 0xaaaacd297be0 in stitchMain(argNames:_:) /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:543:12
    #15 0xaaaacd408b94 in worker_main .stitch/build/app/pkg/Sources/worker/main.swift:7:6

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH swift toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

