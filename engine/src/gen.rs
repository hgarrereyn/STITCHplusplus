//! Program and parameter generation (producer-aware graph completion).
use crate::data;
use crate::ffi;
use crate::health;
use crate::manifest::{self, Kind, ParamDef};
use crate::program::{Node, Param, Program, FORM_CHOICES, FORM_VALUE};
use libafl_bolts::rands::Rand;
use std::collections::HashSet;
use std::num::NonZero;
use std::sync::OnceLock;

pub struct GenConfig {
    pub max_nodes: usize,
    /// endpoints the fuzzer may pick on its own (all reachable endpoints unless --only is used);
    /// producers needed by selected endpoints are always allowed
    pub selected: Vec<bool>,
}

static CONFIG: OnceLock<GenConfig> = OnceLock::new();

pub fn init(cfg: GenConfig) {
    let _ = CONFIG.set(cfg);
}

pub fn config() -> &'static GenConfig {
    CONFIG.get().expect("gen config")
}

pub fn below<R: Rand>(r: &mut R, n: usize) -> usize {
    match NonZero::new(n) {
        Some(nz) => r.below(nz),
        None => 0,
    }
}

pub fn unit<R: Rand>(r: &mut R) -> f64 {
    (r.next() >> 11) as f64 * (1.0 / 9007199254740992.0)
}

pub fn weighted<R: Rand>(r: &mut R, w: &[f64]) -> Option<usize> {
    let total: f64 = w.iter().sum();
    if !(total > 0.0) {
        return None;
    }
    let mut x = unit(r) * total;
    for (i, &v) in w.iter().enumerate() {
        if x < v {
            return Some(i);
        }
        x -= v;
    }
    Some(w.len() - 1)
}

const INTERESTING: &[u64] = &[
    0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 511, 512, 1000, 1023, 1024, 4096, 0x7fff,
    0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff, 0x100000000,
];

fn fresh_int<R: Rand>(r: &mut R, size: usize) -> Vec<u8> {
    let mask: u64 = if size >= 8 { !0 } else { (1u64 << (8 * size)) - 1 };
    let u = unit(r);
    let v: u64 = if u < 0.35 {
        INTERESTING[below(r, INTERESTING.len())]
    } else if u < 0.45 {
        mask // -1
    } else if u < 0.5 {
        mask >> 1
    } else if u < 0.8 {
        below(r, 256) as u64
    } else {
        r.next()
    };
    (v & mask).to_le_bytes()[..size].to_vec()
}

fn fresh_float<R: Rand>(r: &mut R, size: usize) -> Vec<u8> {
    let specials = [0.0, 1.0, -1.0, 0.5, 2.0, 1e-9, 1e308, -1e308, f64::INFINITY, f64::NAN, 3.14159, 100.0, 1e6];
    let v = if unit(r) < 0.6 { specials[below(r, specials.len())] } else { (unit(r) - 0.5) * 2e6 };
    if size == 4 {
        (v as f32).to_le_bytes().to_vec()
    } else {
        v.to_le_bytes().to_vec()
    }
}

fn fresh_len<R: Rand>(r: &mut R, max: usize) -> usize {
    let u = unit(r);
    let l = if u < 0.55 {
        below(r, 17)
    } else if u < 0.85 {
        below(r, 129)
    } else {
        below(r, max + 1)
    };
    l.min(max)
}

fn fresh_blob<R: Rand>(r: &mut R, max: usize) -> Vec<u8> {
    let toks = data::tokens();
    if !toks.is_empty() && unit(r) < 0.2 {
        let mut t = toks[below(r, toks.len())].clone();
        t.truncate(max);
        return t;
    }
    let l = fresh_len(r, max);
    let printable = unit(r) < 0.7;
    (0..l).map(|_| if printable { 32 + below(r, 95) as u8 } else { r.next() as u8 }).collect()
}

/// A fresh value for one parameter.
pub fn fresh_param<R: Rand>(r: &mut R, p: &ParamDef, dt: Option<usize>) -> Param {
    let bytes = match p.kind {
        Kind::Int => fresh_int(r, p.size as usize),
        Kind::Float => fresh_float(r, p.size as usize),
        Kind::Bool => vec![below(r, 2) as u8],
        Kind::Range => {
            let span = (p.hi as i128 - p.lo as i128 + 1) as u128;
            let u = unit(r);
            let v: i64 = if u < 0.08 {
                p.lo
            } else if u < 0.16 {
                p.hi
            } else {
                (p.lo as i128 + ((r.next() as u128 % span.max(1)) as i128)) as i64
            };
            v.to_le_bytes().to_vec()
        }
        Kind::Oneof => {
            let v = if p.values.is_empty() { 0 } else { p.values[below(r, p.values.len())] };
            v.to_le_bytes().to_vec()
        }
        Kind::Flags => {
            let mut v = 0i64;
            for &f in &p.values {
                if unit(r) < 0.3 {
                    v |= f;
                }
            }
            v.to_le_bytes().to_vec()
        }
        Kind::Raw => {
            if unit(r) < 0.3 {
                vec![0; p.size as usize]
            } else {
                (0..p.size).map(|_| if unit(r) < 0.5 { below(r, 16) as u8 } else { r.next() as u8 }).collect()
            }
        }
        Kind::Source => {
            let l = 8 + below(r, (p.size as usize).saturating_sub(8).max(1));
            (0..l.min(p.size as usize)).map(|_| r.next() as u8).collect()
        }
        Kind::Value => {
            let max = p.size as usize;
            if let Some(dt) = dt {
                if manifest::get().datatype_decoder[dt] {
                    return Param { form: FORM_CHOICES, bytes: ffi::generate(dt, r.next(), &[], max) };
                }
            }
            let l = 8 + below(r, max.saturating_sub(8).max(1));
            return Param { form: FORM_CHOICES, bytes: (0..l.min(max)).map(|_| r.next() as u8).collect() };
        }
        Kind::Bytes | Kind::Str | Kind::File => {
            let max = p.size as usize;
            if let Some(dt) = dt {
                return fresh_typed(r, dt, max);
            }
            fresh_blob(r, max)
        }
    };
    Param { form: FORM_VALUE, bytes }
}

