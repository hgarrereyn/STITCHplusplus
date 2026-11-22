// Stubs: one `let NAME = endpoint("name") { ... }` per API call. The closure's types are the spec.
//
// Borrow<T> uses an object, Take<T> consumes it, New<T> (returned) produces one.
// Every other argument is fuzzed: Str<64, Plain>, Bytes<256, Plain>, Int32, Between<0, 9>, Pick<E>, ...
// A thrown error is a bail; traps (bounds, overflow, nil unwrap), ASan reports and failed checks are bugs.
import Stitch

// import Foo
// enum Doc: StitchObject { typealias Handle = Foo.Document }
//
// let parse = endpoint("parse") { (text: Str<4096, Plain>) throws -> New<Doc> in
//     New(try Foo.parse(text.value))
// }
//
// let render = endpoint("render") { (doc: Borrow<Doc>) in
//     _ = doc.value.render()
// }
