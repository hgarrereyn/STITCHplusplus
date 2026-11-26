//! regex-automata: the meta regex and every engine underneath it (PikeVM, bounded backtracker,
//! lazy DFA, dense and sparse DFAs, one-pass DFA), built from the same patterns and configuration,
//! plus DFA serialization, NFA inspection, prefilters, look-around matching and the Captures,
//! GroupInfo, Input and PatternSet utilities.
//!
//! Oracles: every engine reports the same leftmost-first match, the same capture groups and the
//! same overlapping pattern set for the same Input (span, anchored mode, earliest); engines that
//! refuse a search (haystack too long, quit byte, gave up, unsupported anchored mode) are skipped.
//! Serialized DFAs deserialize to DFAs that search the same; `from_bytes` never panics on any
//! input (documented); a prefilter never misses a needle occurrence; meta with a prefilter built
//! from the patterns' prefixes searches the same as without; LookMatcher agrees with a model.
pub use crate::objects::*;
use regex_automata::{
    dfa::{self, dense, onepass, sparse, Automaton},
    hybrid, meta,
    nfa::thompson::{self, backtrack::BoundedBacktracker, pikevm::PikeVM, WhichCaptures},
    util::{
        captures::{Captures, GroupInfo},
        look::{Look, LookMatcher, LookSet},
        prefilter::Prefilter,
        syntax,
    },
    Anchored, Input, MatchKind, PatternID, PatternSet, Span,
};
use stitch::*;

flags!(SynOpts = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]);
flags!(MetaOpts = [1, 2, 4, 8, 16, 32, 64, 128, 256]);
flags!(DfaOpts = [1, 2, 4, 8, 16, 32]);

fn syntax_config(f: u32) -> syntax::Config {
    syntax::Config::new()
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
}

endpoint! {
    fn auto_build(pats: Val<PatList, 384>, syn: Flags<SynOpts>, mopts: Flags<MetaOpts>) -> New<A> {
        let (f, m) = (syn.0 as u32, mopts.0 as u32);
        let sc = syntax_config(f);
        let utf8 = sc.get_utf8();
        let mut lm = LookMatcher::new();
        lm.set_line_terminator(sc.get_line_terminator());
        let th = thompson::Config::new().utf8(utf8).nfa_size_limit(Some(1 << 17)).look_matcher(lm).shrink(m & 256 != 0);
        let implicit_only = m & 4 != 0;
        let mut mc = meta::Config::new()
            .match_kind(MatchKind::LeftmostFirst)
            .utf8_empty(utf8)
            .auto_prefilter(m & 1 == 0)
            .which_captures(if implicit_only { WhichCaptures::Implicit } else { WhichCaptures::All })
            .nfa_size_limit(Some(1 << 17))
            .onepass_size_limit(Some(1 << 17))
            .dfa_size_limit(Some(1 << 16))
            .dfa_state_limit(Some(100))
            .hybrid_cache_capacity(1 << 16)
            .line_terminator(sc.get_line_terminator())
            .hybrid(m & 8 == 0)
            .dfa(m & 16 == 0)
            .onepass(m & 32 == 0)
            .backtrack(m & 64 == 0)
            .byte_classes(m & 128 == 0);
        if m & 2 != 0 {
            // an explicit prefilter from the patterns' prefixes: must not change any result
            let hirs = match syntax::parse_many_with(&pats[..], &sc) { Ok(h) => h, Err(_) => bail("does not parse") };
            mc = mc.prefilter(Prefilter::from_hirs_prefix(MatchKind::LeftmostFirst, &hirs));
        }
        let meta = match meta::Builder::new().configure(mc).syntax(sc.clone()).build_many(&pats[..]) {
            Ok(r) => r,
            Err(e) => { let _ = (e.to_string(), e.pattern(), e.size_limit(), e.syntax_error()); bail("meta build error") }
        };
        // one NFA shared by the PikeVM and the backtracker
        let nfa = match thompson::Compiler::new().syntax(sc.clone()).configure(th.clone()).build_many(&pats[..]) {
            Ok(n) => n,
            Err(_) => bail("NFA build error (size limit)"),
        };
        let pike = match PikeVM::new_from_nfa(nfa.clone()) {
            Ok(p) => p,
            Err(_) => bail("pikevm build error"),
        };
        let bt = match BoundedBacktracker::new_from_nfa(nfa) {
            Ok(b) => b,
            Err(_) => bail("backtracker build error"),
        };
        check(meta.pattern_len() == pats.len() && pike.pattern_len() == pats.len(), "pattern_len()");
        check(meta.get_config().get_match_kind() == MatchKind::LeftmostFirst, "Config getters");
        let _ = (meta.is_accelerated(), meta.memory_usage(), meta.static_captures_len());
        New::new(Auto { pats: pats.to_vec(), syn: sc, th, meta, implicit_only, pike, bt, hybrid: None, dense: None, sparse: None, onepass: None })
    }
}

endpoint! {
    fn auto_hybrid(a: Borrow<A>, each: bool, clear: Range<0, 3>) {
        let hc = hybrid::dfa::Config::new().unicode_word_boundary(true).cache_capacity(1 << 15)
            .minimum_cache_clear_count(Some(*clear as usize)).starts_for_each_pattern(each);
        let h = match hybrid::regex::Builder::new().syntax(a.syn.clone()).thompson(a.th.clone()).dfa(hc).build_many(&a.pats) {
            Ok(h) => h,
            Err(_) => bail("lazy DFA build error"),
        };
        check(h.pattern_len() == a.pats.len() && h.forward().pattern_len() == a.pats.len(), "lazy DFA pattern_len()");
        let _ = (h.forward().memory_usage(), h.reverse().get_config().get_cache_capacity(), h.forward().byte_classes().alphabet_len());
        a.hybrid = Some(h);
    }
}

endpoint! {
    fn auto_dense(a: Borrow<A>, opts: Flags<DfaOpts>) -> New<D> {
        let f = opts.0 as u32;
        let dc = dense::Config::new()
            .minimize(f & 1 != 0)
            .accelerate(f & 2 == 0)
            .byte_classes(f & 4 == 0)
            .specialize_start_states(f & 8 != 0)
            .starts_for_each_pattern(f & 16 != 0)
            .unicode_word_boundary(true)
            .dfa_size_limit(Some(1 << 16))
            .determinize_size_limit(Some(1 << 16));
        let re = match dfa::regex::Builder::new().syntax(a.syn.clone()).thompson(a.th.clone()).dense(dc).build_many(&a.pats) {
            Ok(r) => r,
            Err(_) => bail("dense DFA build error (size limit or unsupported)"),
        };
        check(re.pattern_len() == a.pats.len(), "dense pattern_len()");
        let (fw, rv) = (re.forward(), re.reverse());
        let _ = (fw.memory_usage(), fw.alphabet_len(), fw.stride(), fw.stride2(), fw.start_kind(), fw.byte_classes().alphabet_len(), fw.has_empty(), fw.is_utf8(), fw.is_always_start_anchored());
        let sparse = match (fw.to_sparse(), rv.to_sparse()) {
            (Ok(sf), Ok(sr)) => Some(dfa::regex::Builder::new().build_from_dfas(sf, sr)),
            _ => None,
        };
        a.dense = Some(re.clone());
        a.sparse = sparse.clone();
        New::new(Dense { dense: re, sparse, npats: a.pats.len() })
    }
}

