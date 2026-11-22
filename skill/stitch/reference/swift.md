# Swift harnesses

Stubs are closures registered with `endpoint`. The closure's argument and return types are the
signature. Swift 6.2 builds the harness and the library (a Swift package) with AddressSanitizer
and coverage instrumentation, and the fuzzer runs the stubs in its own process.

## Project setup

```sh
stitch init swiftsoup --lang swift --git https://github.com/scinfu/SwiftSoup --rev 2.4.3
cd swiftsoup
```

`Dockerfile`: put the package source at `/src`. The harness package depends on it, and
`stitch build` compiles both.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
```

`stitch.toml`: the harness package's dependencies and the products it imports, as SwiftPM
expressions. A path dependency on `/src` is named `src`.

```toml
[project]
name = "swiftsoup"
language = "swift"

[source]
git = "https://github.com/scinfu/SwiftSoup"
rev = "2.4.3"

[swift]
dependencies = ['.package(path: "/src")']
products = ['.product(name: "SwiftSoup", package: "src")']
# swiftc_flags = ["-Xcc", "-DFOO"]
# build_jobs = 2

[fuzz]
timeout_ms = 2000
```

## Files

```
harness/
  stubs.swift          every harness/**/*.swift file is compiled into one executable target
  data/HtmlText/       seeds/, *.dict for the HtmlText data type
```

## A complete example

Each stub is a global `let NAME = endpoint("name") { ... }`. The build finds them by that shape
and reads the closure's argument names.

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
    New(try SwiftSoup.parse(html.value))                    // a thrown error is a bail
}

let select = endpoint("select") { (doc: Borrow<Doc>, query: Str<64, Plain>) throws in
    _ = try doc.value.select(query.value)
}

let removeChild = endpoint("remove_child") { (doc: Borrow<Doc>, s: Choices<8>) throws in
    let kids = doc.value.body()?.children().array() ?? []
    try require(!kids.isEmpty, "no children")
    try s.source.pick(kids).remove()                        // pick an existing element
}

let reparse = endpoint("reparse") { (doc: Borrow<Doc>) throws in
    let again = try SwiftSoup.parse(try doc.value.outerHtml())
    try check(again.body() != nil, "a reparsed document has a body")   // a bug if false
}
```

## Objects

```swift
enum Doc: StitchObject { typealias Handle = Document }      // a class
enum Buf: StitchObject { typealias Handle = [UInt8] }       // a value type works too
```

| Type | Meaning |
|---|---|
| `Borrow<T>` | uses an object; it stays available. `.value` is the handle (mutable) |
| `Take<T>` | consumes the object. `.value` is the handle |
| `x.consume()` | on a `Borrow`: the library took ownership in this call |
| `-> New<T>` | produces an object: `New(handle)` |
| `-> New<T>?` | optional output: return `nil` for no object |
| `-> New2<A, B>` | two outputs, each optional: `New2(New(a), nil)` |
| no return type | produces nothing |

A `Borrow`'s `.value` is stored in a box, so mutating a value-type handle changes the stored
object:

```swift
let append = endpoint("append") { (b: Borrow<Buf>, x: UInt8) in
    b.value.append(x)
}
```

```swift
enum Elem: StitchObject { typealias Handle = Element }

let parseBody = endpoint("parse_body") { (html: Str<4096, HtmlText>) throws -> New2<New<Doc>, New<Elem>> in
    let doc = try SwiftSoup.parse(html.value)
    return New2(New(doc), doc.body().map { New($0) })
}

let detach = endpoint("detach") { (e: Take<Elem>) throws in
    try e.value.remove()
}
```

A stub takes at most 12 arguments. Group more values in a struct returned by a decoder (`Val`).

## Fuzzed parameters

| Type | Value |
|---|---|
| `Int8`..`Int64`, `UInt8`..`UInt64`, `Int`, `UInt` | integer |
| `Float`, `Double`, `Bool` | float, bool |
| `Between<lo, hi>` | Int in lo...hi (`.value`) |
| `Pick<E>` | a case of a `CaseIterable` enum (`.value`) |
| `Str<max, T>` | String of at most `max` bytes, invalid UTF-8 replaced (`.value`); `T` is a data type or `Plain` |
| `Bytes<max, T>` | `[UInt8]` (`.value`) |
| `FilePath<max, T>` | path of a temporary file holding the data (`.path`) |
| `Val<T, max>` | the value `T`'s decoder returns, any type (`.value`) |
| `Choices<max>` | a `Source` for choices inside the stub (`.source`) |

```swift
enum Part: CaseIterable { case head, body }

let childText = endpoint("child_text") { (doc: Borrow<Doc>, part: Pick<Part>, i: Between<0, 8>) throws in
    let el = part.value == .head ? doc.value.head() : doc.value.body()
    let kids = el?.children().array() ?? []
    try require(i.value < kids.count, "no such child")
    _ = try kids[i.value].text()
}
```

