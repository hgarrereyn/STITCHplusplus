//! regex-syntax: the AST parser and printer, the HIR translator, printer and properties, literal
//! extraction, Unicode and byte class set operations, UTF-8 sequences and the escaping helpers.
//!
//! Oracles: the AST printer round-trips and is a fixed point; Parser == ast::parse + translate;
//! the printed HIR compiles and matches like the pattern; every match (found with regex-automata
//! from the HIR) has a length within minimum_len..=maximum_len, is valid UTF-8 when is_utf8(),
//! equals the literal when is_literal(), and has static_explicit_captures_len() groups; every
//! match starts (ends) with one of the extracted prefix (suffix) literals, before and after the
//! Seq optimizations; class set operations agree with a membership model; each char in a
//! Utf8Sequences range matches exactly one sequence; parse(escape(s)) is the literal s.
pub use crate::objects::*;
use regex::RegexBuilder;
use regex_automata::meta;
use regex_syntax::ast::{self, parse::ParserBuilder as AstParserBuilder};
use regex_syntax::hir::literal::{ExtractKind, Extractor, Literal, Seq};
use regex_syntax::hir::{self, ClassBytes, ClassBytesRange, ClassUnicode, ClassUnicodeRange, Hir, HirKind};
use regex_syntax::utf8::Utf8Sequences;
use stitch::*;

flags!(SynFlags = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]);

fn parser(f: u32) -> regex_syntax::Parser {
    regex_syntax::ParserBuilder::new()
        .case_insensitive(f & 1 != 0)
        .multi_line(f & 2 != 0)
        .dot_matches_new_line(f & 4 != 0)
        .crlf(f & 8 != 0)
        .swap_greed(f & 16 != 0)
        .ignore_whitespace(f & 32 != 0)
        .unicode(f & 64 == 0)
        .utf8(f & 128 == 0)
        .octal(f & 256 != 0)
        .line_terminator(if f & 512 != 0 { 0 } else { b'\n' })
        .nest_limit(64)
        .build()
}

fn parse_hir(pattern: &str, f: u32) -> Hir {
    match parser(f).parse(pattern) {
        Ok(h) => h,
        Err(e) => {
            let _ = (e.to_string(), format!("{e:?}"));
            bail("does not parse")
        }
    }
}

fn meta_from_hir(hir: &Hir, f: u32) -> meta::Regex {
    let cfg = meta::Config::new().utf8_empty(f & 128 == 0).nfa_size_limit(Some(1 << 20));
    match meta::Builder::new().configure(cfg).build_from_hir(hir) {
        Ok(r) => r,
        Err(_) => bail("HIR too big to compile"),
    }
}

struct Count(usize);
impl ast::Visitor for Count {
    type Output = usize;
    type Err = ();
    fn finish(self) -> Result<usize, ()> { Ok(self.0) }
    fn visit_pre(&mut self, _ast: &ast::Ast) -> Result<(), ()> { self.0 += 1; Ok(()) }
}

endpoint! {
    fn ast_roundtrip(pattern: Str<128, Pattern>, whitespace: bool, empty_min_range: bool) {
        let build = || AstParserBuilder::new().ignore_whitespace(whitespace).empty_min_range(empty_min_range).nest_limit(64).build();
        let ast = match build().parse(&pattern) {
            Ok(a) => a,
            Err(e) => {
                check(e.pattern() == pattern.as_str() && e.span().end.offset <= pattern.len(), "ast::Error pattern / span");
                let _ = (e.kind(), e.auxiliary_span(), e.to_string());
                bail("does not parse")
            }
        };
        check(ast.span().end.offset <= pattern.len(), "Ast::span() within the pattern");
        let nodes = ast::visit(&ast, Count(0)).unwrap();
        check(nodes >= 1, "ast::visit visits the root");
        if whitespace {
            let wc = build().parse_with_comments(&pattern);
            check(wc.as_ref().map(|w| &w.ast) == Ok(&ast), "parse_with_comments gives the same AST");
        }
        let mut printed = String::new();
        ast::print::Printer::new().print(&ast, &mut printed).unwrap();
        log(&format!("printed: {printed}"));
        check(ast.to_string() == printed, "Ast Display == Printer");
        let again = build().parse(&printed);
        check(again.is_ok(), "the printed AST parses");
        let mut printed2 = String::new();
        ast::print::Printer::new().print(&again.unwrap(), &mut printed2).unwrap();
        check(printed == printed2, "AST printing is a fixed point");
    }
}