endpoint! {
    fn auto_onepass(a: Borrow<A>, each: bool, classes: bool) {
        let oc = onepass::Config::new().starts_for_each_pattern(each).byte_classes(classes).size_limit(Some(1 << 17));
        let op = match onepass::Builder::new().syntax(a.syn.clone()).thompson(a.th.clone()).configure(oc).build_many(&a.pats) {
            Ok(o) => o,
            Err(_) => bail("not one-pass"),
        };
        let _ = (op.state_len(), op.alphabet_len(), op.stride(), op.memory_usage(), op.get_nfa().pattern_len());
        a.onepass = Some(op);
    }
}

fn mk_input<'h>(hay: &'h [u8], s: i64, e: i64, anch: i64, pid: i64, earliest: bool, npats: usize) -> Input<'h> {
    let e = (e as usize).min(hay.len());
    let s = (s as usize).min(e);
    let anchored = match anch {
        0 => Anchored::No,
        1 => Anchored::Yes,
        _ => Anchored::Pattern(PatternID::must((pid as usize) % npats)),
    };
    let mut input = Input::new(hay).anchored(anchored).earliest(earliest);
    input.set_span(Span { start: s, end: e });
    check(input.start() == s && input.end() == e && input.get_range() == (s..e) && input.get_span() == Span::from(s..e), "Input span accessors");
    check(input.haystack().len() == hay.len() && input.get_anchored() == anchored && input.get_earliest() == earliest, "Input accessors");
    check(input.is_done() == (s > e), "Input::is_done()");
    input
}

type Found = Option<(usize, usize, usize)>;

fn key(m: Option<regex_automata::Match>) -> Found {
    m.map(|m| (m.pattern().as_usize(), m.start(), m.end()))
}

/// Every engine's leftmost-first match for this input (engines that refuse the search are left out).
fn all_finds(a: &Auto, input: &Input) -> Vec<(&'static str, Found)> {
    let mut out = vec![("meta", key(a.meta.search(input)))];
    let mut pc = a.pike.create_cache();
    let mut caps = a.pike.create_captures();
    a.pike.search(&mut pc, input, &mut caps);
    out.push(("pikevm", key(caps.get_match())));
    let mut bc = a.bt.create_cache();
    if let Ok(m) = a.bt.try_find(&mut bc, input.clone()) {
        out.push(("backtrack", key(m)));
    }
    if let Some(h) = &a.hybrid {
        let mut hc = h.create_cache();
        if let Ok(m) = h.try_search(&mut hc, input) {
            out.push(("hybrid", key(m)));
        }
    }
    if let Some(d) = &a.dense {
        if let Ok(m) = d.try_search(input) {
            out.push(("dense", key(m)));
        }
    }
    if let Some(d) = &a.sparse {
        if let Ok(m) = d.try_search(input) {
            out.push(("sparse", key(m)));
        }
    }
    if let Some(o) = &a.onepass {
        let mut oc = o.create_cache();
        let mut ocaps = o.create_captures();
        if o.try_search(&mut oc, input, &mut ocaps).is_ok() {
            out.push(("onepass", key(ocaps.get_match())));
        }
    }
    out
}

endpoint! {
    fn auto_search(a: Borrow<A>, hay: Bytes<256, Haystack>, s: Range<0, 300>, e: Range<0, 300>,
                   anch: Range<0, 2>, pid: Range<0, 2>, earliest: bool) {
        let input = mk_input(&hay, *s, *e, *anch, *pid, earliest, a.pats.len());
        let all = all_finds(&a, &input);
        log(&format!("{all:?}"));
        let first = all[0].1;
        for (name, f) in &all {
            if earliest {
                // (earliest: any engine may stop at a different match end)
                if f.is_some() != first.is_some() {
                    log(name);
                    check(false, "engines agree on whether there is a match (earliest)");
                }
            } else if *f != first {
                log(name);
                check(false, "engines agree on the leftmost-first match");
            }
            if let Some((_, ms, me)) = f {
                check(*ms >= input.start() && ms <= me && *me <= input.end(), "a match lies within the input span");
            }
        }
        check(a.meta.is_match(input.clone()) == first.is_some(), "meta is_match agrees with search");
        let mut pc = a.pike.create_cache();
        check(a.pike.is_match(&mut pc, input.clone()) == first.is_some(), "pikevm is_match agrees with search");
        if !earliest {
            let half = a.meta.search_half(&input);
            check(half.map(|h| (h.pattern().as_usize(), h.offset())) == first.map(|(p, _, e)| (p, e)), "search_half agrees with search");
        }
    }
}

