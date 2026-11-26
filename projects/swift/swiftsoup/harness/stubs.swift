// SwiftSoup (a Swift port of jsoup): HTML/XML parsing from strings and bytes, fragments, the cleaner and
// whitelists, CSS selectors and the getElementsBy* indexes, tree edits on elements, nodes and Elements,
// attributes, output settings, entities.
// Oracles: parse(html()) prints the same again; byte and String parsers agree; String and UTF-8
// serializers agree; clean() output passes isValid-style checks and is idempotent; the tag, class and id
// indexes agree with a walk of the tree after any edit; Element.select, CssSelector, QueryParser and
// Collector agree; cssSelector() selects the element; splitText keeps the text; unescape(escape(s)) == s.
// A thrown SwiftSoup.Exception is a bail.
import Foundation
import Stitch
import SwiftSoup

enum Doc: StitchObject { typealias Handle = Document }
enum Elem: StitchObject { typealias Handle = Element }
enum Nd: StitchObject { typealias Handle = Node }
enum Els: StitchObject { typealias Handle = Elements }
enum WL: StitchObject { typealias Handle = Whitelist }
enum TextN: StitchObject { typealias Handle = TextNode }

let base = "http://example.com/dir/"
let tags = ["div", "p", "a", "b", "i", "span", "table", "tr", "td", "ul", "li", "script", "style", "svg", "math", "img", "br",
            "textarea", "select", "option", "form", "input", "html", "head", "body", "title", "pre", "template", "noscript", "frameset"]
let attrs = ["href", "src", "id", "class", "style", "onclick", "title", "alt", "rel", "type", "name", "value", "data-x", "checked"]

enum Html: Decoded {
    static func decode(_ s: Source) -> String {
        var out = ""
        var open: [String] = []
        while s.more() {
            switch s.choice(8) {
            case 0, 1:
                let t = s.coin(0.8) ? s.pick(tags) : s.str(6)
                var a = ""
                for _ in 0..<s.range(0, 2) {
                    a += " \(s.coin(0.8) ? s.pick(attrs) : s.token())=\"\(s.coin(0.6) ? s.str(10) : s.token())\""
                }
                out += "<\(t)\(a)\(s.coin(0.1) ? "/" : "")>"
                open.append(t)
            case 2:
                if let t = open.popLast(), s.coin(0.9) { out += "</\(t)>" } else { out += "</\(s.pick(tags))>" }
            case 3: out += s.str(12)
            case 4: out += "&\(s.pick(["amp", "lt", "gt", "quot", "nbsp", "#60", "#x3c", "#0", "#1114112", "notit", "amp"]))\(s.coin(0.7) ? ";" : "")"
            case 5: out += s.pick(["<!--", "-->", "<![CDATA[", "]]>", "<!DOCTYPE html>", "<?", "<!", "<?xml version=\"1.0\"?>"])
            case 6: out += s.pick(["javascript:", "java\tscript:", "data:", "vbscript:", "http://", "/rel", "//host/x", "#frag"])
            default: out += s.token()
            }
        }
        return out
    }
}

// HTML as bytes, sometimes with invalid UTF-8 or a BOM
enum HtmlBytes: DecodedBytes {
    static func decode(_ s: Source) -> [UInt8] {
        var out = s.coin(0.05) ? [0xEF, 0xBB, 0xBF] : [UInt8]()
        out += Array(s.draw(Html.self).utf8)
        while s.coin(0.2) {
            let b: [UInt8] = s.pick([[0xFF], [0xC3], [0xE2, 0x82], [0xED, 0xA0, 0x80], [0xF4, 0x90, 0x80, 0x80], [0x00], [0xC3, 0xA9]])
            out.insert(contentsOf: b, at: s.range(0, out.count))
        }
        return out
    }
}

enum Selector: Decoded {
    static func decode(_ s: Source) -> String {
        var out = ""
        while s.more() {
            switch s.choice(6) {
            case 0: out += s.pick(tags + ["*"])
            case 1: out += s.pick(["#", ".", "[", "]", "=", "^=", "$=", "*=", "~=", "!=", ">", "+", "~", " ", ",", ":", "|", "\\"])
            case 2: out += s.pick([":has(", ":not(", ":contains(", ":containsOwn(", ":matches(", ":matchesOwn(", ":eq(", ":lt(", ":gt(",
                                  ":nth-child(", ":nth-last-child(", ":nth-of-type(", ":first-child", ":last-of-type", ":only-child",
                                  ":empty", ":root", ":containsData(", ":is(", "[^data-", ")"])
            case 3: out += s.pick(attrs)
            case 4: out += s.str(4)
            default: out += s.token()
            }
        }
        return out
    }
}

// MARK: - helpers

/// Every element under (and including) `root`, in document order, from a plain walk of children().
func walk(_ root: Element) -> [Element] {
    var out: [Element] = [root]
    for c in root.children().array() { out += walk(c) }
    return out
}

func ids(_ els: [Element]) -> Set<ObjectIdentifier> { Set(els.map { ObjectIdentifier($0) }) }

/// The element's top-most ancestor (itself if detached).
func root(_ n: Node) -> Node {
    var r = n
    while let p = r.parent() { r = p }
    return r
}

/// The attribute-key trimming Attribute(key:value:) applies (space and \t...\r).
func trimKey(_ k: String) -> String {
    var b = Array(k.utf8)[...]
    func ws(_ c: UInt8) -> Bool { c == 32 || (c >= 9 && c <= 13) }
    while let f = b.first, ws(f) { b = b.dropFirst() }
    while let l = b.last, ws(l) { b = b.dropLast() }
    return String(decoding: b, as: UTF8.self)
}

/// How many of the element's attribute keys equal `k` ignoring ASCII case (case variants make the
/// case-insensitive getters and removeAttr pick one of them).
func variants(_ e: Node, _ k: String) -> Int {
    let lk = Array(k.utf8).map { $0 >= 65 && $0 <= 90 ? $0 + 32 : $0 }
    return (e.getAttributes()?.asList() ?? []).filter { Array($0.getKey().utf8).map { $0 >= 65 && $0 <= 90 ? $0 + 32 : $0 } == lk }.count
}

