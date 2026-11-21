// swift-syntax 509: the Swift parser written in Swift (full-fidelity trees, incremental reparsing,
// operator folding, formatting, diagnostics).
// Oracles: a tree prints back to exactly its source (byte for byte, with errors and all); an
// incremental reparse after an edit equals a fresh parse of the edited text; formatting a tree
// without errors gives source that parses without errors; folding operators keeps the text;
// token positions are increasing and inside the file. Traps (preconditions, index out of range,
// force unwraps) are bugs.
import Stitch
import SwiftBasicFormat
import SwiftDiagnostics
import SwiftIDEUtils
import SwiftOperators
import SwiftParser
import SwiftParserDiagnostics
import SwiftSyntax

enum Tree: StitchObject { typealias Handle = SourceFileSyntax }

enum SwiftText: Decoded {
    static func decode(_ s: Source) -> String {
        var out = ""
        var depth = 0
        func ident() -> String { s.coin(0.8) ? s.pick(["a", "b", "x", "foo", "T", "self", "Self", "_", "`class`", "$0", "#if", "init"]) : s.token() }
        func expr(_ d: Int) -> String {
            switch s.choice(d > 3 ? 4 : 10) {
            case 0: return ident()
            case 1: return s.pick(["0", "1.5", "0x1p3", "1_000", "\"s\"", "\"\"\"\nm\n\"\"\"", "#\"raw\"#", "\"\\(x)\"", "nil", "true", "/a+b/", "#/re/#", "[]", "[:]"])
            case 2: return s.token()
            case 3: return s.str(6)
            case 4: return "\(expr(d + 1)) \(s.pick(["+", "-", "*", "..<", "...", "??", "==", "&&", "<<", "as?", "is", "as!", "=", "+=", "~=", "<>"])) \(expr(d + 1))"
            case 5: return "\(expr(d + 1))\(s.pick(["?", "!", ".x", "[0]", "()", "(a: 1)", " { $0 }", ".self", "?.y", "<Int>"]))"
            case 6: return "{ \(s.pick(["x in", "(a, b) -> Int in", "[weak self] in", ""])) \(expr(d + 1)) }"
            case 7: return "(\(expr(d + 1)), \(expr(d + 1)))"
            case 8: return "\(s.pick(["try", "await", "try?", "-", "!", "&", "consume", "copy", "each", "repeat"])) \(expr(d + 1))"
            default: return "if \(expr(d + 1)) { \(expr(d + 1)) } else { \(expr(d + 1)) }"
            }
        }
        while s.more() {
            let pad = String(repeating: "  ", count: depth)
            switch s.choice(14) {
            case 0: out += "\(pad)let \(ident())\(s.coin(0.3) ? ": \(s.pick(["Int", "[String]", "Int?", "some P", "any P", "(Int) -> Void", "inout T"]))" : "") = \(expr(0))\n"
            case 1: out += "\(pad)\(expr(0))\n"
            case 2: out += "\(pad)\(s.pick(["func", "init", "subscript", "macro", "func <", "static func"])) \(ident())\(s.pick(["()", "(_ x: Int)", "<T>(x: T)", "(a b: Int = 1)", "(x: Int...)"]))\(s.pick(["", " -> Int", " async throws", " where T: P"])) {\n"; depth += 1
            case 3: out += "\(pad)\(s.pick(["struct", "class", "enum", "protocol", "extension", "actor", "public final class"])) \(ident())\(s.pick(["", ": P", "<T>", "<each T>"])) {\n"; depth += 1
            case 4: if depth > 0 { depth -= 1; out += "\(String(repeating: "  ", count: depth))}\n" }
            case 5: out += "\(pad)\(s.pick(["if", "guard", "while", "switch", "for x in", "repeat"])) \(expr(0)) \(s.pick(["{", "else {", "{"]))\n"; depth += 1
            case 6: out += "\(pad)\(s.pick(["case .a:", "case let x where x > 0:", "default:", "@unknown default:", "case (a, b):"]))\n"
            case 7: out += s.pick(["#if os(Linux)\n", "#elseif DEBUG\n", "#else\n", "#endif\n", "#sourceLocation(file: \"a\", line: 1)\n", "#warning(\"w\")\n"])
            case 8: out += "\(pad)\(s.pick(["@available(*, deprecated)", "@MainActor", "@objc(x:)", "@_spi(X)", "@freestanding(expression)", "@attached(member, names: arbitrary)"]))\n"
            case 9: out += s.pick(["// c\n", "/* c */", "/// doc\n", "/* /* nested */ */", "\t", " ", "\r\n", ";"])
            case 10: out += "\(pad)var \(ident()): Int { \(s.pick(["get { 1 } set { }", "willSet { } didSet { }", "1", "_read { yield x }"])) }\n"
            case 11: out += "\(pad)\(s.pick(["import", "typealias X =", "precedencegroup P {", "operator", "infix operator +++ :"])) \(ident())\n"
            case 12: out += "\(pad)return \(expr(0))\n"
            default: out += s.str(8)
            }
        }
        while depth > 0 && s.coin(0.9) { depth -= 1; out += "}\n" }
        return out
    }
}

/// Every token: positions increase, lengths add up, text comes from the source.
func checkTokens(_ tree: SourceFileSyntax, _ source: String) throws {
    let bytes = Array(source.utf8)
    var pos = 0
    for tok in tree.tokens(viewMode: .all) {
        let start = tok.position.utf8Offset
        try check(start == pos, "tokens are contiguous (token at \(start), expected \(pos))")
        let len = tok.totalLength.utf8Length
        try check(start + len <= bytes.count, "a token lies inside the file")
        pos = start + len
    }
    try check(pos == bytes.count || (pos == 0 && bytes.isEmpty), "tokens cover the file")
}

let parse = endpoint("parse") { (text: Str<2048, SwiftText>) throws -> New<Tree> in
    let tree = Parser.parse(source: text.value)
    try check(tree.description == text.value, "the tree prints back to its source")
    try check(tree.totalLength.utf8Length == text.value.utf8.count, "the tree's length is the source's")
    try checkTokens(tree, text.value)
    return New(tree)
}

// an incremental reparse after an edit equals a fresh parse of the edited text
let reparse = endpoint("reparse") { (tree: Borrow<Tree>, offset: Between<0, 4096>, length: Between<0, 64>, insert: Str<32, SwiftText>) throws -> New<Tree> in
    let old = tree.value.description
    var bytes = Array(old.utf8)
    let off = min(offset.value, bytes.count)
    let len = min(length.value, bytes.count - off)
    let ins = Array(insert.value.utf8)
    bytes.replaceSubrange(off..<(off + len), with: ins)
    guard let newText = String(validating: bytes, as: UTF8.self) else { throw Bail("edit splits a UTF-8 character") }
    // (the previous parse's lookahead ranges come from parsing the old text incrementally)
    let (prev, ranges) = Parser.parseIncrementally(source: old, parseTransition: nil)
    let edit = IncrementalEdit(offset: off, length: len, replacementLength: ins.count)
    let transition = IncrementalParseTransition(previousTree: prev, edits: ConcurrentEdits(edit), lookaheadRanges: ranges)
    let (incremental, _) = Parser.parseIncrementally(source: newText, parseTransition: transition)
    let fresh = Parser.parse(source: newText)
    try check(incremental.description == newText, "the incremental tree prints back to the edited source")
    log("edit at \(off) -\(len) +\(insert.value.debugDescription)")
    try check(incremental.debugDescription == fresh.debugDescription, "incremental reparse == fresh parse of the edited text")
    return New(fresh)
}

let format = endpoint("format") { (tree: Borrow<Tree>, indent: Between<0, 8>, tabs: Bool) throws in
    let src = tree.value
    let fmt = BasicFormat(indentationWidth: tabs ? .tabs(1) : .spaces(indent.value))
    let formatted = src.formatted(using: fmt)
    let text = formatted.description
    log("formatted: \(text.prefix(400).debugDescription)")
    if !src.hasError {
        let again = Parser.parse(source: text)
        try check(!again.hasError, "formatting a tree without errors gives source without errors")
    }
}

let fold = endpoint("fold") { (tree: Borrow<Tree>) throws in
    var errors = 0
    let folded = OperatorTable.standardOperators.foldAll(tree.value) { _ in errors += 1 }
    try check(folded.description == tree.value.description, "folding operators keeps the text")
}

let diagnose = endpoint("diagnose") { (tree: Borrow<Tree>) throws in
    let diags = ParseDiagnosticsGenerator.diagnostics(for: tree.value)
    let n = tree.value.totalLength.utf8Length
    let conv = SourceLocationConverter(fileName: "f.swift", tree: tree.value)
    for d in diags {
        let p = d.position.utf8Offset
        try check(p >= 0 && p <= n, "a diagnostic points inside the file")
        _ = d.location(converter: conv)
        _ = d.message
        for fix in d.fixIts { _ = fix.message; _ = fix.changes.count }
    }
    try check(tree.value.hasError == !diags.filter { $0.diagMessage.severity == .error }.isEmpty || !tree.value.hasError,
              "a tree with errors gets error diagnostics")
    _ = DiagnosticsFormatter.annotatedSource(tree: tree.value, diags: diags)
}

let classify = endpoint("classify") { (tree: Borrow<Tree>) throws in
    var last = -1
    for c in tree.value.classifications {
        let start = c.offset  // (range changed type in 600; offset exists in every version)
        try check(start >= last, "classified ranges are in order")
        last = start
    }
    let conv = SourceLocationConverter(fileName: "f.swift", tree: tree.value)
    for tok in tree.value.tokens(viewMode: .sourceAccurate).prefix(200) {
        let loc = tok.startLocation(converter: conv)
        try check(loc.line >= 1 && loc.column >= 1, "token locations are 1-based")
    }
}
