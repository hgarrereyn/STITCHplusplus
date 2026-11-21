# Swift harnesses

Stubs are closures registered with `endpoint`. The closure's argument and return types are the
signature. Swift 6.2 builds the harness and the library (a Swift package) with AddressSanitizer
and SanitizerCoverage; the fuzzer runs them in-process, on the C/C++ runtime.

## Project

```sh
stitch init swiftsoup --lang swift --git https://github.com/scinfu/SwiftSoup --rev 2.4.3
```

```toml
# stitch.toml
[project]
language = "swift"

[swift]
dependencies = ['.package(path: "/src")']                          # SwiftPM dependency expressions
products = ['.product(name: "SwiftSoup", package: "src")']         # what the harness imports
```

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
```

## Stubs

Every `harness/*.swift` file is part of the harness package's executable target. Each stub is a
global `let NAME = endpoint("name") { ... }` (stitch-build registers them and reads the argument
names from the closure).

```swift
import Stitch
import SwiftSoup

enum Doc: StitchObject { typealias Handle = Document }      // object type
enum HtmlText: Decoded {                                    // data type with a decoder
    static func decode(_ s: Source) -> String {
        var out = ""
        while s.more() { out += "<\(s.token())>\(s.str(8))" }
        return out
    }
}

let parse = endpoint("parse") { (html: Str<4096, HtmlText>) throws -> New<Doc> in
    New(try SwiftSoup.parse(html.value))
}

let select = endpoint("select") { (doc: Borrow<Doc>, query: Str<64, Plain>) throws in
    _ = try doc.value.select(query.value)
}

let removeChild = endpoint("remove_child") { (doc: Borrow<Doc>, s: Choices<8>) throws in
    let kids = doc.value.body()?.children().array() ?? []
    try require(!kids.isEmpty, "no children")
    try s.source.pick(kids).remove()
}
```

| Type | Value |
|---|---|
| `Borrow<T>` | an object (`.value`, mutable); stays available. `.consume()` if the library took it |
| `Take<T>` | an object the call consumes |
| `-> New<T>` | returns an object: `New(handle)` |
| `-> New<T>?` | optional output |
| `-> New2<A, B>` | two outputs |
| `Int8`..`Int64`, `UInt8`..`UInt64`, `Int`, `UInt` | integers |
| `Float`, `Double`, `Bool` | |
| `Between<lo, hi>` | Int in lo...hi (`.value`) |
| `Pick<E>` | a case of a `CaseIterable` enum (`.value`) |
| `Str<max, T>` | String (UTF-8, invalid bytes replaced); `T` a data type or `Plain` |
| `Bytes<max, T>` | `[UInt8]` |
| `FilePath<max, T>` | path of a temporary file (`.path`) |
| `Choices<max>` | a `Source` for choices inside the stub (`.source`) |

An object type is `enum T: StitchObject { typealias Handle = H }`; handles may be classes or
values (a `Borrow`'s `.value` is stored in a box, so mutations of value types stay).

## Bugs

Traps are bugs: out-of-bounds indexing, integer overflow, force-unwrapping nil, failed
preconditions, `fatalError`, ...; so are AddressSanitizer reports (unsafe pointers, C
dependencies) and a failed `try check(cond, "what")`. The bucket kind is the runtime's message
("Index out of range"). A thrown Swift error is a bail: it is how the API reports failures.
`throw Bail("reason")` and `try require(cond, "reason")` stop the call.

## Attributes

`Borrow`, `Take` and `New` values have `set(key, Int | String)`, `int(key)`, `string(key)`,
`has(key)`, `delete(key)`; `globalSet` / `globalGet` for program-wide state.

## Coverage and comparisons

The package and its Swift and C dependencies are compiled with
`-sanitize-coverage=edge,trace-pc-guard,trace-cmp` and ASan, and linked with the C/C++ runtime
and the engine library: fuzzing runs in a persistent fork server, like C/C++. `stitch cov`
builds the package again with `-profile-generate -profile-coverage-mapping` (llvm-cov).

## Reproducers

`stitch write c1` writes `repro.swift`, a main.swift for the harness package that calls each
stub's body with its recorded arguments (`try call(n, "name") { try parse.body(Str("...")) }`).
Shrink it; `stitch try min.swift` builds it in the harness package and compares the report with
the original crash.
