// STITCH harness API for Swift.
//
//     import Stitch
//     import SwiftSoup
//
//     enum Doc: StitchObject { typealias Handle = Document }
//     enum HtmlText: Decoded { static func decode(_ s: Source) -> String { ... } }
//
//     let parse = endpoint("parse") { (html: Str<4096, HtmlText>) throws -> New<Doc> in
//         New(try SwiftSoup.parse(html.value))
//     }
//     let title = endpoint("title") { (doc: Borrow<Doc>) in
//         _ = try? doc.value.title()
//     }
//
// Borrow<T> uses an object, Take<T> consumes it, New<T> (returned) produces one. Other arguments
// are fuzzed. A thrown error is a bail (the API reported a failure); bugs are traps (bounds,
// overflow, force unwrap, precondition), sanitizer reports and failed checks.
// Reference: `stitch guide swift`.
import CStitch

#if canImport(Glibc)
import Glibc
#endif

// MARK: - control

/// Thrown to stop this call; not a bug. The program continues. `throw Bail("reason")`
public struct Bail: Error {
    public let reason: String
    public init(_ reason: String = "bail") { self.reason = reason }
}

/// A failed oracle (`check`).
public struct CheckFailed: Error {
    public let what: String
}

/// Bail unless `cond` holds.
public func require(_ cond: Bool, _ reason: String = "requirement failed") throws {
    if !cond { throw Bail(reason) }
}

/// An oracle: a false condition is reported as a bug ("check failed: what").
public func check(_ cond: Bool, _ what: String = "check") throws {
    if !cond { throw CheckFailed(what: what) }
}

/// Printed by `stitch exec --trace`.
public func log(_ msg: @autoclosure () -> String) {
    if stitch_c_tracing() != 0 { stitch_c_log(msg()) }
}

public func globalSet(_ key: String, _ v: Int) { stitch_c_global_set_int(key, Int64(v)) }
public func globalGet(_ key: String) -> Int? {
    var v: Int64 = 0
    if stitch_c_global_get_int(key, &v) != 0 { return Int(v) }
    stitch_c_note_missing(key)
    return nil
}

// MARK: - objects

/// An object type: `enum Doc: StitchObject { typealias Handle = Document }`.
public protocol StitchObject {
    associatedtype Handle
}

/// Holds an object's handle (values and references alike), so it has an identity.
public final class Box<H> {
    public var value: H
    init(_ v: H) { value = v }
}

enum Objects {
    nonisolated(unsafe) static var ids: [ObjectIdentifier: Int32] = [:]
    static func id<T: StitchObject>(_ t: T.Type) -> Int32 {
        Runtime.setup()
        if let i = ids[ObjectIdentifier(t)] { return i }
        let i = stitch_c_object(String(describing: t))
        ids[ObjectIdentifier(t)] = i
        return i
    }
}

/// Attributes: state attached to an object (`stitch probe` reports keys read before they are set).
public protocol Attributes {
    var pointer: UnsafeMutableRawPointer { get }
}

extension Attributes {
    public func set(_ key: String, _ v: Int) { stitch_c_attr_set_int(pointer, key, Int64(v)) }
    public func set(_ key: String, _ v: String) {
        var s = v
        s.withUTF8 { b in b.withMemoryRebound(to: CChar.self) { stitch_c_attr_set_str(pointer, key, $0.baseAddress, b.count) } }
    }
    public func int(_ key: String) -> Int? {
        var v: Int64 = 0
        if stitch_c_attr_get_int(pointer, key, &v) != 0 { return Int(v) }
        stitch_c_note_missing(key)
        return nil
    }
    public func string(_ key: String) -> String? {
        var n = 0
        guard let p = stitch_c_attr_get_str(pointer, key, &n) else {
            stitch_c_note_missing(key)
            return nil
        }
        return String(decoding: UnsafeRawBufferPointer(start: p, count: n), as: UTF8.self)
    }
    public func has(_ key: String) -> Bool { stitch_c_attr_has(pointer, key) != 0 }
    public func delete(_ key: String) { stitch_c_attr_del(pointer, key) }
}

