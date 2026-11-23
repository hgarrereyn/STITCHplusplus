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
    /// wall time of the run, worker start-up included
    pub elapsed_ms: u64,
    #[serde(skip)]
    pub output: String,
}

fn skip_frame(func: &str, loc: &str) -> bool {
    const PFX: &[&str] = &[
        "__rust_try", "std::panicking", "std::panic::", "core::ops::function", "core::panicking", "core::ptr::",
        "<core::", "<alloc::", "<std::", "alloc::", "__asan", "__interceptor", "___interceptor", "__sanitizer", "__ubsan", "__lsan", "stitch::", "stitch_", "std::",
        "__libc", "abort", "raise", "__pthread", "_start", "main", "libafl", "__cxa", "__gxx", "operator new",
        "operator delete", "pthread_kill", "gsignal", "__GI_", "__assert", "__interceptor_", "malloc", "calloc",
        "realloc", "free", "memcpy", "memmove", "memset", "strlen", "strcpy", "strcmp", "strncmp", "memcmp",
    ];
    if PFX.iter().any(|p| func.starts_with(p)) {
        return true;
    }
    loc.contains("/runtime/src/") || loc.contains("libc.so") || loc.contains("compiler-rt") || loc.contains("libstdc++")
        || loc.contains("/rustc/") || loc.contains("/stitch/rust/")
        // Swift: the standard library (mangled $sS / $ss), compiler-generated thunks, the Stitch package
        || loc.contains("<compiler-generated>") || loc.contains("/stitch/swift/") || loc.contains("/stitch/cpp-runtime/")
        || func.starts_with("$sS") || func.starts_with("$ss") || func.starts_with("$s6Stitch") || func == "worker_main"
        // Objective-C: the runtime (libobjc2), GNUstep Base and the Stitch layer
        || loc.contains("libobjc.so") || loc.contains("libgnustep-base") || loc.contains("/usr/src/gnustep/")
        || loc.contains("/stitch/objc/")
}