endpoint! {
    fn auto_captures(a: Borrow<A>, hay: Bytes<256, Haystack>, s: Range<0, 300>, e: Range<0, 300>, anch: Range<0, 2>, pid: Range<0, 2>) {
        let input = mk_input(&hay, *s, *e, *anch, *pid, false, a.pats.len());
        let groups = |c: &Captures| -> Option<(usize, Vec<Option<Span>>)> {
            c.pattern().map(|p| (p.as_usize(), (0..c.group_len()).map(|i| c.get_group(i)).collect()))
        };
        let mut pc = a.pike.create_cache();
        let mut pcaps = a.pike.create_captures();
        a.pike.search(&mut pc, &input, &mut pcaps);
        let want = groups(&pcaps);
        log(&format!("pikevm {want:?}"));
        let mut mcaps = a.meta.create_captures();
        a.meta.search_captures(&input, &mut mcaps);
        let got = groups(&mcaps);
        if a.implicit_only {
            check(got.as_ref().map(|g| (g.0, g.1[0])) == want.as_ref().map(|g| (g.0, g.1[0])), "meta (implicit captures) agrees with the PikeVM");
        } else {
            check(got == want, "meta captures agree with the PikeVM");
        }
        let mut bc = a.bt.create_cache();
        let mut bcaps = a.bt.create_captures();
        if a.bt.try_search(&mut bc, &input, &mut bcaps).is_ok() {
            check(groups(&bcaps) == want, "backtracker captures agree with the PikeVM");
        }
        if let Some(o) = &a.onepass {
            let mut oc = o.create_cache();
            let mut ocaps = o.create_captures();
            if o.try_search(&mut oc, &input, &mut ocaps).is_ok() {
                check(groups(&ocaps) == want, "one-pass DFA captures agree with the PikeVM");
            }
        }
        // slots API
        let mut slots = vec![None; a.pike.get_nfa().group_info().slot_len()];
        let sp = a.pike.search_slots(&mut pc, &input, &mut slots);
        check(sp.map(|p| p.as_usize()) == want.as_ref().map(|w| w.0), "search_slots returns the matching pattern");
        let mut mslots = vec![None; 2 * a.pats.len()];
        let ms = a.meta.search_slots(&input, &mut mslots);
        check(ms.map(|p| p.as_usize()) == want.as_ref().map(|w| w.0), "meta search_slots (implicit slots) returns the matching pattern");
        // Captures utilities
        let gi = pcaps.group_info().clone();
        check(gi.pattern_len() == a.pats.len() && gi.slot_len() == slots.len() && gi.implicit_slot_len() == 2 * a.pats.len()
              && gi.explicit_slot_len() + gi.implicit_slot_len() == gi.slot_len(), "GroupInfo slot counts");
        check(gi.all_group_len() == (0..a.pats.len()).map(|p| gi.group_len(PatternID::must(p))).sum::<usize>(), "GroupInfo::all_group_len()");
        for (pid, gidx, name) in gi.all_names() {
            if let Some(n) = name {
                check(gi.to_index(pid, n) == Some(gidx) && gi.to_name(pid, gidx) == Some(n), "GroupInfo to_index / to_name agree");
            }
            let _ = gi.slots(pid, gidx);
        }
        let _ = (gi.memory_usage(), gi.pattern_names(PatternID::ZERO).count());
        if let Some((p, gs)) = &want {
            let pid = PatternID::must(*p);
            for name in gi.pattern_names(pid).flatten() {
                let idx = gi.to_index(pid, name).unwrap();
                check(pcaps.get_group_by_name(name) == gs[idx], "get_group_by_name == get_group(to_index)");
            }
            check(pcaps.iter().collect::<Vec<_>>() == *gs && pcaps.is_match(), "Captures::iter()");
            let mut dst = Vec::new();
            pcaps.interpolate_bytes_into(&hay, b"[$0]", &mut dst);
            let m0 = gs[0].unwrap();
            check(dst.len() == m0.len() + 2 && dst[1..dst.len() - 1] == hay[m0.range()], "interpolate_bytes $0");
            if let Ok(text) = std::str::from_utf8(&hay) {
                if text.is_char_boundary(m0.start) && text.is_char_boundary(m0.end) {
                    check(pcaps.interpolate_string(text, "$0") == text[m0.range()], "interpolate_string $0");
                    if gs.len() == 1 {
                        let (whole, []) = pcaps.extract::<0>(text);
                        check(whole == &text[m0.range()], "Captures::extract::<0>()");
                    }
                }
            }
            let _ = format!("{pcaps:?}");
        }
        let mut all = Captures::all(gi.clone());
        let mut matches = Captures::matches(gi.clone());
        let mut empty = Captures::empty(gi);
        a.pike.search(&mut pc, &input, &mut all);
        a.pike.search(&mut pc, &input, &mut matches);
        a.pike.search(&mut pc, &input, &mut empty);
        check(all.get_match() == pcaps.get_match() && matches.get_match() == pcaps.get_match(), "Captures::all / matches find the match");
        check(empty.pattern() == pcaps.pattern(), "Captures::empty still reports the pattern");
        all.clear();
        check(!all.is_match(), "Captures::clear()");
    }
}

endpoint! {
    fn auto_iter(a: Borrow<A>, hay: Bytes<256, Haystack>, n: Range<0, 4>) {
        let ms: Vec<Found> = a.meta.find_iter(&hay[..]).take(300).map(|m| key(Some(m))).collect();
        let mut pc = a.pike.create_cache();
        let ps: Vec<Found> = a.pike.find_iter(&mut pc, &hay[..]).take(300).map(|m| key(Some(m))).collect();
        check(ms == ps, "meta find_iter agrees with the PikeVM");
        let mut bc = a.bt.create_cache();
        let bs: Result<Vec<Found>, _> = a.bt.try_find_iter(&mut bc, &hay[..]).take(300).map(|m| m.map(|m| key(Some(m)))).collect();
        if let Ok(bs) = bs {
            check(ms == bs, "backtracker find_iter agrees with the PikeVM");
        }
        let cs: Vec<Found> = a.meta.captures_iter(&hay[..]).take(300).map(|c| key(c.get_match())).collect();
        check(cs == ms, "meta captures_iter visits the find_iter matches");
        let pcs: Vec<Found> = a.pike.captures_iter(&mut pc, &hay[..]).take(300).map(|c| key(c.get_match())).collect();
        check(pcs == ms, "pikevm captures_iter visits the find_iter matches");
        require(ms.len() < 300, "too many matches");
        let pieces: Vec<Span> = a.meta.split(&hay[..]).collect();
        check(pieces.len() == ms.len() + 1, "split gives one more piece than there are matches");
        let n = *n as usize;
        check(a.meta.splitn(&hay[..], n).count() == n.min(ms.len() + 1), "splitn gives min(n, matches + 1) pieces");
        // the lower level iterator
        let mut cache = a.meta.create_cache();
        let it = regex_automata::util::iter::Searcher::new(Input::new(&hay[..]));
        let viaiter: Vec<Found> = it.into_matches_iter(|input| Ok(a.meta.search_with(&mut cache, input))).infallible().take(300).map(|m| key(Some(m))).collect();
        check(viaiter == ms, "util::iter::Searcher agrees with find_iter");
        cache.reset(&a.meta);
        let _ = cache.memory_usage();
    }
}

