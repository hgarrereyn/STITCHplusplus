//! regex: compile and search with Rust regular expressions (and regex-syntax's parser/printers).
//!
//! Oracles: the search APIs agree with each other (is_match, find, captures, shortest_match,
//! find_iter, RegexSet); the lazy DFA and the fallback engines agree (a tiny DFA cache forces the
//! fallback); `str` and `bytes` regexes agree on UTF-8 text; the AST printer round-trips; the HIR
//! printer gives an equivalent regex; escape(s) matches exactly s. Panics, overflow and debug
//! assertions are bugs. Compile limits are lowered so blowups by design bail.
use regex::bytes::Regex as BRegex;
use regex::{Regex, RegexBuilder, RegexSet};
use regex_syntax::ast::parse::ParserBuilder as AstParserBuilder;
use stitch::*;

pub struct Re {
    re: Regex,
    slow: Regex, // same pattern, tiny DFA cache (fallback engines)
    bytes: Option<BRegex>,
    pattern: String,
}

object!(R, Re);
datatype!(Pattern);
datatype!(Haystack);

decoder!(Pattern, fn pattern(s: &mut Source) -> String {
    fn atom(s: &mut Source, d: u32) -> String {
        match s.choice(if d > 3 { 5 } else { 9 }) {
            0 => s.str(3),
            1 => s.pick(&["a", "b", "ab", "x", "\u{e9}", "\u{3b1}", "\u{1f600}", "\\n", "0", " "]).to_string(),
            2 => s.pick(&[".", "\\w", "\\W", "\\d", "\\s", "\\b", "\\B", "^", "$", "\\A", "\\z", "\\pL", "\\p{Greek}", "\\PN", "\\p{Lu}", "\\x41", "\\x{10FFFF}", "\\u{e9}"]).to_string(),
            3 => format!("[{}{}]", if s.coin(0.2) { "^" } else { "" }, s.pick(&["a-z", "0-9", "[:alpha:]", "\\d&&[0-5]", "a-z--aeiou", "\\pL~~\\p{Greek}", "\u{e9}-\u{3c9}", "]", "-a", "^"])),
            4 => s.token(),
            5 => format!("({})", alt(s, d + 1)),
            6 => format!("(?{}:{})", s.pick(&["i", "m", "s", "U", "x", "-u", "u", "i-s", "P<name>", "", "#"]), alt(s, d + 1)),
            7 => format!("(?P<{}>{})", s.pick(&["a", "b", "x1"]), alt(s, d + 1)),
            _ => format!("{}{}", atom(s, d + 1), s.pick(&["*", "+", "?", "*?", "+?", "??", "{2}", "{1,3}", "{0,}", "{3,2}", "{1000}", "{2,}?", "{,3}"])),
        }
    }
    fn seq(s: &mut Source, d: u32) -> String {
        let mut out = String::new();
        loop {
            out += &atom(s, d);
            if !s.more() || !s.coin(0.6) { break; }
        }
        out
    }
    fn alt(s: &mut Source, d: u32) -> String {
        let mut out = seq(s, d);
        while s.more() && s.coin(0.25) {
            out += "|";
            out += &seq(s, d);
        }
        out
    }
    alt(s, 0)
});

decoder!(Haystack, fn haystack(s: &mut Source) -> String {
    let mut out = String::new();
    while s.more() {
        out += &match s.choice(4) {
            0 => s.str(8),
            1 => s.token(),
            2 => s.pick(&["a", "ab", "\n", "\r\n", "x y", "\u{e9}", "\u{3b1}\u{3b2}", "\u{1f600}", "0123", "  "]).to_string(),
            _ => s.pick(&["foo", "bar", "Hello World", "AaBb", "123abc"]).to_string(),
        };
    }
    out
});

flags!(Opts = [1, 2, 4, 8, 16, 32, 64]);

fn builder(pattern: &str, opts: u32) -> RegexBuilder {
    let mut b = RegexBuilder::new(pattern);
    b.case_insensitive(opts & 1 != 0)
        .multi_line(opts & 2 != 0)
        .dot_matches_new_line(opts & 4 != 0)
        .swap_greed(opts & 8 != 0)
        .ignore_whitespace(opts & 16 != 0)
        .unicode(opts & 32 == 0)
        .octal(opts & 64 != 0)
        .size_limit(1 << 20)
        .nest_limit(64);
    b
}