/// "-[Class(Category) selector:]" / "+[Class sel]": an Objective-C method name, kept whole
/// (it contains spaces and parentheses).
fn objc_method(func: &str) -> bool {
    (func.starts_with("-[") || func.starts_with("+[")) && func.ends_with(']')
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

/// A panic message as a bucket kind (same rule as the Go runtime's shortMsg).
fn go_short(msg: &str) -> String {
    let flat = msg.replace('\n', " ");
    let mut k: String = flat.chars().take(48).collect();
    if flat.chars().count() > 48 {
        if let Some(i) = k.rfind(' ').filter(|&i| i > 0) {
            k.truncate(i);
        }
    }
    normalize(&k.replace(": ", " - ")).trim().trim_end_matches(|c| c == ' ' || c == '-' || c == ':').to_string()
}

/// A Go panic or fatal error printed by the Go runtime (the worker died): kind and frames.
fn go_runtime_report(output: &str) -> Option<(String, Vec<String>)> {
    let lines: Vec<&str> = output.lines().collect();
    let head = lines.iter().position(|l| l.starts_with("panic: ") || l.starts_with("fatal error: "))?;
    let l = lines[head];
    let kind = if let Some(k) = l.strip_prefix("fatal error: ") {
        k.trim().to_string()
    } else {
        let msg = l["panic: ".len()..].trim();
        match msg.strip_prefix("runtime error: ") {
            Some(k) => {
                let k = k.split(|c| c == '[' || c == ':').next().unwrap_or(k).trim();
                k.strip_prefix("invalid memory address or ").unwrap_or(k).to_string()
            }
            None => format!("panic {}", go_short(msg)),
        }
    };
    let mut i = head + 1;
    while i < lines.len() && !(lines[i].starts_with("goroutine ") && lines[i].contains("[running")) {
        i += 1;
    }
    i += 1;
    let mut frames = vec![];
    while i + 1 < lines.len() && !lines[i].trim().is_empty() && frames.len() < 3 {
        let func = lines[i].trim();
        let loc = lines[i + 1].trim();
        i += 2;
        let name = func.rsplit_once('(').map(|x| x.0).unwrap_or(func);
        let name = if name.ends_with(')') || name.ends_with('.') { func.split("({").next().unwrap_or(func) } else { name };
        if name.starts_with("runtime.") || name.starts_with("reflect.") || name.starts_with("stitch.") || name.starts_with("main.")
            || func.contains("created by")
        {
            continue;
        }
        let short = name.rsplit('/').next().unwrap_or(name).replace("(*", "").replace(['(', ')'], "").replace("%2e", ".");
        let path = loc.split_whitespace().next().unwrap_or(loc);
        let file = path.rsplit('/').next().unwrap_or(path);
        frames.push(format!("{short} {file}"));
    }
    Some((kind, frames))
}

/// Demangle Swift names ($s...) in "func location" frames with swift-demangle, when it exists.
fn demangle_swift(frames: &mut [String]) {
    let names: Vec<String> =
        frames.iter().filter_map(|f| f.split(' ').next()).filter(|n| n.starts_with("$s")).map(String::from).collect();
    if names.is_empty() {
        return;
    }
    let Ok(out) = Command::new("swift-demangle").arg("--simplified").arg("--compact").args(&names).output() else { return };
    let text = String::from_utf8_lossy(&out.stdout);
    let demangled: Vec<&str> = text.lines().collect();
    if demangled.len() != names.len() {
        return;
    }
    let map: std::collections::HashMap<&str, &str> = names.iter().map(|s| s.as_str()).zip(demangled).collect();
    for f in frames.iter_mut() {
        let (func, rest) = f.split_once(' ').unwrap_or((f.as_str(), ""));
        if let Some(d) = map.get(func) {
            // "method(_:_:)" -> the name (the CLI cuts reproducer output the same way)
            let d = d.split('(').next().unwrap_or(d);
            *f = format!("{d} {rest}");
        }
    }
}

pub fn parse_report(output: &str, status_crashed: bool, timed_out: bool) -> Report {
    let mut kind = String::new();
    let mut summary = String::new();
    let mut frames = vec![];
    let mut endpoint = None;
    let mut node = None;
    let mut in_stack = false;
    let mut stack_done = false;
    // a language runtime reported an exception (Objective-C aborts after it, so a sanitizer's
    // ABRT report follows): the exception is the kind
    let mut lang_exception = false;
    // Swift: "Fatal error: <message>" (fatalError, -Onone checks) or a trap frame named
    // "Swift runtime failure: <message>" (-O checks): the message is the kind
    let mut swift_failure: Option<String> = None;
    for line in output.lines() {
        let l = line.trim();
        if let Some(i) = l.find("Fatal error: ") {
            swift_failure.get_or_insert_with(|| normalize(&l[i + "Fatal error: ".len()..]));
        }
        if let Some(i) = l.find(" in Swift runtime failure: ") {
            let msg = &l[i + " in Swift runtime failure: ".len()..];
            let msg = msg.rsplit_once(" <compiler-generated>").map(|x| x.0).unwrap_or(msg);
            let msg = msg.rsplit_once(" /").map(|x| x.0).unwrap_or(msg);
            swift_failure.get_or_insert_with(|| normalize(msg));
            continue;
        }
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
        if let Some(rest) = l.strip_prefix("[stitch] EXCEPTION in ") {
            // language runtimes: "<stub>: <kind>: <message>", then innermost-first frames
            let (ep, after) = rest.split_once(": ").unwrap_or((rest, ""));
            let k = after.split_once(": ").map(|x| x.0).unwrap_or(after);
            kind = if k.is_empty() { "exception".into() } else { normalize(k) };
            lang_exception = true;
            if endpoint.is_none() {
                endpoint = Some(ep.to_string());
            }
            summary = l.to_string();
            in_stack = true;
            continue;
        }
        if let Some(i) = l.find("ERROR: AddressSanitizer: ") {
            let rest = &l[i + "ERROR: AddressSanitizer: ".len()..];
            let k = rest.split_whitespace().next().unwrap_or("unknown");
            let k = if k == "SEGV" || rest.starts_with("SEGV") { "SEGV" } else { k };
            // (the ABRT of a runtime that aborts after reporting an exception is not the bug)
            if kind.is_empty() || !kind.starts_with("check failed") && !kind.starts_with("uncaught") && !(lang_exception && k == "ABRT") {
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
                    // Objective-C: the generated trampoline that calls the stub; the runtime is below
                    if func.starts_with("stitch_invoke_") {
                        stack_done = true;
                        continue;
                    }
                    let func = func.strip_prefix("void ").unwrap_or(func);
                    // Rust legacy mangling: drop the ::h<16 hex> suffix
                    let func = match func.rfind("::h") {
                        Some(i) if func.len() - i == 19 && func[i + 3..].chars().all(|c| c.is_ascii_hexdigit()) => &func[..i],
                        _ => func,
                    };
                    if !skip_frame(func, loc) && frames.len() < 3 {
                        let file = loc.rsplit('/').next().unwrap_or(loc);
                        let file = file.split(' ').next().unwrap_or(file);
                        let fname = if objc_method(func) { func } else { func.split('(').next().unwrap_or(func) };
                        frames.push(format!("{fname} {file}"));
                    }
                }
            } else if l.is_empty() && !frames.is_empty() {
                stack_done = true;
            }
        }
    }
    if kind.is_empty() && !output.contains("[stitch] EXCEPTION in ") {
        if let Some((k, f)) = go_runtime_report(output) {
            kind = k;
            frames = f;
        }
    }
    if let Some(f) = swift_failure {
        if kind.is_empty() || kind.starts_with("TRAP") || kind.starts_with("ILL") || kind == "ABRT" {
            kind = f;
        }
    }
    demangle_swift(&mut frames);
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
    // frames are "func location": drop ":col" (first frame) or ":line:col" from the location only,
    // since function names may contain "::"
    fn strip_pos(f: &str, keep_line: bool) -> String {
        let Some((func, loc)) = f.rsplit_once(' ') else { return f.to_string() };
        let mut parts: Vec<&str> = loc.split(':').collect();
        let keep = if keep_line { 2 } else { 1 };
        while parts.len() > keep && parts.last().map(|p| !p.is_empty() && p.chars().all(|c| c.is_ascii_digit())).unwrap_or(false) {
            parts.pop();
        }
        format!("{func} {}", parts.join(":"))
    }
    for (i, f) in frames.iter().take(3).enumerate() {
        signature.push_str(" | ");
        signature.push_str(&if i == 0 { strip_pos(f, true) } else { strip_pos(f, false) });
        signature_fn.push_str(" | ");
        signature_fn.push_str(&strip_pos(f, false));
    }
    Report { crashed, kind, endpoint, node, frames, signature, signature_fn, summary, elapsed_ms: 0, output: output.to_string() }
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
                elapsed_ms: 0,
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
    let mut r = parse_report(&out, crashed && !timed_out, timed_out);
    r.elapsed_ms = start.elapsed().as_millis() as u64;
    r
}