endpoint! {
    fn auto_overlapping(a: Borrow<A>, hay: Bytes<256, Haystack>, s: Range<0, 300>, e: Range<0, 300>, anchored: bool, dfas: bool) {
        let input = mk_input(&hay, *s, *e, anchored as i64, 0, false, a.pats.len());
        let n = a.pats.len();
        let meta = match meta::Builder::new().configure(meta::Config::new().match_kind(MatchKind::All).utf8_empty(a.syn.get_utf8())
                .nfa_size_limit(Some(1 << 17)).line_terminator(a.syn.get_line_terminator())).syntax(a.syn.clone()).build_many(&a.pats) {
            Ok(r) => r,
            Err(_) => bail("build error"),
        };
        let pike = match PikeVM::builder().configure(PikeVM::config().match_kind(MatchKind::All)).syntax(a.syn.clone()).thompson(a.th.clone()).build_many(&a.pats) {
            Ok(p) => p,
            Err(_) => bail("build error"),
        };
        let mut want = PatternSet::new(n);
        meta.which_overlapping_matches(&input, &mut want);
        let mut got = PatternSet::new(n);
        let mut pc = pike.create_cache();
        pike.which_overlapping_matches(&mut pc, &input, &mut got);
        check(got == want, "PikeVM and meta agree on the overlapping pattern set");
        check(want.len() == want.iter().count() && want.is_full() == (want.len() == n) && want.capacity() == n, "PatternSet accessors");
        if dfas {
            let hy = hybrid::dfa::Builder::new().syntax(a.syn.clone()).thompson(a.th.clone())
                .configure(hybrid::dfa::Config::new().match_kind(MatchKind::All).unicode_word_boundary(true).cache_capacity(1 << 15))
                .build_many(&a.pats);
            if let Ok(hy) = hy {
                let mut hc = hy.create_cache();
                let mut hs = PatternSet::new(n);
                if hy.try_which_overlapping_matches(&mut hc, &input, &mut hs).is_ok() {
                    check(hs == want, "lazy DFA agrees on the overlapping pattern set");
                }
            }
            let dd = dense::Builder::new().syntax(a.syn.clone()).thompson(a.th.clone())
                .configure(dense::Config::new().match_kind(MatchKind::All).unicode_word_boundary(true).dfa_size_limit(Some(1 << 16)).determinize_size_limit(Some(1 << 16)))
                .build_many(&a.pats);
            if let Ok(dd) = dd {
                let mut ds = PatternSet::new(n);
                if dd.try_which_overlapping_matches(&input, &mut ds).is_ok() {
                    check(ds == want, "dense DFA agrees on the overlapping pattern set");
                }
            }
        }
        // PatternSet mutation
        let mut ps = want.clone();
        check(want.iter().all(|p| ps.contains(p)), "PatternSet::contains");
        ps.clear();
        check(ps.is_empty(), "PatternSet empty after clear");
        check(ps.insert(PatternID::ZERO) && !ps.insert(PatternID::ZERO) && ps.try_insert(PatternID::must(n)).is_err(), "PatternSet::insert / try_insert");
        ps.clear();
        check(ps.is_empty(), "PatternSet::clear");
    }
}

endpoint! {
    fn dfa_serialize(d: Borrow<D>, hay: Bytes<256, Haystack>, s: Range<0, 300>, e: Range<0, 300>, anch: Range<0, 1>) {
        let re = &d.dense;
        let input = mk_input(&hay, *s, *e, *anch, 0, false, d.npats);
        let want = re.try_search(&input).map(key).ok();
        let (fb, fpad) = re.forward().to_bytes_native_endian();
        let (rb, rpad) = re.reverse().to_bytes_native_endian();
        let fw = dense::DFA::from_bytes(&fb[fpad..]);
        let rv = dense::DFA::from_bytes(&rb[rpad..]);
        check(fw.is_ok() && rv.is_ok(), "a serialized dense DFA deserializes");
        let (fw, fn_) = fw.unwrap();
        let (rv, _) = rv.unwrap();
        check(fn_ <= fb.len() - fpad, "from_bytes reads within the serialization");
        let again = dfa::regex::Builder::new().build_from_dfas(fw, rv);
        check(again.try_search(&input).map(key).ok() == want, "the deserialized dense DFA searches the same");
        let mut buf = vec![0u8; re.forward().write_to_len()];
        check(re.forward().write_to_native_endian(&mut buf).ok() == Some(buf.len()) && buf[..] == fb[fpad..], "write_to_native_endian == to_bytes_native_endian");
        let (le, lpad) = re.forward().to_bytes_little_endian();
        let (be, bpad) = re.forward().to_bytes_big_endian();
        check(le.len() - lpad == be.len() - bpad, "little and big endian serializations have the same length");
        if let Some(sp) = &d.sparse {
            let fb = sp.forward().to_bytes_native_endian();
            let rb = sp.reverse().to_bytes_native_endian();
            match (sparse::DFA::from_bytes(&fb), sparse::DFA::from_bytes(&rb)) {
                (Ok((f, _)), Ok((r, _))) => {
                    let again = dfa::regex::Builder::new().build_from_dfas(f, r);
                    check(again.try_search(&input).map(key).ok() == want, "the deserialized sparse DFA searches the same");
                }
                _ => check(false, "a serialized sparse DFA deserializes"),
            }
            let _ = (sp.forward().memory_usage(), sp.forward().write_to_len());
        }
    }
}

endpoint! {
    fn dfa_from_bytes(d: Borrow<D>, edits: Bytes<64>, sparse_form: bool, truncate: Range<0, 64>) {
        // a real serialization, damaged: from_bytes must refuse or accept it, never panic
        let re = &d.dense;
        let mut bytes = if sparse_form {
            match &d.sparse { Some(s) => s.forward().to_bytes_native_endian(), None => bail("no sparse DFA") }
        } else {
            let (b, pad) = re.forward().to_bytes_native_endian();
            b[pad..].to_vec()
        };
        for ch in edits.chunks(3) {
            if ch.len() == 3 && !bytes.is_empty() {
                let at = (u16::from_le_bytes([ch[0], ch[1]]) as usize) % bytes.len();
                bytes[at] = ch[2];
            }
        }
        let keep = bytes.len().saturating_sub(*truncate as usize * 7);
        bytes.truncate(keep);
        // copy into u32-aligned storage
        let mut words = vec![0u32; bytes.len() / 4 + 1];
        let aligned: &mut [u8] = unsafe { std::slice::from_raw_parts_mut(words.as_mut_ptr() as *mut u8, bytes.len()) };
        aligned.copy_from_slice(&bytes);
        let ok = if sparse_form {
            sparse::DFA::from_bytes(aligned).map(|(d, n)| (d.memory_usage(), n)).is_ok()
        } else {
            dense::DFA::from_bytes(aligned).map(|(d, n)| (d.memory_usage(), n)).is_ok()
        };
        log(&format!("accepted: {ok}"));
    }
}

