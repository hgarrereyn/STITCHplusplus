//! regex: compile and search with the `regex` crate (str and bytes APIs, sets, replacement).
//!
//! Oracles: the search APIs agree with each other (is_match, find, captures, captures_read,
//! shortest_match, the *_at variants, find_iter, captures_iter, split, replace, RegexSet); the
//! default engines and the fallback engines agree (a tiny DFA cache forces the fallback); `str`
//! and `bytes` regexes agree on UTF-8 text; escape(s) matches exactly s; Captures accessors agree
//! (get, name, iter, expand, extract, static_captures_len). Panics, overflow and debug assertions
//! are bugs. Compile limits are lowered so blowups by design bail.
pub use crate::objects::*;
use regex::bytes::{RegexBuilder as BRegexBuilder, RegexSetBuilder as BRegexSetBuilder};
use regex::{Regex, RegexBuilder, RegexSet, RegexSetBuilder};
use stitch::*;

fn builder(pattern: &str, opts: u32) -> RegexBuilder {
    let mut b = RegexBuilder::new(pattern);
    b.case_insensitive(opts & 1 != 0)
        .multi_line(opts & 2 != 0)
        .dot_matches_new_line(opts & 4 != 0)
        .swap_greed(opts & 8 != 0)
        .ignore_whitespace(opts & 16 != 0)
        .unicode(opts & 32 == 0)
        .octal(opts & 64 != 0)
        .crlf(opts & 128 != 0)
        .size_limit(1 << 20)
        .nest_limit(64);
    b
}

fn bbuilder(pattern: &str, opts: u32) -> BRegexBuilder {
    let mut b = BRegexBuilder::new(pattern);
    b.case_insensitive(opts & 1 != 0)
        .multi_line(opts & 2 != 0)
        .dot_matches_new_line(opts & 4 != 0)
        .swap_greed(opts & 8 != 0)
        .ignore_whitespace(opts & 16 != 0)
        .unicode(opts & 32 == 0)
        .octal(opts & 64 != 0)
        .crlf(opts & 128 != 0)
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
        let bytes = bbuilder(&pattern, opts).build();
        check(bytes.is_ok(), "a pattern that compiles as str regex compiles as bytes regex");
        check(re.captures_len() == slow.as_ref().unwrap().captures_len(), "capture counts agree");
        check(re.as_str() == pattern.as_str() && re.to_string() == pattern.as_str(), "as_str() / Display give the pattern");
        let names: Vec<Option<&str>> = re.capture_names().collect();
        check(names.len() == re.captures_len() && names[0].is_none(), "capture_names() has captures_len() entries");
        if let Ok(b) = &bytes {
            check(b.captures_len() == re.captures_len() && b.capture_names().eq(re.capture_names()), "str and bytes agree on groups");
            check(b.static_captures_len() == re.static_captures_len(), "str and bytes agree on static_captures_len");
        }
        Ok(New::new(Re { re, slow: slow.unwrap(), bytes: bytes.ok(), bslow: None, pattern: pattern.to_string(), opts }))
    }
}

type M = Option<(usize, usize)>;

