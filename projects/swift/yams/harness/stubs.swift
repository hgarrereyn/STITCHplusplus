// Yams (libyaml's C parser and emitter + a Swift layer): load, compose (single, all, from bytes in UTF-8
// and UTF-16, with custom resolvers and constructors), serialize and dump with every emitter option, the
// Emitter class, Node building and editing, scalar representation/construction, Codable with aliasing
// strategies, error descriptions with snippets.
// Oracles: a composed node serializes (with any non-canonical, unsorted options) to text that composes to
// an equal node; serialize(nodes:) composes back to the same documents; dump(load(x)) reaches a fixed point;
// a scalar's representation constructs the same value; a Codable value survives encode/decode, with or
// without anchors/aliases; parsing from UTF-8 and UTF-16 agrees. Thrown errors are bails.
import Foundation
import Stitch
import Yams

enum Doc: StitchObject { typealias Handle = Node }

enum YamlText: Decoded {
    static func decode(_ s: Source) -> String {
        var out = s.coin(0.2) ? "%YAML 1.1\n---\n" : ""
        var indent = 0
        while s.more() {
            let pad = String(repeating: " ", count: indent)
            switch s.choice(10) {
            case 0: out += "\(pad)\(s.coin(0.7) ? s.pick(["a", "b", "key", "x y", "? k", "<<", "&a k", "*a"]) : s.str(6)): \(s.str(8))\n"
            case 1: out += "\(pad)- \(s.str(8))\n"
            case 2:
                out += "\(pad)\(s.pick(["k", "m", "seq", "&x m", "!!omap m", "!!set s", "!!pairs p"])):\n"
                indent = min(indent + 2, 12)
            case 3: indent = max(indent - 2, 0)
            case 4: out += "\(pad)\(s.pick(["!!str", "!!int", "!!binary", "!!map", "!!seq", "!custom", "!!timestamp", "!!float", "!!bool", "!!null"])) \(s.str(8))\n"
            case 5: out += "\(pad)\(s.pick(["|", ">", "|-", ">+", "|2", "|9"]))\n\(pad)  \(s.str(12))\n"
            case 6: out += "\(pad){\(s.str(4)): [\(s.str(4)), \(s.str(4))]}\n"
            case 7: out += s.pick(["---\n", "...\n", "# c\n", "\t", "\"\\u00e9\"", "'q''q'", "~", "null", "0x1F", "1e3", ".inf", "-.nan",
                                   "2001-12-14t21:59:43.10-05:00", "0o17", "+12_000", "*x", "<<: *x\n", "%TAG ! tag:e.com,2000:\n"])
            case 8: out += "\(pad)\(s.pick(["a", "b", "k"])): \(s.pick(["&x", "*x", "!!str", "&y [1, 2]", "*y"])) \(s.str(4))\n"
            default: out += s.token()
            }
        }
        return out
    }
}

// YAML shaped like the Codable types decode_types asks for: sequences and mappings of typed scalars
let typedScalars = ["1", "-0", "127", "-129", "1.5e3", ".nan", "-.inf", "true", "no", "null", "~", "", "2001-12-14", "2001-12-14t21:59:43.10-05:00",
                    "1969-12-31T23:59:59.999Z", "aGVsbG8=", "\"http://x/a b\"", "C54A4EDB-AA93-4D49-B2F5-C60E33063F7E", "18446744073709551616",
                    "0x1F", "0o17", "abc", "'q'", "alpha", "gamma", "1e400", "4.2e-45", "12345.678901234567890", "*a", "&a 1"]
enum TypedText: Decoded {
    static func decode(_ s: Source) -> String {
        func scalar() -> String { s.coin(0.85) ? s.pick(typedScalars) : s.str(8) }
        func flowSeq() -> String { "[" + (0..<s.range(0, 4)).map { _ in scalar() }.joined(separator: ", ") + "]" }
        var out = ""
        switch s.choice(4) {
        case 0: while s.more() { out += "- \(s.coin(0.2) ? flowSeq() : scalar())\n" }
        case 1: while s.more() { out += "\(s.coin(0.5) ? s.pick(["name", "count", "small", "big", "tags", "ratio", "f", "flag", "data", "when", "id", "dec", "kind", "nested", "children", "maybe", "label", "n"]) : scalar()): \(s.coin(0.3) ? flowSeq() : scalar())\n" }
        case 2: while s.more() { out += "- {label: \(scalar()), n: \(scalar())}\n" }
        default: while s.more() { out += "- {\(scalar()): \(s.coin() ? scalar() : flowSeq())}\n" }
        }
        return out
    }
}

