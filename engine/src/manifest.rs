//! Harness manifest (emitted by the runtime) and derived static analysis.
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::OnceLock;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ObjectDef {
    pub name: String,
    #[serde(default)]
    pub drop: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct DatatypeDef {
    pub name: String,
    #[serde(default)]
    pub decoder: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct InputDef {
    #[serde(rename = "type")]
    pub ty: String,
    pub mode: String,
    #[serde(default)]
    pub name: String,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct OutputDef {
    #[serde(rename = "type")]
    pub ty: String,
    #[serde(default)]
    pub optional: bool,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq, Copy)]
#[serde(rename_all = "lowercase")]
pub enum Kind {
    Int,
    Float,
    Bool,
    Range,
    Oneof,
    Flags,
    Bytes,
    Str,
    File,
    Raw,
    Source,
    /// a typed value from a data type's decoder: always stored as the decoder's choices
    Value,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ParamDef {
    pub kind: Kind,
    #[serde(rename = "type", default)]
    pub ctype: String,
    pub size: u32,
    #[serde(default)]
    pub signed: bool,
    #[serde(default)]
    pub lo: i64,
    #[serde(default)]
    pub hi: i64,
    #[serde(default)]
    pub values: Vec<i64>,
    #[serde(default)]
    pub datatype: Option<String>,
    #[serde(default)]
    pub name: String,
}

impl ParamDef {
    /// Fixed-size value encodings (everything except variable-length buffers).
    pub fn fixed_size(&self) -> Option<usize> {
        match self.kind {
            Kind::Bytes | Kind::Str | Kind::File | Kind::Source | Kind::Value => None,
            Kind::Range | Kind::Oneof | Kind::Flags => Some(8),
            _ => Some(self.size as usize),
        }
    }
    pub fn max_len(&self) -> usize {
        self.fixed_size().unwrap_or(self.size as usize)
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct EndpointDef {
    pub name: String,
    pub file: String,
    pub inputs: Vec<InputDef>,
    pub params: Vec<ParamDef>,
    pub outputs: Vec<OutputDef>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RawManifest {
    pub version: u32,
    pub objects: Vec<ObjectDef>,
    pub datatypes: Vec<DatatypeDef>,
    pub endpoints: Vec<EndpointDef>,
}

/// Endpoint with resolved type indices.
#[derive(Clone, Debug)]
pub struct Endpoint {
    pub name: String,
    pub file: String,
    pub in_types: Vec<usize>,
    pub in_take: Vec<bool>,
    pub out_types: Vec<usize>,
    pub out_opt: Vec<bool>,
    pub params: Vec<ParamDef>,
    pub param_dt: Vec<Option<usize>>,
}

#[derive(Clone, Debug)]
pub struct Manifest {
    pub raw: RawManifest,
    pub endpoints: Vec<Endpoint>,
    pub ep_by_name: HashMap<String, usize>,
    pub type_names: Vec<String>,
    pub datatype_names: Vec<String>,
    pub datatype_decoder: Vec<bool>,
    /// producers[t] = (endpoint, output slot) creating objects of type t
    pub producers: Vec<Vec<(usize, usize)>>,
    /// minimal number of calls needed to construct an object of type t (usize::MAX = impossible)
    pub cost: Vec<usize>,
    /// endpoints whose inputs can all be constructed
    pub reachable: Vec<bool>,
}

impl Manifest {
    pub fn parse(json: &str) -> Result<Manifest, String> {
        let raw: RawManifest = serde_json::from_str(json).map_err(|e| format!("bad manifest: {e}"))?;
        let type_idx: HashMap<String, usize> = raw.objects.iter().enumerate().map(|(i, o)| (o.name.clone(), i)).collect();
        let dt_idx: HashMap<String, usize> = raw.datatypes.iter().enumerate().map(|(i, o)| (o.name.clone(), i)).collect();
        let mut endpoints = vec![];
        for e in &raw.endpoints {
            endpoints.push(Endpoint {
                name: e.name.clone(),
                file: e.file.clone(),
                in_types: e.inputs.iter().map(|i| type_idx[&i.ty]).collect(),
                in_take: e.inputs.iter().map(|i| i.mode == "take").collect(),
                out_types: e.outputs.iter().map(|o| type_idx[&o.ty]).collect(),
                out_opt: e.outputs.iter().map(|o| o.optional).collect(),
                params: e.params.clone(),
                param_dt: e.params.iter().map(|p| p.datatype.as_ref().map(|d| dt_idx[d])).collect(),
            });
        }
        let nt = raw.objects.len();
        let mut producers = vec![vec![]; nt];
        for (i, e) in endpoints.iter().enumerate() {
            for (k, &t) in e.out_types.iter().enumerate() {
                producers[t].push((i, k));
            }
        }
        // constructibility fixpoint
        let mut cost = vec![usize::MAX; nt];
        loop {
            let mut changed = false;
            for e in &endpoints {
                let mut c: usize = 1;
                let mut ok = true;
                for &t in &e.in_types {
                    if cost[t] == usize::MAX {
                        ok = false;
                        break;
                    }
                    c = c.saturating_add(cost[t]);
                }
                if !ok {
                    continue;
                }
                for &t in &e.out_types {
                    if c < cost[t] {
                        cost[t] = c;
                        changed = true;
                    }
                }
            }
            if !changed {
                break;
            }
        }
        let reachable = endpoints.iter().map(|e| e.in_types.iter().all(|&t| cost[t] != usize::MAX)).collect();
        let ep_by_name = endpoints.iter().enumerate().map(|(i, e)| (e.name.clone(), i)).collect();
        Ok(Manifest {
            type_names: raw.objects.iter().map(|o| o.name.clone()).collect(),
            datatype_names: raw.datatypes.iter().map(|d| d.name.clone()).collect(),
            datatype_decoder: raw.datatypes.iter().map(|d| d.decoder).collect(),
            raw,
            endpoints,
            ep_by_name,
            producers,
            cost,
            reachable,
        })
    }
}

static MANIFEST: OnceLock<Manifest> = OnceLock::new();

pub fn init(m: Manifest) {
    let _ = MANIFEST.set(m);
}

pub fn get() -> &'static Manifest {
    MANIFEST.get().expect("manifest not initialized")
}