/// An object input that stays available after the call.
public struct Borrow<T: StitchObject>: StitchArg, Attributes {
    public let pointer: UnsafeMutableRawPointer
    var box: Box<T.Handle> { Unmanaged<Box<T.Handle>>.fromOpaque(pointer).takeUnretainedValue() }
    /// The handle; assignments and mutations (value types too) stay with the object.
    public var value: T.Handle {
        get { box.value }
        nonmutating set { box.value = newValue }
    }
    /// The library took ownership of the object in this call: it leaves the pool.
    public func consume() { stitch_c_consume(pointer) }
    public static var spec: Spec { .object(Objects.id(T.self), take: false) }
    public static func make(_ c: Call, _ i: Int) -> Self { Self(pointer: stitch_c_in(c.ctx, Int32(i))) }
}

/// An object input that the call consumes.
public struct Take<T: StitchObject>: StitchArg, Attributes {
    public let pointer: UnsafeMutableRawPointer
    public var value: T.Handle { Unmanaged<Box<T.Handle>>.fromOpaque(pointer).takeUnretainedValue().value }
    public static var spec: Spec { .object(Objects.id(T.self), take: true) }
    public static func make(_ c: Call, _ i: Int) -> Self { Self(pointer: stitch_c_in(c.ctx, Int32(i))) }
}

// MARK: - outputs

public protocol StitchRet {
    static var outputs: [(Int32, Bool)] { get }
    func store(_ c: Call)
}

public protocol NewOutput {
    static var typeId: Int32 { get }
    var retained: UnsafeMutableRawPointer { get }
}

/// A new object, returned by the stub: `New(handle)`.
public struct New<T: StitchObject>: StitchRet, NewOutput, Attributes {
    let box: Box<T.Handle>
    public init(_ v: T.Handle) { box = Box(v) }
    public var pointer: UnsafeMutableRawPointer { Unmanaged.passUnretained(box).toOpaque() }
    public var retained: UnsafeMutableRawPointer { Unmanaged.passRetained(box).toOpaque() }
    public static var typeId: Int32 { Objects.id(T.self) }
    public static var outputs: [(Int32, Bool)] { [(typeId, false)] }
    public func store(_ c: Call) { stitch_c_out(c.ctx, 0, retained) }
    /// this object as an input (reproducers; the New value keeps it alive)
    public var borrow: Borrow<T> { Borrow(pointer: pointer) }
    public var take: Take<T> { Take(pointer: pointer) }
}

/// `New<T>?`: an optional output.
extension Optional: StitchRet where Wrapped: NewOutput {
    public static var outputs: [(Int32, Bool)] { [(Wrapped.typeId, true)] }
    public func store(_ c: Call) {
        if let v = self { stitch_c_out(c.ctx, 0, v.retained) }
    }
}

/// Two outputs: `New2(a, b)` (either may be nil when declared optional: `New2<A, B?>` is not
/// expressible, so both are required unless wrapped in Optional by `New2(a, nil)`).
public struct New2<A: NewOutput, B: NewOutput>: StitchRet {
    let a: A?
    let b: B?
    public init(_ a: A?, _ b: B?) {
        self.a = a
        self.b = b
    }
    public static var outputs: [(Int32, Bool)] { [(A.typeId, true), (B.typeId, true)] }
    public func store(_ c: Call) {
        if let a { stitch_c_out(c.ctx, 0, a.retained) }
        if let b { stitch_c_out(c.ctx, 1, b.retained) }
    }
}

// MARK: - parameters

public enum Spec {
    case object(Int32, take: Bool)
    case param(stitch_c_param)
}

public struct Call {
    public let ctx: UnsafeMutableRawPointer
}

public protocol StitchArg {
    static var spec: Spec { get }
    static func make(_ c: Call, _ i: Int) -> Self
}