endpoint! {
    fn nfa_inspect(a: Borrow<A>, reverse: bool) {
        let nfa = a.pike.get_nfa();
        check(nfa.pattern_len() == a.pats.len() && nfa.patterns().count() == a.pats.len(), "NFA pattern_len / patterns");
        check(nfa.states().len() > 0 && nfa.state(nfa.start_anchored()) == &nfa.states()[nfa.start_anchored().as_usize()], "NFA states / state()");
        for p in nfa.patterns() {
            check(nfa.start_pattern(p).is_some(), "start_pattern() for every pattern");
        }
        check(nfa.look_set_prefix_any().subtract(nfa.look_set_any()).is_empty(), "NFA prefix look sets are subsets of look_set_any()");
        let _ = (nfa.start_unanchored(), nfa.byte_classes().alphabet_len(), nfa.has_capture(), nfa.has_empty(), nfa.is_utf8(),
                 nfa.is_reverse(), nfa.is_always_start_anchored(), nfa.look_matcher().get_line_terminator(), nfa.memory_usage(), format!("{nfa:?}"));
        if reverse {
            let r = thompson::Compiler::new().syntax(a.syn.clone()).configure(a.th.clone().reverse(true).which_captures(WhichCaptures::None)).build_many(&a.pats);
            if let Ok(r) = r {
                check(r.is_reverse() && r.pattern_len() == a.pats.len(), "a reverse NFA");
            }
        } else {
            let hirs = match syntax::parse_many_with(&a.pats, &a.syn) { Ok(h) => h, Err(_) => bail("does not parse") };
            let n2 = thompson::Compiler::new().configure(a.th.clone()).build_many_from_hir(&hirs);
            check(n2.as_ref().map(|n| n.states().len()).ok() == Some(nfa.states().len()), "build_many_from_hir == build_many");
        }
    }
}

datatype!(Needles);

decoder!(Needles, fn needles(s: &mut Source) -> Vec<Vec<u8>> {
    let mut v = vec![];
    while s.more() && v.len() < 8 {
        v.push(match s.choice(3) {
            0 => s.bytes(4),
            1 => s.pick(&["a", "ab", "foo", "bar", "\u{e9}", "Hello", "x y"]).as_bytes().to_vec(),
            _ => s.token().into_bytes(),
        });
    }
    v
});

endpoint! {
    fn prefilter(n: Val<Needles, 128>, hay: Bytes<256, Haystack>, s: Range<0, 300>, all: bool) {
        let kind = if all { MatchKind::All } else { MatchKind::LeftmostFirst };
        let pre = match Prefilter::new(kind, &n[..]) { Some(p) => p, None => bail("no prefilter for these needles") };
        let start = (*s as usize).min(hay.len());
        let span = Span::from(start..hay.len());
        let _ = (pre.is_fast(), pre.memory_usage());
        check(pre.max_needle_len() == n.iter().map(|x| x.len()).max().unwrap_or(0), "max_needle_len()");
        // the earliest occurrence of any needle in the span
        let first = (start..=hay.len()).find(|&i| n.iter().any(|x| hay[i..].starts_with(x)));
        let got = pre.find(&hay, span);
        log(&format!("first {first:?} got {got:?}"));
        if let Some(sp) = got {
            check(sp.start >= start && sp.end <= hay.len() && sp.start <= sp.end, "Prefilter::find stays within the span");
        }
        if let Some(i) = first {
            check(got.map_or(false, |sp| sp.start <= i), "Prefilter::find does not miss a needle occurrence");
        }
        if let Some(sp) = pre.prefix(&hay, span) {
            check(sp.start == start, "Prefilter::prefix matches at the start");
        }
    }
}

endpoint! {
    fn look(hay: Bytes<64, Haystack>, at: Range<0, 64>, lt: u8) {
        let at = (*at as usize).min(hay.len());
        let mut m = LookMatcher::new();
        m.set_line_terminator(lt);
        check(m.get_line_terminator() == lt, "LookMatcher line terminator");
        let prev = if at > 0 { Some(hay[at - 1]) } else { None };
        let next = hay.get(at).copied();
        let w = |b: Option<u8>| b.map_or(false, |b| b == b'_' || b.is_ascii_alphanumeric());
        let model = |l: Look| -> Option<bool> {
            Some(match l {
                Look::Start => at == 0,
                Look::End => at == hay.len(),
                Look::StartLF => at == 0 || prev == Some(lt),
                Look::EndLF => at == hay.len() || next == Some(lt),
                Look::StartCRLF => at == 0 || prev == Some(b'\n') || (prev == Some(b'\r') && next != Some(b'\n')),
                Look::EndCRLF => at == hay.len() || next == Some(b'\r') || (next == Some(b'\n') && prev != Some(b'\r')),
                Look::WordAscii => w(prev) != w(next),
                Look::WordAsciiNegate => w(prev) == w(next),
                Look::WordStartAscii => !w(prev) && w(next),
                Look::WordEndAscii => w(prev) && !w(next),
                Look::WordStartHalfAscii => !w(prev),
                Look::WordEndHalfAscii => !w(next),
                _ => return None,
            })
        };
        let mut set = LookSet::empty();
        for repr in 0..32u32 {
            let l = match Look::from_repr(1 << repr) { Some(l) => l, None => continue };
            check(l.as_repr() == 1 << repr && l.reversed().reversed() == l, "Look repr / reversed");
            let _ = l.as_char();
            let got = m.matches(l, &hay, at);
            if let Some(want) = model(l) {
                if got != want {
                    log(&format!("{l:?}"));
                    check(false, "LookMatcher::matches agrees with the model");
                }
            }
            if got { set = set.insert(l) }
        }
        check(m.matches_set(set, &hay, at), "matches_set of the looks that match");
        let full = LookSet::full();
        check(set.union(full) == full && set.intersect(full) == set && set.subtract(set).is_empty(), "LookSet algebra");
        check(set.len() == set.iter().count() && set.iter().all(|l| set.contains(l)), "LookSet::iter / contains");
        let mut buf = [0u8; 4];
        set.write_repr(&mut buf);
        check(LookSet::read_repr(&buf) == set, "LookSet read_repr / write_repr round trip");
    }
}

endpoint! {
    fn group_info(names: Bytes<16>) {
        // GroupInfo::new with fuzzed group names (duplicates, a name on group 0, ...)
        let mut groups: Vec<Vec<Option<String>>> = vec![vec![None]];
        for &b in names.iter() {
            match b % 4 {
                0 => groups.push(vec![None]),
                1 => groups.last_mut().unwrap().push(None),
                _ => groups.last_mut().unwrap().push(Some(((b'a' + b % 3) as char).to_string())),
            }
        }
        match GroupInfo::new(groups.clone()) {
            Ok(gi) => {
                check(gi.pattern_len() == groups.len(), "GroupInfo::pattern_len");
                for (p, g) in groups.iter().enumerate() {
                    check(gi.group_len(PatternID::must(p)) == g.len(), "GroupInfo::group_len");
                }
            }
            Err(e) => { let _ = e.to_string(); }
        }
    }
}

