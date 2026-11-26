//! Execution statistics that drive both the engine (endpoint / producer weights) and the agent
//! reports (`stitch status`, `stitch probe`).
use crate::ffi;
use crate::manifest;
use crate::program::Program;
use libafl::executors::ExitKind;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::hash::{BuildHasherDefault, Hasher};
use std::sync::Mutex;

/// FxHash (rustc's hasher): the statistics maps are keyed by small integers and updated on every
/// execution, where SipHash's DoS resistance buys nothing.
#[derive(Default, Clone, Copy)]
pub struct FxHasher(u64);

impl Hasher for FxHasher {
    #[inline]
    fn write(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.write_u64(b as u64);
        }
    }
    #[inline]
    fn write_u8(&mut self, i: u8) {
        self.write_u64(i as u64);
    }
    #[inline]
    fn write_u32(&mut self, i: u32) {
        self.write_u64(i as u64);
    }
    #[inline]
    fn write_u64(&mut self, i: u64) {
        self.0 = (self.0.rotate_left(5) ^ i).wrapping_mul(0x51_7c_c1_b7_27_22_0a_95);
    }
    #[inline]
    fn write_usize(&mut self, i: usize) {
        self.write_u64(i as u64);
    }
    #[inline]
    fn finish(&self) -> u64 {
        self.0
    }
}

pub type FxMap<K, V> = HashMap<K, V, BuildHasherDefault<FxHasher>>;

#[derive(Default, Clone, Debug)]
pub struct EpStats {
    pub invoked: u64,
    pub ok: u64,
    pub bail: u64,
    pub skip_missing: u64,
    pub skip_invalid: u64,
    pub crash: u64,
    pub timeout: u64,
}

#[derive(Default)]
pub struct Health {
    pub ep: Vec<EpStats>,
    /// (consumer endpoint, input slot, endpoint that last touched the object) -> (calls, ok)
    pub pairs: FxMap<(u32, u8, u32), (u64, u64)>,
    pub reasons: FxMap<(u32, u32), u64>,
    pub miss: FxMap<(u32, u32), u64>,
    pub strings: FxMap<u32, String>,
    pub execs: u64,
    pub crashes: u64,
    pub timeouts: u64,
    pub execs_with_bail: u64,
    pub nodes_run: u64,
    pub cum: Vec<u8>,
    /// per mutation operator: (applied, produced a new corpus entry)
    pub ops: Vec<(u64, u64)>,
    pub last_ops: Vec<usize>,
    pub learned_tokens: Vec<String>,
    pub edges_hit: u64,
    pub n_edges: usize,
}

pub const OP_NAMES: [&str; 15] = [
    "append", "insert", "delete", "rewire", "splice", "regen_call_params", "duplicate_call", "havoc", "i2s",
    "tokens", "regen_param", "param_crossover", "lower_or_regen_tail", "i2s_strings", "elements",
];

pub static HEALTH: Mutex<Option<Health>> = Mutex::new(None);

pub fn init(n_edges: usize) {
    let m = manifest::get();
    *HEALTH.lock().unwrap() = Some(Health {
        ep: vec![EpStats::default(); m.endpoints.len()],
        cum: vec![0; n_edges],
        ops: vec![(0, 0); OP_NAMES.len()],
        n_edges,
        ..Default::default()
    });
}

pub fn with<R>(f: impl FnOnce(&mut Health) -> R) -> R {
    let mut g = HEALTH.lock().unwrap();
    f(g.as_mut().expect("health not initialized"))
}