func cstr(_ s: String?) -> UnsafePointer<CChar>? {
    guard let s else { return nil }
    return UnsafePointer(strdup(s))
}

func param(_ kind: Int32, _ size: Int, signed: Bool = false, lo: Int64 = 0, hi: Int64 = 0, values: [Int64] = [],
           datatype: String? = nil, ctype: String) -> Spec {
    let vals = UnsafeMutablePointer<Int64>.allocate(capacity: max(values.count, 1))
    for (i, v) in values.enumerated() { vals[i] = v }
    return .param(stitch_c_param(kind: kind, size: UInt32(size), is_signed: signed ? 1 : 0, lo: lo, hi: hi,
                                 n_values: Int32(values.count), values: vals, datatype: cstr(datatype), ctype: cstr(ctype)))
}

func raw(_ c: Call, _ i: Int) -> UnsafeRawBufferPointer {
    var n = 0
    let p = stitch_c_raw(c.ctx, Int32(i), &n)
    return UnsafeRawBufferPointer(start: p, count: n)
}

func loadLE<T: FixedWidthInteger>(_ b: UnsafeRawBufferPointer, _ t: T.Type) -> T {
    var v: T = 0
    for i in 0..<min(b.count, MemoryLayout<T>.size) { v |= T(truncatingIfNeeded: b[i]) << (8 * i) }
    return v
}

/// A data type: a name for a kind of fuzzed value (seeds and *.dict tokens in harness/data/<Name>/).
public protocol Datatype {
    static var stitchName: String { get }
}

extension Datatype {
    public static var stitchName: String { String(describing: self) }
}

/// A data type with a decoder: builds the value from fuzzer choices.
public protocol Decoded: Datatype {
    static func decode(_ s: Source) -> String
}

/// A data type with a decoder that builds bytes.
public protocol DecodedBytes: Datatype {
    static func decode(_ s: Source) -> [UInt8]
}

/// No data type: plain fuzzed bytes.
public enum Plain: Datatype {}

enum Datatypes {
    nonisolated(unsafe) static var seen: Set<String> = []
    nonisolated(unsafe) static var decoders: [(Source) -> [UInt8]] = []
    static func name<T: Datatype>(_ t: T.Type) -> String? {
        if t == Plain.self { return nil }
        Runtime.setup()
        let n = t.stitchName
        if !seen.contains(n) {
            seen.insert(n)
            if let d = t as? any Decoded.Type {
                let i = stitch_c_datatype(n, 1)
                precondition(Int(i) == decoders.count)
                decoders.append { Array(d.decode($0).utf8) }
            } else if let d = t as? any DecodedBytes.Type {
                let i = stitch_c_datatype(n, 1)
                precondition(Int(i) == decoders.count)
                decoders.append { d.decode($0) }
            } else {
                _ = stitch_c_datatype(n, 0)
            }
        }
        return n
    }
}

/// A String of at most N bytes (UTF-8 decoded, invalid bytes replaced): `Str<64, Plain>`.
public struct Str<let N: Int, T: Datatype>: StitchArg {
    public let value: String
    public init(_ value: String) { self.value = value }
    public static var spec: Spec { param(7, N, datatype: Datatypes.name(T.self), ctype: "Str") }
    public static func make(_ c: Call, _ i: Int) -> Self {
        var n = 0
        let p = stitch_c_decoded(c.ctx, Int32(i), Datatypes.name(T.self), N, &n)
        return Self(String(decoding: UnsafeRawBufferPointer(start: p, count: n), as: UTF8.self))
    }
}

/// At most N bytes: `Bytes<256, Plain>`.
public struct Bytes<let N: Int, T: Datatype>: StitchArg {
    public let value: [UInt8]
    public init(_ value: [UInt8]) { self.value = value }
    public static var spec: Spec { param(6, N, datatype: Datatypes.name(T.self), ctype: "Bytes") }
    public static func make(_ c: Call, _ i: Int) -> Self {
        var n = 0
        let p = stitch_c_decoded(c.ctx, Int32(i), Datatypes.name(T.self), N, &n)
        return Self(Array(UnsafeRawBufferPointer(start: p, count: n)))
    }
}