/// A detached copy prints like the original: compare the source-free serializations under the
/// original's output settings (a detached element would print with the defaults).
func checkCopy(_ orig: Node, _ copy: Node) throws {
    if let e = orig as? Element, let c = copy as? Element {
        try check(c.parent() == nil, "a copy is detached")
        var holder: Document? = nil
        if let doc = e.ownerDocument(), !(e is Document) {
            holder = Document("")
            holder!.outputSettings(doc.outputSettings().copy() as! OutputSettings)
            try holder!.appendChild(c)
        }
        let same = String(decoding: try c.htmlUTF8WithoutSourceReuse(), as: UTF8.self) == String(decoding: try e.htmlUTF8WithoutSourceReuse(), as: UTF8.self)
        if holder != nil { try c.remove() }
        try check(same, "a copy prints the same (htmlUTF8WithoutSourceReuse)")
    } else if let t = orig as? TextNode, let c = copy as? TextNode {
        try check(t.getWholeText() == c.getWholeText(), "a copied text node has the same text")
    } else if let t = orig as? DataNode, let c = copy as? DataNode {
        try check(t.getWholeData() == c.getWholeData(), "a copied data node has the same data")
    } else if let t = orig as? Comment, let c = copy as? Comment {
        try check(t.getData() == c.getData(), "a copied comment has the same data")
    }
}

/// Serialize, reparse, serialize again... the output reaches a fixed point within a few rounds
/// (pretty printing may move whitespace on the first rounds; it must not grow forever).
func converges(_ first: String, _ f: (String) throws -> String) throws -> Bool {
    var prev = first
    for _ in 0..<5 {
        let next = try f(prev)
        if next == prev { return true }
        prev = next
    }
    return false
}

/// Trees that the HTML serialization cannot represent (the spec's known hazards, reachable through DOM
/// edits): a <plaintext> element (swallows everything after it on reparse), raw-text elements whose text
/// contains "</", and tag names that are not plain names.
func unserializable(_ root: Element) -> Bool {
    let raw: Set<String> = ["script", "style", "xmp", "iframe", "noembed", "noframes", "textarea", "title", "noscript", "plaintext"]
    for e in walk(root) where !(e is Document) {
        let t = e.tagNameNormal()
        if t == "plaintext" { return true }
        if !t.utf8.allSatisfy({ ($0 >= 97 && $0 <= 122) || ($0 >= 48 && $0 <= 57) || $0 == 45 || $0 == 58 }) { return true }
        if raw.contains(t) && (e.data().contains("</") || ((try? e.text()) ?? "").contains("</")) { return true }
    }
    return false
}

func isPlainAscii(_ s: String) -> Bool {
    !s.isEmpty && s.utf8.allSatisfy { $0 > 32 && $0 < 127 }
}

/// The tag, class and id indexes agree with a walk of the tree below `el`.
func checkIndexes(_ el: Element) throws {
    let all = walk(el)
    try check(ids(try el.getAllElements().array()) == ids(all), "getAllElements() is the element and its descendants")
    for e in all.prefix(12) {
        let t = e.tagNameNormal()
        if isPlainAscii(t) && t == t.lowercased() {
            let want = all.filter { $0.tagNameNormal() == t }
            try check(ids(try el.getElementsByTag(t).array()) == ids(want), "getElementsByTag agrees with a walk of the tree")
        }
        for c in Array((try? e.classNames()) ?? OrderedSet<String>()) where isPlainAscii(c) {
            let want = all.filter { $0.hasClass(c) }
            try check(ids(try el.getElementsByClass(c).array()) == ids(want), "getElementsByClass agrees with hasClass on a walk")
        }
        let id = e.id()
        if !id.isEmpty {
            let first = all.first { $0.id() == id }
            try check(try el.getElementById(id) === first, "getElementById finds the first element with that id")
        }
    }
}

// MARK: - parsing

let parse = endpoint("parse") { (html: Str<2048, Html>) throws -> New<Doc> in
    New(try SwiftSoup.parse(html.value, base))
}

let parseFragment = endpoint("parse_fragment") { (html: Str<2048, Html>, relaxed: Bool) throws -> New<Doc> in
    New(relaxed ? try Parser.parseBodyFragmentRelaxed(html.value, base) : try SwiftSoup.parseBodyFragment(html.value, base))
}

let parseXml = endpoint("parse_xml") { (xml: Str<2048, Html>) throws -> New<Doc> in
    New(try SwiftSoup.parseXML(xml.value, base))
}

enum ByteApi: CaseIterable { case data, parserBytes, withBytes, withBytesTop, html, xml }

// parsing bytes agrees with parsing the same text as a String
let parseBytes = endpoint("parse_bytes") { (b: Bytes<2048, HtmlBytes>, api: Pick<ByteApi>) throws -> New<Doc> in
    var bytes = b.value      // a local copy, freed when the stub returns
    let doc: Document
    switch api.value {
    case .data: doc = try SwiftSoup.parse(Data(bytes), base)
    case .parserBytes: doc = try Parser.parse(bytes, Array(base.utf8))
    case .withBytes: doc = try Parser.parse(base, withBytes: { p in try bytes.withUnsafeBufferPointer { try p($0) } })
    case .withBytesTop: doc = try SwiftSoup.parse(base, withBytes: { p in try bytes.withUnsafeBufferPointer { try p($0) } })
    case .html: doc = try SwiftSoup.parseHTML(Data(bytes), base)
    case .xml: doc = try SwiftSoup.parseXML(Data(bytes), base)
    }
    if let s = String(validating: bytes, as: UTF8.self) {
        let ref: Document
        switch api.value {
        case .data: ref = try SwiftSoup.parse(s, base)
        case .xml: ref = try SwiftSoup.parseXML(s, base)
        default: ref = try Parser.parse(s, base)
        }
        try check(try doc.outerHtml() == ref.outerHtml(), "parsing bytes and the same text as a String agree")
    }
    bytes.removeAll()
    return New(doc)
}

let parseWith = endpoint("parse_with") { (html: Str<2048, Html>, xml: Bool, maxErrors: Between<0, 16>, caseTag: Bool, caseAttr: Bool,
                                         ranges: Bool) throws -> New<Doc> in
    let p = xml ? Parser.xmlParser() : Parser.htmlParser()
    _ = p.setTrackErrors(maxErrors.value).settings(ParseSettings(caseTag, caseAttr, ranges))
    let doc = try p.parseInput(html.value, base)
    _ = p.isTrackErrors()
    _ = p.getErrors()
    _ = p.getTreeBuilder()
    _ = p.settings().normalizeTag(html.value.prefix(8).description)
    return New(doc)
}

