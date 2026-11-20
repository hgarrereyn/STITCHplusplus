//! Programs: the fuzzer input. A program is a sequence of endpoint calls; object inputs refer to
//! outputs of earlier calls, fuzz parameters are byte buffers.
use crate::manifest;
use libafl::corpus::CorpusId;
use libafl::inputs::Input;
use libafl_bolts::HasLen;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::hash::{Hash, Hasher};
use std::path::Path;

pub const FORM_VALUE: u8 = 0;
pub const FORM_CHOICES: u8 = 1;

#[derive(Clone, Debug, Serialize, Deserialize, Hash, PartialEq, Eq)]
pub struct Param {
    pub form: u8,
    pub bytes: Vec<u8>,
}

#[derive(Clone, Debug, Serialize, Deserialize, Hash, PartialEq, Eq)]
pub struct Node {
    pub ep: u32,
    /// (source node, output index) for each object input
    pub inputs: Vec<(u32, u8)>,
    pub params: Vec<Param>,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct Program {
    pub nodes: Vec<Node>,
}

impl Hash for Program {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.nodes.hash(state)
    }
}

impl HasLen for Program {
    fn len(&self) -> usize {
        self.nodes.len()
    }
}

impl Input for Program {
    fn to_file<P: AsRef<Path>>(&self, path: P) -> Result<(), libafl::Error> {
        libafl_bolts::fs::write_file_atomic(path, &self.serialize())
    }
    fn from_file<P: AsRef<Path>>(path: P) -> Result<Self, libafl::Error> {
        let data = std::fs::read(path)?;
        Program::deserialize(&data).ok_or_else(|| libafl::Error::illegal_argument("invalid .stp program"))
    }
    fn generate_name(&self, _id: Option<CorpusId>) -> String {
        let mut h = std::collections::hash_map::DefaultHasher::new();
        self.hash(&mut h);
        format!("{:016x}.stp", h.finish())
    }
}

impl Program {
    /// Binary STCH format shared with the runtime (endpoint names, not indices, so saved
    /// programs survive harness rebuilds).
    pub fn serialize(&self) -> Vec<u8> {
        let m = manifest::get();
        let mut names: Vec<u32> = vec![];
        let mut index = std::collections::HashMap::new();
        for n in &self.nodes {
            index.entry(n.ep).or_insert_with(|| {
                names.push(n.ep);
                names.len() as u32 - 1
            });
        }
        let mut o = Vec::with_capacity(64 + self.nodes.len() * 32);
        o.extend_from_slice(b"STCH");
        o.extend_from_slice(&(names.len() as u32).to_le_bytes());
        for &ep in &names {
            let nm = m.endpoints[ep as usize].name.as_bytes();
            o.extend_from_slice(&(nm.len() as u16).to_le_bytes());
            o.extend_from_slice(nm);
        }
        o.extend_from_slice(&(self.nodes.len() as u32).to_le_bytes());
        for n in &self.nodes {
            o.extend_from_slice(&index[&n.ep].to_le_bytes());
            o.push(n.inputs.len() as u8);
            for &(src, out) in &n.inputs {
                o.extend_from_slice(&src.to_le_bytes());
                o.push(out);
            }
            o.push(n.params.len() as u8);
            for p in &n.params {
                o.push(p.form);
                o.extend_from_slice(&(p.bytes.len() as u32).to_le_bytes());
                o.extend_from_slice(&p.bytes);
            }
        }
        o
    }

    pub fn deserialize(d: &[u8]) -> Option<Program> {
        let m = manifest::get();
        let mut r = Rd { d, off: 0 };
        if r.take(4)? != b"STCH" {
            return None;
        }
        let nn = r.u32()? as usize;
        let mut names = vec![];
        for _ in 0..nn {
            let l = r.u16()? as usize;
            names.push(String::from_utf8_lossy(r.take(l)?).into_owned());
        }
        let n = r.u32()? as usize;
        let mut nodes = vec![];
        for _ in 0..n {
            let ni = r.u32()? as usize;
            let ep = *m.ep_by_name.get(names.get(ni)?)? as u32;
            let nin = r.u8()?;
            let mut inputs = vec![];
            for _ in 0..nin {
                inputs.push((r.u32()?, r.u8()?));
            }
            let np = r.u8()?;
            let mut params = vec![];
            for _ in 0..np {
                let form = r.u8()?;
                let l = r.u32()? as usize;
                params.push(Param { form, bytes: r.take(l)?.to_vec() });
            }
            nodes.push(Node { ep, inputs, params });
        }
        let p = Program { nodes };
        if p.validate().is_err() {
            return None;
        }
        Some(p)
    }

