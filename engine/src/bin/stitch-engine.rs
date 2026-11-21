//! `stitch-engine`: the fuzzing engine for worker runtimes (every language except C/C++).
//!
//! Implements the runtime ABI (`ffi.rs`) by forwarding to a worker process started from the
//! `STITCH_WORKER` shell command, then runs the same commands as the in-process engine:
//! `fuzz`, `exec`, `triage`, `minimize`, `sample`, `gen`, `manifest`.
use stitch_engine::{ffi, worker};
use std::ffi::{c_char, c_int, CStr, CString};
use std::sync::OnceLock;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let cmd = match std::env::var("STITCH_WORKER") {
        Ok(c) if !c.trim().is_empty() => c,
        _ => {
            eprintln!("error: STITCH_WORKER is not set (the shell command that starts the language runtime)");
            std::process::exit(2);
        }
    };
    if let Err(e) = worker::install(&cmd) {
        eprintln!("error: {e}");
        std::process::exit(1);
    }
    let code = stitch_engine::run(args);
    worker::shutdown();
    std::process::exit(code);
}

static MANIFEST: OnceLock<CString> = OnceLock::new();

#[no_mangle]
pub extern "C" fn stitch_manifest() -> *const c_char {
    MANIFEST.get_or_init(|| CString::new(worker::with(|c| c.manifest.clone())).unwrap_or_default()).as_ptr()
}

#[no_mangle]
pub extern "C" fn stitch_init_once() {}

#[no_mangle]
pub extern "C" fn stitch_exec(prog: *const u8, len: usize, flags: u32) -> c_int {
    let p = unsafe { std::slice::from_raw_parts(prog, len) };
    let cmplog = if ffi::cmplog().enabled != 0 { worker::FLAG_CMPLOG } else { 0 };
    match worker::with(|c| c.exec(p, (flags & 1) | cmplog, None)) {
        worker::Outcome::Ok => 0,
        // `exec` reports a finding like a crash: the runtime printed the report, exit non-zero
        _ => {
            worker::shutdown();
            std::process::exit(1);
        }
    }
}

#[no_mangle]
pub extern "C" fn stitch_run_file(path: *const c_char, flags: u32) -> c_int {
    let path = unsafe { CStr::from_ptr(path) }.to_string_lossy().into_owned();
    match std::fs::read(&path) {
        Ok(data) => stitch_exec(data.as_ptr(), data.len(), flags),
        Err(e) => {
            eprintln!("[stitch] cannot open {path}: {e}");
            -1
        }
    }
}

#[no_mangle]
pub extern "C" fn stitch_edges(n: *mut usize) -> *mut u8 {
    worker::with(|c| {
        if !n.is_null() {
            unsafe { *n = c.map_size };
        }
        c.edges_ptr()
    })
}

#[no_mangle]
pub extern "C" fn stitch_results() -> *mut ffi::Results {
    worker::with(|c| c.results_ptr())
}

#[no_mangle]
pub extern "C" fn stitch_cmplog() -> *mut ffi::Cmplog {
    worker::with(|c| c.cmplog_ptr())
}

#[no_mangle]
pub extern "C" fn stitch_cmplog_reset() {
    worker::with(|c| c.cmplog_reset())
}

fn manifest_datatypes() -> Vec<bool> {
    let m: serde_json::Value = serde_json::from_str(&worker::with(|c| c.manifest.clone())).unwrap_or_default();
    m["datatypes"].as_array().map(|a| a.iter().map(|d| d["decoder"].as_bool().unwrap_or(false)).collect()).unwrap_or_default()
}

#[no_mangle]
pub extern "C" fn stitch_datatype_count() -> c_int {
    manifest_datatypes().len() as c_int
}

#[no_mangle]
pub extern "C" fn stitch_has_decoder(dt: c_int) -> c_int {
    manifest_datatypes().get(dt as usize).copied().unwrap_or(false) as c_int
}

#[no_mangle]
pub extern "C" fn stitch_decode(dt: c_int, choices: *const u8, n: usize, out: *mut u8, cap: usize) -> usize {
    let ch = unsafe { std::slice::from_raw_parts(choices, n) };
    let v = worker::with(|c| c.decode(dt as u32, ch, cap));
    let k = v.len().min(cap);
    unsafe { std::ptr::copy_nonoverlapping(v.as_ptr(), out, k) };
    k
}

#[no_mangle]
pub extern "C" fn stitch_generate(dt: c_int, seed: u64, prefix: *const u8, plen: usize, out: *mut u8, cap: usize) -> usize {
    let pre = unsafe { std::slice::from_raw_parts(prefix, plen) };
    let v = worker::with(|c| c.generate(dt as u32, seed, pre, cap));
    let k = v.len().min(cap);
    unsafe { std::ptr::copy_nonoverlapping(v.as_ptr(), out, k) };
    k
}

#[no_mangle]
pub extern "C" fn stitch_dict_add(dt: c_int, tok: *const u8, len: usize) {
    let t = unsafe { std::slice::from_raw_parts(tok, len) };
    worker::with(|c| c.dict_add(dt as u32, t))
}

#[no_mangle]
pub extern "C" fn stitch_decode_spans(dt: c_int, choices: *const u8, n: usize, out: *mut u32, max_spans: usize) -> usize {
    let ch = unsafe { std::slice::from_raw_parts(choices, n) };
    let v = worker::with(|c| c.spans(dt as u32, ch));
    let k = (v.len() / 3).min(max_spans);
    unsafe { std::ptr::copy_nonoverlapping(v.as_ptr(), out, 3 * k) };
    k
}

#[no_mangle]
pub extern "C" fn stitch_set_plan(path: *const c_char) {
    let p = if path.is_null() { None } else { Some(unsafe { CStr::from_ptr(path) }.to_string_lossy().into_owned()) };
    worker::with(|c| c.set_plan(p.as_deref()))
}
