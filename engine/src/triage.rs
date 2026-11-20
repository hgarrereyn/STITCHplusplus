//! Crash triage: run a program in a subprocess, parse the sanitizer report into a signature,
//! bucket crashes, and minimize reproducers.
use crate::program::Program;
use serde::Serialize;
use std::collections::BTreeMap;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

#[derive(Clone, Debug, Serialize, PartialEq)]
pub struct Report {
    pub crashed: bool,
    pub kind: String,
    pub endpoint: Option<String>,
    pub node: Option<usize>,
    pub frames: Vec<String>,
    pub signature: String,
    pub signature_fn: String,
    pub summary: String,
    #[serde(skip)]
    pub output: String,
}

fn skip_frame(func: &str, loc: &str) -> bool {
    const PFX: &[&str] = &[
        "__asan", "__interceptor", "___interceptor", "__sanitizer", "__ubsan", "__lsan", "stitch::", "stitch_", "std::",
        "__libc", "abort", "raise", "__pthread", "_start", "main", "libafl", "__cxa", "__gxx", "operator new",
        "operator delete", "pthread_kill", "gsignal", "__GI_", "__assert", "__interceptor_", "malloc", "calloc",
        "realloc", "free", "memcpy", "memmove", "memset", "strlen", "strcpy", "strcmp", "strncmp", "memcmp",
    ];
    if PFX.iter().any(|p| func.starts_with(p)) {
        return true;
    }
    loc.contains("/runtime/src/") || loc.contains("libc.so") || loc.contains("compiler-rt") || loc.contains("libstdc++")
}

fn normalize(msg: &str) -> String {
    // drop addresses and numbers so similar messages bucket together
    let mut o = String::new();
    let mut prev_digit = false;
    for c in msg.chars() {
        if c.is_ascii_digit() {
            if !prev_digit {
                o.push('N');
            }
            prev_digit = true;
        } else {
            prev_digit = false;
            o.push(c);
        }
    }
    o.replace("0xN", "ADDR").chars().take(120).collect()
}

pub fn parse_report(output: &str, status_crashed: bool, timed_out: bool) -> Report {
    let mut kind = String::new();
    let mut summary = String::new();
    let mut frames = vec![];
    let mut endpoint = None;
    let mut node = None;
    let mut in_stack = false;
    let mut stack_done = false;
    for line in output.lines() {
        let l = line.trim();
        if let Some(rest) = l.strip_prefix("[stitch] #") {
            if let Some((num, after)) = rest.split_once(' ') {
                if let Ok(n) = num.parse::<usize>() {
                    node = Some(n);
                    let name: String = after.chars().take_while(|c| *c != '(' && *c != ':').collect();
                    endpoint = Some(name);
                }
            }
            continue;
        }
        if let Some(rest) = l.strip_prefix("[stitch] CHECK FAILED: ") {
            let what = rest.rsplit_once(" (node").map(|x| x.0).unwrap_or(rest);
            kind = format!("check failed: {what}");
            summary = l.to_string();
            continue;
        }
        if let Some(rest) = l.strip_prefix("[stitch] UNCAUGHT EXCEPTION in ") {
            kind = format!("uncaught exception: {}", normalize(rest.split_once(": ").map(|x| x.1).unwrap_or(rest)));
            summary = l.to_string();
            continue;
        }
        if let Some(i) = l.find("ERROR: AddressSanitizer: ") {
            let rest = &l[i + "ERROR: AddressSanitizer: ".len()..];
            let k = rest.split_whitespace().next().unwrap_or("unknown");
            let k = if k == "SEGV" || rest.starts_with("SEGV") { "SEGV" } else { k };
            if kind.is_empty() || !kind.starts_with("check failed") && !kind.starts_with("uncaught") {
                kind = k.to_string();
                summary = l[i..].to_string();
            }
            in_stack = true;
            continue;
        }
        if l.contains("runtime error: ") && kind.is_empty() {
            let (loc, msg) = l.split_once(": runtime error: ").unwrap_or(("", l));
            kind = format!("ubsan: {}", normalize(msg));
            summary = l.to_string();
            let file = loc.rsplit('/').next().unwrap_or(loc);
            frames.push(format!("at {file}"));
            in_stack = true;
            continue;
        }
        if l.starts_with("READ of size") || l.starts_with("WRITE of size") {
            if !kind.contains(' ') && !kind.is_empty() {
                let rw = if l.starts_with("READ") { "READ" } else { "WRITE" };
                kind = format!("{kind} {rw}");
            }
            continue;
        }
        if in_stack && !stack_done {
            if let Some(rest) = l.strip_prefix('#') {
                // "#0 0x55 in func /path/file.c:12:3"
                let parts: Vec<&str> = rest.splitn(4, ' ').collect();
                if parts.len() >= 4 && parts[2] == "in" {
                    let tail = parts[3];
                    let (func, loc) = match tail.rfind(" /") {
                        Some(p) => (&tail[..p], &tail[p + 1..]),
                        None => match tail.rfind(" (") {
                            Some(p) => (&tail[..p], &tail[p + 1..]),
                            None => (tail, ""),
                        },
                    };
                    // stub bodies inlined into the reflection invoker: name the stub
                    if let Some(p) = func.find("Endpoint<&") {
                        let name: String =
                            func[p + 10..].chars().take_while(|c| c.is_alphanumeric() || *c == '_' || *c == ':').collect();
                        if frames.len() < 3 && !name.is_empty() {
                            frames.push(format!("{name} (stub)"));
                        }
                        continue;
                    }
                    let func = func.strip_prefix("void ").unwrap_or(func);
                    if !skip_frame(func, loc) && frames.len() < 3 {
                        let file = loc.rsplit('/').next().unwrap_or(loc);
                        let file = file.split(' ').next().unwrap_or(file);
                        let fname = func.split('(').next().unwrap_or(func);
                        frames.push(format!("{fname} {file}"));
                    }
                }
            } else if l.is_empty() && !frames.is_empty() {
                stack_done = true;
            }
        }
    }
    let crashed = status_crashed || timed_out || !kind.is_empty();
    if timed_out {
        kind = "timeout".into();
    } else if crashed && kind.is_empty() {
        kind = "crash (no sanitizer report)".into();
    }
    // bucket signature: kind + top frame with its line + next two frames (function + file);
    // `signature_fn` drops the line (stable across library versions, used by bisect)
    let mut signature_fn = kind.clone();
    if kind.starts_with("check failed") || kind.starts_with("uncaught") || kind == "timeout" || frames.is_empty() {
        if let Some(e) = &endpoint {
            signature_fn.push_str(&format!(" | in {e}"));
        }
    }
    let mut signature = signature_fn.clone();
    for (i, f) in frames.iter().take(3).enumerate() {
        let no_col = {
            let parts: Vec<&str> = f.rsplitn(3, ':').collect();
            if parts.len() == 3 && parts[0].chars().all(|c| c.is_ascii_digit()) { format!("{}:{}", parts[2], parts[1]) } else { f.clone() }
        };
        signature.push_str(" | ");
        signature.push_str(if i == 0 { &no_col } else { f.split(':').next().unwrap_or(f) });
        signature_fn.push_str(" | ");
        signature_fn.push_str(f.split(':').next().unwrap_or(f));
    }
    Report { crashed, kind, endpoint, node, frames, signature, signature_fn, summary, output: output.to_string() }
}