    /// Static validity: inputs refer to earlier outputs of the right type, no use after take,
    /// no object twice in one call, parameter count matches.
    pub fn validate(&self) -> Result<(), String> {
        let m = manifest::get();
        let mut taken: HashSet<(u32, u8)> = HashSet::new();
        for (i, n) in self.nodes.iter().enumerate() {
            let e = m.endpoints.get(n.ep as usize).ok_or("bad endpoint")?;
            if n.inputs.len() != e.in_types.len() || n.params.len() != e.params.len() {
                return Err(format!("node {i}: arity mismatch"));
            }
            let mut seen = HashSet::new();
            for (k, &(src, out)) in n.inputs.iter().enumerate() {
                if src as usize >= i {
                    return Err(format!("node {i}: forward reference"));
                }
                let se = &m.endpoints[self.nodes[src as usize].ep as usize];
                if out as usize >= se.out_types.len() || se.out_types[out as usize] != e.in_types[k] {
                    return Err(format!("node {i}: type mismatch"));
                }
                if taken.contains(&(src, out)) {
                    return Err(format!("node {i}: use after take"));
                }
                if !seen.insert((src, out)) {
                    return Err(format!("node {i}: object used twice"));
                }
            }
            for (k, &(src, out)) in n.inputs.iter().enumerate() {
                if e.in_take[k] {
                    taken.insert((src, out));
                }
            }
        }
        Ok(())
    }

    /// Objects of type `ty` available as inputs to a call inserted at position `pos`.
    pub fn available(&self, pos: usize, ty: usize) -> Vec<(u32, u8)> {
        let m = manifest::get();
        let mut out = vec![];
        for (i, n) in self.nodes.iter().enumerate().take(pos) {
            let e = &m.endpoints[n.ep as usize];
            for (k, &t) in e.out_types.iter().enumerate() {
                if t == ty {
                    out.push((i as u32, k as u8));
                }
            }
        }
        // remove objects taken before pos, and objects taken at/after pos (a new use at pos
        // would come before their take, which is fine) -> only before-pos takes matter
        let mut taken = HashSet::new();
        for n in self.nodes.iter().take(pos) {
            let e = &m.endpoints[n.ep as usize];
            for (k, &src) in n.inputs.iter().enumerate() {
                if e.in_take[k] {
                    taken.insert(src);
                }
            }
        }
        out.retain(|o| !taken.contains(o));
        out
    }

    /// Is object `obj` used (borrowed or taken) by any node at index >= `from`?
    pub fn taken_at_or_after(&self, obj: (u32, u8), from: usize) -> Option<usize> {
        let m = manifest::get();
        for (i, n) in self.nodes.iter().enumerate().skip(from) {
            let e = &m.endpoints[n.ep as usize];
            for (k, &src) in n.inputs.iter().enumerate() {
                if src == obj && e.in_take[k] {
                    return Some(i);
                }
            }
        }
        None
    }

    /// Insert a node at `pos`, shifting references.
    pub fn insert_node(&mut self, pos: usize, node: Node) {
        for n in self.nodes.iter_mut().skip(pos) {
            for inp in n.inputs.iter_mut() {
                if inp.0 as usize >= pos {
                    inp.0 += 1;
                }
            }
        }
        self.nodes.insert(pos, node);
    }

    /// Remove node `idx` and every node that (transitively) depends on its outputs.
    pub fn remove_with_dependents(&mut self, idx: usize) {
        let mut dead = vec![false; self.nodes.len()];
        dead[idx] = true;
        for i in idx + 1..self.nodes.len() {
            if self.nodes[i].inputs.iter().any(|&(s, _)| dead[s as usize]) {
                dead[i] = true;
            }
        }
        self.retain_nodes(&dead);
    }

    /// Drop nodes marked dead (caller guarantees no live node references a dead one).
    pub fn retain_nodes(&mut self, dead: &[bool]) {
        let mut remap = vec![u32::MAX; self.nodes.len()];
        let mut next = 0u32;
        for i in 0..self.nodes.len() {
            if !dead[i] {
                remap[i] = next;
                next += 1;
            }
        }
        let old = std::mem::take(&mut self.nodes);
        for (i, mut n) in old.into_iter().enumerate() {
            if dead[i] {
                continue;
            }
            for inp in n.inputs.iter_mut() {
                inp.0 = remap[inp.0 as usize];
            }
            self.nodes.push(n);
        }
    }

    /// Nodes that `idx` (transitively) depends on, including idx, in order.
    pub fn ancestors(&self, idx: usize) -> Vec<usize> {
        let mut need = vec![false; self.nodes.len()];
        need[idx] = true;
        for i in (0..=idx).rev() {
            if need[i] {
                for &(s, _) in &self.nodes[i].inputs {
                    need[s as usize] = true;
                }
            }
        }
        (0..=idx).filter(|&i| need[i]).collect()
    }

    /// Total bytes in all parameters (for size limits).
    pub fn param_bytes(&self) -> usize {
        self.nodes.iter().flat_map(|n| n.params.iter()).map(|p| p.bytes.len()).sum()
    }
}

struct Rd<'a> {
    d: &'a [u8],
    off: usize,
}
impl<'a> Rd<'a> {
    fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.off + n > self.d.len() {
            return None;
        }
        let s = &self.d[self.off..self.off + n];
        self.off += n;
        Some(s)
    }
    fn u8(&mut self) -> Option<u8> {
        Some(self.take(1)?[0])
    }
    fn u16(&mut self) -> Option<u16> {
        Some(u16::from_le_bytes(self.take(2)?.try_into().ok()?))
    }
    fn u32(&mut self) -> Option<u32> {
        Some(u32::from_le_bytes(self.take(4)?.try_into().ok()?))
    }
}