impl Health {
    /// Record one execution of `prog` from the shared results the child wrote.
    pub fn record(&mut self, prog: &Program, exit: &ExitKind, edges: &[u8]) {
        let r = ffi::results();
        self.execs += 1;
        let strings = &mut self.strings;
        ffi::for_each_result_string(r, |h, b| {
            if !strings.contains_key(&h) {
                strings.insert(h, String::from_utf8_lossy(b).into_owned());
            }
        });
        let crashed = matches!(exit, ExitKind::Crash | ExitKind::Oom);
        let timed_out = matches!(exit, ExitKind::Timeout);
        if crashed {
            self.crashes += 1;
        }
        if timed_out {
            self.timeouts += 1;
        }
        // object -> node that last used it (programs are short: a list beats a hash map)
        let mut last_touch: Vec<((u32, u8), u32)> = Vec::with_capacity(16);
        let n = prog.nodes.len().min(ffi::MAX_NODES);
        let mut any_bail = false;
        for i in 0..n {
            let node = &prog.nodes[i];
            let ep = node.ep as usize;
            let res = r.nodes[i];
            let mut status = res.status;
            if (crashed || timed_out) && r.current == i as i32 {
                if crashed {
                    self.ep[ep].crash += 1;
                } else {
                    self.ep[ep].timeout += 1;
                }
                status = 0xff;
            }
            match status {
                ffi::ST_OK | ffi::ST_BAIL | 0xff => {
                    let ok = status == ffi::ST_OK;
                    let st = &mut self.ep[ep];
                    st.invoked += 1;
                    self.nodes_run += 1;
                    if ok {
                        st.ok += 1;
                    } else if status == ffi::ST_BAIL {
                        st.bail += 1;
                        any_bail = true;
                        *self.reasons.entry((ep as u32, res.reason)).or_default() += 1;
                        if res.miss_key != 0 {
                            *self.miss.entry((ep as u32, res.miss_key)).or_default() += 1;
                        }
                    }
                    for (k, &obj) in node.inputs.iter().enumerate() {
                        let toucher = last_touch.iter().rev().find(|(o, _)| *o == obj).map(|x| x.1).unwrap_or(obj.0);
                        let pe = prog.nodes[toucher as usize].ep;
                        let e = self.pairs.entry((ep as u32, k as u8, pe)).or_default();
                        e.0 += 1;
                        if ok {
                            e.1 += 1;
                        }
                    }
                }
                ffi::ST_SKIP_MISSING => self.ep[ep].skip_missing += 1,
                ffi::ST_SKIP_INVALID => self.ep[ep].skip_invalid += 1,
                _ => {}
            }
            for &obj in &node.inputs {
                last_touch.push((obj, i as u32));
            }
        }
        if any_bail {
            self.execs_with_bail += 1;
        }
        let lim = edges.len().min(self.cum.len());
        let mut i = 0;
        while i < lim {
            // most of the map is zero: skip 8 bytes at a time
            if i + 8 <= lim && u64::from_ne_bytes(edges[i..i + 8].try_into().unwrap()) == 0 {
                i += 8;
                continue;
            }
            let k = i;
            i += 1;
            if edges[k] != 0 && self.cum[k] == 0 {
                self.cum[k] = 1;
                self.edges_hit += 1;
            }
        }
    }

    /// Selection weight of an endpoint. Endpoints that never succeed are mostly skipped;
    /// rarely-successful ones get an exploration bonus.
    pub fn ep_weight(&self, ep: usize) -> f64 {
        let s = &self.ep[ep];
        if s.ok == 0 && s.invoked >= 200 {
            return 0.05; // written off, still retried occasionally
        }
        if s.ok == 0 && s.invoked >= 40 {
            return 0.5; // failing so far, maybe for lack of good inputs: keep exploring
        }
        1.0 + 2.0 / (1.0 + s.ok as f64 / 64.0)
    }

    /// Estimated success rate of calling `consumer` with input `slot` last touched by `toucher`.
    pub fn pair_rate(&self, consumer: u32, slot: u8, toucher: u32) -> f64 {
        match self.pairs.get(&(consumer, slot, toucher)) {
            Some(&(n, ok)) => ((ok as f64 + 1.0) / (n as f64 + 2.0)).max(0.02),
            None => 0.6,
        }
    }