// fragment parsing in the context of an element; the nodes are inserted into it
let parseNodes = endpoint("parse_nodes") { (ctx: Borrow<Elem>, html: Str<512, Html>, xml: Bool, at: Between<-2, 8>, s: Choices<4>) throws -> New<Nd>? in
    let n = ctx.value.childNodeSize()
    let index = s.source.coin(0.9) ? s.source.range(-n - 1, n) : at.value   // mostly a valid position (negative counts from the end)
    let nodes = xml ? try Parser.parseXmlFragment(html.value, base) : try Parser.parseFragment(html.value, ctx.value, Array(base.utf8))
    let first = nodes.first
    try ctx.value.insertChildren(index, nodes)
    for n in nodes { try check(n.parent() === ctx.value, "inserted nodes have the element as parent") }
    return first.map { New($0) }
}

let newDocument = endpoint("new_document") { (shell: Bool, uri: Str<32, Plain>) throws -> New<Doc> in
    New(shell ? Document.createShell(uri.value) : Document(uri.value))
}

let createElement = endpoint("create_element") { (doc: Borrow<Doc>, tag: Str<16, Plain>, known: Bool, s: Choices<4>) throws -> New<Elem> in
    New(try doc.value.createElement(known ? s.source.pick(tags) : tag.value))
}

let newText = endpoint("new_text") { (text: Str<64, Html>, encoded: Bool) throws -> New2<New<Nd>, New<TextN>> in
    let t = encoded ? try TextNode.createFromEncoded(text.value, base) : TextNode(text.value, base)
    if !encoded { try check(t.getWholeText() == text.value, "a new text node keeps its text") }
    return New2(New(t), New(t))
}

let newNode = endpoint("new_node") { (text: Str<64, Html>, kind: Between<2, 6>) throws -> New<Nd> in
    let t = text.value
    switch kind.value {
    case 2: return New(Comment(Array(t.utf8), Array(base.utf8)))
    case 3: return New(DataNode(Array(t.utf8), Array(base.utf8)))
    case 4: return New(try DataNode.createFromEncoded(t, base))
    case 5: return New(DocumentType(t, "-//W3C//DTD", "http://x/\(t)", base))
    default: return New(XmlDeclaration(t, base, true))
    }
}

// MARK: - serialization oracles

// html() parses again and prints the same
let roundtrip = endpoint("roundtrip") { (doc: Borrow<Doc>) throws in
    try require(!unserializable(doc.value), "tree has no faithful HTML serialization")
    let a = try doc.value.html()
    log("html: \(a.debugDescription)")
    try check(try converges(a) { try SwiftSoup.parse($0).html() }, "parse(html()) reaches a fixed point")
}

let xmlRoundtrip = endpoint("xml_roundtrip") { (doc: Borrow<Doc>) throws in
    func ser(_ d: Document) throws -> String {
        d.outputSettings().prettyPrint(pretty: false).syntax(syntax: .xml)
        return try d.outerHtml()
    }
    let a = try ser(doc.value)
    log("xml: \(a.debugDescription)")
    try check(try converges(a) { try ser(SwiftSoup.parseXML($0, base)) }, "parseXML(outerHtml()) reaches a fixed point")
}

enum Escape: CaseIterable { case base, extended, xhtml }
func escapeMode(_ e: Escape) -> Entities.EscapeMode { e == .base ? .base : e == .extended ? .extended : .xhtml }

let output = endpoint("output") { (doc: Borrow<Doc>, pretty: Bool, outline: Bool, indent: Between<0, 8>, esc: Pick<Escape>,
                                   ascii: Bool, xml: Bool) throws in
    let o = doc.value.outputSettings()
    o.prettyPrint(pretty: pretty).outline(outlineMode: outline).indentAmount(indentAmount: UInt(indent.value))
        .escapeMode(escapeMode(esc.value)).charset(ascii ? .ascii : .utf8).syntax(syntax: xml ? .xml : .html)
    _ = o.encoder(); _ = o.prettyPrint(); _ = o.outline(); _ = o.indentAmount(); _ = o.escapeMode(); _ = o.syntax()
    _ = doc.value.outputSettings(o.copy() as! OutputSettings)
    let s = try doc.value.outerHtml()
    let u = try doc.value.outerHtmlUTF8()
    try check(String(decoding: u, as: UTF8.self) == s, "outerHtml() and outerHtmlUTF8() agree")
    _ = try doc.value.outerHtmlUTF8WithoutSourceReuse()
    _ = try doc.value.outerHtmlUTF8ReusingSourceOutsideBody()
    if let body = doc.value.body() {
        _ = try? doc.value.outerHtmlUTF8ReusingSourceOutsideBody(preSerializedBodyContents: try body.htmlUTF8())
    }
    _ = doc.value.description
}

// MARK: - the cleaner

let clean = endpoint("clean") { (html: Str<2048, Html>, list: Between<0, 4>, relative: Bool) throws in
    let lists: [() throws -> Whitelist] = [Whitelist.none, Whitelist.simpleText, Whitelist.basic, Whitelist.basicWithImages, Whitelist.relaxed]
    let wl = try lists[list.value]()
    if relative { try wl.preserveRelativeLinks(true) }
    guard let out = try SwiftSoup.clean(html.value, base, wl) else { return }
    log("clean: \(out.debugDescription)")
    // look at elements and attributes, not at text that merely mentions "javascript:"
    let doc = try SwiftSoup.parseBodyFragment(out)
    for e in try doc.select("*").array() {
        try check(e.tagName() != "script", "clean() output has no script element")
        for a in e.getAttributes()?.asList() ?? [] {
            let v = a.getValue().lowercased().filter { !$0.isWhitespace && !($0.asciiValue.map { $0 < 32 } ?? false) }
            try check(!a.getKey().lowercased().hasPrefix("on"), "clean() output has no event handler")
            try check(!v.hasPrefix("javascript:") && !v.hasPrefix("vbscript:"), "clean() output has no javascript: URL")
        }
    }
    try check(try converges(out) { try SwiftSoup.clean($0, base, wl) ?? "" }, "clean(clean(...)) reaches a fixed point")
    _ = try SwiftSoup.isValid(html.value, wl)
    _ = try SwiftSoup.clean(html.value, wl)
    _ = try SwiftSoup.clean(html.value, base, wl, OutputSettings().prettyPrint(pretty: false))
}

let newWhitelist = endpoint("new_whitelist") { (list: Between<0, 4>) throws -> New<WL> in
    let lists: [() throws -> Whitelist] = [Whitelist.none, Whitelist.simpleText, Whitelist.basic, Whitelist.basicWithImages, Whitelist.relaxed]
    return New(try lists[list.value]())
}