/// Path of a temporary file holding at most N bytes (valid during the call).
public struct FilePath<let N: Int, T: Datatype>: StitchArg {
    public let path: String
    public init(path: String) { self.path = path }
    /// a temporary file holding `bytes` (reproducers)
    public init(_ bytes: [UInt8]) {
        let path = "/tmp/stitch-repro-\(getpid())-\(UInt32.random(in: 0...UInt32.max))"
        if let f = fopen(path, "wb") {
            _ = bytes.withUnsafeBufferPointer { fwrite($0.baseAddress, 1, $0.count, f) }
            fclose(f)
        }
        self.path = path
    }
    public static var spec: Spec { param(8, N, datatype: Datatypes.name(T.self), ctype: "File") }
    public static func make(_ c: Call, _ i: Int) -> Self {
        Self(path: String(cString: stitch_c_file(c.ctx, Int32(i), Datatypes.name(T.self), N)))
    }
}

/// An Int in Lo...Hi: `Between<0, 9>`.
public struct Between<let Lo: Int, let Hi: Int>: StitchArg {
    public let value: Int
    public init(_ value: Int) { self.value = value }
    public static var spec: Spec { param(3, 8, signed: true, lo: Int64(Lo), hi: Int64(Hi), ctype: "Range") }
    public static func make(_ c: Call, _ i: Int) -> Self {
        let v = loadLE(raw(c, i), Int64.self)
        if v >= Int64(Lo) && v <= Int64(Hi) { return Self(Int(v)) }
        let span = UInt64(bitPattern: Int64(Hi)) &- UInt64(bitPattern: Int64(Lo)) &+ 1
        let off = UInt64(bitPattern: v) &- UInt64(bitPattern: Int64(Lo))
        return Self(Int(Int64(bitPattern: UInt64(bitPattern: Int64(Lo)) &+ (span == 0 ? off : off % span))))
    }
}

/// One of an enum's cases: `Pick<Mode>` with `enum Mode: CaseIterable`.
public struct Pick<E: CaseIterable>: StitchArg {
    public let value: E
    public init(_ value: E) { self.value = value }
    /// the case at `index` in E.allCases (reproducers)
    public init(index: Int) { value = Array(E.allCases)[index] }
    public static var spec: Spec {
        param(4, 8, values: (0..<Int64(E.allCases.count)).map { $0 }, ctype: "OneOf")
    }
    public static func make(_ c: Call, _ i: Int) -> Self {
        let all = Array(E.allCases)
        let v = loadLE(raw(c, i), Int64.self)
        let idx = v >= 0 && v < Int64(all.count) ? Int(v) : Int(UInt64(bitPattern: v) % UInt64(all.count))
        return Self(all[idx])
    }
}

/// A Source for choices made inside the stub (at most N bytes of choices): `Choices<256>`.
public struct Choices<let N: Int>: StitchArg {
    public let source: Source
    public init(_ source: Source) { self.source = source }
    public static var spec: Spec { param(10, N, ctype: "Source") }
    public static func make(_ c: Call, _ i: Int) -> Self { Self(Source(stitch_c_source(c.ctx, Int32(i), N))) }
}

extension Bool: StitchArg {
    public static var spec: Spec { param(2, 1, ctype: "Bool") }
    public static func make(_ c: Call, _ i: Int) -> Self { let b = raw(c, i); return b.count > 0 && b[0] & 1 == 1 }
}

extension Double: StitchArg {
    public static var spec: Spec { param(1, 8, ctype: "F64") }
    public static func make(_ c: Call, _ i: Int) -> Self { Double(bitPattern: loadLE(raw(c, i), UInt64.self)) }
}