/// A fresh value for a buffer of datatype `dt`: decoder output (as choices), a seed, or random.
pub fn fresh_typed<R: Rand>(r: &mut R, dt: usize, max: usize) -> Param {
    let m = manifest::get();
    let seeds = data::seeds(dt);
    let has_dec = m.datatype_decoder[dt];
    let u = unit(r);
    if has_dec && (seeds.is_empty() || u < 0.7) {
        let bytes = ffi::generate(dt, r.next(), &[], max);
        return Param { form: FORM_CHOICES, bytes };
    }
    if !seeds.is_empty() && u < 0.95 {
        let mut s = seeds[below(r, seeds.len())].clone();
        s.truncate(max);
        return Param { form: FORM_VALUE, bytes: s };
    }
    Param { form: FORM_VALUE, bytes: fresh_blob(r, max) }
}

/// The node that last used/produced object `obj` before position `pos`.
pub fn last_toucher(prog: &Program, obj: (u32, u8), pos: usize) -> u32 {
    let mut t = obj.0;
    for (i, n) in prog.nodes.iter().enumerate().take(pos).skip(obj.0 as usize + 1) {
        if n.inputs.contains(&obj) {
            t = i as u32;
        }
    }
    t
}

/// Choose an endpoint for the fuzzer to add.
pub fn pick_endpoint<R: Rand>(r: &mut R) -> Option<usize> {
    let m = manifest::get();
    let cfg = config();
    let w: Vec<f64> = health::with(|h| {
        (0..m.endpoints.len())
            .map(|i| {
                if !cfg.selected[i] || !m.reachable[i] {
                    0.0
                } else {
                    let base = if m.endpoints[i].in_types.is_empty() { 0.5 } else { 1.0 };
                    base * h.ep_weight(i)
                }
            })
            .collect()
    });
    weighted(r, &w)
}

/// Insert a call to `ep` at `pos`, inserting producer calls before it when an input object is
/// needed. Returns the index of the inserted call.
pub fn add_call<R: Rand>(r: &mut R, prog: &mut Program, pos: usize, ep: usize, depth: usize) -> Option<usize> {
    let m = manifest::get();
    let e = &m.endpoints[ep];
    if prog.nodes.len() + 1 > config().max_nodes || depth > 8 {
        return None;
    }
    let mut cur = pos;
    let mut inputs: Vec<(u32, u8)> = vec![];
    for (slot, &ty) in e.in_types.iter().enumerate() {
        let take = e.in_take[slot];
        let mut cands = prog.available(cur, ty);
        cands.retain(|c| !inputs.contains(c));
        if take {
            // a taken object must not be used after this call
            let used_later: HashSet<(u32, u8)> =
                prog.nodes.iter().skip(cur).flat_map(|n| n.inputs.iter().copied()).collect();
            cands.retain(|c| !used_later.contains(c));
        }
        let make_new = cands.is_empty() || unit(r) < 0.12;
        let chosen = if !make_new {
            let w: Vec<f64> = health::with(|h| {
                cands
                    .iter()
                    .map(|&c| {
                        let t = last_toucher(prog, c, cur);
                        h.pair_rate(ep as u32, slot as u8, prog.nodes[t as usize].ep)
                    })
                    .collect()
            });
            Some(cands[weighted(r, &w).unwrap_or(0)])
        } else {
            None
        };
        let obj = match chosen {
            Some(o) => o,
            None => {
                let prods: Vec<(usize, usize)> =
                    m.producers[ty].iter().copied().filter(|&(p, _)| m.reachable[p] && p != ep).collect();
                if prods.is_empty() {
                    if cands.is_empty() {
                        return None;
                    }
                    cands[below(r, cands.len())]
                } else {
                    let w: Vec<f64> = health::with(|h| {
                        prods
                            .iter()
                            .map(|&(p, _)| {
                                let extra = m.endpoints[p].in_types.len() as f64;
                                h.pair_rate(ep as u32, slot as u8, p as u32) * h.ep_weight(p) / (1.0 + extra)
                            })
                            .collect()
                    });
                    let (p, out) = prods[weighted(r, &w).unwrap_or(0)];
                    let at = add_call(r, prog, cur, p, depth + 1)?;
                    cur = at + 1;
                    (at as u32, out as u8)
                }
            }
        };
        inputs.push(obj);
    }
    let params = e.params.iter().zip(&e.param_dt).map(|(p, dt)| fresh_param(r, p, *dt)).collect();
    prog.insert_node(cur, Node { ep: ep as u32, inputs, params });
    Some(cur)
}

/// A random program with about `calls` top-level calls.
pub fn random_program<R: Rand>(r: &mut R, calls: usize) -> Program {
    let mut p = Program::default();
    for _ in 0..calls {
        if let Some(ep) = pick_endpoint(r) {
            let end = p.nodes.len();
            let _ = add_call(r, &mut p, end, ep, 0);
        }
    }
    p
}