enum WLOp: CaseIterable { case addTags, removeTags, addAttrs, removeAttrs, addEnforced, removeEnforced, addProtocols, removeProtocols,
                          addCss, removeCss, relative, whitespace }

let editWhitelist = endpoint("edit_whitelist") { (wl: Borrow<WL>, op: Pick<WLOp>, tag: Str<12, Plain>, key: Str<12, Plain>,
                                                  value: Str<16, Plain>, s: Choices<4>) throws in
    let w = wl.value
    let t = s.source.coin(0.7) ? s.source.pick(tags + [":all"]) : tag.value
    let k = s.source.coin(0.7) ? s.source.pick(attrs) : key.value
    switch op.value {
    case .addTags: try w.addTags(t)
    case .removeTags: try w.removeTags(t)
    case .addAttrs: try w.addAttributes(t, k)
    case .removeAttrs: try w.removeAttributes(t, k)
    case .addEnforced: try w.addEnforcedAttribute(t, k, value.value)
    case .removeEnforced: try w.removeEnforcedAttribute(t, k)
    case .addProtocols: try w.addProtocols(t, k, s.source.coin(0.7) ? s.source.pick(["http", "https", "mailto", "javascript", "data"]) : value.value)
    case .removeProtocols: try w.removeProtocols(t, k, s.source.pick(["http", "https", "mailto", "javascript", "data", value.value]))
    case .addCss: try w.addCSSProperties(t, s.source.pick(["color", "width", "background", "position", value.value]))
    case .removeCss: try w.removeCSSProperties(t, s.source.pick(["color", "width", "background", "position", value.value]))
    case .relative: w.preserveRelativeLinks(s.source.coin())
    case .whitespace: w.urlWhitespace(s.source.pick([.strict, .trim]))
    }
    _ = w.isSafeTag(Array(t.utf8))
    _ = try w.getEnforcedAttributes(t)
}

// Cleaner.clean keeps only whitelisted tags; isValid agrees with an equal clean
let cleanDoc = endpoint("clean_doc") { (doc: Borrow<Doc>, wl: Borrow<WL>, head: Bool) throws -> New<Doc> in
    let cleaner = Cleaner(headWhitelist: head ? wl.value : nil, bodyWhitelist: wl.value)
    let out = try cleaner.clean(doc.value)
    _ = try cleaner.isValid(doc.value)
    if let body = out.body() {
        for e in walk(body).dropFirst() {
            try check(wl.value.isSafeTag(e.tagNameUTF8()), "the cleaned body has only whitelisted tags")
            if let a = e.getAttributes() {
                for attr in a.asList() where try !wl.value.isSafeAttribute(e.tagName(), e, attr) {
                    try check(try wl.value.getEnforcedAttributes(e.tagName()).hasKey(key: attr.getKey()) || attr.getKey().lowercased() == "style",
                              "cleaned attributes are whitelisted or enforced")
                }
            }
        }
    }
    return New(out)
}

// MARK: - selecting

let select = endpoint("select") { (doc: Borrow<Doc>, q: Str<64, Selector>) throws -> New2<New<Els>, New<Elem>> in
    let els = try doc.value.select(q.value)
    for e in els.array() {
        try check(e.ownerDocument() === doc.value, "selected elements belong to the document")
    }
    return New2(New(els), els.first().map { New($0) })
}

// the selector APIs agree with each other
let selectFrom = endpoint("select_from") { (el: Borrow<Elem>, q: Str<64, Selector>) throws -> New<Els> in
    let e = el.value
    let a = try e.select(q.value)
    let eval = try QueryParser.parse(q.value)
    let b = try e.select(eval)
    let c = try CssSelector.select(q.value, e)
    let d = try CssSelector.select(eval, [e])
    try check(ids(a.array()) == ids(b.array()) && ids(b.array()) == ids(c.array()) && ids(c.array()) == ids(d.array()),
              "select(query), select(evaluator) and CssSelector.select agree")
    let all = ids(walk(e))
    for x in a.array() { try check(all.contains(ObjectIdentifier(x)), "selected elements are the element or its descendants") }
    let matches = try e.iS(q.value)
    try check(matches == (try e.iS(eval)), "iS(query) and iS(evaluator) agree")
    _ = try Collector.collect(eval, e)
    return New(a)
}

let queryString = endpoint("query_string") { (q: Str<64, Selector>) throws in
    _ = try QueryParser.parse(q.value).toString()
}

let body = endpoint("body") { (doc: Borrow<Doc>, head: Bool) throws -> New<Elem>? in
    (head ? doc.value.head() : doc.value.body()).map { New($0) }
}

enum Nav: CaseIterable { case parent, next, previous, first, last, child, parents, siblings, root }

let navigate = endpoint("navigate") { (el: Borrow<Elem>, how: Pick<Nav>, s: Choices<8>) throws -> New<Elem>? in
    let e = el.value
    switch how.value {
    case .parent: return e.parent().map { New($0) }
    case .next: return try e.nextElementSibling().map { New($0) }
    case .previous: return try e.previousElementSibling().map { New($0) }
    case .first: return e.firstElementSibling().map { New($0) }
    case .last: return e.lastElementSibling().map { New($0) }
    case .child:
        let n = e.children().size()
        guard n > 0 else { return nil }
        let i = s.source.choice(n)
        let c = e.child(i)
        try check(c === e.children().get(i), "child(i) == children()[i]")
        if c.parent() === e { try check(try c.elementSiblingIndex() == i, "elementSiblingIndex of child(i) is i") }
        return New(c)
    case .parents:
        let ps = e.parents().array()
        return ps.isEmpty ? nil : New(s.source.pick(ps))
    case .siblings:
        let sib = e.siblingElements().array()
        for x in sib { try check(x !== e, "siblingElements excludes the element") }
        return sib.isEmpty ? nil : New(s.source.pick(sib))
    case .root:
        return (root(e) as? Element).map { New($0) }
    }
}

enum Query: CaseIterable { case tag, cls, attr, attrStart, attrValue, attrNot, attrPrefix, attrSuffix, attrContains, attrMatch,
                           lt, gt, eq, text, ownText, matchText, matchOwn, id, all }

