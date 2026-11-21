//! Program mutations: graph edits plus LibAFL byte mutators (havoc, tokens, cmplog I2S) applied
//! to individual parameter buffers.
use crate::cmplog::NodeCmps;
use crate::ffi;
use crate::gen::{self, below, unit, weighted};
use crate::health;
use crate::manifest::{self, Kind};
use crate::program::{Node, Program, FORM_CHOICES, FORM_VALUE};
use libafl::corpus::{Corpus, HasCurrentCorpusId};
use libafl::inputs::{HasMutatorBytes, ResizableMutator};
use libafl::mutators::{
    havoc_mutations_no_crossover, tokens_mutations, HavocMutationsNoCrossoverType, HavocScheduledMutator, I2SRandReplace,
    MutationResult, Mutator,
};
use libafl::observers::cmp::CmpValuesMetadata;
use libafl::state::{HasCorpus, HasMaxSize, HasRand};
use libafl::Error;
use libafl_bolts::rands::Rand;
use libafl_bolts::tuples::tuple_list_type;
use libafl::common::HasMetadata;
use libafl_bolts::{HasLen, Named};
use std::borrow::Cow;
use std::ops::RangeBounds;
use std::vec::{Drain, Splice};

/// A single parameter buffer exposed to LibAFL's byte mutators.
#[derive(Debug)]
pub struct BufView<'a> {
    pub buf: &'a mut Vec<u8>,
}

impl HasLen for BufView<'_> {
    fn len(&self) -> usize {
        self.buf.len()
    }
}

impl HasMutatorBytes for BufView<'_> {
    fn mutator_bytes(&self) -> &[u8] {
        self.buf
    }
    fn mutator_bytes_mut(&mut self) -> &mut [u8] {
        self.buf
    }
}

impl ResizableMutator<u8> for BufView<'_> {
    fn resize(&mut self, new_len: usize, value: u8) {
        self.buf.resize(new_len, value);
    }
    fn extend<'b, I: IntoIterator<Item = &'b u8>>(&mut self, iter: I) {
        <Vec<u8> as Extend<&u8>>::extend(self.buf, iter);
    }
    fn splice<R, I>(&mut self, range: R, replace_with: I) -> Splice<'_, I::IntoIter>
    where
        R: RangeBounds<usize>,
        I: IntoIterator<Item = u8>,
    {
        self.buf.splice(range, replace_with)
    }
    fn drain<R>(&mut self, range: R) -> Drain<'_, u8>
    where
        R: RangeBounds<usize>,
    {
        self.buf.drain(range)
    }
}

type TokensType = tuple_list_type!(libafl::mutators::TokenInsert, libafl::mutators::TokenReplace);

pub struct StitchMutator {
    havoc: HavocScheduledMutator<HavocMutationsNoCrossoverType>,
    tokens: HavocScheduledMutator<TokensType>,
    i2s: I2SRandReplace,
}

impl Named for StitchMutator {
    fn name(&self) -> &Cow<'static, str> {
        static N: Cow<'static, str> = Cow::Borrowed("StitchMutator");
        &N
    }
}

impl StitchMutator {
    pub fn new() -> Self {
        Self {
            havoc: HavocScheduledMutator::with_max_stack_pow(havoc_mutations_no_crossover(), 4),
            tokens: HavocScheduledMutator::with_max_stack_pow(tokens_mutations(), 1),
            i2s: I2SRandReplace,
        }
    }
}

/// All (node, param) positions of the program, with a selection weight.
fn param_sites(p: &Program) -> (Vec<(usize, usize)>, Vec<f64>) {
    let m = manifest::get();
    let mut sites = vec![];
    let mut w = vec![];
    for (i, n) in p.nodes.iter().enumerate() {
        let e = &m.endpoints[n.ep as usize];
        for (k, pd) in e.params.iter().enumerate() {
            sites.push((i, k));
            w.push(match pd.kind {
                Kind::Bool => 0.3,
                Kind::Bytes | Kind::Str | Kind::File | Kind::Source | Kind::Value => 3.0,
                _ => 1.0,
            });
        }
    }
    (sites, w)
}