/// Checks shared by the str and the bytes API (same method names, different haystack types).
macro_rules! consistency {
    ($re:expr, $slow:expr, $text:expr, $st:expr, $sub:expr) => {{
        let re = $re;
        let text = $text;
        let sub = $sub;
        let is = re.is_match(text);
        let found: M = re.find(text).map(|m| (m.start(), m.end()));
        let short = re.shortest_match(text);
        let caps = re.captures(text);
        let cv: Option<Vec<M>> = caps.as_ref().map(|c| c.iter().map(|m| m.map(|m| (m.start(), m.end()))).collect());
        log(&format!("is_match {is} find {found:?} shortest {short:?} caps {cv:?}"));
        check(is == found.is_some(), "is_match agrees with find");
        check(is == short.is_some(), "is_match agrees with shortest_match");
        check(cv.as_ref().map(|c| c[0]) == found.map(Some), "captures group 0 is the find match");
        if let (Some(e), Some((s, _))) = (short, found) {
            check(e >= s, "shortest_match ends at or after the leftmost match start");
        }
        if let Some(m) = re.find(text) {
            check(sub(m.start(), m.end()) == m.as_bytes_or_str() && m.len() == m.end() - m.start()
                  && m.range() == (m.start()..m.end()) && m.is_empty() == (m.len() == 0), "Match accessors");
        }
        // Captures accessors
        if let Some(c) = &caps {
            check(c.len() == re.captures_len(), "Captures::len() == captures_len()");
            check(c.get_match().start() == found.unwrap().0, "Captures::get_match()");
            for (i, name) in re.capture_names().enumerate() {
                if let Some(n) = name {
                    check(c.name(n).map(|m| m.range()) == c.get(i).map(|m| m.range()), "Captures::name() == get(index)");
                }
            }
            match re.static_captures_len() {
                Some(n) => {
                    check(c.iter().filter(|g| g.is_some()).count() == n, "static_captures_len() groups always match");
                    if n == 1 {
                        let (full, []) = c.extract::<0>();
                        check(full == c.get(0).unwrap().as_bytes_or_str(), "extract::<0>()");
                    } else if n == 2 {
                        let (full, [g1]) = c.extract::<1>();
                        // (extract gives the participating groups in order, whatever their index)
                        let first = c.iter().skip(1).flatten().next().map(|m| m.as_bytes_or_str());
                        check(full == c.get(0).unwrap().as_bytes_or_str() && Some(g1) == first, "extract::<1>()");
                    }
                }
                None => {}
            }
        }
        // CaptureLocations
        let mut locs = re.capture_locations();
        let rm: M = re.captures_read(&mut locs, text).map(|m| (m.start(), m.end()));
        check(rm == found, "captures_read() returns the find match");
        check(locs.len() == re.captures_len(), "CaptureLocations::len()");
        if let Some(cv) = &cv {
            for (i, g) in cv.iter().enumerate() {
                check(locs.get(i) == *g, "CaptureLocations::get() == captures()");
            }
        }
        // fallback engines
        let slow_found: M = $slow.find(text).map(|m| (m.start(), m.end()));
        check(found == slow_found, "default and fallback engines find the same match");
        let scv: Option<Vec<M>> = $slow.captures(text).map(|c| c.iter().map(|m| m.map(|m| (m.start(), m.end()))).collect());
        check(cv == scv, "default and fallback engines capture the same");
        // *_at from a start position
        let st: usize = $st;
        let a: M = re.find_at(text, st).map(|m| (m.start(), m.end()));
        let b: M = $slow.find_at(text, st).map(|m| (m.start(), m.end()));
        check(a == b, "find_at agrees across engines");
        check(re.is_match_at(text, st) == a.is_some(), "is_match_at agrees with find_at");
        check(re.shortest_match_at(text, st).is_some() == a.is_some(), "shortest_match_at agrees with find_at");
        let ca: M = re.captures_at(text, st).map(|c| { let m = c.get(0).unwrap(); (m.start(), m.end()) });
        check(ca == a, "captures_at group 0 is the find_at match");
        let ra: M = re.captures_read_at(&mut locs, text, st).map(|m| (m.start(), m.end()));
        check(ra == a && (a.is_none() || locs.get(0) == a), "captures_read_at agrees with find_at");
        if let Some((s, e)) = a {
            check(s >= st && e >= s, "find_at returns a match at or after start");
        }
        found
    }};
}

/// `Match::as_str` / `Match::as_bytes` under one name for the macro.
trait MatchText<'h> { type T: PartialEq + ?Sized; fn as_bytes_or_str(&self) -> &'h Self::T; }
impl<'h> MatchText<'h> for regex::Match<'h> { type T = str; fn as_bytes_or_str(&self) -> &'h str { self.as_str() } }
impl<'h> MatchText<'h> for regex::bytes::Match<'h> { type T = [u8]; fn as_bytes_or_str(&self) -> &'h [u8] { self.as_bytes() } }