let getElements = endpoint("get_elements") { (el: Borrow<Elem>, how: Pick<Query>, key: Str<12, Plain>, value: Str<12, Plain>,
                                               i: Between<-1, 8>, s: Choices<4>) throws -> New<Els>? in
    let e = el.value
    let k = s.source.coin(0.6) ? s.source.pick(attrs) : key.value
    let r: Elements
    switch how.value {
    case .tag: r = try e.getElementsByTag(s.source.coin(0.6) ? s.source.pick(tags) : key.value)
    case .cls: r = try e.getElementsByClass(value.value)
    case .attr: r = try e.getElementsByAttribute(k)
    case .attrStart: r = try e.getElementsByAttributeStarting(k)
    case .attrValue: r = try e.getElementsByAttributeValue(k, value.value)
    case .attrNot: r = try e.getElementsByAttributeValueNot(k, value.value)
    case .attrPrefix: r = try e.getElementsByAttributeValueStarting(k, value.value)
    case .attrSuffix: r = try e.getElementsByAttributeValueEnding(k, value.value)
    case .attrContains: r = try e.getElementsByAttributeValueContaining(k, value.value)
    case .attrMatch: r = try e.getElementsByAttributeValueMatching(k, value.value)
    case .lt: r = try e.getElementsByIndexLessThan(i.value)
    case .gt: r = try e.getElementsByIndexGreaterThan(i.value)
    case .eq: r = try e.getElementsByIndexEquals(i.value)
    case .text: r = try e.getElementsContainingText(value.value)
    case .ownText: r = try e.getElementsContainingOwnText(value.value)
    case .matchText: r = try e.getElementsMatchingText(value.value)
    case .matchOwn: r = try e.getElementsMatchingOwnText(value.value)
    case .id: r = try e.getElementById(value.value).map { Elements([$0]) } ?? Elements()
    case .all: r = try e.getAllElements()
    }
    let all = ids(walk(e))
    for x in r.array() { try check(all.contains(ObjectIdentifier(x)), "getElementsBy* returns the element or its descendants") }
    return r.isEmpty() ? nil : New(r)
}

// the tag, class and id indexes agree with a walk of the tree (after whatever edits came before)
let indexes = endpoint("indexes") { (doc: Borrow<Doc>) throws in
    try checkIndexes(doc.value)
}

let elIndexes = endpoint("el_indexes") { (el: Borrow<Elem>) throws in
    try checkIndexes(el.value)
}

// MARK: - inspecting

let inspect = endpoint("inspect") { (el: Borrow<Elem>, key: Str<12, Plain>, s: Choices<4>) throws in
    let e = el.value
    let t = try e.text()
    try check(String(decoding: try e.textUTF8(), as: UTF8.self) == t, "text() and textUTF8() agree")
    _ = try e.text(trimAndNormaliseWhitespace: false)
    _ = try e.textUTF8Slice()
    let own = e.ownText()
    try check(String(decoding: e.ownTextUTF8(), as: UTF8.self) == own, "ownText() and ownTextUTF8() agree")
    let h = try e.html()
    try check(String(decoding: try e.htmlUTF8(), as: UTF8.self) == h, "html() and htmlUTF8() agree")
    _ = try e.htmlUTF8WithoutSourceReuse()
    _ = try e.outerHtml()
    _ = e.data(); _ = e.hasText(); _ = e.isBlock(); _ = e.isEmpty(); _ = e.id(); _ = e.dataset()
    _ = e.textNodes(); _ = e.dataNodes(); _ = e.siblingIndex; _ = try e.className(); _ = try e.classNames(); _ = try e.val()
    _ = e.getBaseUri(); _ = e.childNodeSize(); _ = e.hasChildNodes(); _ = e.nodeName(); _ = e.tagName(); _ = e.tag().toString()
    let k = s.source.coin(0.7) ? s.source.pick(attrs) : key.value
    _ = try e.absUrl(k)
    _ = try e.attr("abs:" + k)
    _ = e.description
    _ = e.debugDescription
}

// cssSelector() selects the element in its document
let cssSelector = endpoint("css_selector") { (el: Borrow<Elem>) throws in
    let e = el.value
    let sel = try e.cssSelector()
    log("selector: \(sel.debugDescription)")
    guard let doc = e.ownerDocument(), root(e) === doc else { return }
    // (CSS cannot express U+0000: escaping turns it into U+FFFD, so names with NUL cannot be selected)
    var x: Element? = e
    while let y = x {
        if (y.tagName() + y.id() + ((try? y.className()) ?? "")).unicodeScalars.contains("\0") { return }
        x = y.parent()
    }
    let found = try doc.select(sel).array()
    try check(found.count == 1 && found[0] === e, "select(cssSelector()) finds exactly the element")
}

let tagInfo = endpoint("tag_info") { (name: Str<16, Plain>, s: Choices<4>, preserve: Bool) throws in
    let n = s.source.coin(0.6) ? s.source.pick(tags) : name.value
    let t = try Tag.valueOf(n, preserve ? ParseSettings.preserveCase : ParseSettings.htmlDefault)
    _ = t.getName(); _ = t.getNameNormal(); _ = t.isBlock(); _ = t.formatAsBlock(); _ = t.canContainBlock(); _ = t.isInline()
    _ = t.isData(); _ = t.isEmpty(); _ = t.isSelfClosing(); _ = t.isKnownTag(); _ = t.preserveWhitespace(); _ = t.isFormListed()
    _ = t.isFormSubmittable(); _ = Tag.isKnownTag(Array(n.utf8))
    try check(t.equals(try Tag.valueOf(n, preserve ? ParseSettings.preserveCase : ParseSettings.htmlDefault)), "Tag.valueOf is consistent")
}

// MARK: - attributes

let setAttr = endpoint("set_attr") { (el: Borrow<Elem>, key: Str<16, Plain>, value: Str<32, Plain>, s: Choices<4>) throws in
    let k = s.source.coin(0.5) ? s.source.pick(attrs) : key.value
    try el.value.attr(k, value.value)
    let t = trimKey(k)
    try check(el.value.hasAttr(t), "a set attribute is present")
    if !t.lowercased().hasPrefix("abs:") && variants(el.value, t) == 1 {
        try check(try el.value.attr(t) == value.value, "attr(key) returns the value just set")
    }
}

let setBoolAttr = endpoint("set_bool_attr") { (el: Borrow<Elem>, key: Str<16, Plain>, on: Bool, s: Choices<4>) throws in
    let k = s.source.coin(0.5) ? s.source.pick(attrs) : key.value
    try el.value.attr(k, on)
}

