import Stitch
import Ink

enum Parser: StitchObject { typealias Handle = MarkdownParser }
enum Doc: StitchObject { typealias Handle = Markdown }

enum MdText: Decoded {
    static func decode(_ s: Source) -> String {
        var out = ""
        if s.coin(0.2) {
            out += "---\n"
            while s.coin(0.7) { out += "\(s.str(6)): \(s.str(10))\n" }
            out += s.coin(0.9) ? "---\n" : ""
        }
        while s.more() {
            switch s.choice(14) {
            case 0: out += "#\(s.coin() ? "#" : "") \(s.str(12))\n"
            case 1: out += "\(s.token())\(s.str(8))"
            case 2: out += "> \(s.str(12))\n"
            case 3: out += "- \(s.str(8))\n  - \(s.str(8))\n"
            case 4: out += "\(s.range(0, 12)). \(s.str(8))\n"
            case 5: out += "```\(s.str(4))\n\(s.str(16))\n```\n"
            case 6: out += "| \(s.str(4)) | \(s.str(4)) |\n|---|---|\n| \(s.str(4)) | \(s.str(4)) |\n"
            case 7: out += "[\(s.str(6))](\(s.str(10)))"
            case 8: out += "![\(s.str(6))](\(s.str(10)))"
            case 9: out += "**\(s.str(6))**_\(s.str(6))_`\(s.str(6))`"
            case 10: out += "<\(s.str(5))>\(s.str(6))</\(s.str(5))>"
            case 11: out += "[\(s.str(5))]: \(s.str(12))\n"
            case 12: out += "\n\n"
            default: out += s.str(20)
            }
        }
        return out
    }
}

enum Tgt: CaseIterable {
    case metadataKeys, metadataValues, blockquotes, codeBlocks, headings, horizontalLines
    case html, images, inlineCode, links, lists, paragraphs, tables
    var target: Modifier.Target {
        switch self {
        case .metadataKeys: return .metadataKeys
        case .metadataValues: return .metadataValues
        case .blockquotes: return .blockquotes
        case .codeBlocks: return .codeBlocks
        case .headings: return .headings
        case .horizontalLines: return .horizontalLines
        case .html: return .html
        case .images: return .images
        case .inlineCode: return .inlineCode
        case .links: return .links
        case .lists: return .lists
        case .paragraphs: return .paragraphs
        case .tables: return .tables
        }
    }
}
enum Kind: CaseIterable { case identity, wrap, upper, markdownEcho, empty }

func makeModifier(_ t: Tgt, _ k: Kind) -> Modifier {
    switch k {
    case .identity: return Modifier(target: t.target) { $0.html }
    case .wrap: return Modifier(target: t.target) { "<span>\($0.html)</span>" }
    case .upper: return Modifier(target: t.target) { $0.html.uppercased() }
    case .markdownEcho: return Modifier(target: t.target) { String($0.markdown) }
    case .empty: return Modifier(target: t.target) { _ in "" }
    }
}

let newParser = endpoint("new_parser") { () -> New<Parser> in
    New(MarkdownParser())
}

let newParserWith = endpoint("new_parser_with") { (t: Pick<Tgt>, k: Pick<Kind>) -> New<Parser> in
    New(MarkdownParser(modifiers: [makeModifier(t.value, k.value)]))
}

let addModifier = endpoint("add_modifier") { (p: Borrow<Parser>, t: Pick<Tgt>, k: Pick<Kind>) in
    p.value.addModifier(makeModifier(t.value, k.value))
    p.set("mods", (p.int("mods") ?? 0) + 1)
}

let parseMd = endpoint("parse") { (p: Borrow<Parser>, md: Str<4096, MdText>) -> New<Doc> in
    New(p.value.parse(md.value))
}

let htmlFrom = endpoint("html_from") { (p: Borrow<Parser>, md: Str<4096, MdText>) in
    let a = p.value.html(from: md.value)
    let b = p.value.parse(md.value).html
    try check(a == b, "html(from:) == parse().html")
}

let deterministic = endpoint("deterministic") { (p: Borrow<Parser>, md: Str<4096, MdText>) in
    let a = p.value.parse(md.value), b = p.value.parse(md.value)
    try check(a.html == b.html && a.metadata == b.metadata && a.title == b.title, "parse is deterministic")
}

let titleCheck = endpoint("title_consistent") { (md: Str<2048, MdText>) in
    let r = MarkdownParser().parse(md.value)
    if let t = r.title { try check(r.html.contains("<h1"), "title implies an <h1> in html (\(t.count))") }
}

let metadataCheck = endpoint("metadata_consistent") { (md: Str<2048, MdText>) in
    let r = MarkdownParser().parse(md.value)
    if !r.metadata.isEmpty {
        try check(md.value.drop(while: { $0.isWhitespace }).hasPrefix("-"), "metadata implies leading ---")
        for (k, _) in r.metadata { try check(md.value.contains(k), "metadata key appears in input") }
    }
}

let docHtml = endpoint("doc_html") { (d: Borrow<Doc>) in
    _ = d.value.html.count
}

let docTitle = endpoint("doc_title") { (d: Borrow<Doc>) in
    _ = d.value.title
    _ = d.value.title
}

let setTitle = endpoint("set_title") { (d: Borrow<Doc>, t: Str<32, Plain>) in
    d.value.title = t.value
    try check(d.value.title == t.value, "title setter roundtrip")
}

let setMeta = endpoint("set_metadata") { (d: Borrow<Doc>, k: Str<8, Plain>, v: Str<16, Plain>) in
    d.value.metadata[k.value] = v.value
    try check(d.value.metadata[k.value] == v.value, "metadata set")
}

let reparseHtml = endpoint("reparse_html") { (md: Str<2048, MdText>) in
    // parsing is idempotent on its own output only roughly; just must not trap
    let p = MarkdownParser()
    _ = p.parse(p.html(from: md.value))
}

let rawParse = endpoint("parse_plain") { (md: Str<4096, Plain>) -> New<Doc> in
    New(MarkdownParser().parse(md.value))
}