endpoint! {
    fn search(r: Borrow<R>, text: Str<256, Haystack>, start: Range<0, 64>) {
        let st = (*start as usize).min(text.len());
        require(text.is_char_boundary(st), "start inside a char");
        let found = consistency!(&r.re, &r.slow, text.as_str(), st, |s: usize, e: usize| &text[s..e]);
        if let Some(b) = &r.bytes {
            let bfound: M = b.find(text.as_bytes()).map(|m| (m.start(), m.end()));
            check(found == bfound, "str and bytes regexes find the same match on UTF-8 text");
            let bc: Option<Vec<M>> = b.captures(text.as_bytes()).map(|c| c.iter().map(|m| m.map(|m| (m.start(), m.end()))).collect());
            let sc: Option<Vec<M>> = r.re.captures(&text).map(|c| c.iter().map(|m| m.map(|m| (m.start(), m.end()))).collect());
            check(bc == sc, "str and bytes regexes capture the same on UTF-8 text");
        }
    }
}

endpoint! {
    fn search_bytes(r: Borrow<R>, data: Bytes<256, Haystack>, start: Range<0, 64>) {
        require(r.bytes.is_some(), "no bytes regex");
        if r.bslow.is_none() {
            r.bslow = bbuilder(&r.pattern, r.opts).dfa_size_limit(0).build().ok();
            check(r.bslow.is_some(), "the bytes pattern compiles with a tiny DFA cache too");
        }
        let (b, bs) = (r.bytes.as_ref().unwrap(), r.bslow.as_ref().unwrap());
        let st = (*start as usize).min(data.len());
        let _ = consistency!(b, bs, &data[..], st, |s: usize, e: usize| &data[s..e]);
        // the same checks the str iterators get
        let all: Vec<(usize, usize)> = b.find_iter(&data).take(1000).map(|m| (m.start(), m.end())).collect();
        let slow: Vec<(usize, usize)> = bs.find_iter(&data).take(1000).map(|m| (m.start(), m.end())).collect();
        check(all == slow, "bytes find_iter agrees across engines");
        let caps: Vec<(usize, usize)> = b.captures_iter(&data).take(1000).map(|c| { let m = c.get(0).unwrap(); (m.start(), m.end()) }).collect();
        check(all == caps, "bytes captures_iter visits the find_iter matches");
        if all.len() < 1000 {
            check(b.split(&data).count() == all.len() + 1, "bytes split gives one more piece than there are matches");
            let replaced = b.replace_all(&data, regex::bytes::NoExpand(b"<>"));
            check(replaced.len() == data.len() + 2 * all.len() - all.iter().map(|(s, e)| e - s).sum::<usize>(), "bytes replace_all with NoExpand");
            let mut dst = Vec::new();
            if let Some(c) = b.captures(&data) {
                c.expand(b"[$0]", &mut dst);
                check(dst.len() == c.get(0).unwrap().len() + 2, "bytes Captures::expand($0)");
            }
        }
    }
}

endpoint! {
    fn iterate(r: Borrow<R>, text: Str<256, Haystack>, n: Range<0, 4>) {
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
        require(all.len() < 1000, "too many matches");
        let pieces: Vec<&str> = re.split(&text).collect();
        check(pieces.len() == all.len() + 1, "split gives one more piece than there are matches");
        let n = *n as usize;
        let pn: Vec<&str> = re.splitn(&text, n).collect();
        check(pn.len() == n.min(all.len() + 1), "splitn gives min(n, matches + 1) pieces");
        if n > all.len() {
            check(pn == pieces, "splitn with a large n == split");
        }
        let replaced = re.replace_all(&text, "<$0>");
        check(replaced.len() == text.len() + 2 * all.len(), "replace_all wraps every match");
        check(re.replacen(&text, 0, "<$0>") == replaced, "replacen(0) == replace_all");
        check(re.replace(&text, "<${0}>") == re.replacen(&text, 1, "<$0>"), "replace == replacen(1)");
        let lit = re.replace_all(&text, regex::NoExpand("$0"));
        let matched: usize = all.iter().map(|(s, e)| e - s).sum();
        check(lit.len() == text.len() - matched + 2 * all.len(), "replace_all with NoExpand inserts the text literally");
        let counted = re.replace_all(&text, |c: &regex::Captures| format!("{}", c[0].len()));
        let want: usize = all.iter().map(|(s, e)| (e - s).to_string().len()).sum();
        check(counted.len() == text.len() - matched + want, "replace_all with a closure");
        if let Some(c) = re.captures(&text) {
            let mut dst = String::new();
            c.expand("$0|$$|${0}", &mut dst);
            let m0 = c.get(0).unwrap().as_str();
            check(dst == format!("{m0}|$|{m0}"), "Captures::expand");
        }
    }
}