let removeAttr = endpoint("remove_attr") { (el: Borrow<Elem>, key: Str<16, Plain>, s: Choices<4>) throws in
    let keys = el.value.getAttributes()?.asList().map { $0.getKey() } ?? []
    let k = !keys.isEmpty && s.source.coin(0.7) ? s.source.pick(keys) : key.value
    let one = variants(el.value, k) <= 1
    try el.value.removeAttr(k)
    if !k.lowercased().hasPrefix("abs:") && one { try check(!el.value.hasAttr(k), "a removed attribute is gone") }
}

enum AttrOp: CaseIterable { case put, putBool, putAttr, get, remove, removeIgnoreCase, removeAll, lower, addAll, compact, iterate }

// the element's Attributes object, edited directly; selectors see the change
let attributes = endpoint("attributes") { (el: Borrow<Elem>, op: Pick<AttrOp>, key: Str<12, Plain>, value: Str<16, Plain>, s: Choices<4>) throws in
    let e = el.value
    guard let a = e.getAttributes() else { throw Bail("no attributes") }
    let keys = a.asList().map { $0.getKey() }
    let k = !keys.isEmpty && s.source.coin(0.5) ? s.source.pick(keys) : (s.source.coin(0.5) ? s.source.pick(attrs) : key.value)
    switch op.value {
    case .put:
        try a.put(k, value.value)
        try check(a.get(key: trimKey(k)) == value.value, "Attributes.get returns the value just put")
    case .putBool: try a.put(Array(k.utf8), s.source.coin())
    case .putAttr: a.put(attribute: try Attribute(key: k, value: value.value))
    case .get:
        _ = a.get(key: k); _ = try a.getIgnoreCase(key: k); _ = a.hasKey(key: k); _ = a.hasKeyIgnoreCase(key: k)
    case .remove:
        try a.remove(key: k)
        try check(!a.hasKey(key: k), "Attributes.remove removes the key")
    case .removeIgnoreCase:
        let one = variants(e, k) <= 1
        try a.removeIgnoreCase(key: Array(k.utf8))
        if one { try check(!a.hasKeyIgnoreCase(key: k), "Attributes.removeIgnoreCase removes the key in any case") }
    case .removeAll: a.removeAll(keys: keys.prefix(2).map { Array($0.utf8) })
    case .lower: a.lowercaseAllKeys()
    case .addAll:
        let other = Attributes()
        try other.put(k, value.value)
        a.addAll(incoming: other)
    case .compact:
        a.compactAndMutate { attr in AttributeMutation(keep: attr.getKey() != k, newValue: s.source.coin() ? Array(value.value.utf8) : nil) }
    case .iterate:
        var n = 0
        for _ in a { n += 1 }
        try check(n == a.size() && a.asList().count == a.size(), "iterating Attributes visits size() attributes")
    }
    let copy = a.clone()
    try check(copy.equals(o: a), "a clone of Attributes equals the original")
    _ = try a.html(); _ = try a.toString(); _ = a.dataset()
    // a selector finds the element by an attribute it has
    if let doc = e.ownerDocument(), root(e) === doc {
        for attr in a.asList().prefix(3) where isPlainAscii(attr.getKey()) && attr.getKey().allSatisfy({ $0.isLetter || $0 == "-" }) {
            let found = try doc.select("[\(attr.getKey())]").array()
            try check(found.contains { $0 === e }, "select([key]) finds an element with that attribute")
        }
    }
}

let attribute = endpoint("attribute") { (key: Str<16, Plain>, value: Str<32, Html>, newKey: Str<16, Plain>) throws in
    let a = try Attribute(key: key.value, value: value.value)
    _ = a.html(); _ = a.toString(); _ = a.isDataAttribute(); _ = a.isBooleanAttribute(); _ = a.hashCode()
    let c = a.clone()
    try check(c.getKey() == a.getKey() && c.getValue() == a.getValue(), "a cloned Attribute is equal")
    try a.setKey(key: newKey.value)
    _ = a.setValue(value: Array(value.value.utf8.reversed()))
    _ = try Attribute.createFromEncoded(unencodedKey: Array(key.value.utf8), encodedValue: Array(value.value.utf8)).getValue()
}

// MARK: - classes

enum ClassOp: CaseIterable { case add, remove, toggle, set }

let editClass = endpoint("edit_class") { (el: Borrow<Elem>, op: Pick<ClassOp>, c: Str<16, Plain>, s: Choices<4>) throws in
    let e = el.value
    let existing = Array((try? e.classNames()) ?? OrderedSet<String>())
    let name = !existing.isEmpty && s.source.coin(0.4) ? s.source.pick(existing) : c.value
    let had = e.hasClass(name)
    switch op.value {
    case .add: try e.addClass(name)
    case .remove: try e.removeClass(name)
    case .toggle: try e.toggleClass(name)
    case .set:
        let set = OrderedSet<String>()
        set.append(name)
        try e.classNames(set)
    }
    if isPlainAscii(name) {
        let want: Bool
        switch op.value {
        case .add, .set: want = true
        case .remove: want = false
        case .toggle: want = !had
        }
        try check(e.hasClass(name) == want, "hasClass reflects addClass/removeClass/toggleClass")
        if let doc = e.ownerDocument(), root(e) === doc {
            try check(try doc.getElementsByClass(name).array().contains { $0 === e } == want, "getElementsByClass reflects a class edit")
        }
    }
}

// MARK: - tree edits on elements

enum Edit: CaseIterable { case append, prepend, html, before, after, wrap, text, appendText, prependText, appendElement, prependElement, val }

let edit = endpoint("edit") { (el: Borrow<Elem>, op: Pick<Edit>, html: Str<256, Html>, s: Choices<4>) throws -> New<Elem>? in
    let e = el.value
    let h = html.value
    switch op.value {
    case .append: try e.append(h)
    case .prepend: try e.prepend(h)
    case .html: try e.html(h)
    case .before: try e.before(h)
    case .after: try e.after(h)
    case .wrap: try e.wrap(h)
    case .text:
        try e.text(h)
        if !(e is Document) { try check(e.children().isEmpty(), "text(set) removes child elements") }
    case .appendText: try e.appendText(h)
    case .prependText: try e.prependText(h)
    case .appendElement: return New(try e.appendElement(s.source.coin(0.8) ? s.source.pick(tags) : h))
    case .prependElement: return New(try e.prependElement(s.source.coin(0.8) ? s.source.pick(tags) : h))
    case .val: try e.val(h)
    }
    return nil
}

let unwrap = endpoint("unwrap") { (el: Borrow<Elem>) throws in
    try require(el.value.parent() != nil, "no parent")
    let kids = el.value.getChildNodes()
    let parent = el.value.parent()
    _ = try el.value.unwrap()
    try check(el.value.parent() == nil, "an unwrapped element is detached")
    for k in kids { try check(k.parent() === parent, "unwrap moves the children to the parent") }
}