enum YamlBytes: DecodedBytes {
    static func decode(_ s: Source) -> [UInt8] {
        var out = Array(s.draw(YamlText.self).utf8)
        while s.coin(0.15) {
            out.insert(contentsOf: s.pick([[0xFF], [0xC3], [0xEF, 0xBB, 0xBF], [0x00], [0xE2, 0x80, 0xA8], [0xF0, 0x9F, 0x98, 0x80]]),
                       at: s.range(0, out.count))
        }
        return out
    }
}

// MARK: - helpers

enum ResolverChoice: CaseIterable { case standard, basic, noInt, noBool, noMerge }
func resolver(_ r: ResolverChoice) -> Resolver {
    switch r {
    case .standard: return .default
    case .basic: return .basic
    case .noInt: return Resolver.default.removing(.int)
    case .noBool: return Resolver.default.removing(.bool)
    case .noMerge: return Resolver.default.removing(.merge)
    }
}

/// Run `f`; on a YamlError, format it (with its snippet) before passing it on as a bail.
func described<T>(_ yaml: String, _ f: () throws -> T) throws -> T {
    do { return try f() } catch {
        _ = String(describing: error)
        if let e = error as? YamlError {
            _ = e.description
            if case let .parser(context, _, mark, _) = e { _ = mark.snippet(from: yaml); _ = context?.description }
            if case let .scanner(context, _, mark, _) = e { _ = mark.snippet(from: yaml); _ = context?.description }
            if case let .composer(context, _, mark, _) = e { _ = mark.snippet(from: yaml); _ = context?.description }
        }
        throw error
    }
}

/// Equal as far as the text form can carry it: Yams emits every scalar with implicit tags, so a scalar
/// whose tag was explicit (e.g. `!!str 1`, or a Node built with a tag) comes back with the tag the resolver
/// gives its text. (Collection tags are compared resolved: Tag is a class whose name resolves in place.) Implicitly tagged scalars (and all collections) must compare equal with their tags.
func sameNode(_ a: Node, _ b: Node) -> Bool {
    switch (a, b) {
    case let (.scalar(x), .scalar(y)):
        let implicit = x.tag.rawValue.isEmpty || x.tag.rawValue == "!"
        return x.string == y.string && (!implicit || x == y)
    case let (.sequence(x), .sequence(y)):
        return x.count == y.count && Node.sequence(.init([], x.tag)) == Node.sequence(.init([], y.tag)) && zip(x, y).allSatisfy { sameNode($0, $1) }
    case let (.mapping(x), .mapping(y)):
        return x.count == y.count && Node.mapping(.init([], x.tag)) == Node.mapping(.init([], y.tag)) && zip(x, y).allSatisfy { sameNode($0.key, $1.key) && sameNode($0.value, $1.value) }
    case let (.alias(x), .alias(y)):
        return x == y
    default:
        return false
    }
}

/// An empty plain scalar at the root is emitted as an empty document, which composes to no document.
func isEmptyRoot(_ n: Node) -> Bool {
    guard let s = n.scalar else { return false }
    return s.string.isEmpty && (s.style == .any || s.style == .plain)
}

/// Every document of a stream (at most 65).
func allDocs(_ text: String, _ r: Resolver = .default) throws -> [Node] {
    var out: [Node] = []
    var seq = try Yams.compose_all(yaml: text, r)
    while let n = seq.next() {
        out.append(n)
        if out.count > 64 { break }
    }
    if let e = seq.error { throw e }
    return out
}

enum LineBreakChoice: CaseIterable {
    case ln, cr, crln
    var lineBreak: Emitter.LineBreak { self == .ln ? .ln : self == .cr ? .cr : .crln }
}
enum SeqStyle: CaseIterable { case any, block, flow
    var style: Node.Sequence.Style { self == .any ? .any : self == .block ? .block : .flow }
    var mapStyle: Node.Mapping.Style { self == .any ? .any : self == .block ? .block : .flow }
}
enum ScalarStyle: CaseIterable { case any, plain, single, double, literal, folded
    var style: Node.Scalar.Style {
        switch self {
        case .any: return .any
        case .plain: return .plain
        case .single: return .singleQuoted
        case .double: return .doubleQuoted
        case .literal: return .literal
        case .folded: return .folded
        }
    }
}