endpoint! {
    fn set(r: Borrow<R>, other: Str<64, Pattern>, text: Str<128, Haystack>, start: Range<0, 64>) {
        let pats = [r.pattern.as_str(), other.as_str()];
        let o = r.opts;
        let set = match RegexSetBuilder::new(pats)
            .case_insensitive(o & 1 != 0).multi_line(o & 2 != 0).dot_matches_new_line(o & 4 != 0)
            .swap_greed(o & 8 != 0).ignore_whitespace(o & 16 != 0).unicode(o & 32 == 0).octal(o & 64 != 0)
            .crlf(o & 128 != 0).size_limit(1 << 21).nest_limit(64).build() {
            Ok(s) => s,
            Err(_) => bail("set does not compile"),
        };
        check(set.len() == 2 && !set.is_empty() && set.patterns() == &pats.map(|p| p.to_string())[..], "RegexSet len / patterns");
        let m = set.matches(&text);
        check(m.matched(0) == r.re.is_match(&text), "RegexSet agrees with Regex::is_match");
        if let Ok(o2) = builder(&other, o).build() {
            check(m.matched(1) == o2.is_match(&text), "RegexSet agrees with Regex::is_match (second pattern)");
        }
        check(set.is_match(&text) == m.matched_any(), "RegexSet::is_match agrees with matches()");
        check(m.matched_all() == (m.matched(0) && m.matched(1)) && m.len() == 2, "SetMatches::matched_all / len");
        check(m.iter().collect::<Vec<_>>() == m.clone().into_iter().collect::<Vec<_>>(), "SetMatches iter == into_iter");
        let st = (*start as usize).min(text.len());
        require(text.is_char_boundary(st), "start inside a char");
        let ma = set.matches_at(&text, st);
        check(set.is_match_at(&text, st) == ma.matched_any(), "is_match_at agrees with matches_at");
        check(ma.matched(0) == r.re.is_match_at(&text, st), "matches_at agrees with Regex::is_match_at");
        let mut buf = [false; 2];
        let any = set.matches_read_at(&mut buf, &text, st);
        check(any == ma.matched_any() && buf == [ma.matched(0), ma.matched(1)], "matches_read_at agrees with matches_at");
        // the bytes set on the same text
        if let Ok(bset) = BRegexSetBuilder::new(pats).case_insensitive(o & 1 != 0).multi_line(o & 2 != 0)
            .dot_matches_new_line(o & 4 != 0).swap_greed(o & 8 != 0).ignore_whitespace(o & 16 != 0)
            .unicode(o & 32 == 0).octal(o & 64 != 0).crlf(o & 128 != 0).size_limit(1 << 21).nest_limit(64).build() {
            let bm = bset.matches(text.as_bytes());
            check(bm.iter().eq(m.iter()), "bytes RegexSet agrees with the str RegexSet on UTF-8 text");
            check(bset.matches_at(text.as_bytes(), st).iter().eq(ma.iter()), "bytes matches_at agrees");
        } else {
            check(false, "a set that compiles as str compiles as bytes");
        }
        check(!RegexSet::empty().is_match(&text) && regex::bytes::RegexSet::empty().len() == 0, "RegexSet::empty()");
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