/// The search loop from the `Automaton` docs, written out by hand: forward, unanchored,
/// leftmost-first, over the whole haystack. Must agree with `try_search_fwd`.
fn walk_fwd<A: Automaton>(dfa: &A, hay: &[u8]) -> Result<Option<(usize, usize)>, ()> {
    let input = Input::new(hay);
    let mut state = dfa.start_state_forward(&input).map_err(|_| ())?;
    let mut last = None;
    for (i, &b) in hay.iter().enumerate() {
        state = dfa.next_state(state, b);
        if dfa.is_special_state(state) {
            if dfa.is_match_state(state) {
                check(dfa.match_len(state) >= 1, "a match state has at least one pattern");
                last = Some((dfa.match_pattern(state, 0).as_usize(), i));
            } else if dfa.is_dead_state(state) {
                return Ok(last);
            } else if dfa.is_quit_state(state) {
                return Err(());
            }
            let _ = (dfa.is_start_state(state), dfa.is_accel_state(state));
        }
    }
    state = dfa.next_eoi_state(state);
    if dfa.is_match_state(state) {
        last = Some((dfa.match_pattern(state, 0).as_usize(), hay.len()));
    }
    Ok(last)
}

fn dfa_checks<A: Automaton>(dfa: &A, rev: &A, hay: &[u8], at: usize, what: &str) {
    let api = dfa.try_search_fwd(&Input::new(hay)).map(|h| h.map(|h| (h.pattern().as_usize(), h.offset())));
    let manual = walk_fwd(dfa, hay);
    // (with UTF-8 mode and empty matches, try_search_fwd also skips empty matches that split a
    // codepoint; the hand-written loop does not)
    let utf8empty = dfa.has_empty() && dfa.is_utf8();
    if let (Ok(a), Ok(m), false) = (&api, &manual, utf8empty) {
        if a != m {
            log(what);
            check(false, "a hand-written DFA walk agrees with try_search_fwd");
        }
    }
    // start states: start::Config::look_behind/anchored == start_state_forward of an input starting at `at`
    let at = at.min(hay.len());
    for mode in [Anchored::No, Anchored::Yes] {
        let input = Input::new(hay).range(at..).anchored(mode);
        let cfg = regex_automata::util::start::Config::new().look_behind(if at == 0 { None } else { Some(hay[at - 1]) }).anchored(mode);
        check(cfg.get_anchored() == mode, "start::Config getters");
        let a = dfa.start_state(&cfg).ok();
        let b = dfa.start_state_forward(&input).ok();
        let c = dfa.start_state(&regex_automata::util::start::Config::from_input_forward(&input)).ok();
        check(a == b && b == c, "start_state(Config) == start_state_forward(Input)");
        let _ = (dfa.universal_start_state(mode), rev.start_state_reverse(&input).ok(), regex_automata::util::start::Config::from_input_reverse(&input).get_look_behind());
    }
    // the reverse DFA finds the start of the forward match
    if let Ok(Some((p, end))) = api {
        let rinput = Input::new(hay).range(..end).anchored(Anchored::Pattern(PatternID::must(p)));
        if let Ok(Some(h)) = rev.try_search_rev(&rinput) {
            check(h.offset() <= end && h.pattern().as_usize() == p, "reverse search finds a start at or before the end");
        }
        let _ = rev.try_search_rev(&Input::new(hay).range(..end).anchored(Anchored::Yes));
    }
    let mut st = regex_automata::dfa::OverlappingState::start();
    let mut n = 0;
    while n < 64 {
        if dfa.try_search_overlapping_fwd(&Input::new(hay), &mut st).is_err() { break }
        match st.get_match() { Some(_) => n += 1, None => break }
    }
    let mut rst = regex_automata::dfa::OverlappingState::start();
    let _ = rev.try_search_overlapping_rev(&Input::new(hay), &mut rst);
    let _ = (dfa.pattern_len(), dfa.has_empty(), dfa.is_utf8(), dfa.is_always_start_anchored(), dfa.get_prefilter().is_some());
}

endpoint! {
    fn dfa_walk(d: Borrow<D>, hay: Bytes<128, Haystack>, at: Range<0, 128>) {
        let (fw, rv) = (d.dense.forward(), d.dense.reverse());
        dfa_checks(fw, rv, &hay, *at as usize, "dense");
        if let Some(sp) = &d.sparse {
            dfa_checks(sp.forward(), sp.reverse(), &hay, *at as usize, "sparse");
        }
        // equivalent bytes (same byte class) lead to the same state
        let classes = fw.byte_classes();
        let mut state = match fw.start_state_forward(&Input::new(&hay[..])) { Ok(s) => s, Err(_) => bail("no start state") };
        for &b in hay.iter().take(16) {
            for rep in classes.elements(regex_automata::util::alphabet::Unit::u8(classes.get(b))) {
                if let Some(r) = rep.as_u8() {
                    check(fw.next_state(state, r) == fw.next_state(state, b), "bytes of one byte class have the same transitions");
                }
            }
            check(classes.get(b) as usize == classes.get_by_unit(regex_automata::util::alphabet::Unit::u8(b)), "ByteClasses::get == get_by_unit");
            state = fw.next_state(state, b);
        }
        let reps: Vec<_> = classes.representatives(..).collect();
        check(reps.len() == classes.alphabet_len() && classes.eoi().is_eoi() && classes.is_singleton() == (classes.alphabet_len() == 257), "ByteClasses representatives / eoi / is_singleton");
        let _ = (classes.iter().count(), classes.stride2(), format!("{:?}", regex_automata::util::escape::DebugHaystack(&hay)));
    }
}