/// Restore the size invariants of a parameter after a byte mutation.
fn fix_param(prog: &mut Program, node: usize, k: usize) {
    let m = manifest::get();
    let pd = &m.endpoints[prog.nodes[node].ep as usize].params[k];
    let p = &mut prog.nodes[node].params[k];
    match pd.fixed_size() {
        Some(sz) => p.bytes.resize(sz, 0),
        None => p.bytes.truncate(pd.size as usize),
    }
}

impl StitchMutator {
    // ---------------------------------------------------------------- graph mutations
    fn append<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        let Some(ep) = gen::pick_endpoint(r) else { return false };
        let end = p.nodes.len();
        gen::add_call(r, p, end, ep, 0).is_some()
    }

    fn insert<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        let Some(ep) = gen::pick_endpoint(r) else { return false };
        let pos = below(r, p.nodes.len() + 1);
        gen::add_call(r, p, pos, ep, 0).is_some()
    }

    fn delete<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        if p.nodes.len() <= 1 {
            return false;
        }
        let idx = below(r, p.nodes.len());
        p.remove_with_dependents(idx);
        !p.nodes.is_empty()
    }

    /// Re-pick one object input among the available alternatives (producer-aware).
    fn rewire<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        let m = manifest::get();
        let cands: Vec<usize> = (0..p.nodes.len()).filter(|&i| !p.nodes[i].inputs.is_empty()).collect();
        if cands.is_empty() {
            return false;
        }
        let i = cands[below(r, cands.len())];
        let e = &m.endpoints[p.nodes[i].ep as usize];
        let slot = below(r, e.in_types.len());
        let cur = p.nodes[i].inputs[slot];
        let mut alts = p.available(i, e.in_types[slot]);
        alts.retain(|a| *a != cur && !p.nodes[i].inputs.contains(a));
        if e.in_take[slot] {
            alts.retain(|a| !p.nodes.iter().skip(i + 1).any(|n| n.inputs.contains(a)));
        }
        if alts.is_empty() {
            return false;
        }
        let ep = p.nodes[i].ep;
        let w: Vec<f64> = health::with(|h| {
            alts.iter().map(|&a| h.pair_rate(ep, slot as u8, p.nodes[gen::last_toucher(p, a, i) as usize].ep)).collect()
        });
        let new = alts[weighted(r, &w).unwrap_or(0)];
        let old = cur;
        p.nodes[i].inputs[slot] = new;
        // if this call takes `new`, and it used to take `old`, later uses of `old` are fine;
        // validate and revert if needed
        if p.validate().is_err() {
            p.nodes[i].inputs[slot] = old;
            return false;
        }
        true
    }

    /// Append a node from another corpus entry together with everything it depends on.
    fn splice<R: Rand>(&self, r: &mut R, p: &mut Program, other: &Program) -> bool {
        if other.nodes.is_empty() {
            return false;
        }
        let idx = below(r, other.nodes.len());
        let anc = other.ancestors(idx);
        if p.nodes.len() + anc.len() > gen::config().max_nodes {
            return false;
        }
        let base = p.nodes.len() as u32;
        let mut remap = std::collections::HashMap::new();
        for (j, &a) in anc.iter().enumerate() {
            remap.insert(a as u32, base + j as u32);
        }
        for &a in &anc {
            let mut n = other.nodes[a].clone();
            for inp in n.inputs.iter_mut() {
                inp.0 = remap[&inp.0];
            }
            p.nodes.push(n);
        }
        true
    }

    fn regen_node_params<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        if p.nodes.is_empty() {
            return false;
        }
        let m = manifest::get();
        let i = below(r, p.nodes.len());
        let e = &m.endpoints[p.nodes[i].ep as usize];
        if e.params.is_empty() {
            return false;
        }
        p.nodes[i].params = e.params.iter().zip(&e.param_dt).map(|(pd, dt)| gen::fresh_param(r, pd, *dt)).collect();
        true
    }

    fn duplicate_call<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        let m = manifest::get();
        if p.nodes.is_empty() || p.nodes.len() >= gen::config().max_nodes {
            return false;
        }
        let i = below(r, p.nodes.len());
        let e = &m.endpoints[p.nodes[i].ep as usize];
        if e.in_take.iter().any(|&t| t) {
            return false;
        }
        let n: Node = p.nodes[i].clone();
        let pos = i + 1 + below(r, p.nodes.len() - i);
        p.insert_node(pos, n);
        p.validate().is_ok()
    }

    // ---------------------------------------------------------------- parameter mutations
    fn pick_param<R: Rand>(&self, r: &mut R, p: &Program) -> Option<(usize, usize)> {
        let (sites, w) = param_sites(p);
        weighted(r, &w).map(|i| sites[i])
    }

    fn param_havoc<S>(&mut self, state: &mut S, p: &mut Program) -> Result<bool, Error>
    where
        S: HasRand + HasMaxSize,
    {
        let Some((i, k)) = self.pick_param(state.rand_mut(), p) else { return Ok(false) };
        let res = {
            let mut view = BufView { buf: &mut p.nodes[i].params[k].bytes };
            self.havoc.mutate(state, &mut view)?
        };
        fix_param(p, i, k);
        Ok(res == MutationResult::Mutated)
    }

    fn param_tokens<S>(&mut self, state: &mut S, p: &mut Program) -> Result<bool, Error>
    where
        S: HasRand + HasMaxSize + HasMetadata,
    {
        let Some((i, k)) = self.pick_param(state.rand_mut(), p) else { return Ok(false) };
        let res = {
            let mut view = BufView { buf: &mut p.nodes[i].params[k].bytes };
            self.tokens.mutate(state, &mut view)?
        };
        fix_param(p, i, k);
        Ok(res == MutationResult::Mutated)
    }

    /// Input-to-state replacement: operands compared by a node are searched for in that node's
    /// parameter buffers (then in any buffer) and replaced with the other operand.
    fn param_i2s<S>(&mut self, state: &mut S, p: &mut Program) -> Result<bool, Error>
    where
        S: HasRand + HasMaxSize + HasMetadata + HasCorpus<Program> + HasCurrentCorpusId,
    {
        let cmps: Option<NodeCmps> = match state.current_corpus_id()? {
            Some(id) => state.corpus().get(id)?.borrow().metadata_map().get::<NodeCmps>().cloned(),
            None => None,
        };
        let Some(cmps) = cmps else { return Ok(false) };
        let nodes: Vec<u32> = cmps.per_node.keys().copied().filter(|&n| (n as usize) < p.nodes.len()).collect();
        if nodes.is_empty() {
            return Ok(false);
        }
        let node = nodes[below(state.rand_mut(), nodes.len())];
        let list: Vec<_> = cmps.per_node[&node].iter().map(|c| c.to_libafl()).collect();
        state.add_metadata(CmpValuesMetadata { list });
        // candidate buffers: the comparing node's params first, then any node's
        let mut sites: Vec<(usize, usize)> = (0..p.nodes[node as usize].params.len()).map(|k| (node as usize, k)).collect();
        if sites.is_empty() || unit(state.rand_mut()) < 0.3 {
            let (all, _) = param_sites(p);
            sites = all;
        }
        for _ in 0..4 {
            if sites.is_empty() {
                break;
            }
            let si = below(state.rand_mut(), sites.len());
            let (i, k) = sites.swap_remove(si);
            let res = {
                let mut view = BufView { buf: &mut p.nodes[i].params[k].bytes };
                self.i2s.mutate(state, &mut view)?
            };
            if res == MutationResult::Mutated {
                fix_param(p, i, k);
                return Ok(true);
            }
        }
        Ok(false)
    }

    /// String input-to-state with resizing: replace a compared string operand found in a buffer
    /// by the other operand, growing/shrinking the buffer. In decoder choice buffers the operand is
    /// located inside a str()/token() draw and its length prefix is rewritten.
    fn param_i2s_strings<S>(&mut self, state: &mut S, p: &mut Program) -> Result<bool, Error>
    where
        S: HasRand + HasCorpus<Program> + HasCurrentCorpusId,
    {
        let m = manifest::get();
        let cmps: Option<NodeCmps> = match state.current_corpus_id()? {
            Some(id) => state.corpus().get(id)?.borrow().metadata_map().get::<NodeCmps>().cloned(),
            None => None,
        };
        let Some(cmps) = cmps else { return Ok(false) };
        let pairs: Vec<(&Vec<u8>, &Vec<u8>)> = cmps
            .per_node
            .values()
            .flatten()
            .filter(|r| r.kind == 1 && !r.a.is_empty() && !r.b.is_empty() && r.a != r.b)
            .map(|r| (&r.a, &r.b))
            .collect();
        if pairs.is_empty() {
            return Ok(false);
        }
        let (a, b) = pairs[below(state.rand_mut(), pairs.len())];
        let (from, to) = if below(state.rand_mut(), 2) == 0 { (a, b) } else { (b, a) };
        let mut sites = vec![];
        for (i, n) in p.nodes.iter().enumerate() {
            let e = &m.endpoints[n.ep as usize];
            for (k, pd) in e.params.iter().enumerate() {
                if matches!(pd.kind, Kind::Bytes | Kind::Str | Kind::File | Kind::Value) {
                    sites.push((i, k));
                }
            }
        }
        while !sites.is_empty() {
            let (i, k) = sites.swap_remove(below(state.rand_mut(), sites.len()));
            let e = &m.endpoints[p.nodes[i].ep as usize];
            let max = e.params[k].size as usize;
            let prm = &mut p.nodes[i].params[k];
            if prm.form == FORM_VALUE {
                if let Some(pos) = find(&prm.bytes, from) {
                    prm.bytes.splice(pos..pos + from.len(), to.iter().copied());
                    prm.bytes.truncate(max);
                    return Ok(true);
                }
            } else if let Some(dt) = e.param_dt[k] {
                for (off, w, len) in ffi::decode_spans(dt, &prm.bytes) {
                    let start = off + w;
                    if start + len > prm.bytes.len() {
                        continue;
                    }
                    let content = &prm.bytes[start..start + len];
                    let Some(pos) = find(content, from) else { continue };
                    let mut nc = content.to_vec();
                    nc.splice(pos..pos + from.len(), to.iter().copied());
                    // w == 0: rest() at the end of the buffer, no length prefix to rewrite
                    let cap = if w == 0 || w >= 8 { usize::MAX } else { (1usize << (8 * w)) - 1 };
                    if nc.len() > cap || prm.bytes.len() - len + nc.len() > max {
                        continue;
                    }
                    if w > 0 {
                        let nl = (nc.len() as u64).to_le_bytes();
                        prm.bytes[off..off + w].copy_from_slice(&nl[..w]);
                    }
                    prm.bytes.splice(start..start + len, nc);
                    return Ok(true);
                }
            }
        }
        Ok(false)
    }

    fn regen_param<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        let m = manifest::get();
        let Some((i, k)) = self.pick_param(r, p) else { return false };
        let e = &m.endpoints[p.nodes[i].ep as usize];
        p.nodes[i].params[k] = gen::fresh_param(r, &e.params[k], e.param_dt[k]);
        true
    }

    /// Copy a buffer between parameters of the same kind and datatype (cross-pollination).
    fn param_crossover<R: Rand>(&self, r: &mut R, p: &mut Program, other: &Program) -> bool {
        let m = manifest::get();
        let Some((i, k)) = self.pick_param(r, p) else { return false };
        let e = &m.endpoints[p.nodes[i].ep as usize];
        let (kind, dt) = (e.params[k].kind, e.param_dt[k]);
        let mut cands = vec![];
        for (src, prog) in [(0, &*p), (1, other)] {
            for (j, n) in prog.nodes.iter().enumerate() {
                let ee = &m.endpoints[n.ep as usize];
                for (kk, pd) in ee.params.iter().enumerate() {
                    if (src, j, kk) != (0, i, k)
                        && ee.param_dt[kk] == dt
                        && (pd.kind == kind || dt.is_some())
                        && (kind != Kind::Value || n.params[kk].form == FORM_CHOICES)
                    {
                        cands.push(n.params[kk].clone());
                    }
                }
            }
        }
        if cands.is_empty() {
            return false;
        }
        let c = cands.swap_remove(below(r, cands.len()));
        if c == p.nodes[i].params[k] {
            return false;
        }
        p.nodes[i].params[k] = c;
        fix_param(p, i, k);
        true
    }

    /// Choices -> concrete value: lets byte mutators break the structure the decoder enforces.
    fn lower<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        let m = manifest::get();
        let mut sites = vec![];
        for (i, n) in p.nodes.iter().enumerate() {
            for (k, prm) in n.params.iter().enumerate() {
                if prm.form == FORM_CHOICES {
                    sites.push((i, k));
                }
            }
        }
        if sites.is_empty() {
            return false;
        }
        let (i, k) = sites[below(r, sites.len())];
        let e = &m.endpoints[p.nodes[i].ep as usize];
        if e.params[k].kind == Kind::Value {
            return false; // a typed value has no byte form: it stays choices
        }
        let Some(dt) = e.param_dt[k] else { return false };
        let v = ffi::decode(dt, &p.nodes[i].params[k].bytes, e.params[k].size as usize);
        p.nodes[i].params[k].bytes = v;
        p.nodes[i].params[k].form = FORM_VALUE;
        true
    }

    /// Keep a prefix of a choice buffer and let the decoder generate a fresh tail.
    fn regen_tail<R: Rand>(&self, r: &mut R, p: &mut Program) -> bool {
        let m = manifest::get();
        let mut sites = vec![];
        for (i, n) in p.nodes.iter().enumerate() {
            for (k, prm) in n.params.iter().enumerate() {
                if prm.form == FORM_CHOICES {
                    sites.push((i, k));
                }
            }
        }
        if sites.is_empty() {
            return false;
        }
        let (i, k) = sites[below(r, sites.len())];
        let e = &m.endpoints[p.nodes[i].ep as usize];
        let Some(dt) = e.param_dt[k] else { return false };
        let cur = &p.nodes[i].params[k].bytes;
        let cut = below(r, cur.len() + 1);
        let prefix = cur[..cut].to_vec();
        let seed = r.next();
        p.nodes[i].params[k].bytes = ffi::generate(dt, seed, &prefix, e.params[k].size as usize);
        true
    }

    /// Loop-element mutations on a decoder choice buffer: delete, duplicate, move or swap one
    /// iteration of a `more()` loop, or copy in an iteration of the same loop from another buffer of
    /// the same data type. The boundaries come from replaying the decoder, so the other values keep
    /// their positions relative to their own iteration.
    fn param_elements<R: Rand>(&self, r: &mut R, p: &mut Program, other: &Program) -> bool {
        let m = manifest::get();
        let mut sites = vec![];
        for (i, n) in p.nodes.iter().enumerate() {
            let e = &m.endpoints[n.ep as usize];
            for (k, prm) in n.params.iter().enumerate() {
                if let Some(dt) = e.param_dt[k] {
                    if prm.form == FORM_CHOICES && m.datatype_decoder[dt] {
                        sites.push((i, k, dt));
                    }
                }
            }
        }
        if sites.is_empty() {
            return false;
        }
        let (i, k, dt) = sites[below(r, sites.len())];
        let max = m.endpoints[p.nodes[i].ep as usize].params[k].size as usize;
        let buf = p.nodes[i].params[k].bytes.clone();
        let items = ffi::decode_items(dt, &buf);
        let elems: Vec<ffi::Item> = items.iter().copied().filter(|x| x.start < x.end).collect();
        // insertion points per loop execution: every iteration start and the end marker
        let points = |site: u32| -> Vec<(u32, usize)> {
            items.iter().filter(|x| x.site == site).map(|x| (x.inst, x.start)).collect()
        };
        let u = unit(r);
        let new = if u < 0.25 {
            // delete an iteration
            if elems.is_empty() {
                return false;
            }
            let e = elems[below(r, elems.len())];
            let mut b = buf.clone();
            b.drain(e.start..e.end);
            b
        } else if u < 0.5 {
            // duplicate an iteration (right after itself, or elsewhere in the same loop execution)
            if elems.is_empty() {
                return false;
            }
            let e = elems[below(r, elems.len())];
            let pts: Vec<usize> = points(e.site).into_iter().filter(|(inst, _)| *inst == e.inst).map(|(_, x)| x).collect();
            let at = if unit(r) < 0.5 || pts.is_empty() { e.end } else { pts[below(r, pts.len())] };
            let mut b = buf.clone();
            b.splice(at..at, buf[e.start..e.end].iter().copied());
            b
        } else if u < 0.65 {
            // swap two iterations of one loop execution
            let mut by_inst: std::collections::HashMap<u32, Vec<ffi::Item>> = Default::default();
            for e in &elems {
                by_inst.entry(e.inst).or_default().push(*e);
            }
            let multi: Vec<&Vec<ffi::Item>> = by_inst.values().filter(|v| v.len() >= 2).collect();
            if multi.is_empty() {
                return false;
            }
            let v = multi[below(r, multi.len())];
            let (mut a, mut c) = (below(r, v.len()), below(r, v.len()));
            if a == c {
                return false;
            }
            if a > c {
                std::mem::swap(&mut a, &mut c);
            }
            let (a, c) = (v[a], v[c]);
            if a.end > c.start {
                return false;
            }
            let mut b = buf[..a.start].to_vec();
            b.extend_from_slice(&buf[c.start..c.end]);
            b.extend_from_slice(&buf[a.end..c.start]);
            b.extend_from_slice(&buf[a.start..a.end]);
            b.extend_from_slice(&buf[c.end..]);
            b
        } else {
            // copy in an iteration of the same loop from another buffer of this data type
            let mut donors: Vec<Vec<u8>> = vec![];
            for prog in [&*p, other] {
                for (j, n) in prog.nodes.iter().enumerate() {
                    let ee = &m.endpoints[n.ep as usize];
                    for (kk, prm) in n.params.iter().enumerate() {
                        if ee.param_dt[kk] == Some(dt) && prm.form == FORM_CHOICES && !(std::ptr::eq(prog, &*p) && j == i && kk == k) {
                            donors.push(prm.bytes.clone());
                        }
                    }
                }
            }
            donors.push(buf.clone());
            let d = donors.swap_remove(below(r, donors.len()));
            let delems: Vec<ffi::Item> = ffi::decode_items(dt, &d).into_iter().filter(|x| x.start < x.end).collect();
            let sites_here: Vec<u32> = items.iter().map(|x| x.site).collect();
            let fit: Vec<ffi::Item> = delems.into_iter().filter(|x| sites_here.contains(&x.site)).collect();
            if fit.is_empty() {
                return false;
            }
            let e = fit[below(r, fit.len())];
            let pts = points(e.site);
            if pts.is_empty() {
                return false;
            }
            let at = pts[below(r, pts.len())].1;
            let mut b = buf.clone();
            b.splice(at..at, d[e.start..e.end].iter().copied());
            if unit(r) < 0.3 {
                // replace instead of insert: drop the iteration that was at `at`
                if let Some(old) = elems.iter().find(|x| x.start == at && x.site == e.site) {
                    let l = e.end - e.start;
                    b.drain(old.start + l..old.end + l);
                }
            }
            b
        };
        if new == buf || new.len() > max {
            return false;
        }
        p.nodes[i].params[k].bytes = new;
        true
    }
}