/// Run `self exec --trace FILE` in a subprocess and parse the outcome.
pub fn run_file(file: &Path, timeout: Duration) -> Report {
    let exe = std::env::current_exe().unwrap_or_else(|_| PathBuf::from("/proc/self/exe"));
    let mut child = match Command::new(exe)
        .arg("exec")
        .arg("--trace")
        .arg(file)
        .stdout(Stdio::null())
        .stderr(Stdio::piped())
        .spawn()
    {
        Ok(c) => c,
        Err(e) => {
            return Report {
                crashed: false,
                kind: format!("spawn failed: {e}"),
                endpoint: None,
                node: None,
                frames: vec![],
                signature: String::new(),
                signature_fn: String::new(),
                summary: String::new(),
                output: String::new(),
            }
        }
    };
    let mut stderr = child.stderr.take().unwrap();
    let reader = std::thread::spawn(move || {
        let mut s = vec![];
        let _ = stderr.read_to_end(&mut s);
        String::from_utf8_lossy(&s).into_owned()
    });
    let start = Instant::now();
    let mut timed_out = false;
    let status = loop {
        match child.try_wait() {
            Ok(Some(st)) => break Some(st),
            Ok(None) => {
                if start.elapsed() > timeout {
                    let _ = child.kill();
                    timed_out = true;
                    break child.wait().ok();
                }
                std::thread::sleep(Duration::from_millis(5));
            }
            Err(_) => break None,
        }
    };
    let output = reader.join().unwrap_or_default();
    let crashed = status.map(|s| !s.success()).unwrap_or(true);
    let mut out = output;
    if out.len() > 200_000 {
        out.truncate(200_000);
    }
    parse_report(&out, crashed && !timed_out, timed_out)
}

/// `*.stp` files, ignoring LibAFL's hidden lock files (`.name.stp`).
pub fn is_program_file(p: &Path) -> bool {
    p.extension().map(|x| x == "stp").unwrap_or(false)
        && !p.file_name().map(|n| n.to_string_lossy().starts_with('.')).unwrap_or(true)
}

#[derive(Serialize)]
pub struct Bucket {
    pub signature: String,
    pub kind: String,
    pub endpoint: Option<String>,
    pub frames: Vec<String>,
    pub summary: String,
    pub count: usize,
    pub example: String,
    pub example_nodes: usize,
    pub files: Vec<String>,
}