endpoint! {
    fn hybrid_walk(a: Borrow<A>, hay: Bytes<128, Haystack>, cap: Range<0, 2>) {
        let nfa = a.pike.get_nfa().clone();
        let cfg = hybrid::dfa::Config::new().unicode_word_boundary(true)
            .cache_capacity(match *cap { 0 => 1 << 12, 1 => 1 << 15, _ => 1 << 20 })
            .skip_cache_capacity_check(*cap == 0).minimum_cache_clear_count(Some(1)).minimum_bytes_per_state(Some(10));
        let dfa = match hybrid::dfa::Builder::new().configure(cfg).build_from_nfa(nfa) {
            Ok(d) => d,
            Err(_) => bail("lazy DFA build error"),
        };
        let mut cache = dfa.create_cache();
        let api = dfa.try_search_fwd(&mut cache, &Input::new(&hay[..])).map(|h| h.map(|h| (h.pattern().as_usize(), h.offset())));
        // the documented hand-written lazy DFA search loop
        let manual = (|| -> Result<Option<(usize, usize)>, ()> {
            let mut c = dfa.create_cache();
            let input = Input::new(&hay[..]);
            let mut sid = dfa.start_state_forward(&mut c, &input).map_err(|_| ())?;
            let mut last = None;
            c.search_start(0);
            for (i, &b) in hay.iter().enumerate() {
                sid = dfa.next_state(&mut c, sid, b).map_err(|_| ())?;
                c.search_update(i);
                if sid.is_tagged() {
                    if sid.is_match() {
                        check(dfa.match_len(&c, sid) >= 1, "a lazy match state has a pattern");
                        last = Some((dfa.match_pattern(&c, sid, 0).as_usize(), i));
                    } else if sid.is_dead() {
                        return Ok(last);
                    } else if sid.is_quit() || sid.is_unknown() {
                        return Err(());
                    }
                    let _ = sid.is_start();
                } else {
                    let _ = dfa.next_state_untagged(&c, sid, b);
                }
            }
            sid = dfa.next_eoi_state(&mut c, sid).map_err(|_| ())?;
            c.search_finish(hay.len());
            let _ = (c.search_total_len(), c.clear_count(), c.memory_usage());
            if sid.is_match() {
                last = Some((dfa.match_pattern(&c, sid, 0).as_usize(), hay.len()));
            }
            Ok(last)
        })();
        log(&format!("api {api:?} manual {manual:?}"));
        let utf8empty = dfa.get_nfa().has_empty() && dfa.get_nfa().is_utf8();
        if let (Ok(x), Ok(y), false) = (&api, &manual, utf8empty) {
            check(x == y, "a hand-written lazy DFA walk agrees with try_search_fwd");
        }
        let _ = (dfa.pattern_len(), dfa.memory_usage(), dfa.get_nfa().pattern_len(), dfa.byte_classes().alphabet_len(),
                 dfa.get_config().get_minimum_cache_capacity(a.pike.get_nfa()).is_ok());
        cache.reset(&dfa);
        dfa.reset_cache(&mut cache);
    }
}

endpoint! {
    fn with_cache(a: Borrow<A>, hay: Bytes<256, Haystack>, s: Range<0, 300>, e: Range<0, 300>, anch: Range<0, 2>, pid: Range<0, 2>) {
        // the *_with variants (explicit cache) agree with the plain ones; Input setters
        let mut input = Input::new(&hay[..]);
        let e = (*e as usize).min(hay.len());
        let s = (*s as usize).min(e);
        input.set_range(s..e);
        input.set_start(s);
        input.set_end(e);
        input.set_anchored(match *anch { 0 => Anchored::No, 1 => Anchored::Yes, _ => Anchored::Pattern(PatternID::must(*pid as usize % a.pats.len())) });
        input.set_earliest(false);
        check(input.get_range() == (s..e) && input.is_char_boundary(s) == (s == hay.len() || hay[s] < 0x80 || hay[s] >= 0xC0), "Input setters / is_char_boundary");
        let mut c = a.meta.create_cache();
        check(a.meta.search_with(&mut c, &input) == a.meta.search(&input), "search_with == search");
        check(a.meta.is_match_with(&mut c, &input) == a.meta.is_match(input.clone()), "is_match_with == is_match");
        check(a.meta.search_half_with(&mut c, &input) == a.meta.search_half(&input), "search_half_with == search_half");
        let (mut c1, mut c2) = (a.meta.create_captures(), a.meta.create_captures());
        a.meta.search_captures_with(&mut c, &input, &mut c1);
        a.meta.search_captures(&input, &mut c2);
        check(c1.get_match() == c2.get_match(), "search_captures_with == search_captures");
        let (mut s1, mut s2) = (vec![None; 2 * a.pats.len()], vec![None; 2 * a.pats.len()]);
        check(a.meta.search_slots_with(&mut c, &input, &mut s1) == a.meta.search_slots(&input, &mut s2) && s1 == s2, "search_slots_with == search_slots");
        let (mut p1, mut p2) = (PatternSet::new(a.pats.len()), PatternSet::new(a.pats.len()));
        a.meta.which_overlapping_matches_with(&mut c, &input, &mut p1);
        a.meta.which_overlapping_matches(&input, &mut p2);
        check(p1 == p2, "which_overlapping_matches_with == which_overlapping_matches");
        // the backtracker's try_* family
        let mut bc = a.bt.create_cache();
        let maxlen = a.bt.max_haystack_len();
        let need = regex_automata::nfa::thompson::backtrack::min_visited_capacity(a.bt.get_nfa(), &input);
        let r = a.bt.try_is_match(&mut bc, input.clone());
        check(r.is_ok() || e - s > maxlen, "the backtracker accepts spans up to max_haystack_len()");
        let _ = need;
        if let Ok(is) = r {
            let mut caps = a.bt.create_captures();
            check(a.bt.try_captures(&mut bc, input.clone(), &mut caps).is_ok() && caps.is_match() == is, "try_captures agrees with try_is_match");
            let mut slots = vec![None; a.bt.get_nfa().group_info().slot_len()];
            let pid = a.bt.try_search_slots(&mut bc, &input, &mut slots);
            check(pid.ok().flatten().is_some() == is, "try_search_slots agrees with try_is_match");
            let n = a.bt.try_captures_iter(&mut bc, &hay[..]).take(50).count();
            let _ = n;
        }
        a.bt.reset_cache(&mut bc);
        let mut pc = a.pike.create_cache();
        a.pike.reset_cache(&mut pc);
        let _ = (pc.memory_usage(), bc.memory_usage(), a.bt.get_config().get_visited_capacity(), a.pike.get_config().get_match_kind());
        // the Searcher by hand
        let mut srch = regex_automata::util::iter::Searcher::new(Input::new(&hay[..]));
        let mut mc = a.meta.create_cache();
        let first = srch.advance(|inp| Ok(a.meta.search_with(&mut mc, inp)));
        check(first == a.meta.find(&hay[..]), "Searcher::advance finds the first match");
        let mut srch = regex_automata::util::iter::Searcher::new(Input::new(&hay[..]));
        let half = srch.try_advance_half(|inp| Ok(a.meta.search_half_with(&mut mc, inp)));
        check(half.ok().flatten().map(|h| h.offset()) == a.meta.find(&hay[..]).map(|m| m.end()), "Searcher::try_advance_half");
        let _ = srch.input().start();
    }
}

