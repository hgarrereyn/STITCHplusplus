// SwiftSoup (a Swift port of jsoup): parsing, the cleaner, selectors, tree edits.
// Oracles: parse(html()) prints the same again; clean() output passes isValid and stays the same
// when cleaned again; selected elements belong to the document. Thrown SwiftSoup.Exception is a bail.
import Stitch
import SwiftSoup

enum Doc: StitchObject { typealias Handle = Document }
enum Elem: StitchObject { typealias Handle = Element }

let tags = ["div", "p", "a", "b", "i", "span", "table", "tr", "td", "ul", "li", "script", "style", "svg", "math", "img", "br",
            "textarea", "select", "option", "form", "html", "head", "body", "title", "pre", "template", "noscript", "frameset"]
let attrs = ["href", "src", "id", "class", "style", "onclick", "title", "alt", "rel", "type", "name", "value"]

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
            case 5: out += s.pick(["<!--", "-->", "<![CDATA[", "]]>", "<!DOCTYPE html>", "<?", "<!"])
            case 6: out += s.pick(["javascript:", "java\tscript:", "data:", "vbscript:", "http://", "/rel"])
            default: out += s.token()
            }
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
            case 1: out += s.pick(["#", ".", "[", "]", "=", "^=", "$=", "*=", "~=", ">", "+", "~", " ", ",", ":"])
            case 2: out += s.pick([":has(", ":not(", ":contains(", ":matches(", ":eq(", ":lt(", ":gt(", ":nth-child(", ":first-child", ":empty", ")"])
            case 3: out += s.pick(attrs)
            case 4: out += s.str(4)
            default: out += s.token()
            }
        }
        return out
    }
}

let parse = endpoint("parse") { (html: Str<2048, Html>) throws -> New<Doc> in
    New(try SwiftSoup.parse(html.value))
}

let parseFragment = endpoint("parse_fragment") { (html: Str<2048, Html>) throws -> New<Doc> in
    New(try SwiftSoup.parseBodyFragment(html.value))
}

// html() parses again and prints the same
let roundtrip = endpoint("roundtrip") { (doc: Borrow<Doc>) throws in
    let a = try doc.value.html()
    let b = try SwiftSoup.parse(a).html()
    log("html: \(a.debugDescription)")
    try check(try SwiftSoup.parse(b).html() == b, "parse(html()) is stable")
}

let clean = endpoint("clean") { (html: Str<2048, Html>, list: Between<0, 4>, relative: Bool) throws in
    let lists: [() throws -> Whitelist] = [Whitelist.none, Whitelist.simpleText, Whitelist.basic, Whitelist.basicWithImages, Whitelist.relaxed]
    let wl = try lists[list.value]()
    if relative { try wl.preserveRelativeLinks(true) }
    guard let out = try SwiftSoup.clean(html.value, "http://example.com/", wl) else { return }
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
    let again = try SwiftSoup.clean(out, "http://example.com/", wl)
    try check(again == out, "clean() is idempotent")
}

let select = endpoint("select") { (doc: Borrow<Doc>, q: Str<64, Selector>) throws -> New<Elem>? in
    let els = try doc.value.select(q.value)
    for e in els.array() {
        try check(e.ownerDocument() === doc.value, "selected elements belong to the document")
    }
    return els.first().map { New($0) }
}

let body = endpoint("body") { (doc: Borrow<Doc>) throws -> New<Elem>? in
    doc.value.body().map { New($0) }
}

let child = endpoint("child") { (el: Borrow<Elem>, s: Choices<8>) throws -> New<Elem>? in
    let kids = el.value.children().array()
    return kids.isEmpty ? nil : New(s.source.pick(kids))
}

let text = endpoint("text") { (el: Borrow<Elem>) throws in
    _ = try el.value.text()
    _ = try el.value.outerHtml()
    _ = el.value.ownText()
    _ = try el.value.cssSelector()
    _ = el.value.siblingIndex
}

let setAttr = endpoint("set_attr") { (el: Borrow<Elem>, key: Str<16, Plain>, value: Str<32, Plain>) throws in
    try el.value.attr(key.value, value.value)
    try check(el.value.hasAttr(key.value) || key.value.isEmpty, "a set attribute is present")
}

let removeAttr = endpoint("remove_attr") { (el: Borrow<Elem>, key: Str<16, Plain>) throws in
    try el.value.removeAttr(key.value)
}

let append = endpoint("append") { (el: Borrow<Elem>, html: Str<256, Html>) throws in
    try el.value.append(html.value)
}

let prepend = endpoint("prepend") { (el: Borrow<Elem>, html: Str<256, Html>) throws in
    try el.value.prepend(html.value)
}

let wrap = endpoint("wrap") { (el: Borrow<Elem>, html: Str<128, Html>) throws in
    try el.value.wrap(html.value)
}

let unwrap = endpoint("unwrap") { (el: Borrow<Elem>) throws in
    try require(el.value.parent() != nil, "no parent")
    _ = try el.value.unwrap()
}

let remove = endpoint("remove") { (el: Borrow<Elem>) throws in
    try require(el.value.parent() != nil, "no parent")
    try el.value.remove()
}

let setText = endpoint("set_text") { (el: Borrow<Elem>, t: Str<64, Plain>) throws in
    try el.value.text(t.value)
}

let empty = endpoint("empty") { (el: Borrow<Elem>) throws in
    el.value.empty()
    try check(el.value.childNodeSize() == 0, "empty() leaves no children")
}

let addClass = endpoint("add_class") { (el: Borrow<Elem>, c: Str<16, Plain>, toggle: Bool) throws in
    if toggle { try el.value.toggleClass(c.value) } else { try el.value.addClass(c.value) }
    _ = try el.value.classNames()
}

let tagName = endpoint("tag_name") { (el: Borrow<Elem>, t: Str<16, Plain>) throws in
    try el.value.tagName(t.value)
}