endpoint! {
    fn hir_roundtrip(r: Borrow<R>, text: Str<128, Haystack>) {
        let mut parser = regex_syntax::ParserBuilder::new().nest_limit(64).build();
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
        let plain = match regex::Regex::new(&r.pattern) { Ok(p) => p, Err(_) => bail("pattern needs options") };
        let a = plain.find(&text).map(|m| (m.start(), m.end()));
        let b = again.find(&text).map(|m| (m.start(), m.end()));
        check(a == b, "the printed HIR matches like the pattern");
    }
}

fn alternation_literals(h: &Hir) -> Vec<Vec<u8>> {
    match h.kind() {
        HirKind::Literal(l) => vec![l.0.to_vec()],
        HirKind::Alternation(subs) => subs.iter().flat_map(alternation_literals).collect(),
        _ => vec![],
    }
}

endpoint! {
    fn hir_props(pattern: Str<128, Pattern>, flags: Flags<SynFlags>, text: Bytes<256, Haystack>) {
        let f = flags.0 as u32;
        let hir = parse_hir(&pattern, f);
        // Parser == ast parser + translator
        let ast = AstParserBuilder::new().nest_limit(64).octal(f & 256 != 0).ignore_whitespace(f & 32 != 0).build().parse(&pattern);
        check(ast.is_ok(), "the AST parser accepts what the parser accepts");
        let t = hir::translate::TranslatorBuilder::new()
            .case_insensitive(f & 1 != 0).multi_line(f & 2 != 0).dot_matches_new_line(f & 4 != 0)
            .crlf(f & 8 != 0).swap_greed(f & 16 != 0).unicode(f & 64 == 0).utf8(f & 128 == 0)
            .line_terminator(if f & 512 != 0 { 0 } else { b'\n' })
            .build().translate(&pattern, &ast.unwrap());
        check(t.as_ref().ok() == Some(&hir), "Parser == ast::parse + translate");
        let p = hir.properties();
        let (minl, maxl) = (p.minimum_len(), p.maximum_len());
        log(&format!("min {minl:?} max {maxl:?} utf8 {} literal {} alt-literal {} static {:?} looks {:?}",
                     p.is_utf8(), p.is_literal(), p.is_alternation_literal(), p.static_explicit_captures_len(), p.look_set()));
        let _ = (p.look_set_prefix(), p.look_set_suffix(), p.look_set_prefix_any(), p.look_set_suffix_any(), p.memory_usage(), hir.kind().subs().len());
        check(p.look_set_prefix().subtract(p.look_set()).is_empty() && p.look_set_suffix().subtract(p.look_set()).is_empty(), "prefix/suffix look sets are subsets of look_set()");
        if let (Some(a), Some(b)) = (minl, maxl) {
            check(a <= b, "minimum_len <= maximum_len");
        }
        let re = meta_from_hir(&hir, f);
        check(re.captures_len() == p.explicit_captures_len() + 1, "explicit_captures_len() + 1 == compiled group count");
        let lits = if p.is_alternation_literal() { alternation_literals(&hir) } else { vec![] };
        let mut n = 0;
        for c in re.captures_iter(&text[..]).take(200) {
            let m = c.get_match().unwrap();
            let s = &text[m.range()];
            n += 1;
            check(minl.is_some(), "minimum_len() is None only for regexes that never match");
            check(m.len() >= minl.unwrap_or(0) && maxl.map_or(true, |x| m.len() <= x), "match length within minimum_len..=maximum_len");
            if p.is_utf8() {
                check(std::str::from_utf8(s).is_ok(), "an is_utf8() regex matches only valid UTF-8");
            }
            if p.is_literal() {
                check(hir.kind() == &HirKind::Literal(hir::Literal(s.into())), "an is_literal() regex matches only its literal");
            }
            if p.is_alternation_literal() {
                check(lits.iter().any(|l| l == s), "an is_alternation_literal() regex matches one of its literals");
            }
            if let Some(k) = p.static_explicit_captures_len() {
                check((1..c.group_len()).filter(|&i| c.get_group(i).is_some()).count() == k, "static_explicit_captures_len() groups always match");
            }
        }
        log(&format!("{n} matches"));
    }
}

/// Every match starts (prefix) or ends (suffix) with one of the literals; with `exact`, an exact
/// literal must be the whole match.
fn covers(seq: &Seq, m: &[u8], prefix: bool, exact: bool) -> bool {
    match seq.literals() {
        None => true,
        Some(lits) => lits.iter().any(|l| {
            if exact && l.is_exact() { l.as_bytes() == m }
            else if prefix { m.starts_with(l.as_bytes()) } else { m.ends_with(l.as_bytes()) }
        }),
    }
}

