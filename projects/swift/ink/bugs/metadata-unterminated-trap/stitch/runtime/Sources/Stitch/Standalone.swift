// Reproducers written by `stitch write`: the harness package with main.swift replaced by a
// program that calls the stub bodies directly (no engine, no program interpreter).
//
//     exit(reproMain {
//         let store0 = try call(0, "new_store") { try newStore.body() }
//         try callBails(1, "put") { try put.body(store0.borrow, Str("k"), Str("v")) }
//     })
//
// A call that succeeded in the original run but bails now is a divergence (exit 99). Traps and
// sanitizer reports end the process as in the fuzzer; a failed check prints its report.
#if canImport(Glibc)
import Glibc
#endif

nonisolated(unsafe) var reproNode = -1
nonisolated(unsafe) var reproName = "?"
let reproTrace = getenv("STITCH_REPRO_TRACE") != nil

struct Diverged: Error {
    let why: String
}

/// A call that succeeded in the original run.
@discardableResult
public func call<R>(_ n: Int, _ name: String, _ f: () throws -> R) throws -> R {
    reproNode = n
    reproName = name
    do {
        let r = try f()
        if reproTrace { fputs("[repro] #\(n) \(name) -> ok\n", stderr) }
        return r
    } catch let b as Bail {
        throw Diverged(why: "bailed: \"\(b.reason)\" (it succeeded in the original run)")
    } catch let c as CheckFailed {
        throw c
    } catch {
        throw Diverged(why: "threw \(type(of: error)) (it succeeded in the original run)")
    }
}

/// A call that bailed (or threw) in the original run.
public func callBails(_ n: Int, _ name: String, _ f: () throws -> Void) throws {
    reproNode = n
    reproName = name
    do {
        try f()
    } catch let c as CheckFailed {
        throw c
    } catch {}
    if reproTrace { fputs("[repro] #\(n) \(name) -> bail\n", stderr) }
}

/// Run the program; report a failed check in the fuzzer's format (exit 1), a divergence (exit 99).
public func reproMain(_ program: () throws -> Void) -> Int32 {
    do {
        try program()
        return 0
    } catch let c as CheckFailed {
        fputs("\n[stitch] CHECK FAILED: \(c.what) (node \(reproNode))\n", stderr)
        return 1
    } catch let d as Diverged {
        fputs("\n[repro] DIVERGED at call #\(reproNode) \(reproName): \(d.why)\n", stderr)
        return 99
    } catch {
        fputs("\n[repro] DIVERGED at call #\(reproNode) \(reproName): threw \(type(of: error))\n", stderr)
        return 99
    }
}

/// An output of a call that produced it in the original run (optional outputs too).
public func obj<T>(_ v: New<T>) -> New<T> { v }
public func obj<T>(_ v: New<T>?) -> New<T> { v! }