endpoint! {
    fn config_getters(f: u32, n: Range<0, 100000>, b: u8) {
        let yes = |i: u32| f & (1 << i) != 0;
        let n = *n as usize;
        let sc = syntax::Config::new().case_insensitive(yes(0)).multi_line(yes(1)).dot_matches_new_line(yes(2)).crlf(yes(3))
            .line_terminator(b).swap_greed(yes(4)).ignore_whitespace(yes(5)).unicode(yes(6)).utf8(yes(7)).nest_limit(n as u32).octal(yes(8));
        check((sc.get_case_insensitive(), sc.get_multi_line(), sc.get_dot_matches_new_line(), sc.get_crlf(), sc.get_line_terminator(),
               sc.get_swap_greed(), sc.get_ignore_whitespace(), sc.get_unicode(), sc.get_utf8(), sc.get_nest_limit(), sc.get_octal())
              == (yes(0), yes(1), yes(2), yes(3), b, yes(4), yes(5), yes(6), yes(7), n as u32, yes(8)), "syntax::Config getters");
        let mc = meta::Config::new().utf8_empty(yes(9)).auto_prefilter(yes(10)).nfa_size_limit(Some(n)).onepass_size_limit(Some(n))
            .hybrid_cache_capacity(n).dfa_size_limit(Some(n)).dfa_state_limit(Some(n)).byte_classes(yes(11)).line_terminator(b)
            .hybrid(yes(12)).dfa(yes(13)).onepass(yes(14)).backtrack(yes(15)).pool_capacity(n.max(1));
        check((mc.get_utf8_empty(), mc.get_auto_prefilter(), mc.get_nfa_size_limit(), mc.get_onepass_size_limit(), mc.get_hybrid_cache_capacity(),
               mc.get_dfa_size_limit(), mc.get_dfa_state_limit())
              == (yes(9), yes(10), Some(n), Some(n), n, Some(n), Some(n)), "meta::Config getters");
        check((mc.get_byte_classes(), mc.get_line_terminator(), mc.get_hybrid(), mc.get_dfa(), mc.get_onepass(), mc.get_backtrack(), mc.get_pool_capacity())
              == (yes(11), b, yes(12), yes(13), yes(14), yes(15), n.max(1)), "meta::Config getters");
        let _ = (mc.get_prefilter().is_some(), mc.get_which_captures().is_any());
        let tc = thompson::Config::new().utf8(yes(16)).reverse(yes(17)).nfa_size_limit(Some(n)).shrink(yes(18)).captures(yes(19));
        check((tc.get_utf8(), tc.get_reverse(), tc.get_nfa_size_limit(), tc.get_shrink(), tc.get_captures())
              == (yes(16), yes(17), Some(n), yes(18), yes(19)), "thompson::Config getters");
        let _ = (tc.get_which_captures().is_none(), tc.get_look_matcher().get_line_terminator());
        // (quit(b, false) panics for a non-ASCII byte while unicode_word_boundary is on: documented)
        let quit = yes(25) || (b >= 0x80 && yes(24));
        let dc = dense::Config::new().accelerate(yes(20)).minimize(yes(21)).starts_for_each_pattern(yes(22)).byte_classes(yes(23))
            .unicode_word_boundary(yes(24)).quit(b, quit).specialize_start_states(yes(26)).dfa_size_limit(Some(n)).determinize_size_limit(Some(n));
        check((dc.get_accelerate(), dc.get_minimize(), dc.get_starts_for_each_pattern(), dc.get_byte_classes(), dc.get_unicode_word_boundary(),
               dc.get_specialize_start_states(), dc.get_dfa_size_limit(), dc.get_determinize_size_limit())
              == (yes(20), yes(21), yes(22), yes(23), yes(24), yes(26), Some(n), Some(n)), "dense::Config getters");
        check(dc.get_quit(b) == (quit || (yes(24) && b >= 0x80)), "dense::Config::get_quit");
        let _ = (dc.get_match_kind(), dc.get_starts(), dc.get_prefilter().is_some());
        let hc = hybrid::dfa::Config::new().starts_for_each_pattern(yes(22)).byte_classes(yes(23)).unicode_word_boundary(yes(24))
            .quit(b, quit).specialize_start_states(yes(26)).cache_capacity(n).skip_cache_capacity_check(yes(27))
            .minimum_cache_clear_count(Some(n)).minimum_bytes_per_state(Some(n));
        check((hc.get_starts_for_each_pattern(), hc.get_byte_classes(), hc.get_unicode_word_boundary(), hc.get_specialize_start_states(),
               hc.get_cache_capacity(), hc.get_skip_cache_capacity_check(), hc.get_minimum_cache_clear_count(), hc.get_minimum_bytes_per_state())
              == (yes(22), yes(23), yes(24), yes(26), n, yes(27), Some(n), Some(n)), "hybrid::dfa::Config getters");
        check(hc.get_quit(b) == (quit || (yes(24) && b >= 0x80)), "hybrid::dfa::Config::get_quit");
        let _ = (hc.get_match_kind(), hc.get_prefilter().is_some());
        let oc = onepass::Config::new().starts_for_each_pattern(yes(22)).byte_classes(yes(23)).size_limit(Some(n));
        check((oc.get_starts_for_each_pattern(), oc.get_byte_classes(), oc.get_size_limit()) == (yes(22), yes(23), Some(n)), "onepass::Config getters");
        let _ = oc.get_match_kind();
        let bc = regex_automata::nfa::thompson::backtrack::Config::new().visited_capacity(n);
        check(bc.get_visited_capacity() == n && bc.get_prefilter().is_none(), "backtrack::Config getters");
        let _ = PikeVM::config().get_prefilter().is_none();
        // primitives
        let p = PatternID::new(n);
        check(p.is_ok() && p.as_ref().unwrap().as_usize() == n && p.as_ref().unwrap().one_more() == n + 1, "PatternID::new / as_usize / one_more");
        let p = p.unwrap();
        check(PatternID::from_ne_bytes(p.to_ne_bytes()) == Ok(p) && p.as_u32() as usize == n && p.as_u64() as usize == n && p.as_i32() as usize == n, "PatternID byte round trip");
        check(PatternID::new(usize::MAX).is_err(), "PatternID::new(usize::MAX) is refused");
        let sid = regex_automata::util::primitives::StateID::new(n);
        check(sid.map(|s| s.as_usize()) == Ok(n), "StateID::new");
        check(regex_automata::util::primitives::NonMaxUsize::new(n).map(|x| x.get()) == Some(n) && regex_automata::util::primitives::NonMaxUsize::new(usize::MAX).is_none(), "NonMaxUsize");
        let errs = [regex_automata::MatchError::quit(b, n), regex_automata::MatchError::gave_up(n), regex_automata::MatchError::haystack_too_long(n),
                    regex_automata::MatchError::unsupported_anchored(Anchored::Yes)];
        for e in &errs { let _ = (e.kind(), e.to_string()); }
        let _ = (regex_automata::util::look::UnicodeWordBoundaryError::check().is_ok(), regex_automata::util::alphabet::ByteClasses::singletons().alphabet_len());
    }
}
