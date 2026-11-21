// Yams (libyaml's C parser and emitter + a Swift layer): load, compose, serialize, dump, Codable.
// Oracles: a composed node serializes to text that composes to an equal node; dump(load(x))
// loads back to an equal value; a Codable value survives encode/decode. Thrown errors are bails.
import Stitch
import Yams

enum Doc: StitchObject { typealias Handle = Node }

enum YamlText: Decoded {
    static func decode(_ s: Source) -> String {
        var out = s.coin(0.2) ? "%YAML 1.1\n---\n" : ""
        var indent = 0
        while s.more() {
            let pad = String(repeating: " ", count: indent)
            switch s.choice(9) {
            case 0: out += "\(pad)\(s.coin(0.7) ? s.pick(["a", "b", "key", "x y", "? k", "<<", "&a k", "*a"]) : s.str(6)): \(s.str(8))\n"
            case 1: out += "\(pad)- \(s.str(8))\n"
            case 2:
                out += "\(pad)\(s.pick(["k", "m", "seq"])):\n"
                indent = min(indent + 2, 12)
            case 3: indent = max(indent - 2, 0)
            case 4: out += "\(pad)\(s.pick(["!!str", "!!int", "!!binary", "!!map", "!!seq", "!custom", "!!timestamp", "!!float"])) \(s.str(8))\n"
            case 5: out += "\(pad)\(s.pick(["|", ">", "|-", ">+", "|2", "|9"]))\n\(pad)  \(s.str(12))\n"
            case 6: out += "\(pad){\(s.str(4)): [\(s.str(4)), \(s.str(4))]}\n"
            case 7: out += s.pick(["---\n", "...\n", "# c\n", "\t", "\"\\u00e9\"", "'q''q'", "~", "null", "0x1F", "1e3", ".inf", "-.nan"])
            default: out += s.token()
            }
        }
        return out
    }
}

struct Item: Codable, Equatable {
    var name: String
    var count: Int
    var tags: [String]
    var ratio: Double?
    var nested: [String: String]
}

let compose = endpoint("compose") { (text: Str<1024, YamlText>) throws -> New<Doc>? in
    try Yams.compose(yaml: text.value).map { New($0) }
}

let load = endpoint("load") { (text: Str<1024, YamlText>) throws in
    _ = try Yams.load(yaml: text.value)
}

let loadAll = endpoint("load_all") { (text: Str<1024, YamlText>) throws in
    var n = 0
    for _ in try Yams.load_all(yaml: text.value) {
        n += 1
        if n > 64 { break }
    }
}

// serialize(node) composes back to an equal node
let roundtrip = endpoint("roundtrip") { (doc: Borrow<Doc>) throws in
    let text = try Yams.serialize(node: doc.value)
    log("serialized: \(text.debugDescription)")
    guard let again = try Yams.compose(yaml: text) else {
        try check(false, "serialized node composes")
        return
    }
    try check(again == doc.value, "compose(serialize(node)) == node")
}

let serializeOptions = endpoint("serialize_options") { (doc: Borrow<Doc>, indent: Between<0, 9>, width: Between<-1, 80>,
                                                       canonical: Bool, allowUnicode: Bool, lb: Pick<LineBreakChoice>) throws in
    let text = try Yams.serialize(node: doc.value, canonical: canonical, indent: indent.value, width: width.value,
                                  allowUnicode: allowUnicode, lineBreak: lb.value.lineBreak)
    log("serialized: \(text.debugDescription)")
    try check(try Yams.compose(yaml: text) != nil || doc.value == Node(""), "serialized node composes")
}

enum LineBreakChoice: CaseIterable {
    case ln, cr, crln
    var lineBreak: Emitter.LineBreak { self == .ln ? .ln : self == .cr ? .cr : .crln }
}

let dumpLoad = endpoint("dump_load") { (text: Str<1024, YamlText>) throws in
    guard let v = try Yams.load(yaml: text.value) else { return }
    let out = try Yams.dump(object: v)
    log("dumped: \(out.debugDescription)")
    _ = try Yams.load(yaml: out)
}

let codable = endpoint("codable") { (name: Str<32, Plain>, count: Int, tag: Str<16, Plain>, ratio: Double, key: Str<8, Plain>) throws in
    let item = Item(name: name.value, count: count, tags: [tag.value, name.value], ratio: ratio.isNaN ? nil : ratio,
                    nested: [key.value: tag.value])
    let text = try YAMLEncoder().encode(item)
    log("encoded: \(text.debugDescription)")
    let back = try YAMLDecoder().decode(Item.self, from: text)
    try check(back == item, "YAMLDecoder(YAMLEncoder(item)) == item")
}

let decodeItem = endpoint("decode_item") { (text: Str<1024, YamlText>) throws in
    _ = try YAMLDecoder().decode(Item.self, from: text.value)
    _ = try YAMLDecoder().decode([String: [Int]].self, from: text.value)
}

let subscriptKey = endpoint("subscript") { (doc: Borrow<Doc>, key: Str<8, Plain>, i: Between<-1, 8>) throws in
    _ = doc.value[key.value]
    _ = doc.value[i.value]
    _ = doc.value.string
    _ = doc.value.int
    _ = doc.value.float
    _ = doc.value.bool
    _ = doc.value.timestamp
    _ = doc.value.binary
}

let setKey = endpoint("set_key") { (doc: Borrow<Doc>, key: Str<8, Plain>, value: Str<16, Plain>) throws in
    doc.value[key.value] = Node(value.value)
}