// Emitter options as one structured value (more than the 12 arguments a stub takes)
struct EmitOpts {
    var canonical = false, indent = 2, width = 80, allowUnicode = false, lineBreak = LineBreakChoice.ln
    var explicitStart = false, explicitEnd = false, version = false, sortKeys = false
    var seq = SeqStyle.any, map = SeqStyle.any, newline = ScalarStyle.any
    var options: Emitter.Options {
        Emitter.Options(canonical: canonical, indent: indent, width: width, allowUnicode: allowUnicode, lineBreak: lineBreak.lineBreak,
                        explicitStart: explicitStart, explicitEnd: explicitEnd, version: version ? (1, 1) : nil, sortKeys: sortKeys,
                        sequenceStyle: seq.style, mappingStyle: map.mapStyle, newLineScalarStyle: newline.style)
    }
}

enum Opts: Decoded {
    static func decode(_ s: Source) -> EmitOpts {
        var o = EmitOpts()
        o.canonical = s.coin(0.1); o.indent = s.range(0, 10); o.width = s.range(-1, 100); o.allowUnicode = s.coin()
        o.lineBreak = s.pick(LineBreakChoice.allCases); o.explicitStart = s.coin(0.3); o.explicitEnd = s.coin(0.3)
        o.version = s.coin(0.2); o.sortKeys = s.coin(0.2)
        o.seq = s.pick(SeqStyle.allCases); o.map = s.pick(SeqStyle.allCases); o.newline = s.pick(ScalarStyle.allCases)
        return o
    }
}

func serialize(_ node: Node, _ o: EmitOpts) throws -> String {
    try Yams.serialize(node: node, canonical: o.canonical, indent: o.indent, width: o.width, allowUnicode: o.allowUnicode,
                       lineBreak: o.lineBreak.lineBreak, explicitStart: o.explicitStart, explicitEnd: o.explicitEnd,
                       version: o.version ? (1, 1) : nil, sortKeys: o.sortKeys, sequenceStyle: o.seq.style,
                       mappingStyle: o.map.mapStyle, newLineScalarStyle: o.newline.style)
}

// MARK: - parsing

let compose = endpoint("compose") { (text: Str<1024, YamlText>, r: Pick<ResolverChoice>) throws -> New<Doc>? in
    try described(text.value) { try Yams.compose(yaml: text.value, resolver(r.value)).map { New($0) } }
}

// serialize(nodes:) of every document composes back to the same documents
let composeAll = endpoint("compose_all") { (text: Str<1024, YamlText>) throws -> New<Doc>? in
    let docs = try described(text.value) { try allDocs(text.value) }
    guard docs.count <= 64 else { return docs.first.map { New($0) } }
    let out = try Yams.serialize(nodes: docs)
    log("serialized: \(out.debugDescription)")
    let again = try allDocs(out)
    let kept = docs.filter { !isEmptyRoot($0) }
    if kept.count == docs.count || again.count == kept.count {
        let want = again.count == kept.count ? kept : docs
        try check(again.count == want.count && zip(again, want).allSatisfy { sameNode($1, $0) }, "compose_all(serialize(nodes:)) == the documents")
    }
    return docs.first.map { New($0) }
}

enum ConstructorChoice: CaseIterable { case standard, nsMutable, custom }

let load = endpoint("load") { (text: Str<1024, YamlText>, r: Pick<ResolverChoice>, c: Pick<ConstructorChoice>) throws in
    let cons: Constructor
    switch c.value {
    case .standard: cons = .default
    case .nsMutable: cons = Constructor(Constructor.defaultScalarMap, Constructor.nsMutableMappingMap, Constructor.nsMutableSequenceMap)
    case .custom:
        var m = Constructor.defaultScalarMap
        m[.int] = { $0.string.count }
        cons = Constructor(m)
    }
    _ = try described(text.value) { try Yams.load(yaml: text.value, resolver(r.value), cons) }
}

let loadAll = endpoint("load_all") { (text: Str<1024, YamlText>) throws in
    var n = 0
    var seq = try described(text.value) { try Yams.load_all(yaml: text.value) }
    while seq.next() != nil {
        n += 1
        if n > 64 { break }
    }
    _ = seq.error.map { String(describing: $0) }
}