let remove = endpoint("remove") { (el: Borrow<Elem>) throws in
    try require(el.value.parent() != nil, "no parent")
    let parent = el.value.parent()!
    try el.value.remove()
    try check(!parent.getChildNodes().contains { $0 === el.value }, "a removed element is no longer a child")
}

let empty = endpoint("empty") { (el: Borrow<Elem>) throws in
    el.value.empty()
    try check(el.value.childNodeSize() == 0, "empty() leaves no children")
}

let tagName = endpoint("tag_name") { (el: Borrow<Elem>, t: Str<16, Plain>, s: Choices<4>) throws in
    try el.value.tagName(s.source.coin(0.5) ? s.source.pick(tags) : t.value)
}

enum Move: CaseIterable { case appendChild, prependChild, before, after, replaceWith, insertAt }

// move one element relative to another (Borrow: the element stays usable)
let move = endpoint("move") { (el: Borrow<Elem>, other: Borrow<Elem>, how: Pick<Move>, at: Between<-2, 8>) throws in
    let a = el.value, b = other.value
    switch how.value {
    case .appendChild: try a.appendChild(b)
    case .prependChild: try a.prependChild(b)
    case .before: try a.before(b)
    case .after: _ = try a.after(b)
    case .replaceWith:
        try require(a.parent() != nil, "no parent")
        try a.replaceWith(b)
    case .insertAt: try a.insertChildren(at.value, [b])
    }
    // no cycles: walking up from either element ends
    var n = 0
    var p: Node? = b
    while let x = p { p = x.parent(); n += 1; try check(n < 10000, "the tree has no cycle") }
}

let cloneElement = endpoint("clone") { (el: Borrow<Elem>) throws -> New<Elem> in
    let c = el.value.copy() as! Element
    try checkCopy(el.value, c)
    _ = try c.hasSameValue(el.value)
    return New(c)
}

let setBaseUri = endpoint("set_base_uri") { (el: Borrow<Elem>, uri: Str<32, Plain>, s: Choices<4>) throws in
    try el.value.setBaseUri(s.source.coin(0.5) ? s.source.pick(["http://a.b/c/", "https://x/", "", "file:///tmp/", "//h"]) : uri.value)
    for e in walk(el.value).prefix(8) { for k in ["href", "src"] { _ = try e.absUrl(k) } }
}

// MARK: - nodes

let childNode = endpoint("child_node") { (el: Borrow<Elem>, s: Choices<8>) throws -> New2<New<Nd>, New<TextN>> in
    let kids = el.value.getChildNodes()
    guard !kids.isEmpty else { return New2(nil, nil) }
    let i = s.source.choice(kids.count)
    let n = el.value.childNode(i)
    try check(n === kids[i], "childNode(i) == getChildNodes()[i]")
    try check(n.siblingIndex == i, "a child's siblingIndex is its position")
    try check(el.value.childNodesCopy().count == kids.count, "childNodesCopy has every child")
    return New2(New(n), (n as? TextNode).map { New($0) })
}

let nodeInfo = endpoint("node_info") { (n: Borrow<Nd>) throws in
    let x = n.value
    _ = try x.outerHtml(); _ = x.nodeName(); _ = x.siblingNodes(); _ = x.nextSibling(); _ = x.previousSibling(); _ = x.hasNextSibling()
    _ = x.getAttributes(); _ = x.ownerDocument(); _ = x.description
    if let t = x as? TextNode { _ = t.text(); _ = t.getWholeText(); _ = t.isBlank() }
    if let c = x as? Comment { _ = c.getData() }
    if let d = x as? DataNode { _ = d.getWholeData() }
    if let d = x as? XmlDeclaration { _ = d.name(); _ = try d.getWholeDeclaration() }
    if let e = x as? Element { _ = try e.text() }
    try checkCopy(x, x.copy() as! Node)
}

enum NodeEdit: CaseIterable { case before, after, wrap, unwrap, remove, setData, attr }

let editNode = endpoint("edit_node") { (n: Borrow<Nd>, op: Pick<NodeEdit>, html: Str<128, Html>, key: Str<8, Plain>) throws in
    let x = n.value
    switch op.value {
    case .before: try x.before(html.value)
    case .after: try x.after(html.value)
    case .wrap: _ = try x.wrap(html.value)
    case .unwrap:
        try require(x.parent() != nil, "no parent")
        _ = try x.unwrap()
    case .remove:
        try require(x.parent() != nil, "no parent")
        try x.remove()
    case .setData:
        guard let d = x as? DataNode else { throw Bail("not a data node") }
        d.setWholeData(html.value)
        try check(d.getWholeData() == html.value, "setWholeData then getWholeData")
    case .attr:
        try x.attr(key.value, html.value)
        _ = try x.attr(key.value); _ = x.hasAttr(key.value); _ = try x.absUrl(key.value)
        try x.removeAttr(key.value)
    }
}

// splitting a text node keeps the text
let splitText = endpoint("split_text") { (n: Borrow<TextN>, at: Between<-1, 64>, utf8: Bool, s: Choices<4>) throws -> New2<New<Nd>, New<TextN>> in
    let t = n.value
    let whole = t.getWholeText()
    let len = utf8 ? whole.utf8.count : whole.count
    let offset = s.source.coin(0.85) ? s.source.range(0, len) : at.value
    let tail = utf8 ? try t.splitText(utf8Offset: offset) : try t.splitText(offset)
    try check(t.getWholeText() + tail.getWholeText() == whole, "the two halves of splitText make the original text")
    if t.parent() != nil { try check(t.nextSibling() === tail, "the split-off node follows the original") }
    return New2(New(tail), New(tail))
}

let textNode = endpoint("text_node") { (n: Borrow<TextN>, text: Str<64, Html>, set: Bool) throws in
    let t = n.value
    if set {
        t.text(text.value)
        try check(t.getWholeText() == text.value, "text(set) then getWholeText")
    }
    _ = t.text(); _ = t.isBlank(); _ = t.getWholeTextUTF8(); _ = try t.outerHtml()
}

// insert a node (moved if attached) into an element
let insertNode = endpoint("insert_node") { (el: Borrow<Elem>, n: Borrow<Nd>, how: Between<0, 3>) throws in
    try require(n.value !== el.value, "same node")
    switch how.value {
    case 0: try el.value.appendChild(n.value)
    case 1: try el.value.prependChild(n.value)
    case 2: try el.value.addChildren(n.value)
    default:
        try require(n.value.parent() != nil, "no parent")
        try n.value.replaceWith(el.value)
    }
}

