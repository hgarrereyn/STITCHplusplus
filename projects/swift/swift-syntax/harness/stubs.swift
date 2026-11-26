// swift-syntax (main): the Swift parser written in Swift (full-fidelity trees, every parse entry point and
// option, incremental reparsing), operator folding with standard and source-declared operator tables,
// formatting and indentation, parser diagnostics, fix-its and their formatting, classification, name
// matching, token/position APIs, rewriters and visitors, trivia, literal values, #if configuration
// (SwiftIfConfig), lexical scope lookup (SwiftLexicalLookup), and refactorings (SwiftRefactor).
// Oracles: a tree prints back to exactly its source (byte for byte, with errors and all, whatever the
// entry point or options); an incremental reparse after edits equals a fresh parse of the edited text;
// formatting a tree without errors gives source that parses without errors; folding keeps the text;
// token positions are contiguous; token(at:), previous/next token and SourceLocationConverter agree;
// ConfiguredRegions.isActive agrees with isActive(in:); a rewriter that keeps tokens prints the same.
// Traps (preconditions, index out of range, force unwraps) are bugs.
import Stitch
import SwiftBasicFormat
import SwiftDiagnostics
import SwiftIDEUtils
import SwiftIfConfig
import SwiftLexicalLookup
import SwiftOperators
import SwiftParser
import SwiftParserDiagnostics
import SwiftRefactor
import SwiftSyntax

enum Tree: StitchObject { typealias Handle = SourceFileSyntax }

let operatorDecls = ["infix operator +++ : AdditionPrecedence\n", "precedencegroup P { higherThan: AdditionPrecedence associativity: left }\n",
                     "infix operator <> : P\n", "precedencegroup Q { lowerThan: P assignment: true }\n", "postfix operator !!\n"]