// parsing bytes as UTF-8 and the same text as UTF-16 agree
let parseBytes = endpoint("parse_bytes") { (b: Bytes<1024, YamlBytes>, utf16: Bool, single: Bool) throws -> New<Doc>? in
    let data = Data(b.value)
    let p = try Parser(yaml: data, encoding: .utf8)
    var first: Node? = nil
    if single {
        first = try described(p.yaml) { try p.singleRoot() }
    } else {
        var n = 0
        while let x = try described(p.yaml, { try p.nextRoot() }) {
            if first == nil { first = x }
            n += 1
            if n > 64 { break }
        }
    }
    if utf16 {
        let q = try Parser(yaml: p.yaml, resolver: p.resolver, constructor: p.constructor, encoding: .utf16)
        let other = single ? try q.singleRoot() : try q.nextRoot()
        try check(other == first, "UTF-8 and UTF-16 parsing agree")
    }
    return first.map { New($0) }
}

// MARK: - serialization oracles

// serialize(node) composes back to an equal node
let roundtrip = endpoint("roundtrip") { (doc: Borrow<Doc>) throws in
    let text = try Yams.serialize(node: doc.value)
    log("serialized: \(text.debugDescription)")
    guard let again = try Yams.compose(yaml: text) else {
        try check(isEmptyRoot(doc.value), "serialized node composes")
        return
    }
    try check(sameNode(doc.value, again), "compose(serialize(node)) == node")
}

// with any non-canonical, unsorted options the text still composes to an equal node
let serializeOptions = endpoint("serialize_options") { (doc: Borrow<Doc>, o: Val<Opts, 32>) throws in
    let text = try serialize(doc.value, o.value)
    log("serialized: \(text.debugDescription)")
    let again = try Yams.compose(yaml: text)
    try check(again != nil || isEmptyRoot(doc.value), "serialized node composes")
    if !o.value.canonical && !o.value.sortKeys, let again {
        try check(sameNode(doc.value, again), "compose(serialize(node, options)) == node")
    }
}

// the Emitter class: several documents in one stream
let emitter = endpoint("emitter") { (a: Borrow<Doc>, b: Borrow<Doc>, o: Val<Opts, 32>, twice: Bool) throws in
    let e = Emitter(canonical: o.value.canonical, indent: o.value.indent, width: o.value.width, allowUnicode: o.value.allowUnicode,
                    lineBreak: o.value.lineBreak.lineBreak, explicitStart: o.value.explicitStart, explicitEnd: o.value.explicitEnd,
                    version: o.value.version ? (1, 1) : nil, sortKeys: o.value.sortKeys, sequenceStyle: o.value.seq.style,
                    mappingStyle: o.value.map.mapStyle, newLineScalarStyle: o.value.newline.style)
    e.options = o.value.options
    try e.open()
    try e.serialize(node: a.value)
    if twice { try e.serialize(node: b.value) }
    try e.close()
    let text = String(decoding: e.data, as: UTF8.self)
    log("emitted: \(text.debugDescription)")
    let docs = try allDocs(text)
    if !isEmptyRoot(a.value) && !(twice && isEmptyRoot(b.value)) {
        try check(docs.count == (twice ? 2 : 1), "an emitted stream composes to as many documents")
        if !o.value.canonical && !o.value.sortKeys {
            try check(sameNode(a.value, docs[0]) && (!twice || sameNode(b.value, docs[1])), "emitted documents compose back equal")
        }
    }
    _ = try? e.serialize(node: a.value)   // after close: an error, not a crash
}

func dumpSorted(_ v: Any) throws -> String { try Yams.dump(object: v, sortKeys: true) }

// dump(load(...)) reaches a fixed point
let dumpLoad = endpoint("dump_load") { (text: Str<1024, YamlText>, o: Val<Opts, 32>) throws in
    guard let v = try Yams.load(yaml: text.value) else { return }
    let out = try dumpSorted(v)
    log("dumped: \(out.debugDescription)")
    _ = try? Yams.dump(object: v, canonical: o.value.canonical, indent: o.value.indent, width: o.value.width,
                       allowUnicode: o.value.allowUnicode, lineBreak: o.value.lineBreak.lineBreak, explicitStart: o.value.explicitStart,
                       explicitEnd: o.value.explicitEnd, version: o.value.version ? (1, 1) : nil, sortKeys: o.value.sortKeys,
                       sequenceStyle: o.value.seq.style, mappingStyle: o.value.map.mapStyle, newLineScalarStyle: o.value.newline.style)
    guard let v2 = try Yams.load(yaml: out) else {
        // an empty scalar (empty string or Data) is dumped as an empty plain scalar, which loads as no document
        try check(out.allSatisfy { $0.isWhitespace }, "dumped text loads")
        return
    }
    let out2 = try dumpSorted(v2)
    guard let v3 = try Yams.load(yaml: out2) else { return }
    try check(try dumpSorted(v3) == out2, "dump(load(dump(load(x)))) is a fixed point")
}

