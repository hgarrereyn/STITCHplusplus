//! Comparison logging attributed to program nodes, and the tracing stage that collects it.
use crate::data;
use crate::ffi;
use crate::manifest;
use crate::program::Program;
use libafl::corpus::{Corpus, HasCurrentCorpusId};
use libafl::executors::Executor;
use libafl::observers::cmp::{CmpValues, CmplogBytes};
use libafl::stages::{Restartable, Stage};
use libafl::state::HasCorpus;
use libafl::Error;
use libafl::common::HasMetadata;
use libafl_bolts::impl_serdeany;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CmpRec {
    pub kind: u8,
    pub size: u8,
    pub a_const: bool,
    pub a: Vec<u8>,
    pub b: Vec<u8>,
}

impl CmpRec {
    pub fn to_libafl(&self) -> CmpValues {
        if self.kind == 1 {
            let mk = |v: &[u8]| {
                let mut buf = [0u8; 32];
                let l = v.len().min(32);
                buf[..l].copy_from_slice(&v[..l]);
                CmplogBytes::from_buf_and_len(buf, l as u8)
            };
            return CmpValues::Bytes((mk(&self.a), mk(&self.b)));
        }
        let ua = u64::from_le_bytes(self.a[..8].try_into().unwrap());
        let ub = u64::from_le_bytes(self.b[..8].try_into().unwrap());
        match self.size {
            1 => CmpValues::U8((ua as u8, ub as u8, self.a_const)),
            2 => CmpValues::U16((ua as u16, ub as u16, self.a_const)),
            4 => CmpValues::U32((ua as u32, ub as u32, self.a_const)),
            _ => CmpValues::U64((ua, ub, self.a_const)),
        }
    }
}

/// Comparison operands observed during one traced run, per program node.
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct NodeCmps {
    pub per_node: HashMap<u32, Vec<CmpRec>>,
}
impl_serdeany!(NodeCmps);

pub fn collect() -> NodeCmps {
    let log = ffi::cmplog();
    let n = (log.count as usize).min(ffi::CMPLOG_RECORDS);
    let mut out = NodeCmps::default();
    for r in &log.records[..n] {
        let rec = CmpRec {
            kind: r.kind,
            size: r.size,
            a_const: r.a_const != 0,
            a: if r.kind == 1 { r.a[..r.len_a as usize].to_vec() } else { r.a[..8].to_vec() },
            b: if r.kind == 1 { r.b[..r.len_b as usize].to_vec() } else { r.b[..8].to_vec() },
        };
        let v = out.per_node.entry(r.node as u32).or_default();
        if v.len() < 512 {
            v.push(rec);
        }
    }
    out
}

fn printable_token(t: &[u8]) -> bool {
    t.len() >= 2 && t.len() <= 32 && t.iter().filter(|&&c| (32..127).contains(&c)).count() * 10 >= t.len() * 8
}

fn contains(hay: &[u8], needle: &[u8]) -> bool {
    needle.len() <= hay.len() && hay.windows(needle.len()).any(|w| w == needle)
}

/// Learn dictionary tokens from string comparisons: globally, and for the datatypes of the
/// buffers passed to the node that made the comparison. Only operands that do not come from the
/// input (not found in any parameter buffer) are learned. Returns the new tokens.
pub fn learn_tokens(prog: &Program, cmps: &NodeCmps) -> Vec<Vec<u8>> {
    let m = manifest::get();
    let mut learned = vec![];
    for (&node, recs) in &cmps.per_node {
        let dts: Vec<usize> = prog
            .nodes
            .get(node as usize)
            .map(|n| m.endpoints[n.ep as usize].param_dt.iter().flatten().copied().collect())
            .unwrap_or_default();
        for r in recs.iter().filter(|r| r.kind == 1) {
            for t in [&r.a, &r.b] {
                if !printable_token(t) || learned.contains(t) {
                    continue;
                }
                let from_input = prog.nodes.iter().flat_map(|n| n.params.iter()).any(|p| contains(&p.bytes, t));
                if from_input {
                    continue;
                }
                data::add_token(t.clone());
                for &dt in &dts {
                    ffi::dict_add(dt, t);
                }
                learned.push(t.clone());
                crate::health::with(|h| {
                    if h.learned_tokens.len() < 500 {
                        h.learned_tokens.push(String::from_utf8_lossy(t).into_owned())
                    }
                });
            }
        }
    }
    learned
}

/// Runs each new corpus entry once with comparison logging enabled and stores the operands on
/// the testcase (used by the I2S mutation).
#[derive(Debug, Default)]
pub struct CmplogStage;

impl<E, EM, S, Z> Stage<E, EM, S, Z> for CmplogStage
where
    E: Executor<EM, Program, S, Z>,
    S: HasCorpus<Program> + HasCurrentCorpusId + HasMetadata,
{
    fn perform(&mut self, fuzzer: &mut Z, executor: &mut E, state: &mut S, manager: &mut EM) -> Result<(), Error> {
        let Some(id) = state.current_corpus_id()? else { return Ok(()) };
        {
            let tc = state.corpus().get(id)?.borrow();
            if tc.has_metadata::<NodeCmps>() {
                return Ok(());
            }
        }
        let input = state.corpus().cloned_input_for_id(id)?;
        unsafe { ffi::stitch_cmplog_reset() };
        ffi::cmplog().enabled = 1;
        let r = executor.run_target(fuzzer, state, manager, &input);
        ffi::cmplog().enabled = 0;
        r?;
        let cmps = collect();
        let learned = learn_tokens(&input, &cmps);
        if !learned.is_empty() {
            let toks = state.metadata_or_insert_with(libafl::mutators::Tokens::default);
            for t in &learned {
                toks.add_token(t);
            }
        }
        state.corpus().get(id)?.borrow_mut().add_metadata(cmps);
        Ok(())
    }
}

impl<S> Restartable<S> for CmplogStage {
    fn should_restart(&mut self, _state: &mut S) -> Result<bool, Error> {
        Ok(true)
    }
    fn clear_progress(&mut self, _state: &mut S) -> Result<(), Error> {
        Ok(())
    }
}