extension Float: StitchArg {
    public static var spec: Spec { param(1, 4, ctype: "F32") }
    public static func make(_ c: Call, _ i: Int) -> Self { Float(bitPattern: loadLE(raw(c, i), UInt32.self)) }
}

public protocol IntArg: FixedWidthInteger, StitchArg {}
extension IntArg {
    public static var spec: Spec {
        let n = MemoryLayout<Self>.size
        let names = isSigned ? [1: "I8", 2: "I16", 4: "I32", 8: "I64"] : [1: "U8", 2: "U16", 4: "U32", 8: "U64"]
        return param(0, n, signed: isSigned, ctype: names[n] ?? "I64")
    }
    public static func make(_ c: Call, _ i: Int) -> Self { loadLE(raw(c, i), Self.self) }
}
extension Int: IntArg {}
extension Int8: IntArg {}
extension Int16: IntArg {}
extension Int32: IntArg {}
extension Int64: IntArg {}
extension UInt: IntArg {}
extension UInt8: IntArg {}
extension UInt16: IntArg {}
extension UInt32: IntArg {}
extension UInt64: IntArg {}

// MARK: - Source

/// Decisions inside decoders and stubs, read from the fuzzer's choice buffer (the C/C++ runtime's
/// encoding, so the engine's string-span and dictionary mutations apply).
public final class Source {
    let p: UnsafeMutableRawPointer
    let owned: Bool
    init(_ p: UnsafeMutableRawPointer) {
        self.p = p
        owned = false
    }
    /// A source replaying recorded choices (reproducers).
    public init(bytes: [UInt8], cap: Int) {
        p = bytes.withUnsafeBufferPointer { stitch_c_source_new($0.baseAddress, $0.count, max(cap, $0.count)) }
        owned = true
    }
    deinit {
        if owned { stitch_c_source_free(p) }
    }

    public func u8() -> UInt8 { UInt8(truncatingIfNeeded: stitch_c_src_uint(p, 1)) }
    public func u16() -> UInt16 { UInt16(truncatingIfNeeded: stitch_c_src_uint(p, 2)) }
    public func u32() -> UInt32 { UInt32(truncatingIfNeeded: stitch_c_src_uint(p, 4)) }
    public func u64() -> UInt64 { stitch_c_src_uint(p, 8) }
    public func i32() -> Int32 { Int32(truncatingIfNeeded: stitch_c_src_uint(p, 4)) }
    public func i64() -> Int64 { Int64(bitPattern: stitch_c_src_uint(p, 8)) }
    public func f64() -> Double { stitch_c_src_f64(p) }
    /// An Int in lo...hi.
    public func range(_ lo: Int, _ hi: Int) -> Int { Int(stitch_c_src_range(p, Int64(lo), Int64(hi))) }
    /// 0..<n
    public func choice(_ n: Int) -> Int { n <= 0 ? 0 : range(0, n - 1) }
    public func pick<T>(_ xs: [T]) -> T { xs[choice(xs.count)] }
    public func coin(_ prob: Double = 0.5) -> Bool { stitch_c_src_coin(p, prob) != 0 }
    /// Loop continuation: `while s.more() { ... }`; false once the buffer is exhausted.
    public func more() -> Bool { stitch_c_src_more(p) != 0 }
    public func bytes(_ max: Int = 64) -> [UInt8] {
        var n = 0
        let b = stitch_c_src_bytes(p, max, &n)
        return Array(UnsafeRawBufferPointer(start: b, count: n))
    }
    public func str(_ max: Int = 64) -> String {
        var n = 0
        let b = stitch_c_src_str(p, max, &n)
        return String(decoding: UnsafeRawBufferPointer(start: b, count: n), as: UTF8.self)
    }
    /// A token from the data type's dictionary (harness/data/<Type>/*.dict, plus learned tokens).
    public func token() -> String {
        var n = 0
        let b = stitch_c_src_token(p, &n)
        return String(decoding: UnsafeRawBufferPointer(start: b, count: n), as: UTF8.self)
    }
    public var position: Int { stitch_c_src_position(p) }
    public var remaining: Int { stitch_c_src_remaining(p) }
    public var exhausted: Bool { stitch_c_src_exhausted(p) != 0 }
}