let dumpAll = endpoint("dump_all") { (text: Str<1024, YamlText>) throws in
    var objs: [Any] = []
    var seq = try Yams.load_all(yaml: text.value)
    while let v = seq.next(), objs.count < 16 { objs.append(v) }
    _ = try Yams.dump(objects: objs, sortKeys: true)
}

// MARK: - nodes

let inspect = endpoint("inspect") { (doc: Borrow<Doc>, key: Str<8, Plain>, i: Between<-1, 8>) throws in
    let n = doc.value
    _ = n[key.value]; _ = n[i.value]; _ = n[Node(key.value)]
    _ = n.string; _ = n.int; _ = n.float; _ = n.bool; _ = n.timestamp; _ = n.binary; _ = n.null; _ = n.uuid
    _ = n.tag.description; _ = n.mark?.description; _ = n.anchor?.rawValue
    _ = n.array(); _ = n.array(of: Int.self); _ = n.array(of: String.self); _ = n.array(of: Double.self)
    if let m = n.mapping {
        try check(m.keys.count == m.count && m.values.count == m.count, "keys and values have count entries")
        for k in m.keys.prefix(8) {
            try check(m[k] != nil, "every key of a mapping finds a value")
            try check(m.index(forKey: k) != nil, "every key of a mapping has an index")
        }
        _ = m.style; _ = m.tag
    }
    if let s = n.sequence {
        try check(s.indices.count == s.count, "indices cover the sequence")
        if !s.isEmpty { _ = s[s.startIndex]; _ = s.index(before: s.endIndex); _ = s.distance(from: s.startIndex, to: s.endIndex) }
    }
    if let s = n.scalar { _ = s.style; _ = s.string; _ = Node.Scalar(s.string) < s }
    _ = n < Node("m")
    _ = n.any   // last: constructing Swift values can trap (a known Yams bug) and would hide the checks above
}

let setKey = endpoint("set_key") { (doc: Borrow<Doc>, key: Str<8, Plain>, value: Str<16, Plain>) throws in
    doc.value[key.value] = Node(value.value)
    if doc.value.mapping != nil {
        try check(doc.value[key.value] == Node(value.value), "a set key reads back")
    }
}

enum NodeEdit: CaseIterable { case append, replace, remove, removeKey, setIndex, sortMap, styleSeq, styleMap }

let editNode = endpoint("edit_node") { (doc: Borrow<Doc>, other: Borrow<Doc>, s: Choices<4>) throws in
    // an edit that applies to this kind of node
    let op: NodeEdit
    switch doc.value {
    case .sequence: op = s.source.pick([.append, .replace, .remove, .setIndex, .styleSeq])
    case .mapping: op = s.source.pick([.append, .removeKey, .sortMap, .styleMap])
    default: throw Bail("a scalar or alias")
    }
    switch (doc.value, op) {
    case (.sequence(var seq), .append):
        seq.append(other.value)
        try check(seq.last == other.value, "an appended node is last")
        doc.value = .sequence(seq)
    case (.sequence(var seq), .replace):
        try require(!seq.isEmpty, "empty sequence")
        let lo = s.source.range(0, seq.count - 1), hi = s.source.range(lo, seq.count)
        seq.replaceSubrange(lo..<hi, with: [other.value, other.value])
        doc.value = .sequence(seq)
    case (.sequence(var seq), .remove):
        try require(!seq.isEmpty, "empty sequence")
        seq.remove(at: s.source.range(0, seq.count - 1))
        doc.value = .sequence(seq)
    case (.sequence, .setIndex):
        let n = doc.value.sequence!.count
        try require(n > 0, "empty sequence")
        let i = s.source.range(0, n - 1)
        doc.value[Node(String(i), Tag(.int))] = other.value
        try check(doc.value.sequence![i] == other.value, "setting an index replaces that element")
    case (.sequence(var seq), .styleSeq):
        seq.style = s.source.pick([.any, .block, .flow])
        doc.value = .sequence(seq)
    case (.mapping(var map), .removeKey):
        let keys = Array(map.keys)
        try require(!keys.isEmpty, "empty mapping")
        let k = s.source.pick(keys)
        map[k] = nil
        doc.value = .mapping(map)
    case (.mapping(var map), .append):
        map[other.value] = Node("v")
        try check(map[other.value] == Node("v"), "a node key reads back")
        doc.value = .mapping(map)
    case (.mapping(let map), .sortMap):
        doc.value = .mapping(Node.Mapping(map.sorted { $0.key < $1.key }, map.tag, map.style))
    case (.mapping(var map), .styleMap):
        map.style = s.source.pick([.any, .block, .flow])
        doc.value = .mapping(map)
    default: throw Bail("op does not apply to this node")
    }
}