    pub fn string(&self, h: u32) -> String {
        self.strings.get(&h).cloned().unwrap_or_else(|| format!("#{h:08x}"))
    }

    pub fn to_json(&self, elapsed_secs: f64, corpus: usize, solutions: usize) -> Value {
        let m = manifest::get();
        let mut eps = vec![];
        for (i, e) in m.endpoints.iter().enumerate() {
            let s = &self.ep[i];
            let mut reasons: Vec<(String, u64)> =
                self.reasons.iter().filter(|((ep, _), _)| *ep == i as u32).map(|((_, r), c)| (self.string(*r), *c)).collect();
            reasons.sort_by(|a, b| b.1.cmp(&a.1));
            reasons.truncate(5);
            let mut miss: Vec<(String, u64)> =
                self.miss.iter().filter(|((ep, _), _)| *ep == i as u32).map(|((_, k), c)| (self.string(*k), *c)).collect();
            miss.sort_by(|a, b| b.1.cmp(&a.1));
            miss.truncate(5);
            let mut prods = vec![];
            for (&(c, slot, p), &(n, ok)) in &self.pairs {
                if c == i as u32 && n >= 20 {
                    prods.push(json!({
                        "input": m.raw.endpoints[i].inputs[slot as usize].name,
                        "after": m.endpoints[p as usize].name,
                        "calls": n,
                        "ok_rate": (ok as f64 / n as f64 * 1000.0).round() / 1000.0,
                    }));
                }
            }
            prods.sort_by(|a, b| b["calls"].as_u64().cmp(&a["calls"].as_u64()));
            prods.truncate(12);
            eps.push(json!({
                "name": e.name,
                "file": e.file,
                "reachable": m.reachable[i],
                "invoked": s.invoked,
                "ok": s.ok,
                "bail": s.bail,
                "skipped_missing_input": s.skip_missing,
                "skipped_invalid": s.skip_invalid,
                "crashes": s.crash,
                "timeouts": s.timeout,
                "bail_rate": if s.invoked > 0 { ((s.bail as f64 / s.invoked as f64) * 1000.0).round() / 1000.0 } else { 0.0 },
                "bail_reasons": reasons.iter().map(|(r, c)| json!({"reason": r, "count": c})).collect::<Vec<_>>(),
                "missing_attr_reads": miss.iter().map(|(k, c)| json!({"key": k, "count": c})).collect::<Vec<_>>(),
                "by_previous_call": prods,
            }));
        }
        let inv: u64 = self.ep.iter().map(|e| e.invoked).sum();
        let bails: u64 = self.ep.iter().map(|e| e.bail).sum();
        let call_bail_rate = if inv > 0 { ((bails as f64 / inv as f64) * 1000.0).round() / 1000.0 } else { 0.0 };
        json!({
            "elapsed_secs": (elapsed_secs * 10.0).round() / 10.0,
            "execs": self.execs,
            "execs_per_sec": if elapsed_secs > 0.0 { (self.execs as f64 / elapsed_secs).round() } else { 0.0 },
            "edges_covered": self.edges_hit,
            "mutations": OP_NAMES.iter().zip(&self.ops).map(|(n, (a, w))| json!({"op": n, "applied": a, "new_coverage": w})).collect::<Vec<_>>(),
            "learned_tokens": self.learned_tokens.iter().take(50).collect::<Vec<_>>(),
            "edges_total": self.n_edges,
            "corpus": corpus,
            "crashes_and_timeouts_saved": solutions,
            "crashing_execs": self.crashes,
            "timeouts": self.timeouts,
            "call_bail_rate": call_bail_rate,
            "exec_bail_rate": if self.execs > 0 { ((self.execs_with_bail as f64 / self.execs as f64) * 1000.0).round() / 1000.0 } else { 0.0 },
            "avg_calls_per_exec": if self.execs > 0 { ((self.nodes_run as f64 / self.execs as f64) * 100.0).round() / 100.0 } else { 0.0 },
            "endpoints": eps,
        })
    }
}
