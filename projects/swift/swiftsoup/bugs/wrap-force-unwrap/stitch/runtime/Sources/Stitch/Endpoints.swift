// Generated (languages/swift/runtime): endpoint overloads for 0 to 6 arguments, with and without outputs,
// and the Stub types they return.

public struct Stub0<R>: AnyStub {
    public let name: String
    public let body: () throws -> R
}

public struct Stub1<A0, R>: AnyStub {
    public let name: String
    public let body: (A0) throws -> R
}

public struct Stub2<A0, A1, R>: AnyStub {
    public let name: String
    public let body: (A0, A1) throws -> R
}

public struct Stub3<A0, A1, A2, R>: AnyStub {
    public let name: String
    public let body: (A0, A1, A2) throws -> R
}

public struct Stub4<A0, A1, A2, A3, R>: AnyStub {
    public let name: String
    public let body: (A0, A1, A2, A3) throws -> R
}

public struct Stub5<A0, A1, A2, A3, A4, R>: AnyStub {
    public let name: String
    public let body: (A0, A1, A2, A3, A4) throws -> R
}

public struct Stub6<A0, A1, A2, A3, A4, A5, R>: AnyStub {
    public let name: String
    public let body: (A0, A1, A2, A3, A4, A5) throws -> R
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<R: StitchRet>(_ name: String, file: String = #filePath, _ body: @escaping () throws -> R) -> Stub0<R> {
    let specs: [Spec] = []
    _ = ordinals(specs)
    return Stub0<R>(name: register(name, file, specs, R.outputs) { c in try body().store(c) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint(_ name: String, file: String = #filePath, _ body: @escaping () throws -> Void) -> Stub0<Void> {
    let specs: [Spec] = []
    _ = ordinals(specs)
    return Stub0<Void>(name: register(name, file, specs, []) { c in try body() }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, R: StitchRet>(_ name: String, file: String = #filePath, _ body: @escaping (A0) throws -> R) -> Stub1<A0, R> {
    let specs: [Spec] = [A0.spec]
    let ord = ordinals(specs)
    return Stub1<A0, R>(name: register(name, file, specs, R.outputs) { c in try body(A0.make(c, ord[0])).store(c) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg>(_ name: String, file: String = #filePath, _ body: @escaping (A0) throws -> Void) -> Stub1<A0, Void> {
    let specs: [Spec] = [A0.spec]
    let ord = ordinals(specs)
    return Stub1<A0, Void>(name: register(name, file, specs, []) { c in try body(A0.make(c, ord[0])) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, R: StitchRet>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1) throws -> R) -> Stub2<A0, A1, R> {
    let specs: [Spec] = [A0.spec, A1.spec]
    let ord = ordinals(specs)
    return Stub2<A0, A1, R>(name: register(name, file, specs, R.outputs) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1])).store(c) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1) throws -> Void) -> Stub2<A0, A1, Void> {
    let specs: [Spec] = [A0.spec, A1.spec]
    let ord = ordinals(specs)
    return Stub2<A0, A1, Void>(name: register(name, file, specs, []) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1])) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, A2: StitchArg, R: StitchRet>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1, A2) throws -> R) -> Stub3<A0, A1, A2, R> {
    let specs: [Spec] = [A0.spec, A1.spec, A2.spec]
    let ord = ordinals(specs)
    return Stub3<A0, A1, A2, R>(name: register(name, file, specs, R.outputs) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1]), A2.make(c, ord[2])).store(c) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, A2: StitchArg>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1, A2) throws -> Void) -> Stub3<A0, A1, A2, Void> {
    let specs: [Spec] = [A0.spec, A1.spec, A2.spec]
    let ord = ordinals(specs)
    return Stub3<A0, A1, A2, Void>(name: register(name, file, specs, []) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1]), A2.make(c, ord[2])) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, A2: StitchArg, A3: StitchArg, R: StitchRet>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1, A2, A3) throws -> R) -> Stub4<A0, A1, A2, A3, R> {
    let specs: [Spec] = [A0.spec, A1.spec, A2.spec, A3.spec]
    let ord = ordinals(specs)
    return Stub4<A0, A1, A2, A3, R>(name: register(name, file, specs, R.outputs) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1]), A2.make(c, ord[2]), A3.make(c, ord[3])).store(c) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, A2: StitchArg, A3: StitchArg>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1, A2, A3) throws -> Void) -> Stub4<A0, A1, A2, A3, Void> {
    let specs: [Spec] = [A0.spec, A1.spec, A2.spec, A3.spec]
    let ord = ordinals(specs)
    return Stub4<A0, A1, A2, A3, Void>(name: register(name, file, specs, []) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1]), A2.make(c, ord[2]), A3.make(c, ord[3])) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, A2: StitchArg, A3: StitchArg, A4: StitchArg, R: StitchRet>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1, A2, A3, A4) throws -> R) -> Stub5<A0, A1, A2, A3, A4, R> {
    let specs: [Spec] = [A0.spec, A1.spec, A2.spec, A3.spec, A4.spec]
    let ord = ordinals(specs)
    return Stub5<A0, A1, A2, A3, A4, R>(name: register(name, file, specs, R.outputs) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1]), A2.make(c, ord[2]), A3.make(c, ord[3]), A4.make(c, ord[4])).store(c) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, A2: StitchArg, A3: StitchArg, A4: StitchArg>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1, A2, A3, A4) throws -> Void) -> Stub5<A0, A1, A2, A3, A4, Void> {
    let specs: [Spec] = [A0.spec, A1.spec, A2.spec, A3.spec, A4.spec]
    let ord = ordinals(specs)
    return Stub5<A0, A1, A2, A3, A4, Void>(name: register(name, file, specs, []) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1]), A2.make(c, ord[2]), A3.make(c, ord[3]), A4.make(c, ord[4])) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, A2: StitchArg, A3: StitchArg, A4: StitchArg, A5: StitchArg, R: StitchRet>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1, A2, A3, A4, A5) throws -> R) -> Stub6<A0, A1, A2, A3, A4, A5, R> {
    let specs: [Spec] = [A0.spec, A1.spec, A2.spec, A3.spec, A4.spec, A5.spec]
    let ord = ordinals(specs)
    return Stub6<A0, A1, A2, A3, A4, A5, R>(name: register(name, file, specs, R.outputs) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1]), A2.make(c, ord[2]), A3.make(c, ord[3]), A4.make(c, ord[4]), A5.make(c, ord[5])).store(c) }, body: body)
}

/// Register a stub. The closure's argument types are its signature.
@discardableResult
public func endpoint<A0: StitchArg, A1: StitchArg, A2: StitchArg, A3: StitchArg, A4: StitchArg, A5: StitchArg>(_ name: String, file: String = #filePath, _ body: @escaping (A0, A1, A2, A3, A4, A5) throws -> Void) -> Stub6<A0, A1, A2, A3, A4, A5, Void> {
    let specs: [Spec] = [A0.spec, A1.spec, A2.spec, A3.spec, A4.spec, A5.spec]
    let ord = ordinals(specs)
    return Stub6<A0, A1, A2, A3, A4, A5, Void>(name: register(name, file, specs, []) { c in try body(A0.make(c, ord[0]), A1.make(c, ord[1]), A2.make(c, ord[2]), A3.make(c, ord[3]), A4.make(c, ord[4]), A5.make(c, ord[5])) }, body: body)
}