enum TagChoice: CaseIterable { case implicit, str, int, float, bool, null, binary, timestamp, custom, nonSpecific }
func tagName(_ t: TagChoice, _ custom: String) -> Tag.Name {
    switch t {
    case .implicit: return .implicit
    case .str: return .str
    case .int: return .int
    case .float: return .float
    case .bool: return .bool
    case .null: return .null
    case .binary: return .binary
    case .timestamp: return .timestamp
    case .custom: return Tag.Name(rawValue: "!" + custom)
    case .nonSpecific: return .nonSpecific
    }
}
func tag(_ t: TagChoice, _ custom: String) -> Tag { t == .implicit ? .implicit : Tag(tagName(t, custom)) }

let newScalar = endpoint("new_scalar") { (text: Str<32, Plain>, t: Pick<TagChoice>, style: Pick<ScalarStyle>, anchor: Str<6, Plain>,
                                          anchored: Bool) throws -> New<Doc> in
    let tg = tag(t.value, anchor.value)
    _ = tg.copy(with: .str).description; _ = tg.rawValue
    return New(.scalar(Node.Scalar(text.value, tg, style.value.style, nil, anchored ? Anchor(rawValue: anchor.value) : nil)))
}

let newCollection = endpoint("new_collection") { (a: Borrow<Doc>, b: Borrow<Doc>, mapping: Bool, style: Pick<SeqStyle>) throws -> New<Doc> in
    if mapping {
        return New(Node([(a.value, b.value), (Node("k"), a.value)], .implicit, style.value.mapStyle))
    }
    return New(Node([a.value, b.value], .implicit, style.value.style))
}

let anchors = endpoint("anchors") { (name: Str<12, Plain>, doc: Borrow<Doc>) throws in
    _ = Anchor.is_cyamlAlpha(name.value)
    let a = Anchor(rawValue: name.value)
    _ = a.description
    // an anchored node and an alias to it
    if Anchor.is_cyamlAlpha(name.value) && !name.value.isEmpty {
        let seq = Node([doc.value, .alias(Node.Alias(a))])
        let text = try Yams.serialize(node: seq)
        log("anchored: \(text.debugDescription)")
        _ = try? Yams.compose(yaml: text)
    }
}

// MARK: - resolver

let customResolver = endpoint("custom_resolver") { (pattern: Str<24, Plain>, text: Str<512, YamlText>, t: Pick<TagChoice>, how: Between<0, 2>) throws in
    let name = tagName(t.value, "x")
    let rule = try Resolver.Rule(name, pattern.value)
    _ = rule.pattern
    let r: Resolver
    switch how.value {
    case 0: r = Resolver.default.appending(rule)
    case 1: r = try Resolver.default.replacing(name, with: pattern.value)
    default: r = try Resolver.basic.appending(name, pattern.value).replacing(rule)
    }
    guard let node = try Yams.compose(yaml: text.value, r) else { return }
    _ = r.resolveTag(of: node)
    _ = r.rules.count
}

// MARK: - scalars