impl<S> Mutator<Program, S> for StitchMutator
where
    S: HasRand + HasMaxSize + HasMetadata + HasCorpus<Program> + HasCurrentCorpusId,
{
    fn mutate(&mut self, state: &mut S, input: &mut Program) -> Result<MutationResult, Error> {
        let orig = input.clone();
        let stack = 1 << below(state.rand_mut(), 3);
        let mut changed = false;
        // weights: graph ops, then parameter ops
        // STITCH_NO_ELEMENTS=1 turns the loop-element mutations off (for comparisons)
        static NO_ELEM: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
        let no_elem = *NO_ELEM.get_or_init(|| std::env::var("STITCH_NO_ELEMENTS").map(|v| v == "1").unwrap_or(false));
        const W0: [f64; 15] = [10.0, 7.0, 5.0, 6.0, 4.0, 3.0, 3.0, 24.0, 14.0, 4.0, 5.0, 6.0, 5.0, 0.0, 5.0];
        let mut w = W0;
        if no_elem {
            w[14] = 0.0;
        }
        let mut applied = vec![];
        for _ in 0..stack {
            let op = weighted(state.rand_mut(), &w).unwrap_or(7);
            let mut op_used = op;
            let ok = match op {
                0 => self.append(state.rand_mut(), input),
                1 => self.insert(state.rand_mut(), input),
                2 => self.delete(state.rand_mut(), input),
                3 => self.rewire(state.rand_mut(), input),
                4 | 11 | 14 => {
                    let other = random_other(state)?;
                    if op == 4 {
                        self.splice(state.rand_mut(), input, &other)
                    } else if op == 11 {
                        self.param_crossover(state.rand_mut(), input, &other)
                    } else {
                        self.param_elements(state.rand_mut(), input, &other)
                    }
                }
                5 => self.regen_node_params(state.rand_mut(), input),
                6 => self.duplicate_call(state.rand_mut(), input),
                7 => self.param_havoc(state, input)?,
                8 => {
                    let strings = unit(state.rand_mut()) < 0.4;
                    if strings && self.param_i2s_strings(state, input)? {
                        op_used = 13;
                        true
                    } else if self.param_i2s(state, input)? {
                        true
                    } else {
                        op_used = 7;
                        self.param_havoc(state, input)?
                    }
                }
                9 => self.param_tokens(state, input)?,
                10 => self.regen_param(state.rand_mut(), input),
                12 => {
                    if unit(state.rand_mut()) < 0.3 {
                        self.lower(state.rand_mut(), input)
                    } else {
                        self.regen_tail(state.rand_mut(), input)
                    }
                }
                _ => false,
            };
            changed |= ok;
            if ok {
                applied.push(op_used);
            }
        }
        if input.nodes.is_empty() || input.nodes.len() > gen::config().max_nodes || input.validate().is_err() {
            *input = orig;
            return Ok(MutationResult::Skipped);
        }
        if !changed || *input == orig {
            return Ok(MutationResult::Skipped);
        }
        health::with(|h| {
            for &o in &applied {
                h.ops[o].0 += 1;
            }
            h.last_ops = applied;
        });
        Ok(MutationResult::Mutated)
    }

    fn post_exec(&mut self, _state: &mut S, new_corpus_id: Option<libafl::corpus::CorpusId>) -> Result<(), Error> {
        if new_corpus_id.is_some() {
            health::with(|h| {
                let ops = std::mem::take(&mut h.last_ops);
                for o in ops {
                    h.ops[o].1 += 1;
                }
            });
        }
        Ok(())
    }
}

fn find(hay: &[u8], needle: &[u8]) -> Option<usize> {
    if needle.is_empty() || needle.len() > hay.len() {
        return None;
    }
    hay.windows(needle.len()).position(|w| w == needle)
}

fn random_other<S>(state: &mut S) -> Result<Program, Error>
where
    S: HasRand + HasCorpus<Program>,
{
    let n = state.corpus().count();
    if n == 0 {
        return Ok(Program::default());
    }
    let idx = below(state.rand_mut(), n);
    let id = state.corpus().nth(idx);
    state.corpus().cloned_input_for_id(id)
}