/// Time to start a worker and run a program with no calls (the fastest of three runs).
pub fn startup_time() -> Duration {
    let f = std::env::temp_dir().join(format!("stitch-empty-{}.stp", std::process::id()));
    let _ = std::fs::write(&f, Program::default().serialize());
    let t = (0..3).map(|_| run_file(&f, Duration::from_secs(30)).elapsed_ms).min().unwrap_or(0);
    let _ = std::fs::remove_file(&f);
    Duration::from_millis(t)
}

/// Run a program; with `hang` (the campaign's per-execution timeout), a program that finishes but
/// spends longer than `hang` in its calls is run again and stopped at that point, which reports a
/// timeout in the call that was running. (Saved timeouts would otherwise finish under the long
/// triage limit and be dropped.)
pub fn run_file_hang(file: &Path, timeout: Duration, hang: Option<(Duration, Duration)>) -> Report {
    let r = run_file(file, timeout);
    let Some((hang, startup)) = hang else { return r };
    if r.crashed || Duration::from_millis(r.elapsed_ms) <= startup + hang {
        return r;
    }
    let again = run_file(file, startup + hang);
    if again.crashed {
        again
    } else {
        r
    }
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
pub fn triage_dir(crash_dir: &Path, timeout: Duration, hang: Option<Duration>) -> Vec<Bucket> {
    let cache_path = crash_dir.parent().unwrap_or(crash_dir).join("triage-cache.json");
    let mut cache: BTreeMap<String, serde_json::Value> =
        std::fs::read(&cache_path).ok().and_then(|d| serde_json::from_slice(&d).ok()).unwrap_or_default();
    // results are only valid for the binary that produced them
    let exe_id = std::env::current_exe()
        .ok()
        .and_then(|p| std::fs::metadata(p).ok())
        .map(|m| format!("{}-{:?}-{:?}", m.len(), m.modified().ok(), hang))
        .unwrap_or_default();
    let mut startup: Option<Duration> = None;
    if cache.get("__binary__").and_then(|v| v.as_str()) != Some(exe_id.as_str()) {
        cache.clear();
        cache.insert("__binary__".into(), serde_json::Value::String(exe_id));
    }
    let mut files: Vec<PathBuf> = std::fs::read_dir(crash_dir)
        .map(|rd| rd.flatten().map(|e| e.path()).filter(|p| is_program_file(p)).collect())
        .unwrap_or_default();
    files.sort();
    // rerun the programs not in the cache, several at a time (each run is its own process);
    // STITCH_TRIAGE_JOBS overrides the default of min(cores, 8)
    let todo: Vec<&PathBuf> =
        files.iter().filter(|f| !cache.contains_key(&*f.file_name().unwrap().to_string_lossy())).collect();
    if !todo.is_empty() {
        let h = hang.map(|h| (h, *startup.get_or_insert_with(startup_time)));
        let jobs = std::env::var("STITCH_TRIAGE_JOBS")
            .ok()
            .and_then(|v| v.parse::<usize>().ok())
            .unwrap_or_else(|| std::thread::available_parallelism().map(|n| n.get()).unwrap_or(1).min(8))
            .max(1);
        let next = std::sync::atomic::AtomicUsize::new(0);
        let results = std::sync::Mutex::new(Vec::new());
        std::thread::scope(|sc| {
            for _ in 0..jobs.min(todo.len()) {
                sc.spawn(|| loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(f) = todo.get(i) else { break };
                    let r = run_file_hang(f, timeout, h);
                    results.lock().unwrap().push((f.file_name().unwrap().to_string_lossy().into_owned(), serde_json::to_value(&r).unwrap()));
                });
            }
        });
        for (k, v) in results.into_inner().unwrap() {
            cache.insert(k, v);
        }
    }
    let mut buckets: BTreeMap<String, Bucket> = BTreeMap::new();
    for f in files {
        let key = f.file_name().unwrap().to_string_lossy().into_owned();
        let rep: serde_json::Value = cache.get(&key).cloned().unwrap_or(serde_json::Value::Null);
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
pub fn minimize(prog: &Program, timeout: Duration, hang: Option<Duration>, budget: Duration) -> (Program, Report) {
    let tmp = std::env::temp_dir().join(format!("stitch-min-{}.stp", std::process::id()));
    let _ = std::fs::write(&tmp, prog.serialize());
    // a slow program (see run_file_hang) is minimized as a timeout at the campaign's limit
    let mut timeout = timeout;
    if let Some(h) = hang {
        let startup = startup_time();
        let r = run_file_hang(&tmp, timeout, Some((h, startup)));
        if r.kind == "timeout" && Duration::from_millis(r.elapsed_ms) < timeout {
            timeout = startup + h;
        }
    }
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
                let mut shrunk = false;
                for t in tries {
                    let mut cand = cur.clone();
                    cand.nodes[n].params[k].bytes = t;
                    if same(&test(&cand)) {
                        cur = cand;
                        changed = true;
                        shrunk = true;
                        break;
                    }
                }
                // 3. delta debugging on a variable-length buffer: remove chunks of len/2, len/4,
                // ..., 1 bytes anywhere (a long string whose crash needs something in the middle)
                if !fixed && !shrunk {
                    let mut chunk = cur.nodes[n].params[k].bytes.len() / 2;
                    while chunk >= 1 && start.elapsed() < budget {
                        let mut at = 0;
                        while at < cur.nodes[n].params[k].bytes.len() && start.elapsed() < budget {
                            let b = &cur.nodes[n].params[k].bytes;
                            let end = (at + chunk).min(b.len());
                            let mut t = b[..at].to_vec();
                            t.extend_from_slice(&b[end..]);
                            let mut cand = cur.clone();
                            cand.nodes[n].params[k].bytes = t;
                            if same(&test(&cand)) {
                                cur = cand; // retry the same position: the next chunk moved here
                                changed = true;
                            } else {
                                at += chunk;
                            }
                        }
                        chunk /= 2;
                    }
                }
            }
        }
    }
    let fin = test(&cur);
    let _ = std::fs::remove_file(&tmp);
    (cur, fin)
}