## Bail, require, check

```swift
throw Bail("reason")                 // stop this call; not a bug
try require(cond, "reason")          // bail unless cond
try check(cond, "what")              // a bug if false: "check failed: what"
log("n=\(n)")                        // printed by `stitch exec --trace`
let setup = stitchInit { configureLibrary() }   // runs once per process
```

A thrown Swift error is a bail: it is how Swift APIs report invalid input.

These are bugs: traps (out-of-bounds index, integer overflow, force-unwrapping nil, failed
`precondition`, `fatalError`), AddressSanitizer reports (unsafe pointers, C dependencies), a
failed `check`, and programs that run past `timeout_ms`. The crash kind is the runtime's message,
such as "Index out of range".

## Attributes

`Borrow`, `Take` and `New` values store integers and strings:

```swift
let addChild = endpoint("add_child") { (doc: Borrow<Doc>, tag: Str<16, Plain>) throws in
    try doc.value.body()?.appendElement(tag.value)
    doc.set("children", (doc.int("children") ?? 0) + 1)
}
```

| Method | Does |
|---|---|
| `x.set(key, Int)`, `x.set(key, String)` | store a value |
| `x.int(key)`, `x.string(key)` | read it; `nil` and reported by `stitch probe` if missing |
| `x.has(key)`, `x.delete(key)` | test, remove |
| `globalSet(key, Int)`, `globalGet(key)` | program-wide integers, reset for every program |

Use `x.has(key)` first when a missing key is normal, so `stitch probe` does not report it.

## Data types and decoders

Tag a `Str`, `Bytes` or `FilePath` parameter with a data type, and create
`harness/data/<Name>/` with `seeds/` (sample inputs) and `*.dict` files (one quoted token per
line). Declare the type in Swift:

```swift
enum HtmlText: Datatype {}                    // seeds and tokens only
enum HtmlText: Decoded {                      // with a decoder returning a String
    static func decode(_ s: Source) -> String { ... }
}
enum Der: DecodedBytes {                      // with a decoder returning bytes
    static func decode(_ s: Source) -> [UInt8] { ... }
}
```

`stitch sample HtmlText -n 5` prints values the decoder makes.

A `Decoded` type can return any type. Take it as `Val<T, max>`, and build decoders on each other
with `s.draw(T.self)`:

```swift
enum Pair: Decoded {
    static func decode(_ s: Source) -> (String, String) {
        let key = s.coin(0.3) ? s.token() : s.str(8)
        return (key, s.str(12))
    }
}

enum Pairs: Decoded {
    static func decode(_ s: Source) -> [(String, String)] {
        var out: [(String, String)] = []
        while s.more() { out.append(s.draw(Pair.self)) }
        return out
    }
}

let putAll = endpoint("put_all") { (st: Borrow<Store>, kvs: Val<Pairs, 256>) throws in
    for (k, v) in kvs.value { try st.value.put(k, v) }
}
```

`Source` methods:

| Method | Returns |
|---|---|
| `u8()` .. `u64()`, `i8()` .. `i64()`, `f32()`, `f64()` | a number |
| `range(lo, hi)` | integer in lo...hi |
| `choice(n)` | index in 0..<n |
| `pick(array)` | one element |
| `coin(p)` | true with probability about p (default 0.5) |
| `more()` | loop condition: `while s.more() { ... }` |
| `f64(lo, hi)`, `f32(lo, hi)` | finite float in lo...hi |
| `str(max)`, `bytes(max)` | text or bytes of at most `max` bytes |
| `fixed(n)` | exactly n bytes |
| `rest()` | every byte that is left |
| `token()` | often a token from the data type's `.dict` files; otherwise (and once mutated) any string, like `str`: check it when the input must be one of the tokens (or use `pick`) |
| `draw(T.self)` | a value from `T`'s decoder |

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/repro.swift`, a `main.swift` for the harness
package that calls each stub's body with its recorded arguments:

```swift
    // #0 parse(html)
    let doc0 = obj(try call(0, "parse") { try parse.body(Str("<p>a")) })
    // #1 select(doc=doc0, query)
    _ = try call(1, "select") { try select.body(doc0.borrow, Str("p")) }
```

Shrink a copy to plain library code and check it after each edit:

```sh
cp .stitch/standalone/c1/repro.swift .stitch/standalone/c1/min.swift
stitch try .stitch/standalone/c1/min.swift
```

A finished reproducer:

```swift
import Yams

_ = try? Yams.load(yaml: "{!!: [int, x]}")    // force-unwraps nil inside the constructor
```

`stitch try` builds the file in the harness package and compares the trap or ASan report with the
original crash. Details: `stitch guide reproducers`.

## Coverage

`stitch cov` builds the package again with Swift's source-based coverage (no sanitizers) and
replays the corpus with llvm-cov.
