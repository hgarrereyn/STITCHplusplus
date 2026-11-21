# yams: Unexpectedly found nil while unwrapping an Optional value in closure

| | |
|---|---|
| Kind | `Unexpectedly found nil while unwrapping an Optional value` |
| Location | `closure #1 in static Dictionary._construct_mapping Constructor.swift:426:58`, `implicit closure #1 in variable initialization expression of static Constructor.defaultMappingMap Constructor.swift:87:15`, `Constructor.any Constructor.swift:48:69` |
| Library source | [https://github.com/jpsim/Yams](https://github.com/jpsim/Yams) @ `9ff1cc932758` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `load-force-unwrap` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 compose(text=Str[31,decoded]"%YAML 1.1\n---\n{!!: [int, \xef\xbf\xbd]}\n")
#1 load_all(text=Str[31,decoded]"%YAML 1.1\n---\n{!!: [int, \xef\xbf\xbd]}\n")
```

## Stubs used

What each call does (from `project/harness/`):

`compose` · harness/stubs.swift

```swift
let compose = endpoint("compose") { (text: Str<1024, YamlText>) throws -> New<Doc>? in
    try Yams.compose(yaml: text.value).map { New($0) }
}
```

`load_all` · harness/stubs.swift

```swift
let loadAll = endpoint("load_all") { (text: Str<1024, YamlText>) throws in
    var n = 0
    for _ in try Yams.load_all(yaml: text.value) {
        n += 1
        if n > 64 { break }
    }
}
```

## Report

```
==12==ERROR: AddressSanitizer: TRAP on unknown address 0xaaaacc3369e0 (pc 0xaaaacc3369e0 bp 0xfffff474c920 sp 0xfffff474c680 T0)
    #0 0xaaaacc3369e0 in Swift runtime failure: Unexpectedly found nil while unwrapping an Optional value /<compiler-generated>
    #1 0xaaaacc3369e0 in closure #1 in static Dictionary._construct_mapping(from:) /src/Sources/Yams/Constructor.swift:426:58
    #2 0xaaaacc3369e0 in thunk for @callee_guaranteed (@guaranteed Node, @guaranteed Node) -> (@owned String, @out Any) /<compiler-generated>
    #3 0xaaaacc3369e0 in specialized Collection.map<A, B>(_:) /<compiler-generated>
    #4 0xaaaacc3369e0 in specialized static Dictionary._construct_mapping(from:) /src/Sources/Yams/Constructor.swift:426:21
    #5 0xaaaacc33a460 in specialized static Dictionary.construct_mapping(from:) /src/Sources/Yams/Constructor.swift:417:16
    #6 0xaaaacc33a460 in implicit closure #1 in variable initialization expression of static Constructor.defaultMappingMap /src/Sources/Yams/Constructor.swift:87:15
    #7 0xaaaacc33a460 in specialized thunk for @escaping @callee_guaranteed (@guaranteed Node.Mapping) -> (@owned [AnyHashable : Any]?) /<compiler-generated>
    #8 0xaaaacc33a460 in specialized thunk for @escaping @callee_guaranteed (@guaranteed Node.Mapping) -> (@out Any?) /<compiler-generated>
    #9 0xaaaacc335190 in thunk for @escaping @callee_guaranteed (@in_guaranteed Node.Mapping) -> (@out Any?) /<compiler-generated>
    #10 0xaaaacc335190 in Constructor.any(from:) /src/Sources/Yams/Constructor.swift:48:69
    #11 0xaaaacc451f98 in Node.any.getter /src/Sources/Yams/Node.swift:77:32
    #12 0xaaaacc451f98 in closure #1 in load_all(yaml:_:_:_:) /src/Sources/Yams/Parser.swift:30:50
    #13 0xaaaacc4629ac in partial apply for closure #1 in load_all(yaml:_:_:_:) /<compiler-generated>
    #14 0xaaaacc48bba4 in specialized YamlSequence.next() /<compiler-generated>
    #15 0xaaaacc48bba4 in closure #1 in variable initialization expression of loadAll .stitch/build/app/pkg/Sources/worker/stubs.swift:51:5
    #16 0xaaaacc306c0c in closure #1 in endpoint<A>(_:file:_:) /stitch/swift/Stitch/Sources/Stitch/Endpoints.swift:68:77
    #17 0xaaaacc306d7c in partial apply for closure #1 in endpoint<A>(_:file:_:) /<compiler-generated>
    #18 0xaaaacc325d8c in thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #19 0xaaaacc32d2c0 in partial apply for thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #20 0xaaaacc3253ac in closure #1 in static Runtime.setup() /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:475:42
    #21 0xaaaacc49d358 in void (anonymous namespace)::invoke<2>(stitch::detail::Ctx&) /stitch/swift/bridge/bridge.cpp:47:25
    #26 0xaaaacc326544 in stitchMain(argNames:_:) /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:543:12
    #27 0xaaaacc48797c in worker_main .stitch/build/app/pkg/Sources/worker/main.swift:7:6

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH swift toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