let traverse = endpoint("traverse") { (el: Borrow<Elem>, all: Bool) throws in
    final class Counter: NodeVisitor {
        var heads = 0, tails = 0, maxDepth = 0
        func head(_ node: Node, _ depth: Int) throws { heads += 1; maxDepth = max(maxDepth, depth) }
        func tail(_ node: Node, _ depth: Int) throws { tails += 1 }
    }
    let c = Counter()
    if all { try el.value.children().traverse(c) } else { try el.value.traverse(c) }
    try check(c.heads == c.tails, "a traversal visits every node's head and tail")
    let d = Counter()
    try NodeTraversor(d).traverse(el.value)
    if !all { try check(d.heads == c.heads, "Node.traverse and NodeTraversor visit the same nodes") }
}

// MARK: - Elements

enum ElsOp: CaseIterable { case attr, removeAttr, addClass, removeClass, toggleClass, val, tagName, html, prepend, append, before, after,
                           wrap, unwrap, empty, remove }

let elsEdit = endpoint("els_edit") { (els: Borrow<Els>, op: Pick<ElsOp>, key: Str<12, Plain>, value: Str<64, Html>, s: Choices<4>) throws in
    let x = els.value
    let k = s.source.coin(0.6) ? s.source.pick(attrs) : key.value
    switch op.value {
    case .attr: try x.attr(k, value.value)
    case .removeAttr: try x.removeAttr(k)
    case .addClass: try x.addClass(key.value)
    case .removeClass: try x.removeClass(key.value)
    case .toggleClass: try x.toggleClass(key.value)
    case .val: try x.val(value.value)
    case .tagName: try x.tagName(s.source.coin(0.7) ? s.source.pick(tags) : key.value)
    case .html: try x.html(value.value)
    case .prepend: try x.prepend(value.value)
    case .append: try x.append(value.value)
    case .before: try x.before(value.value)
    case .after: try x.after(value.value)
    case .wrap: try x.wrap(value.value)
    case .unwrap: try x.unwrap()
    case .empty: x.empty()
    case .remove: try x.remove()
    }
}

let elsQuery = endpoint("els_query") { (els: Borrow<Els>, q: Str<64, Selector>, i: Between<-1, 8>, key: Str<12, Plain>) throws -> New<Els>? in
    let x = els.value
    _ = try x.attr(key.value); _ = x.hasAttr(key.value); _ = x.hasClass(key.value); _ = try x.val(); _ = try x.text(); _ = x.hasText()
    _ = try x.eachText(); _ = try x.html(); _ = try x.outerHtml(); _ = try x.toString(); _ = x.forms(); _ = x.first(); _ = x.last()
    for f in x.forms() { _ = f.elements() }
    let n = x.size()
    try check(n == x.count && n == x.array().count, "size(), count and array() agree")
    let one = x.eq(i.value)
    try check(one.size() == (i.value >= 0 && i.value < n ? 1 : 0), "eq(i) has one element exactly when i is in range")
    let sel = try x.select(q.value)
    let rest = try x.not(q.value)
    try check(rest.size() <= n, "not() keeps a subset")
    _ = try x.iS(q.value)
    _ = x.parents()
    let c = x.copy() as! Elements
    try check(c.size() == n, "a copy of Elements has the same size")
    return sel.isEmpty() ? nil : New(sel)
}

let elsAdd = endpoint("els_add") { (els: Borrow<Els>, el: Borrow<Elem>, at: Between<0, 8>) throws in
    let x = els.value
    let n = x.size()
    if at.value <= n { x.add(at.value, el.value) } else { x.add(el.value) }
    try check(x.size() == n + 1, "add() grows Elements by one")
}

let form = endpoint("form") { (doc: Borrow<Doc>, el: Borrow<Elem>) throws in
    for f in try doc.value.select("form").forms() {
        f.addElement(el.value)
        _ = f.elements()
    }
}

// MARK: - documents

let docOps = endpoint("doc_ops") { (doc: Borrow<Doc>, title: Str<32, Plain>, op: Between<0, 6>, ascii: Bool) throws in
    let d = doc.value
    switch op.value {
    case 0:
        try d.title(title.value)
        let t = try d.title()
        try d.title(t)
        try check(try d.title() == t, "title(title()) keeps the title")
    case 1:
        try d.charset(ascii ? .ascii : .utf8)
        _ = d.charset()
    case 2: d.updateMetaCharsetElement(ascii); _ = d.updateMetaCharsetElement()
    case 3: try d.normalise()
    case 4: d.quirksMode(ascii ? .quirks : .limitedQuirks); _ = d.quirksMode()
    case 5: _ = d.location(); _ = d.nodeName()
    default:
        let c = d.copy() as! Document
        try check(try c.outerHtmlUTF8WithoutSourceReuse() == d.outerHtmlUTF8WithoutSourceReuse(), "a copied document prints the same")
    }
}

// MARK: - entities

// unescape(escape(s)) == s
let entities = endpoint("entities") { (text: Str<128, Html>, esc: Pick<Escape>, ascii: Bool, strict: Bool, name: Str<12, Plain>) throws in
    // (C1 controls and NUL are excluded: the HTML spec maps &#x80;-&#x9F; to windows-1252 and &#0; to U+FFFD)
    let s = String(String.UnicodeScalarView(text.value.unicodeScalars.filter { $0.value != 0 && !(0x80...0x9F).contains($0.value) }))
    let out = OutputSettings().escapeMode(escapeMode(esc.value)).charset(ascii ? .ascii : .utf8)
    let e = Entities.escape(s, out)
    log("escaped: \(e.debugDescription)")
    try check(try Entities.unescape(e) == s, "unescape(escape(s)) == s")
    _ = Entities.escape(s)
    _ = try Entities.unescape(string: s, strict: strict)
    _ = try Parser.unescapeEntities(s, strict)
    let n = Array(name.value.utf8)[...]
    _ = Entities.isNamedEntity(n); _ = Entities.isBaseNamedEntity(n); _ = Entities.getByName(name: name.value)
    _ = Entities.codepointsForName(n)
    let m = escapeMode(esc.value)
    if let cp = m.codepointForName(name.value) { _ = m.nameForCodepoint(cp) }
    if let u = s.unicodeScalars.first { _ = m.nameForCodepoint(u) }
}