/// Bucket every program in `crash_dir` by signature. Results are cached per file in
/// `<crash_dir>/../triage-cache.json`.
pub fn triage_dir(crash_dir: &Path, timeout: Duration) -> Vec<Bucket> {
    let cache_path = crash_dir.parent().unwrap_or(crash_dir).join("triage-cache.json");
    let mut cache: BTreeMap<String, serde_json::Value> =
        std::fs::read(&cache_path).ok().and_then(|d| serde_json::from_slice(&d).ok()).unwrap_or_default();
    // results are only valid for the binary that produced them
    let exe_id = std::env::current_exe()
        .ok()
        .and_then(|p| std::fs::metadata(p).ok())
        .map(|m| format!("{}-{:?}", m.len(), m.modified().ok()))
        .unwrap_or_default();
    if cache.get("__binary__").and_then(|v| v.as_str()) != Some(exe_id.as_str()) {
        cache.clear();
        cache.insert("__binary__".into(), serde_json::Value::String(exe_id));
    }
    let mut files: Vec<PathBuf> = std::fs::read_dir(crash_dir)
        .map(|rd| rd.flatten().map(|e| e.path()).filter(|p| is_program_file(p)).collect())
        .unwrap_or_default();
    files.sort();
    let mut buckets: BTreeMap<String, Bucket> = BTreeMap::new();
    for f in files {
        let key = f.file_name().unwrap().to_string_lossy().into_owned();
        let rep: serde_json::Value = match cache.get(&key) {
            Some(v) => v.clone(),
            None => {
                let r = run_file(&f, timeout);
                let v = serde_json::to_value(&r).unwrap();
                cache.insert(key.clone(), v.clone());
                v
            }
        };
        if !rep["crashed"].as_bool().unwrap_or(false) {
            continue; // does not reproduce (flaky / timeout-only in fork mode)
        }
        let nodes = std::fs::read(&f).ok().and_then(|d| Program::deserialize(&d)).map(|p| p.nodes.len()).unwrap_or(0);
        let sig = rep["signature"].as_str().unwrap_or("").to_string();
        let b = buckets.entry(sig.clone()).or_insert_with(|| Bucket {
            signature: sig.clone(),
            kind: rep["kind"].as_str().unwrap_or("").into(),
            endpoint: rep["endpoint"].as_str().map(String::from),
            frames: rep["frames"].as_array().map(|a| a.iter().filter_map(|x| x.as_str().map(String::from)).collect()).unwrap_or_default(),
            summary: rep["summary"].as_str().unwrap_or("").into(),
            count: 0,
            example: key.clone(),
            example_nodes: usize::MAX,
            files: vec![],
        });
        b.count += 1;
        b.files.push(key.clone());
        if nodes < b.example_nodes {
            b.example_nodes = nodes;
            b.example = key.clone();
        }
    }
    let _ = std::fs::write(&cache_path, serde_json::to_vec(&cache).unwrap());
    let mut v: Vec<Bucket> = buckets.into_values().collect();
    v.sort_by(|a, b| b.count.cmp(&a.count));
    v
}

/// Greedy reduction keeping the same crash signature.
pub fn minimize(prog: &Program, timeout: Duration, budget: Duration) -> (Program, Report) {
    let tmp = std::env::temp_dir().join(format!("stitch-min-{}.stp", std::process::id()));
    let test = |p: &Program| -> Report {
        let _ = std::fs::write(&tmp, p.serialize());
        run_file(&tmp, timeout)
    };
    let base = test(prog);
    let mut cur = prog.clone();
    if !base.crashed {
        return (cur, base);
    }
    let start = Instant::now();
    let same = |r: &Report| r.crashed && r.signature == base.signature;
    let mut changed = true;
    while changed && start.elapsed() < budget {
        changed = false;
        // 1. drop calls (with their dependents), last first
        let mut i = cur.nodes.len();
        while i > 0 && start.elapsed() < budget {
            i -= 1;
            if i >= cur.nodes.len() {
                continue;
            }
            let mut cand = cur.clone();
            cand.remove_with_dependents(i);
            if cand.nodes.is_empty() {
                continue;
            }
            if same(&test(&cand)) {
                cur = cand;
                changed = true;
            }
        }
        // 2. shrink buffers
        for n in 0..cur.nodes.len() {
            for k in 0..cur.nodes[n].params.len() {
                if start.elapsed() >= budget {
                    break;
                }
                let len = cur.nodes[n].params[k].bytes.len();
                if len <= 1 {
                    continue;
                }
                let fixed = crate::manifest::get().endpoints[cur.nodes[n].ep as usize].params[k].fixed_size().is_some();
                let mut tries: Vec<Vec<u8>> = vec![];
                let b = &cur.nodes[n].params[k].bytes;
                if fixed {
                    if b.iter().any(|&x| x != 0) {
                        let mut z = b.clone();
                        for x in z.iter_mut().skip(1) {
                            *x = 0;
                        }
                        tries.push(z);
                    }
                } else {
                    tries.push(vec![]);
                    tries.push(b[..len / 2].to_vec());
                    tries.push(b[len / 2..].to_vec());
                    tries.push(b[..len - 1].to_vec());
                }
                for t in tries {
                    let mut cand = cur.clone();
                    cand.nodes[n].params[k].bytes = t;
                    if same(&test(&cand)) {
                        cur = cand;
                        changed = true;
                        break;
                    }
                }
            }
        }
    }
    let fin = test(&cur);
    let _ = std::fs::remove_file(&tmp);
    (cur, fin)
}