endpoint! {
    fn literals(pattern: Str<128, Pattern>, flags: Flags<SynFlags>, text: Bytes<256, Haystack>, suffix: bool,
                lc: Range<0, 300>, lr: Range<0, 300>, ll: Range<0, 300>, lt: Range<0, 300>, keep: Range<0, 8>) {
        let f = flags.0 as u32;
        let hir = parse_hir(&pattern, f);
        let re = meta_from_hir(&hir, f);
        let mut ex = Extractor::new();
        ex.kind(if suffix { ExtractKind::Suffix } else { ExtractKind::Prefix })
            .limit_class(*lc as usize).limit_repeat(*lr as usize).limit_literal_len(*ll as usize).limit_total(*lt as usize);
        let seq = ex.extract(&hir);
        log(&format!("seq {seq:?}"));
        let prefix = !suffix;
        check(seq.is_finite() == seq.len().is_some() && seq.is_finite() == seq.literals().is_some(), "Seq::is_finite / len / literals agree");
        let mut variants: Vec<(&str, Seq)> = vec![];
        let mut s = seq.clone();
        if prefix { s.optimize_for_prefix_by_preference() } else { s.optimize_for_suffix_by_preference() }
        variants.push(("optimize_for_*_by_preference", s));
        if prefix {
            let mut s = seq.clone();
            s.minimize_by_preference();
            variants.push(("minimize_by_preference", s));
        }
        let mut s = seq.clone();
        if prefix { s.keep_first_bytes(*keep as usize) } else { s.keep_last_bytes(*keep as usize) }
        variants.push(("keep_*_bytes", s));
        let mut s = seq.clone();
        s.union(&mut Seq::singleton(Literal::exact(b"zz".to_vec())));
        s.dedup();
        s.sort();
        variants.push(("union / dedup / sort", s));
        let mut s = seq.clone();
        s.make_inexact();
        variants.push(("make_inexact", s));
        let common: Option<Vec<u8>> = if prefix { seq.longest_common_prefix().map(|x| x.to_vec()) } else { seq.longest_common_suffix().map(|x| x.to_vec()) };
        if let (Some(a), Some(b)) = (seq.min_literal_len(), seq.max_literal_len()) {
            check(a <= b, "min_literal_len <= max_literal_len");
        }
        for m in re.find_iter(&text[..]).take(200) {
            let s = &text[m.range()];
            check(covers(&seq, s, prefix, true), "every match starts/ends with an extracted literal (or equals an exact one)");
            for (what, v) in &variants {
                if !covers(v, s, prefix, false) {
                    log(what);
                    check(false, "every match starts/ends with a literal after a Seq operation");
                }
            }
            if let Some(c) = &common {
                if seq.literals().map_or(false, |l| !l.is_empty()) {
                    check(if prefix { s.starts_with(c) } else { s.ends_with(c) }, "every match starts/ends with longest_common_prefix/suffix");
                }
            }
        }
        // cross products of two literal sets
        let mut a = seq.clone();
        let mut b = Seq::new([b"x".as_slice(), b"yz".as_slice()]);
        let want = a.max_cross_len(&b);
        if prefix { a.cross_forward(&mut b) } else { a.cross_reverse(&mut b) }
        if let (Some(w), Some(n)) = (want, a.len()) {
            check(n <= w, "cross result within max_cross_len");
        }
    }
}

datatype!(Classes);

#[derive(Debug)]
pub struct ClassPair {
    pub a: Vec<(char, char)>,
    pub b: Vec<(char, char)>,
    pub samples: Vec<char>,
}

const POINTS: [u32; 22] = [0, 9, 0x20, 0x30, 0x41, 0x5a, 0x61, 0x6b, 0x7a, 0x7f, 0x80, 0xdf, 0xff, 0x100, 0x3b1, 0x7ff, 0x800, 0x212a, 0xd7ff, 0xe000, 0xffff, 0x10ffff];

fn some_char(s: &mut Source) -> char {
    let base = s.pick(&POINTS) as i64 + s.range(-2, 2);
    char::from_u32(base.clamp(0, 0x10ffff) as u32).unwrap_or('\u{e000}')
}

decoder!(Classes, fn classes(s: &mut Source) -> ClassPair {
    let mut ranges = |s: &mut Source| {
        let mut v = vec![];
        while s.more() && v.len() < 6 {
            v.push((some_char(s), some_char(s)));
        }
        v
    };
    let a = ranges(s);
    let b = ranges(s);
    let mut samples = vec![];
    while s.more() && samples.len() < 8 {
        samples.push(some_char(s));
    }
    ClassPair { a, b, samples }
});