enum SwiftText: Decoded {
    static func decode(_ s: Source) -> String {
        var out = ""
        var depth = 0
        func ident() -> String { s.coin(0.8) ? s.pick(["a", "b", "x", "foo", "T", "self", "Self", "_", "`class`", "$0", "#if", "init", "<#T##Int#>"]) : s.token() }
        func expr(_ d: Int) -> String {
            switch s.choice(d > 3 ? 4 : 11) {
            case 0: return ident()
            case 1: return s.pick(["0", "1.5", "0x1p3", "1_000", "0b101", "0o7", "\"s\"", "\"\"\"\nm\n\"\"\"", "#\"raw\\#(x)\"#", "\"\\(x)\"", "\"\\u{1F600}\\n\"",
                                   "nil", "true", "/a+b/", "#/re/#", "[]", "[:]", "<#placeholder#>"])
            case 2: return s.token()
            case 3: return s.str(6)
            case 4: return "\(expr(d + 1)) \(s.pick(["+", "-", "*", "..<", "...", "??", "==", "&&", "<<", "as?", "is", "as!", "=", "+=", "~=", "<>", "+++", "**"])) \(expr(d + 1))"
            case 5: return "\(expr(d + 1))\(s.pick(["?", "!", ".x", "[0]", "()", "(a: 1)", " { $0 }", ".self", "?.y", "<Int>", "(<#T##() -> Void#>)", "(1) { } b: { }"]))"
            case 6: return "{ \(s.pick(["x in", "(a, b) -> Int in", "[weak self] in", ""])) \(expr(d + 1)) }"
            case 7: return "(\(expr(d + 1)), \(expr(d + 1)))"
            case 8: return "\(s.pick(["try", "await", "try?", "-", "!", "&", "consume", "copy", "each", "repeat"])) \(expr(d + 1))"
            case 9: return "foo(\(expr(d + 1)), x: { \(expr(d + 1)) }, y: <#T##() -> Int#>)"
            default: return "if \(expr(d + 1)) { \(expr(d + 1)) } else { \(expr(d + 1)) }"
            }
        }
        while s.more() {
            let pad = String(repeating: "  ", count: depth)
            switch s.choice(15) {
            case 0: out += "\(pad)let \(ident())\(s.coin(0.3) ? ": \(s.pick(["Int", "[String]", "Int?", "some P", "any P", "(Int) -> Void", "inout T"]))" : "") = \(expr(0))\n"
            case 1: out += "\(pad)\(expr(0))\n"
            case 2: out += "\(pad)\(s.pick(["func", "init", "subscript", "macro", "func <", "static func"])) \(ident())\(s.pick(["()", "(_ x: Int)", "<T>(x: T)", "(a b: Int = 1)", "(x: Int...)"]))\(s.pick(["", " -> Int", " async throws", " where T: P"])) {\n"; depth += 1
            case 3: out += "\(pad)\(s.pick(["struct", "class", "enum", "protocol", "extension", "actor", "public final class"])) \(ident())\(s.pick(["", ": P", "<T>", "<each T>"])) {\n"; depth += 1
            case 4: if depth > 0 { depth -= 1; out += "\(String(repeating: "  ", count: depth))}\n" }
            case 5: out += "\(pad)\(s.pick(["if", "guard", "while", "switch", "for x in", "repeat"])) \(expr(0)) \(s.pick(["{", "else {", "{"]))\n"; depth += 1
            case 6: out += "\(pad)\(s.pick(["case .a:", "case let x where x > 0:", "default:", "@unknown default:", "case (a, b):"]))\n"
            case 7: out += s.pick(["#if os(Linux)\n", "#if DEBUG && !RELEASE\n", "#if compiler(>=5.9)\n", "#if canImport(Foundation)\n", "#if swift(<6)\n",
                                   "#if hasFeature(Foo) || arch(x86_64)\n", "#if targetEnvironment(simulator)\n", "#if _endian(little)\n",
                                   "#elseif DEBUG\n", "#else\n", "#endif\n", "#sourceLocation(file: \"a\", line: 1)\n", "#sourceLocation()\n", "#warning(\"w\")\n"])
            case 8: out += "\(pad)\(s.pick(["@available(*, deprecated)", "@MainActor", "@objc(x:)", "@_spi(X)", "@freestanding(expression)", "@attached(member, names: arbitrary)"]))\n"
            case 9: out += s.pick(["// c\n", "/* c */", "/// doc\n", "/** doc */\n", "/* /* nested */ */", "\t", " ", "\r\n", ";"])
            case 10: out += "\(pad)var \(ident()): Int { \(s.pick(["get { 1 } set { }", "willSet { } didSet { }", "1", "_read { yield x }"])) }\n"
            case 11: out += "\(pad)\(s.pick(["import", "typealias X =", "precedencegroup P {", "operator", "infix operator +++ :", "prefix operator **"])) \(ident())\n"
            case 12: out += "\(pad)return \(expr(0))\n"
            case 13: out += pad + s.pick(operatorDecls)
            default: out += s.str(8)
            }
        }
        while depth > 0 && s.coin(0.9) { depth -= 1; out += "}\n" }
        return out
    }
}

/// Apply edits (sorted, non-overlapping as a fix-it's are) to the text, last first.
func apply(_ edits: [SourceEdit], to text: String) -> String? {
    var bytes = Array(text.utf8)
    var end = bytes.count
    for e in edits.sorted(by: { $0.range.lowerBound > $1.range.lowerBound }) {
        let lo = e.range.lowerBound.utf8Offset, hi = e.range.upperBound.utf8Offset
        guard lo <= hi, hi <= end else { return nil }
        bytes.replaceSubrange(lo..<hi, with: e.replacementBytes)
        end = lo
    }
    return String(validating: bytes, as: UTF8.self)
}

/// Every token: positions increase, lengths add up, text comes from the source.
func checkTokens(_ tree: some SyntaxProtocol, _ source: String) throws {
    let bytes = Array(source.utf8)
    var pos = tree.position.utf8Offset
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

enum Version: CaseIterable { case none, v4, v5, v6
    var version: Parser.SwiftVersion? { self == .none ? nil : self == .v4 ? .v4 : self == .v5 ? .v5 : .v6 }
}

// every entry point and option keeps the text
let parseOptions = endpoint("parse_options") { (text: Str<2048, SwiftText>, version: Pick<Version>, features: UInt16,
                                                nesting: Between<0, 300>, buffer: Bool) throws -> New<Tree> in
    let feats = Parser.LanguageFeatures(rawValue: UInt(features) & ~UInt(1 << 11))   // not the test-only "everything unexpected" feature
    let limit = nesting.value == 0 ? nil : nesting.value
    var src = text.value
    let tree = buffer
        ? src.withUTF8 { Parser.parse(source: $0, maximumNestingLevel: limit, swiftVersion: version.value.version, languageFeatures: feats) }
        : Parser.parse(source: src, maximumNestingLevel: limit, swiftVersion: version.value.version, languageFeatures: feats)
    try check(tree.description == text.value, "a tree parsed with options prints back to its source")
    try checkTokens(tree, text.value)
    _ = tree.hasMaximumNestingLevelOverflow
    return New(tree)
}

// parsing as each kind of node keeps the text (the remainder becomes unexpected nodes)
let parseAs = endpoint("parse_as") { (text: Str<1024, SwiftText>, kind: Between<0, 20>) throws in
    var p = Parser(text.value)
    let node: Syntax
    switch kind.value {
    case 0: node = Syntax(ExprSyntax.parse(from: &p))
    case 1: node = Syntax(StmtSyntax.parse(from: &p))
    case 2: node = Syntax(DeclSyntax.parse(from: &p))
    case 3: node = Syntax(TypeSyntax.parse(from: &p))
    case 4: node = Syntax(PatternSyntax.parse(from: &p))
    case 5: node = Syntax(AttributeSyntax.parse(from: &p))
    case 6: node = Syntax(AccessorBlockSyntax.parse(from: &p))
    case 7: node = Syntax(AccessorDeclSyntax.parse(from: &p))
    case 8: node = Syntax(CatchClauseSyntax.parse(from: &p))
    case 9: node = Syntax(ClosureParameterSyntax.parse(from: &p))
    case 10: node = Syntax(CodeBlockItemSyntax.parse(from: &p))
    case 11: node = Syntax(CodeBlockSyntax.parse(from: &p))
    case 12: node = Syntax(EnumCaseParameterSyntax.parse(from: &p))
    case 13: node = Syntax(FunctionParameterSyntax.parse(from: &p))
    case 14: node = Syntax(GenericParameterClauseSyntax.parse(from: &p))
    case 15: node = Syntax(MemberBlockSyntax.parse(from: &p))
    case 16: node = Syntax(SwitchCaseSyntax.parse(from: &p))
    case 17: node = Syntax(VersionTupleSyntax.parse(from: &p))
    case 18: node = Syntax(AttributeListSyntax.parse(from: &p))
    case 19: node = Syntax(MemberBlockItemListSyntax.parse(from: &p))
    default: node = Syntax(CodeBlockItemListSyntax.parse(from: &p))
    }
    // (a source of trivia alone has no token to attach the trivia to; only SourceFileSyntax has an EOF token)
    let onlyTrivia = Parser.parse(source: text.value).tokens(viewMode: .sourceAccurate).allSatisfy { $0.tokenKind == .endOfFile }
    // (some kinds drop the end-of-file token's leading trivia, a known fidelity bug: the collection kinds
    // always, layout kinds like EnumCaseParameterSyntax on "a b\n". Compare with that trivia added back, so
    // the check still catches any other lost text)
    let eofTrivia = Parser.parse(source: text.value).endOfFileToken.leadingTrivia.description
    if !eofTrivia.isEmpty && node.description + eofTrivia == text.value { return }
    if !onlyTrivia {
        try check(node.description == text.value, "a node parsed as one kind prints back to its source")
        try checkTokens(node, text.value)
    }
    _ = ParseDiagnosticsGenerator.diagnostics(for: node)
}

enum EditMode: CaseIterable { case single, sequential, concurrent }

// an incremental reparse after edits equals a fresh parse of the edited text
let reparse = endpoint("reparse") { (tree: Borrow<Tree>, offset: Between<0, 4096>, length: Between<0, 64>, insert: Str<32, SwiftText>,
                                     mode: Pick<EditMode>, offset2: Between<0, 4096>, insert2: Str<16, SwiftText>) throws -> New<Tree> in
    let old = tree.value.description
    var bytes = Array(old.utf8)
    let off = min(offset.value, bytes.count)
    let len = min(length.value, bytes.count - off)
    let ins = Array(insert.value.utf8)
    bytes.replaceSubrange(off..<(off + len), with: ins)
    var edits = [SourceEdit(range: AbsolutePosition(utf8Offset: off)..<AbsolutePosition(utf8Offset: off + len), replacement: ins)]
    if mode.value != .single {
        // a second, pure insertion, after the first edit (sequential: in post-edit coordinates)
        let ins2 = Array(insert2.value.utf8)
        let base = mode.value == .sequential ? off + ins.count : off + len
        let at2 = base + min(offset2.value, (mode.value == .sequential ? bytes.count : old.utf8.count) - base)
        let pos2 = mode.value == .sequential ? at2 : at2 - len + ins.count
        bytes.insert(contentsOf: ins2, at: pos2)
        edits.append(SourceEdit(range: AbsolutePosition(utf8Offset: at2)..<AbsolutePosition(utf8Offset: at2), replacement: ins2))
    }
    guard let newText = String(validating: bytes, as: UTF8.self) else { throw Bail("edit splits a UTF-8 character") }
    let concurrent: ConcurrentEdits
    switch mode.value {
    case .single: concurrent = ConcurrentEdits(edits[0])
    case .sequential: concurrent = ConcurrentEdits(fromSequential: edits)
    case .concurrent: concurrent = try ConcurrentEdits(concurrent: edits)
    }
    // (the previous parse's lookahead ranges come from parsing the old text incrementally)
    let prev: IncrementalParseResult = Parser.parseIncrementally(source: old, parseTransition: nil)
    let transition = IncrementalParseTransition(previousIncrementalParseResult: prev, edits: concurrent)
    let incremental: IncrementalParseResult = Parser.parseIncrementally(source: newText, parseTransition: transition)
    let fresh = Parser.parse(source: newText)
    try check(incremental.tree.description == newText, "the incremental tree prints back to the edited source")
    log("edits \(edits.map { "\($0.range) \(String(decoding: $0.replacementBytes, as: UTF8.self).debugDescription)" })")
    try check(incremental.tree.debugDescription == fresh.debugDescription, "incremental reparse == fresh parse of the edited text")
    return New(fresh)
}

let format = endpoint("format") { (tree: Borrow<Tree>, indent: Between<0, 8>, tabs: Bool, initial: Between<0, 4>, all: Bool) throws in
    let src = tree.value
    let fmt = BasicFormat(indentationWidth: tabs ? .tabs(1) : .spaces(indent.value), initialIndentation: .spaces(initial.value),
                          viewMode: all ? .all : .sourceAccurate)
    let formatted = src.formatted(using: fmt)
    let text = formatted.description
    log("formatted: \(text.prefix(400).debugDescription)")
    // (a tree built by edits can be free of missing/unexpected nodes and still not be valid source)
    if !src.hasError && initial.value == 0 && !all && !Parser.parse(source: src.description).hasError {
        let again = Parser.parse(source: text)
        try check(!again.hasError, "formatting a tree without errors gives source without errors")
    }
    _ = BasicFormat.inferIndentation(of: src)
    let indented = src.indented(by: tabs ? .tabs(1) : .spaces(indent.value), indentFirstLine: all)
    if !src.hasError { _ = indented.description }
    for tok in src.tokens(viewMode: .sourceAccurate).prefix(50) {
        _ = tok.indentationOfLine
        _ = tok.leadingTrivia.indentation(isOnNewline: false)
    }
}

// folding with the standard table and with the operators the file declares keeps the text
let fold = endpoint("fold") { (tree: Borrow<Tree>, own: Bool) throws in
    var errors = 0
    var table = OperatorTable.standardOperators
    if own {
        table.addSourceFile(tree.value) { e in errors += 1; _ = e.asDiagnostic; _ = e.message }
    }
    let folded = table.foldAll(tree.value) { e in errors += 1; _ = e.asDiagnostic.debugDescription }
    try check(folded.description == tree.value.description, "folding operators keeps the text")
    _ = OperatorTable.logicalOperators.foldAll(tree.value) { _ in }
    // fold each sequence expression on its own
    final class Seqs: SyntaxVisitor {
        var found: [SequenceExprSyntax] = []
        override func visit(_ node: SequenceExprSyntax) -> SyntaxVisitorContinueKind { found.append(node); return .visitChildren }
    }
    let v = Seqs(viewMode: .sourceAccurate)
    v.walk(tree.value)
    for seq in v.found.prefix(20) {
        let one = table.foldSingle(seq) { _ in }
        try check(one.description == seq.description, "foldSingle keeps the text")
    }
    _ = table.description
    for name in ["+", "-", "==", "+++", "<>", "**", "!!", "!"] {
        if let op = table.infixOperator(named: name) { _ = op.synthesizedSyntax().description; _ = op.description }
        if let op = table.prefixOperator(named: name) { _ = op.synthesizedSyntax().description }
        if let op = table.postfixOperator(named: name) { _ = op.synthesizedSyntax().description }
    }
}

let diagnose = endpoint("diagnose") { (tree: Borrow<Tree>, context: Between<0, 4>, colorize: Bool) throws in
    let diags = ParseDiagnosticsGenerator.diagnostics(for: tree.value)
    let n = tree.value.totalLength.utf8Length
    let conv = SourceLocationConverter(fileName: "f.swift", tree: tree.value)
    for d in diags {
        let p = d.position.utf8Offset
        try check(p >= 0 && p <= n, "a diagnostic points inside the file")
        _ = d.location(converter: conv)
        _ = d.message
        _ = d.debugDescription
        for note in d.notes { _ = note.location(converter: conv); _ = note.message }
        for fix in d.fixIts { _ = fix.message; _ = fix.changes.count; _ = fix.edits }
    }
    try check(!tree.value.hasError || !diags.filter { $0.diagMessage.severity == .error }.isEmpty,
              "a tree with errors gets error diagnostics")
    _ = DiagnosticsFormatter.annotatedSource(tree: tree.value, diags: diags)
    let fmt = DiagnosticsFormatter(contextSize: context.value, colorize: colorize)
    _ = fmt.annotatedSource(tree: tree.value, diags: diags)
    var group = GroupedDiagnostics()
    let id = group.addSourceFile(tree: tree.value, displayName: "f.swift", diagnostics: diags)
    _ = id
    _ = fmt.annotateSources(in: group)
}

// applying one of the parser's fix-its gives text that parses back (and prints back)
let fixIts = endpoint("fix_its") { (tree: Borrow<Tree>, s: Choices<4>) throws -> New<Tree>? in
    let diags = ParseDiagnosticsGenerator.diagnostics(for: tree.value)
    let fixes = diags.flatMap { $0.fixIts }
    try require(!fixes.isEmpty, "no fix-its")
    let fix = s.source.pick(fixes)
    log("fix-it: \(fix.message.message)")
    guard let fixed = apply(fix.edits, to: tree.value.description) else { throw Bail("overlapping edits") }
    log("fixed: \(fixed.prefix(400).debugDescription)")
    let again = Parser.parse(source: fixed)
    try check(again.description == fixed, "the fixed source parses back")
    return New(again)
}

let classify = endpoint("classify") { (tree: Borrow<Tree>, a: Between<0, 4096>, b: Between<0, 4096>) throws in
    var last = -1
    for c in tree.value.classifications {
        let start = c.range.lowerBound.utf8Offset
        try check(start >= last, "classified ranges are in order")
        last = start
        _ = c.kind; _ = c.range.upperBound
    }
    let n = tree.value.totalLength.utf8Length
    let lo = min(a.value, b.value, n), hi = min(max(a.value, b.value), n)
    for c in tree.value.classifications(in: AbsolutePosition(utf8Offset: lo)..<AbsolutePosition(utf8Offset: hi)) {
        _ = c.range
    }
    _ = tree.value.classification(at: AbsolutePosition(utf8Offset: lo))
    let conv = SourceLocationConverter(fileName: "f.swift", tree: tree.value)
    for tok in tree.value.tokens(viewMode: .sourceAccurate).prefix(200) {
        let loc = tok.startLocation(converter: conv)
        try check(loc.line >= 1 && loc.column >= 1, "token locations are 1-based")
        _ = tok.tokenClassification
    }
}

// token(at:), previous/next token and SourceLocationConverter agree
let positions = endpoint("positions") { (tree: Borrow<Tree>, s: Choices<16>) throws in
    let t = tree.value
    let n = t.totalLength.utf8Length
    try require(n > 0, "empty file")
    let conv = SourceLocationConverter(fileName: "f.swift", tree: t)
    for _ in 0..<4 {
        let p = AbsolutePosition(utf8Offset: s.source.range(0, n - 1))
        if let tok = t.token(at: p) {
            try check(tok.position <= p && p < tok.endPosition, "token(at:) contains the position")
            if let next = tok.nextToken(viewMode: .sourceAccurate) {
                try check(next.previousToken(viewMode: .sourceAccurate) == tok, "nextToken.previousToken is the token")
            }
            _ = tok.tokenKind; _ = tok.text; _ = tok.presence; _ = tok.trimmedLength; _ = tok.trimmedRange; _ = tok.keyPathInParent
            _ = tok.ancestorOrSelf { $0.as(DeclSyntax.self) }
            _ = t.node(at: tok.id)
        }
        let loc = conv.location(for: p)
        try check(conv.position(ofLine: loc.line, column: loc.column) == p, "position(ofLine:column:) inverts location(for:)")
        try check(conv.isValid(position: p), "a position inside the file is valid")
        _ = loc.presumedLine; _ = loc.presumedFile
    }
    _ = t.firstToken(viewMode: .all); _ = t.lastToken(viewMode: .sourceAccurate)
    _ = SourceLocationConverter(fileName: "f.swift", tree: t).sourceLines.count
}

// a rewriter that renames identifiers changes exactly those tokens; one that changes nothing keeps the tree
let rewrite = endpoint("rewrite") { (tree: Borrow<Tree>, name: Str<8, Plain>) throws -> New<Tree>? in
    final class Renamer: SyntaxRewriter {
        let name: String
        var count = 0
        init(_ name: String) { self.name = name; super.init(viewMode: .sourceAccurate) }
        override func visit(_ token: TokenSyntax) -> TokenSyntax {
            guard case .identifier = token.tokenKind else { return token }
            count += 1
            return token.with(\.tokenKind, .identifier(name))
        }
    }
    final class Identity: SyntaxRewriter {}
    final class Counter: SyntaxAnyVisitor {
        var nodes = 0
        override func visitAny(_ node: Syntax) -> SyntaxVisitorContinueKind { nodes += 1; return .visitChildren }
    }
    let t = tree.value
    let same = Identity(viewMode: .all).rewrite(t)
    try check(same.description == t.description, "an identity rewriter keeps the text")
    let c1 = Counter(viewMode: .all), c2 = Counter(viewMode: .all)
    c1.walk(t); c2.walk(same)
    try check(c1.nodes == c2.nodes, "an identity rewriter keeps every node")
    let r = Renamer(name.value.isValidSwiftIdentifier(for: .variableName) ? name.value : "x")
    let renamed = r.rewrite(t)
    let before = Array(t.tokens(viewMode: .sourceAccurate)).count
    try check(Array(renamed.tokens(viewMode: .sourceAccurate)).count == before, "renaming keeps the token count")
    _ = t.detached.description; _ = t.trimmed.description; _ = t.trimmedDescription
    _ = t.with(\.leadingTrivia, [.lineComment("// x"), .newlines(1)]).description
    return renamed.as(SourceFileSyntax.self).map { New($0) }
}

// trivia pieces add up to the trivia; comments have values
let trivia = endpoint("trivia") { (tree: Borrow<Tree>) throws in
    for tok in tree.value.tokens(viewMode: .sourceAccurate).prefix(200) {
        for tr in [tok.leadingTrivia, tok.trailingTrivia] {
            let total = tr.pieces.reduce(0) { $0 + $1.sourceLength.utf8Length }
            try check(total == tr.sourceLength.utf8Length, "trivia pieces add up to the trivia length")
            try check(Trivia(pieces: tr.pieces).description == tr.description, "trivia rebuilt from its pieces prints the same")
            _ = tr.docCommentValue
        }
        try check(tok.leadingTriviaLength == tok.leadingTrivia.sourceLength, "leadingTriviaLength is the trivia's length")
    }
}

// literal values, identifiers and version tuples
let literals = endpoint("literals") { (tree: Borrow<Tree>) throws in
    final class Lits: SyntaxVisitor {
        override func visit(_ node: StringLiteralExprSyntax) -> SyntaxVisitorContinueKind {
            _ = node.representedLiteralValue
            return .visitChildren
        }
        override func visit(_ node: IntegerLiteralExprSyntax) -> SyntaxVisitorContinueKind { _ = node.representedLiteralValue; _ = node.radix; return .visitChildren }
        override func visit(_ node: FloatLiteralExprSyntax) -> SyntaxVisitorContinueKind { _ = node.representedLiteralValue; return .visitChildren }
        override func visit(_ node: TupleExprSyntax) -> SyntaxVisitorContinueKind { _ = ExprSyntax(node).interpretedAsVersionTuple; return .visitChildren }
        override func visit(_ token: TokenSyntax) -> SyntaxVisitorContinueKind {
            _ = token.text.isValidSwiftIdentifier(for: .variableName)
            _ = token.text.isValidSwiftIdentifier(for: .memberAccess)
            _ = token.identifier
            if case .identifier = token.tokenKind { _ = Identifier(token)?.name }
            return .visitChildren
        }
    }
    Lits(viewMode: .sourceAccurate).walk(tree.value)
}

enum Cond: CaseIterable { case none, debug, linux, simulator, feature }

// SwiftIfConfig: ConfiguredRegions.isActive agrees with isActive(in:); removing inactive code keeps a parseable file
let ifConfig = endpoint("if_config") { (tree: Borrow<Tree>, cond: Pick<Cond>, big: Bool, retain: Bool, version: Between<4, 7>) throws in
    var cfg = StaticBuildConfiguration(languageVersion: VersionTuple(version.value), compilerVersion: VersionTuple(6, 2))
    switch cond.value {
    case .none: break
    case .debug: cfg.customConditions = ["DEBUG"]
    case .linux: cfg.targetOSs = ["Linux"]; cfg.targetArchitectures = ["x86_64"]
    case .simulator: cfg.targetEnvironments = ["simulator"]
    case .feature: cfg.features = ["Foo"]; cfg.attributes = ["MainActor"]
    }
    if big { cfg.endianness = .big; cfg.targetPointerBitWidth = 32 }
    let t = tree.value
    let regions = t.configuredRegions(in: cfg)
    _ = regions.diagnostics
    for (clause, state) in regions.prefix(16) {
        _ = clause.condition?.description; _ = state
        if let decl = clause.parent?.parent?.as(IfConfigDeclSyntax.self) {
            let (active, _) = decl.activeClause(in: cfg)
            try check(regions.activeClause(for: decl) == active, "ConfiguredRegions.activeClause agrees with activeClause(in:)")
        }
    }
    for tok in t.tokens(viewMode: .sourceAccurate).prefix(40) {
        try check(regions.isActive(tok) == tok.isActive(in: cfg).state, "ConfiguredRegions.isActive agrees with isActive(in:)")
    }
    let (result, _) = t.removingInactive(in: cfg)
    _ = retain
    let text = result.description
    log("active: \(text.prefix(300).debugDescription)")
    try check(Parser.parse(source: text).description == text, "the active code parses back")
    _ = regions.removingInactive(from: t).description
    _ = VersionTuple(parsing: "\(version.value).1.2")
}

// SwiftLexicalLookup: names found are declared inside the file
let lookup = endpoint("lookup") { (tree: Borrow<Tree>, s: Choices<16>, all: Bool, sequential: Bool) throws in
    let toks = Array(tree.value.tokens(viewMode: .sourceAccurate))
    try require(!toks.isEmpty, "no tokens")
    let n = tree.value.totalLength.utf8Length
    for _ in 0..<3 {
        let tok = s.source.pick(toks)
        let results = tok.lookup(all ? nil : Identifier(tok), with: LookupConfig(finishInSequentialScope: sequential))
        for r in results.prefix(32) {
            _ = r.scope.kind
            for name in r.names.prefix(32) {
                let p = name.position.utf8Offset
                try check(p >= 0 && p <= n, "a looked-up name lies inside the file")
                _ = name.identifier.name
            }
        }
    }
}

// SwiftRefactor: trailing closures and editor placeholder expansion
let refactor = endpoint("refactor") { (tree: Borrow<Tree>, start: Between<0, 3>) throws in
    final class Calls: SyntaxVisitor {
        var calls: [Syntax] = []
        var placeholders: [TokenSyntax] = []
        override func visit(_ node: FunctionCallExprSyntax) -> SyntaxVisitorContinueKind { calls.append(Syntax(node)); return .visitChildren }
        override func visit(_ node: MacroExpansionExprSyntax) -> SyntaxVisitorContinueKind { calls.append(Syntax(node)); return .visitChildren }
        override func visit(_ token: TokenSyntax) -> SyntaxVisitorContinueKind {
            if token.isEditorPlaceholder { placeholders.append(token) }
            return .visitChildren
        }
    }
    let v = Calls(viewMode: .sourceAccurate)
    v.walk(tree.value)
    for call in v.calls.prefix(10) {
        if let out = try? CallToTrailingClosures.refactor(syntax: call, in: .init(startAtArgument: start.value)) {
            _ = out.description
        }
    }
    for tok in v.placeholders.prefix(10) {
        let edits = ExpandEditorPlaceholder.textRefactor(syntax: tok)
        guard let text = apply(edits, to: tree.value.description) else { continue }
        log("expanded: \(text.prefix(200).debugDescription)")
        _ = Parser.parse(source: text)
    }
}

// node-level APIs on a random node: positions and lengths agree, identifiers round trip, collections
// lose exactly the removed element, source edits apply, common ancestors contain both nodes
let inspect = endpoint("inspect") { (tree: Borrow<Tree>, s: Choices<16>) throws in
    let t = tree.value
    final class All: SyntaxAnyVisitor {
        var nodes: [Syntax] = []
        override func visitAny(_ node: Syntax) -> SyntaxVisitorContinueKind { nodes.append(node); return .visitChildren }
    }
    let all = All(viewMode: .sourceAccurate)
    all.walk(t)
    try require(!all.nodes.isEmpty, "no nodes")
    let n = s.source.pick(all.nodes), m = s.source.pick(all.nodes)
    try check(n.position <= n.positionAfterSkippingLeadingTrivia && n.positionAfterSkippingLeadingTrivia <= n.endPositionBeforeTrailingTrivia
              && n.endPositionBeforeTrailingTrivia <= n.endPosition, "a node's positions are ordered")
    try check(n.position.advanced(by: n.totalLength.utf8Length) == n.endPosition, "position + totalLength == endPosition")
    try check(n.leadingTriviaLength + n.trimmedLength + n.trailingTriviaLength == n.totalLength, "trivia and content lengths add up")
    try check(n.syntaxTextBytes == Array(n.description.utf8), "syntaxTextBytes is the description")
    try check(n.root == Syntax(t), "the root of a node in the tree is the tree")
    try check(n.hasParent == (n.parent != nil), "hasParent agrees with parent")
    let idx = n.id.indexInTree
    try check(SyntaxIdentifier.fromIndexInTree(idx, relativeToRoot: t) == n.id, "fromIndexInTree(indexInTree) is the id")
    try check(t.node(at: n.id) == n, "node(at: id) finds the node")
    if let common = findCommonAncestorOrSelf(n, m) {
        try check(common.range.lowerBound <= min(n.position, m.position) && max(n.endPosition, m.endPosition) <= common.range.upperBound,
                  "a common ancestor contains both nodes")
    }
    _ = n.children(viewMode: .all).count; _ = n.syntaxNodeType; _ = n.hasWarning; _ = n.hasSequenceExpr; _ = n.keyPathInParent
    _ = n.kind.syntaxNodeType.structure; _ = n.asProtocol(SyntaxProtocol.self).kind; _ = n.customMirror.children.count
    var out = ""
    n.write(to: &out)
    try check(out == n.description, "write(to:) writes the description")
    let conv = SourceLocationConverter(fileName: "f.swift", tree: t)
    let r = n.sourceRange(converter: conv)
    try check(r.start.offset <= r.end.offset, "a source range is ordered")
    _ = n.startLocation(converter: conv, afterLeadingTrivia: false); _ = n.endLocation(converter: conv, afterTrailingTrivia: true)
    // source edits built from a node apply to the text
    let text = t.description
    for e in [SourceEdit.remove(n), SourceEdit.replace(n, with: "x"), SourceEdit.insert("y", before: n), SourceEdit.insert("z", after: n)] {
        guard let edited = apply([e], to: text) else { try check(false, "a node's source edit applies"); continue }
        try check(edited.utf8.count == text.utf8.count - e.range.upperBound.utf8Offset + e.range.lowerBound.utf8Offset + e.replacementLength.utf8Length,
                  "an edit changes the length by its replacement")
        _ = e.replacementRange; _ = e.range.overlapsOrTouches(m.range); _ = e.range.intersected(m.range)
    }
    // a collection loses exactly the removed element
    if var coll = n.as(CodeBlockItemListSyntax.self), !coll.isEmpty {
        let i = coll.index(coll.startIndex, offsetBy: s.source.range(0, coll.count - 1))
        let removed = coll.remove(at: i)
        try check(coll.description.utf8.count == n.description.utf8.count - removed.description.utf8.count, "remove(at:) removes one element's text")
    }
    if let tok = n.as(TokenSyntax.self) {
        _ = tok.tokenDiagnostic?.byteOffset; _ = tok.trailingTriviaLength; _ = tok.isEditorPlaceholder
        let tr = tok.leadingTrivia.appending(TriviaPiece.spaces(1)).merging(tok.trailingTrivia)
        _ = tr.description
        _ = Identifier(tok)?.isDollarIdentifier
    }
    let toks = Array(t.tokens(viewMode: .sourceAccurate))
    try check(Array(t.tokens(viewMode: .sourceAccurate).reversed()) == toks.reversed(), "reversed tokens are the tokens backwards")
}

// a StaticBuildConfiguration answers like its settings; the diagnostics formatter adds category footnotes
let buildConfig = endpoint("build_config") { (name: Str<12, Plain>, tree: Borrow<Tree>) throws in
    let cfg = StaticBuildConfiguration(customConditions: ["DEBUG", name.value], features: ["Foo"], attributes: ["MainActor"],
                                       targetOSs: ["Linux"], targetArchitectures: ["x86_64"], targetEnvironments: ["simulator"],
                                       targetRuntimes: ["_Native"], targetPointerAuthenticationSchemes: ["arm64e"],
                                       targetObjectFileFormats: ["ELF"], targetAtomicBitWidths: [64],
                                       languageVersion: VersionTuple(6), compilerVersion: VersionTuple(6, 2, 3))
    if !name.value.hasPrefix("$") {   // ("$Name" asks for a language feature instead)
        try check(cfg.isCustomConditionSet(name: name.value), "a custom condition that was set is set")
    }
    _ = cfg.hasFeature(name: name.value); _ = cfg.hasAttribute(name: name.value); _ = cfg.isActiveTargetOS(name: name.value)
    _ = cfg.isActiveTargetArchitecture(name: name.value); _ = cfg.isActiveTargetEnvironment(name: name.value)
    _ = cfg.isActiveTargetRuntime(name: name.value); _ = cfg.isActiveTargetPointerAuthentication(name: name.value)
    _ = cfg.isActiveTargetObjectFormat(name: name.value); _ = cfg.languageMode.components; _ = cfg.parserSwiftVersion
    if let v = VersionTuple(parsing: name.value) { _ = v.components; _ = v < VersionTuple(6) }
    let diags = ParseDiagnosticsGenerator.diagnostics(for: tree.value)
    var group = GroupedDiagnostics()
    let id = group.addSourceFile(tree: tree.value, displayName: name.value)
    for d in diags.prefix(8) { group.addDiagnostic(d, in: id); _ = d.highlights.count; _ = d.diagMessage.category }
    let fmt = DiagnosticsFormatter()
    _ = DiagnosticsFormatter.annotateSources(in: group)
    _ = fmt.categoryFootnotes(diags.compactMap { $0.diagMessage.category })
}
