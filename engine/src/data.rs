//! Data-type folders: harness/data/<Type>/{seeds/, *.dict}
use crate::ffi;
use crate::manifest;
use std::path::Path;
use std::sync::{Mutex, OnceLock};

struct Store {
    seeds: Vec<Vec<Vec<u8>>>,
}

static STORE: OnceLock<Store> = OnceLock::new();
static TOKENS: Mutex<Vec<Vec<u8>>> = Mutex::new(Vec::new());
static TOKEN_SNAPSHOT: OnceLock<Mutex<std::sync::Arc<Vec<Vec<u8>>>>> = OnceLock::new();

/// Parse an AFL-style dictionary: `name="value"` or `"value"` lines with \xNN escapes; bare lines
/// are taken literally.
pub fn parse_dict(text: &str) -> Vec<Vec<u8>> {
    let mut out = vec![];
    for line in text.lines() {
        let l = line.trim();
        if l.is_empty() || l.starts_with('#') {
            continue;
        }
        let q = match (l.find('"'), l.rfind('"')) {
            (Some(a), Some(b)) if b > a => &l[a + 1..b],
            _ => {
                out.push(l.as_bytes().to_vec());
                continue;
            }
        };
        let mut v = vec![];
        let b = q.as_bytes();
        let mut i = 0;
        while i < b.len() {
            if b[i] == b'\\' && i + 1 < b.len() {
                match b[i + 1] {
                    b'x' if i + 3 < b.len() => {
                        if let Ok(x) = u8::from_str_radix(std::str::from_utf8(&b[i + 2..i + 4]).unwrap_or("zz"), 16) {
                            v.push(x);
                            i += 4;
                            continue;
                        }
                        v.push(b[i]);
                        i += 1;
                    }
                    b'n' => {
                        v.push(b'\n');
                        i += 2;
                    }
                    b't' => {
                        v.push(b'\t');
                        i += 2;
                    }
                    c => {
                        v.push(c);
                        i += 2;
                    }
                }
            } else {
                v.push(b[i]);
                i += 1;
            }
        }
        if !v.is_empty() {
            out.push(v);
        }
    }
    out
}

pub fn load(data_dir: Option<&Path>) {
    let m = manifest::get();
    let mut seeds = vec![vec![]; m.datatype_names.len()];
    if let Some(dir) = data_dir {
        for (dt, name) in m.datatype_names.iter().enumerate() {
            let d = dir.join(name);
            if let Ok(rd) = std::fs::read_dir(d.join("seeds")) {
                let mut files: Vec<_> = rd.flatten().map(|e| e.path()).filter(|p| p.is_file()).collect();
                files.sort();
                for f in files.into_iter().take(5000) {
                    if let Ok(b) = std::fs::read(&f) {
                        seeds[dt].push(b);
                    }
                }
            }
            if let Ok(rd) = std::fs::read_dir(&d) {
                for e in rd.flatten() {
                    let p = e.path();
                    if p.extension().map(|x| x == "dict").unwrap_or(false) {
                        if let Ok(t) = std::fs::read_to_string(&p) {
                            for tok in parse_dict(&t) {
                                ffi::dict_add(dt, &tok);
                                add_token(tok);
                            }
                        }
                    }
                }
            }
        }
        // a global dictionary for untyped buffers
        if let Ok(t) = std::fs::read_to_string(dir.join("global.dict")) {
            for tok in parse_dict(&t) {
                add_token(tok);
            }
        }
    }
    let _ = STORE.set(Store { seeds });
}

pub fn seeds(dt: usize) -> &'static [Vec<u8>] {
    STORE.get().map(|s| s.seeds[dt].as_slice()).unwrap_or(&[])
}

pub fn add_token(t: Vec<u8>) {
    if t.is_empty() || t.len() > 64 {
        return;
    }
    let mut g = TOKENS.lock().unwrap();
    if g.len() < 4096 && !g.contains(&t) {
        g.push(t);
        let snap = TOKEN_SNAPSHOT.get_or_init(|| Mutex::new(std::sync::Arc::new(vec![])));
        *snap.lock().unwrap() = std::sync::Arc::new(g.clone());
    }
}

pub fn tokens() -> std::sync::Arc<Vec<Vec<u8>>> {
    TOKEN_SNAPSHOT.get_or_init(|| Mutex::new(std::sync::Arc::new(vec![]))).lock().unwrap().clone()
}