fn uclass(r: &[(char, char)]) -> ClassUnicode {
    ClassUnicode::new(r.iter().map(|&(a, b)| ClassUnicodeRange::new(a, b)))
}

fn in_u(c: &ClassUnicode, x: char) -> bool {
    c.ranges().iter().any(|r| r.start() <= x && x <= r.end())
}

fn canonical_u(c: &ClassUnicode) -> bool {
    c.ranges().iter().all(|r| r.start() <= r.end()) && c.ranges().windows(2).all(|w| w[0].end() < w[1].start())
}

endpoint! {
    fn class_unicode(p: Val<Classes, 256>) {
        let (a, b) = (uclass(&p.a), uclass(&p.b));
        let mut pts: Vec<char> = p.samples.clone();
        for r in a.ranges().iter().chain(b.ranges()) {
            for x in [r.start() as u32, r.end() as u32] {
                for d in [x.wrapping_sub(1), x, x + 1] {
                    if let Some(c) = char::from_u32(d) { pts.push(c) }
                }
            }
        }
        let ops: Vec<(&str, ClassUnicode, Box<dyn Fn(bool, bool) -> bool>)> = vec![
            ("union", { let mut c = a.clone(); c.union(&b); c }, Box::new(|x, y| x || y)),
            ("intersect", { let mut c = a.clone(); c.intersect(&b); c }, Box::new(|x, y| x && y)),
            ("difference", { let mut c = a.clone(); c.difference(&b); c }, Box::new(|x, y| x && !y)),
            ("symmetric_difference", { let mut c = a.clone(); c.symmetric_difference(&b); c }, Box::new(|x, y| x != y)),
            ("negate", { let mut c = a.clone(); c.negate(); c }, Box::new(|x, _| !x)),
        ];
        for (what, c, f) in &ops {
            check(canonical_u(c), "class ranges are sorted and disjoint");
            for &x in &pts {
                if in_u(c, x) != f(in_u(&a, x), in_u(&b, x)) {
                    log(&format!("{what} at {x:?}"));
                    check(false, "ClassUnicode set operation agrees with the membership model");
                }
            }
        }
        let mut n = a.clone();
        n.negate();
        n.negate();
        check(n == a, "negate twice is the identity");
        let mut folded = a.clone();
        folded.case_fold_simple();
        check(pts.iter().all(|&x| !in_u(&a, x) || in_u(&folded, x)), "case_fold_simple keeps every member");
        let mut twice = folded.clone();
        check(twice.try_case_fold_simple().is_ok() && twice == folded, "case_fold_simple is idempotent");
        check(a.is_ascii() == a.to_byte_class().is_some(), "to_byte_class() iff is_ascii()");
        if let Some(bc) = a.to_byte_class() {
            check((0u8..128).all(|x| in_u(&a, x as char) == bc.ranges().iter().any(|r| r.start() <= x && x <= r.end())), "to_byte_class() keeps the members");
        }
        let single = a.ranges().len() == 1 && a.ranges()[0].start() == a.ranges()[0].end();
        check(a.literal().is_some() == single, "literal() iff a single char");
        if let Some(r) = a.ranges().first() {
            check(a.minimum_len() == Some(r.start().len_utf8()), "minimum_len() is the UTF-8 length of the first char");
            check(a.maximum_len() == Some(a.ranges().last().unwrap().end().len_utf8()), "maximum_len()");
            check(a.iter().map(|r| r.len()).sum::<usize>() > 0, "range len()");
        } else {
            check(a.minimum_len().is_none() && a.maximum_len().is_none(), "empty class has no length");
        }
        // the same in the Hir: a class matches only members
        let hir = Hir::class(hir::Class::Unicode(a.clone()));
        let printed = hir.to_string();
        let back = regex_syntax::ParserBuilder::new().build().parse(&printed);
        if let Ok(back) = back {
            if let HirKind::Class(hir::Class::Unicode(bu)) = back.kind() {
                check(*bu == a, "a printed Unicode class parses back to itself");
            }
        }
    }
}