endpoint! {
    fn compile(pattern: Str<128, Pattern>, opts: Flags<Opts>) -> Result<New<R>, regex::Error> {
        let opts = opts.0 as u32;
        let re = builder(&pattern, opts).build()?;
        let slow = builder(&pattern, opts).dfa_size_limit(0).build();
        check(slow.is_ok(), "the pattern compiles with a tiny DFA cache too");
        let bytes = regex::bytes::RegexBuilder::new(&pattern)
            .case_insensitive(opts & 1 != 0).multi_line(opts & 2 != 0).dot_matches_new_line(opts & 4 != 0)
            .swap_greed(opts & 8 != 0).ignore_whitespace(opts & 16 != 0).unicode(opts & 32 == 0).octal(opts & 64 != 0)
            .size_limit(1 << 20).nest_limit(64).build();
        check(bytes.is_ok(), "a pattern that compiles as str regex compiles as bytes regex");
        check(re.captures_len() == slow.as_ref().unwrap().captures_len(), "capture counts agree");
        Ok(New::new(Re { re, slow: slow.unwrap(), bytes: bytes.ok(), pattern: pattern.to_string() }))
    }
}

type M = Option<(usize, usize)>;

fn caps_vec(re: &Regex, text: &str) -> Option<Vec<M>> {
    re.captures(text).map(|c| c.iter().map(|m| m.map(|m| (m.start(), m.end()))).collect())
}

endpoint! {
    fn search(r: Borrow<R>, text: Str<256, Haystack>, start: Range<0, 64>) {
        let re = &r.re;
        let is = re.is_match(&text);
        let found: M = re.find(&text).map(|m| (m.start(), m.end()));
        let short = re.shortest_match(&text);
        let caps = caps_vec(re, &text);
        log(&format!("is_match {is} find {found:?} shortest {short:?} caps {caps:?}"));
        check(is == found.is_some(), "is_match agrees with find");
        check(is == short.is_some(), "is_match agrees with shortest_match");
        check(caps.as_ref().map(|c| c[0]) == found.map(Some), "captures group 0 is the find match");
        if let (Some(e), Some((s, _))) = (short, found) {
            check(e >= s, "shortest_match ends at or after the leftmost match start");
        }
        // fallback engines
        let slow_found: M = r.slow.find(&text).map(|m| (m.start(), m.end()));
        check(found == slow_found, "lazy DFA and fallback engines find the same match");
        check(caps == caps_vec(&r.slow, &text), "lazy DFA and fallback engines capture the same");
        // bytes regex on the same text
        if let Some(b) = &r.bytes {
            let bfound: M = b.find(text.as_bytes()).map(|m| (m.start(), m.end()));
            check(found == bfound, "str and bytes regexes find the same match on UTF-8 text");
        }
        // find_at from a char boundary
        let st = (*start as usize).min(text.len());
        if text.is_char_boundary(st) {
            let a: M = re.find_at(&text, st).map(|m| (m.start(), m.end()));
            let b: M = r.slow.find_at(&text, st).map(|m| (m.start(), m.end()));
            check(a == b, "find_at agrees across engines");
            if let Some((s, e)) = a {
                check(s >= st && e >= s && e <= text.len(), "find_at returns a match within bounds");
            }
        }
    }
}