// representing a value and constructing it back gives the same value
let scalars = endpoint("scalars") { (i: Int64, u: UInt64, d: Double, f: Float, b: Bool, bytes: Bytes<32, Plain>,
                                      str: Str<32, Plain>) throws in
    try check(Int64.construct(from: i.represented()) == i, "Int64 round trips")
    try check(UInt64.construct(from: u.represented()) == u, "UInt64 round trips")
    try check(Int.construct(from: Int(truncatingIfNeeded: i).represented()) == Int(truncatingIfNeeded: i), "Int round trips")
    try check(UInt.construct(from: UInt(truncatingIfNeeded: u).represented()) == UInt(truncatingIfNeeded: u), "UInt round trips")
    try check(Int8.construct(from: Int8(truncatingIfNeeded: i).represented()) == Int8(truncatingIfNeeded: i), "Int8 round trips")
    try check(Bool.construct(from: b.represented()) == b, "Bool round trips")
    let data = Data(bytes.value)
    try check(Data.construct(from: data.represented()) == data, "Data round trips")
    try check(String.construct(from: Node.Scalar(str.value)) == str.value, "String round trips")
    let id = UUID()
    try check(UUID.construct(from: id.represented()) == id, "UUID round trips")
    if let back = Double.construct(from: d.represented()) {
        if d.isNaN { try check(back.isNaN, "NaN round trips") }
        else if d.isInfinite { try check(back == d, "infinity round trips") }
        else { try check(abs(back - d) <= abs(d) * 1e-14, "Double round trips to 15 digits") }
    } else { try check(false, "a represented Double constructs") }
    if let back = Float.construct(from: f.represented()) {
        if f.isNaN { try check(back.isNaN, "Float NaN round trips") }
        else if f.isInfinite { try check(back == f, "Float infinity round trips") }
        else { try check(abs(back - f) <= abs(f) * 1e-6, "Float round trips to 7 digits") }
    } else { try check(false, "a represented Float constructs") }
    _ = Double.construct(from: Node.Scalar(str.value))
    _ = Int.construct(from: Node.Scalar(str.value)); _ = Date.construct(from: Node.Scalar(str.value))
    _ = Data.construct(from: Node.Scalar(str.value)); _ = Bool.construct(from: Node.Scalar(str.value))
    _ = NSNull.construct(from: Node.Scalar(str.value)); _ = UInt.construct(from: Node.Scalar(str.value))
    _ = Decimal.construct(from: Node.Scalar(str.value)); _ = URL.construct(from: Node.Scalar(str.value))
}

// a Date's representation constructs the same date (to the millisecond)
let dateScalar = endpoint("date_scalar") { (secs: Between<-100000, 400000>, ms: Between<0, 999>, codable: Bool) throws in
    let date = Date(timeIntervalSince1970: Double(secs.value) * 10007 + Double(ms.value) / 1000)
    if codable {
        let text = try YAMLEncoder().encode([date])
        log("encoded: \(text.debugDescription)")
        let back = try YAMLDecoder().decode([Date].self, from: text)
        try check(abs(back[0].timeIntervalSince(date)) < 1e-5, "a Date survives YAMLEncoder/YAMLDecoder")
    } else {
        let s = date.represented()
        log("represented: \(s.string.debugDescription)")
        guard let back = Date.construct(from: s) else { try check(false, "a represented Date constructs"); return }
        try check(abs(back.timeIntervalSince(date)) < 0.001, "Date round trips to the millisecond")
    }
}

// a Double survives YAMLEncoder/YAMLDecoder exactly (the encoder formats Codable floats with 17 digits)
let codableDouble = endpoint("codable_double") { (d: Double) throws in
    try require(!d.isNaN, "NaN")
    let text = try YAMLEncoder().encode([d])
    log("encoded: \(text.debugDescription)")
    try check(try YAMLDecoder().decode([Double].self, from: text) == [d], "a Double survives YAMLEncoder/YAMLDecoder exactly")
}

// MARK: - Codable

enum Kind: String, Codable, CaseIterable { case alpha, beta, gamma }

struct Child: Codable, Equatable, Hashable {
    var label: String
    var n: Int16
}

/// (Doubles are compared to 15 digits: see codable_double.)
func close(_ a: Double?, _ b: Double?) -> Bool {
    guard let a, let b else { return a == nil && b == nil }
    return a == b || abs(a - b) <= abs(a) * 1e-14
}

struct Item: Codable, Equatable {
    var name: String
    var count: Int
    var small: Int8
    var big: UInt64
    var tags: [String]
    var ratio: Double?
    var f: Float
    var flag: Bool
    var data: Data
    var when: Date
    var id: UUID
    var dec: Decimal
    var kind: Kind
    var nested: [String: String]
    var children: [Child]
    var maybe: [String?]