endpoint! {
    fn class_bytes(p: Val<Classes, 256>) {
        let to_b = |r: &[(char, char)]| ClassBytes::new(r.iter().map(|&(a, b)| ClassBytesRange::new((a as u32 & 0xff) as u8, (b as u32 & 0xff) as u8)));
        let (a, b) = (to_b(&p.a), to_b(&p.b));
        let has = |c: &ClassBytes, x: u8| c.ranges().iter().any(|r| r.start() <= x && x <= r.end());
        let ops: Vec<(ClassBytes, Box<dyn Fn(bool, bool) -> bool>)> = vec![
            ({ let mut c = a.clone(); c.union(&b); c }, Box::new(|x, y| x || y)),
            ({ let mut c = a.clone(); c.intersect(&b); c }, Box::new(|x, y| x && y)),
            ({ let mut c = a.clone(); c.difference(&b); c }, Box::new(|x, y| x && !y)),
            ({ let mut c = a.clone(); c.symmetric_difference(&b); c }, Box::new(|x, y| x != y)),
            ({ let mut c = a.clone(); c.negate(); c }, Box::new(|x, _| !x)),
        ];
        for (c, f) in &ops {
            check(c.ranges().windows(2).all(|w| w[0].end() < w[1].start()), "byte class ranges are sorted and disjoint");
            check((0..=255u8).all(|x| has(c, x) == f(has(&a, x), has(&b, x))), "ClassBytes set operation agrees with the membership model");
        }
        let mut folded = a.clone();
        folded.case_fold_simple();
        check((0..=255u8).all(|x| has(&folded, x) == (has(&a, x) || has(&a, x.to_ascii_lowercase()) || has(&a, x.to_ascii_uppercase()))), "ClassBytes::case_fold_simple is ASCII case folding");
        check(a.is_ascii() == a.ranges().iter().all(|r| r.end() < 128), "ClassBytes::is_ascii");
        check(a.to_unicode_class().is_some() == a.is_ascii(), "to_unicode_class() iff is_ascii()");
        check(a.iter().map(|r| r.len()).sum::<usize>() == (0..=255u8).filter(|&x| has(&a, x)).count(), "ClassBytesRange::len()");
        let single = a.ranges().len() == 1 && a.ranges()[0].start() == a.ranges()[0].end();
        check(a.literal().is_some() == single, "ClassBytes::literal() iff a single byte");
        check(a.minimum_len() == if a.ranges().is_empty() { None } else { Some(1) }, "ClassBytes::minimum_len()");
    }
}

endpoint! {
    fn utf8_sequences(p: Val<Classes, 256>) {
        require(!p.a.is_empty(), "no range");
        let (mut lo, mut hi) = p.a[0];
        if lo > hi { std::mem::swap(&mut lo, &mut hi) }
        let seqs: Vec<_> = Utf8Sequences::new(lo, hi).collect();
        let mut pts: Vec<char> = p.samples.clone();
        pts.extend([lo, hi]);
        for x in [0x7fu32, 0x80, 0x7ff, 0x800, 0xffff, 0x10000, lo as u32 + 1, hi as u32 - (hi as u32 > 0) as u32] {
            if let Some(c) = char::from_u32(x) { pts.push(c) }
        }
        for c in pts {
            let mut buf = [0; 4];
            let enc = c.encode_utf8(&mut buf).as_bytes();
            let n = seqs.iter().filter(|s| s.matches(enc)).count();
            let inside = lo <= c && c <= hi;
            check(n == inside as usize, "a char in the range matches exactly one UTF-8 sequence (none outside)");
            if inside {
                let s = seqs.iter().find(|s| s.matches(enc)).unwrap();
                check(s.len() == enc.len() && s.as_slice().iter().zip(enc).all(|(r, b)| r.matches(*b)), "Utf8Sequence::as_slice ranges match the bytes");
                let mut rev = s.clone();
                rev.reverse();
                let mut renc = enc.to_vec();
                renc.reverse();
                check(rev.matches(&renc), "a reversed sequence matches the reversed bytes");
            }
        }
    }
}

endpoint! {
    fn syntax_escape(text: Str<64, Haystack>) {
        let e = regex_syntax::escape(&text);
        let mut e2 = String::from("x");
        regex_syntax::escape_into(&text, &mut e2);
        check(e2[1..] == e, "escape_into == escape");
        let hir = regex_syntax::parse(&e);
        let want = if text.is_empty() { Hir::empty() } else { Hir::literal(text.as_bytes()) };
        check(hir.as_ref().ok() == Some(&want), "parse(escape(s)) is the literal s");
        for c in text.chars() {
            if regex_syntax::is_meta_character(c) {
                check(regex_syntax::is_escapeable_character(c) && regex_syntax::escape(&c.to_string()) == format!("\\{c}"), "meta characters are escaped with a backslash");
            }
            let w = regex_syntax::is_word_character(c);
            check(regex_syntax::try_is_word_character(c).ok() == Some(w), "try_is_word_character == is_word_character");
            if c.is_ascii() {
                check(regex_syntax::is_word_byte(c as u8) == w, "is_word_byte agrees on ASCII");
            }
        }
    }
}