endpoint! {
    fn iterate(r: Borrow<R>, text: Str<256, Haystack>) {
        let re = &r.re;
        let mut last_end = 0usize;
        let mut prev_empty_at: Option<usize> = None;
        let all: Vec<(usize, usize)> = re.find_iter(&text).take(1000).map(|m| (m.start(), m.end())).collect();
        for &(s, e) in &all {
            check(s >= last_end && e >= s, "find_iter matches are in order and do not overlap");
            check(text.is_char_boundary(s) && text.is_char_boundary(e), "str matches fall on char boundaries");
            if s == e {
                check(prev_empty_at != Some(s), "no two empty matches at the same position");
                prev_empty_at = Some(s);
            }
            last_end = e;
        }
        let slow: Vec<(usize, usize)> = r.slow.find_iter(&text).take(1000).map(|m| (m.start(), m.end())).collect();
        check(all == slow, "find_iter agrees across engines");
        let caps: Vec<(usize, usize)> = re.captures_iter(&text).take(1000).map(|c| { let m = c.get(0).unwrap(); (m.start(), m.end()) }).collect();
        check(all == caps, "captures_iter visits the find_iter matches");
        let pieces: Vec<&str> = re.split(&text).take(1001).collect();
        if all.len() < 1000 {
            check(pieces.len() == all.len() + 1, "split gives one more piece than there are matches");
        }
        let replaced = re.replace_all(&text, "<$0>");
        check(replaced.len() == text.len() + 2 * all.len(), "replace_all wraps every match");
    }
}

endpoint! {
    fn set(r: Borrow<R>, other: Str<64, Pattern>, text: Str<128, Haystack>) {
        let set = match RegexSet::new(&[r.pattern.as_str(), other.as_str()]) {
            Ok(s) => s,
            Err(_) => bail("set does not compile"),
        };
        // (RegexSet has no options: compare against the default-option regex)
        let plain = match Regex::new(&r.pattern) { Ok(p) => p, Err(_) => bail("pattern needs options") };
        let m = set.matches(&text);
        check(m.matched(0) == plain.is_match(&text), "RegexSet agrees with Regex::is_match");
        if let Ok(o) = Regex::new(&other) {
            check(m.matched(1) == o.is_match(&text), "RegexSet agrees with Regex::is_match (second pattern)");
        }
        check(set.is_match(&text) == m.matched_any(), "RegexSet::is_match agrees with matches()");
    }
}

endpoint! {
    fn escape(text: Str<64, Haystack>) {
        let e = regex::escape(&text);
        let re = Regex::new(&format!("^{}$", e));
        check(re.is_ok(), "an escaped string compiles");
        check(re.unwrap().is_match(&text), "escape(s) matches s");
    }
}

endpoint! {
    fn ast_roundtrip(pattern: Str<128, Pattern>, whitespace: bool) {
        let mut p = AstParserBuilder::new().ignore_whitespace(whitespace).nest_limit(64).build();
        let ast = match p.parse(&pattern) { Ok(a) => a, Err(_) => bail("does not parse") };
        let mut printed = String::new();
        regex_syntax::ast::print::Printer::new().print(&ast, &mut printed).unwrap();
        log(&format!("printed: {printed}"));
        let mut p2 = AstParserBuilder::new().ignore_whitespace(whitespace).nest_limit(64).build();
        let again = p2.parse(&printed);
        check(again.is_ok(), "the printed AST parses");
        let mut printed2 = String::new();
        regex_syntax::ast::print::Printer::new().print(&again.unwrap(), &mut printed2).unwrap();
        check(printed == printed2, "AST printing is a fixed point");
    }
}

endpoint! {
    fn hir_roundtrip(r: Borrow<R>, text: Str<128, Haystack>) {
        let mut parser = regex_syntax::ParserBuilder::new().nest_limit(64).build();  // (UTF-8 only: the default in every version)
        let hir = match parser.parse(&r.pattern) { Ok(h) => h, Err(_) => bail("needs builder options") };
        let printed = hir.to_string();
        log(&format!("hir: {printed}"));
        let again = RegexBuilder::new(&printed).size_limit(1 << 22).nest_limit(128).build();
        let again = match again {
            Ok(a) => a,
            // (printing spells Unicode classes out range by range: the result may be over the size limit)
            Err(regex::Error::CompiledTooBig(_)) => bail("printed HIR over the size limit"),
            Err(e) => { log(&format!("{e}")); check(false, "the printed HIR compiles"); unreachable!() }
        };
        let plain = match Regex::new(&r.pattern) { Ok(p) => p, Err(_) => bail("pattern needs options") };
        let a: M = plain.find(&text).map(|m| (m.start(), m.end()));
        let b: M = again.find(&text).map(|m| (m.start(), m.end()));
        check(a == b, "the printed HIR matches like the pattern");
    }
}