// MARK: - registration

/// A registered stub (Stub0 ... Stub6 in Endpoints.swift): its name and its body, which
/// `stitch write` reproducers call directly.
public protocol AnyStub {
    var name: String { get }
}

struct InitStub: AnyStub {
    let name = ""
}

enum Runtime {
    nonisolated(unsafe) static var ready = false
    nonisolated(unsafe) static var endpoints: [(Call) throws -> Void] = []
    nonisolated(unsafe) static var inits: [() -> Void] = []
    /// argument names, set by the generated main (stitch-build reads them from the closures)
    nonisolated(unsafe) static var argNames: [String: [String]] = [:]

    static func setup() {
        if ready { return }
        ready = true
        stitch_c_callbacks(
            { ep, ctx in
                let call = Call(ctx: ctx!)
                do {
                    try Runtime.endpoints[Int(ep)](call)
                    return 0
                } catch let b as Bail {
                    stitch_c_reason(b.reason)
                    return 1
                } catch let c as CheckFailed {
                    stitch_c_reason(c.what)
                    return 2
                } catch {
                    stitch_c_reason("threw \(type(of: error))")
                    return 1
                }
            },
            { dt, src, out in
                let bytes = Datatypes.decoders[Int(dt)](Source(src!))
                bytes.withUnsafeBufferPointer { stitch_c_append(out, $0.baseAddress, $0.count) }
            },
            { obj in Unmanaged<AnyObject>.fromOpaque(obj!).release() })
        stitch_c_init { for f in Runtime.inits { f() } }
    }
}

func ordinals(_ specs: [Spec]) -> [Int] {
    var o = 0, p = 0
    return specs.map {
        if case .object = $0 { o += 1; return o - 1 }
        p += 1
        return p - 1
    }
}

public func register(_ name: String, _ file: String, _ specs: [Spec], _ outputs: [(Int32, Bool)],
                     _ run: @escaping (Call) throws -> Void) -> String {
    Runtime.setup()
    var isObj: [UInt8] = [], inTypes: [Int32] = [], inTake: [Int32] = [], params: [stitch_c_param] = []
    for s in specs {
        switch s {
        case let .object(t, take):
            isObj.append(1)
            inTypes.append(t)
            inTake.append(take ? 1 : 0)
        case let .param(p):
            isObj.append(0)
            params.append(p)
        }
    }
    let names: [UnsafePointer<CChar>?] = Runtime.argNames[name].map { $0.map { cstr($0) } } ?? []
    let idx = names.withUnsafeBufferPointer { nb in
        stitch_c_endpoint(name, file, Int32(specs.count), isObj, inTypes, inTake, Int32(outputs.count), outputs.map { $0.0 },
                          outputs.map { $0.1 ? 1 : 0 }, params, nb.count == specs.count ? nb.baseAddress : nil)
    }
    precondition(Int(idx) == Runtime.endpoints.count, "stub registration out of order")
    Runtime.endpoints.append(run)
    return name
}

/// Run `f` once per process, after the harness is registered (library-global initialization).
public func stitchInit(_ f: @escaping () -> Void) -> AnyStub {
    Runtime.setup()
    Runtime.inits.append(f)
    return InitStub()
}

/// The generated main: registers the stubs (by touching them) and runs the engine or the reproducer.
public func stitchMain(argNames: [String: [String]], _ stubs: () -> [any AnyStub]) -> Int32 {
    Runtime.argNames = argNames
    Runtime.setup()
    _ = stubs()
    return stitch_c_main(CommandLine.argc, CommandLine.unsafeArgv)
}