    static func == (a: Item, b: Item) -> Bool {
        a.name == b.name && a.count == b.count && a.small == b.small && a.big == b.big && a.tags == b.tags && close(a.ratio, b.ratio)
            && a.f == b.f && a.flag == b.flag && a.data == b.data && abs(a.when.timeIntervalSince(b.when)) < 1e-5 && a.id == b.id
            && a.dec == b.dec && a.kind == b.kind && a.nested == b.nested && a.children == b.children && a.maybe == b.maybe
    }
}

enum Items: Decoded {
    static func decode(_ s: Source) -> Item {
        let name = s.coin(0.3) ? s.pick(["null", "~", "true", "1.5", "<<", "", "- a", "a: b", "'", "\"", "#", "\n"]) : s.str(16)
        let d = s.f64()
        let f = Float(bitPattern: s.u32())
        let child = Child(label: s.str(8), n: Int16(truncatingIfNeeded: s.i64()))
        return Item(name: name, count: Int(truncatingIfNeeded: s.i64()), small: Int8(truncatingIfNeeded: s.u8()), big: s.u64(), tags: [s.str(6), name],
                    ratio: d.isNaN ? nil : d, f: f.isNaN ? 0 : f, flag: s.coin(), data: Data(s.bytes(12)),
                    when: Date(timeIntervalSince1970: s.f64(1e9, 4e9)), id: UUID(), dec: Decimal(sign: s.coin() ? .minus : .plus,
                    exponent: s.range(-4, 4), significand: Decimal(s.range(0, 1_000_000))), kind: s.pick(Kind.allCases),
                    nested: [s.str(6): s.str(6), "k": name], children: s.coin() ? [child, child] : [child], maybe: [nil, s.str(4)])
    }
}

enum Aliasing: CaseIterable { case none, hashable, strict }

let codable = endpoint("codable") { (item: Val<Items, 256>, aliasing: Pick<Aliasing>, deref: Bool, o: Val<Opts, 32>) throws in
    let enc = YAMLEncoder()
    var opts = o.value.options
    opts.canonical = false
    switch aliasing.value {
    case .none: break
    case .hashable: opts.redundancyAliasingStrategy = HashableAliasingStrategy()
    case .strict: opts.redundancyAliasingStrategy = StrictEncodableAliasingStrategy()
    }
    enc.options = opts
    let text = try enc.encode(item.value)
    log("encoded: \(text.debugDescription)")
    let dec = YAMLDecoder()
    if deref { dec.options.aliasDereferencingStrategy = BasicAliasDereferencingStrategy() }
    let back = try dec.decode(Item.self, from: text)
    try check(back == item.value, "YAMLDecoder(YAMLEncoder(item)) == item")
    let viaData = try YAMLDecoder(encoding: .utf8).decode(Item.self, from: Data(text.utf8))
    try check(viaData == item.value, "decoding from Data agrees")
    if let node = try Yams.compose(yaml: text) {
        try check(try YAMLDecoder().decode(Item.self, from: node) == item.value, "decoding from the composed Node agrees")
    }
}

let decodeTypes = endpoint("decode_types") { (text: Str<1024, TypedText>, which: Between<0, 11>) throws in
    let d = YAMLDecoder()
    switch which.value {
    case 0: _ = try d.decode(Item.self, from: text.value)
    case 1: _ = try d.decode([String: [Int]].self, from: text.value)
    case 2: _ = try d.decode([Double].self, from: text.value)
    case 3: _ = try d.decode([Date].self, from: text.value)
    case 4: _ = try d.decode([Data].self, from: text.value)
    case 5: _ = try d.decode([URL].self, from: text.value)
    case 6: _ = try d.decode([Decimal].self, from: text.value)
    case 7: _ = try d.decode([UUID?].self, from: text.value)
    case 8: _ = try d.decode([Int8: UInt64].self, from: text.value)
    case 9: _ = try d.decode([Kind: [Float]].self, from: text.value)
    case 10: _ = try d.decode(Set<Child>.self, from: text.value)
    default: _ = try d.decode([[String: Bool]].self, from: text.value)
    }
}

let decodeNode = endpoint("decode_node") { (doc: Borrow<Doc>, which: Between<0, 3>) throws in
    let d = YAMLDecoder()
    switch which.value {
    case 0: _ = try d.decode(Item.self, from: doc.value)
    case 1: _ = try d.decode([String: String].self, from: doc.value)
    case 2: _ = try d.decode([Int].self, from: doc.value)
    default: _ = try d.decode(String.self, from: doc.value)
    }
}
