//! In-process fuzzing: the C ABI of languages/cpp/runtime/include/stitch_abi.h, so the engine
//! (libstitch_engine.so, loaded at startup) drives this harness the way it drives C/C++ ones:
//! a persistent fork server, with results, edges and comparisons in shared memory.
//!
//! `STITCH_NATIVE_ENGINE=1 worker fuzz|exec|triage|...` runs the engine in this process. Without
//! it the binary is the worker of the pipe protocol (rt::worker) or the reproducer CLI.
#![allow(static_mut_refs, clippy::missing_safety_doc)]
use crate::{cov, rt};
use std::ffi::{c_char, c_int, CStr, CString};

pub const EXEC_FINDING: i32 = 2;
const MAX_NODES: usize = 256;
const STRINGS_SIZE: usize = 65536;
const CMPLOG_RECORDS: usize = 16384;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NodeResult {
    pub status: u8,
    pub n_out: u8,
    pub out_mask: u16,
    pub reason: u32,
    pub miss_key: u32,
    pub consumed: u32,
}

#[repr(C)]
pub struct Results {
    pub magic: u32,
    pub n_nodes: u32,
    pub current: i32,
    pub str_used: u32,
    pub crash_pc: u64,
    pub nodes: [NodeResult; MAX_NODES],
    pub strings: [u8; STRINGS_SIZE],
}

#[repr(C)]
pub struct Cmplog {
    pub enabled: u32,
    pub count: u32,
    pub records: [[u8; 72]; CMPLOG_RECORDS],
}

static mut NATIVE: bool = false;
static mut RESULTS: *mut Results = std::ptr::null_mut();
static mut CMPLOG: *mut Cmplog = std::ptr::null_mut();
static mut MANIFEST: Option<CString> = None;

pub fn native() -> bool {
    unsafe { NATIVE }
}

pub fn results_shm() -> &'static mut Results {
    unsafe {
        if RESULTS.is_null() {
            RESULTS = cov::shm(std::mem::size_of::<Results>()) as *mut Results;
        }
        &mut *RESULTS
    }
}

fn cmplog_shm() -> &'static mut Cmplog {
    unsafe {
        if CMPLOG.is_null() {
            CMPLOG = cov::shm(std::mem::size_of::<Cmplog>()) as *mut Cmplog;
        }
        &mut *CMPLOG
    }
}

#[inline]
pub fn cmplog_enabled() -> bool {
    unsafe { !CMPLOG.is_null() && (*CMPLOG).enabled != 0 }
}

pub fn cmplog_full() -> bool {
    unsafe { !CMPLOG.is_null() && (*CMPLOG).count as usize >= CMPLOG_RECORDS }
}

pub fn cmplog_push(rec: &[u8; 72]) {
    let c = cmplog_shm();
    if (c.count as usize) < CMPLOG_RECORDS {
        c.records[c.count as usize] = *rec;
        c.count += 1;
    }
}

pub fn set_current(i: i32) {
    if native() {
        results_shm().current = i;
    }
}

/// Load the engine and hand it the process (argv as given).
pub fn engine_main() -> i32 {
    extern "C" {
        fn dlopen(file: *const c_char, mode: c_int) -> *mut u8;
        fn dlsym(h: *mut u8, name: *const c_char) -> *mut u8;
        fn dlerror() -> *const c_char;
    }
    unsafe {
        NATIVE = true;
        cov::set_native(true);
        results_shm();
        cmplog_shm();
        // keep the ABI in the binary (only the engine library refers to it)
        std::hint::black_box([
            stitch_manifest as *const u8, stitch_init_once as *const u8, stitch_exec as *const u8,
            stitch_run_file as *const u8, stitch_edges as *const u8, stitch_results as *const u8,
            stitch_cmplog as *const u8, stitch_cmplog_reset as *const u8, stitch_datatype_count as *const u8,
            stitch_has_decoder as *const u8, stitch_decode as *const u8, stitch_generate as *const u8,
            stitch_dict_add as *const u8, stitch_decode_spans as *const u8, stitch_set_plan as *const u8,
        ]);
        let path = std::env::var("STITCH_ENGINE_LIB").unwrap_or_else(|_| "/stitch/lib/libstitch_engine.so".into());
        let cpath = CString::new(path.clone()).unwrap();
        // RTLD_NOW | RTLD_GLOBAL
        let h = dlopen(cpath.as_ptr(), 0x2 | 0x100);
        if h.is_null() {
            let e = dlerror();
            let e = if e.is_null() { "?".into() } else { CStr::from_ptr(e).to_string_lossy().into_owned() };
            eprintln!("error: cannot load the STITCH++ engine ({path}): {e}");
            return 2;
        }
        let f = dlsym(h, c"stitch_engine_main".as_ptr());
        if f.is_null() {
            eprintln!("error: {path} has no stitch_engine_main");
            return 2;
        }
        let main: extern "C" fn(c_int, *const *const c_char) -> c_int = std::mem::transmute(f);
        let args: Vec<CString> = std::env::args().map(|a| CString::new(a).unwrap_or_default()).collect();
        let mut argv: Vec<*const c_char> = args.iter().map(|a| a.as_ptr()).collect();
        argv.push(std::ptr::null());
        main(args.len() as c_int, argv.as_ptr())
    }
}

#[no_mangle]
pub extern "C" fn stitch_manifest() -> *const c_char {
    unsafe { MANIFEST.get_or_insert_with(|| CString::new(rt::manifest_json()).unwrap_or_default()).as_ptr() }
}

#[no_mangle]
pub extern "C" fn stitch_init_once() {}

#[no_mangle]
pub unsafe extern "C" fn stitch_exec(prog: *const u8, len: usize, flags: u32) -> c_int {
    let data = std::slice::from_raw_parts(prog, len);
    let trace = flags & 1 != 0;
    let rc = rt::exec_native(data, trace);
    if rc == EXEC_FINDING && (trace || !rt::fuzzing()) {
        // `exec` / triage: the report is printed; a finding ends the process like a crash
        std::process::exit(1);
    }
    rc
}

#[no_mangle]
pub unsafe extern "C" fn stitch_run_file(path: *const c_char, flags: u32) -> c_int {
    let path = CStr::from_ptr(path).to_string_lossy().into_owned();
    match std::fs::read(&path) {
        Ok(d) => stitch_exec(d.as_ptr(), d.len(), flags),
        Err(e) => {
            eprintln!("[stitch] cannot open {path}: {e}");
            -1
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn stitch_edges(n: *mut usize) -> *mut u8 {
    let (p, len) = cov::edges();
    if !n.is_null() {
        *n = len;
    }
    p
}

#[no_mangle]
pub extern "C" fn stitch_results() -> *mut Results {
    results_shm()
}

#[no_mangle]
pub extern "C" fn stitch_cmplog() -> *mut Cmplog {
    cmplog_shm()
}

#[no_mangle]
pub extern "C" fn stitch_cmplog_reset() {
    cmplog_shm().count = 0;
}

#[no_mangle]
pub extern "C" fn stitch_datatype_count() -> c_int {
    rt::datatype_info(None).0 as c_int
}

#[no_mangle]
pub extern "C" fn stitch_has_decoder(dt: c_int) -> c_int {
    rt::datatype_info(Some(dt as usize)).1 as c_int
}

#[no_mangle]
pub unsafe extern "C" fn stitch_decode(dt: c_int, choices: *const u8, n: usize, out: *mut u8, cap: usize) -> usize {
    let v = rt::decode(dt as usize, std::slice::from_raw_parts(choices, n), cap);
    let k = v.len().min(cap);
    std::ptr::copy_nonoverlapping(v.as_ptr(), out, k);
    k
}

#[no_mangle]
pub unsafe extern "C" fn stitch_generate(dt: c_int, seed: u64, prefix: *const u8, plen: usize, out: *mut u8, cap: usize) -> usize {
    let pre = if plen == 0 { &[][..] } else { std::slice::from_raw_parts(prefix, plen) };
    let v = rt::generate(dt as usize, seed, &pre[..plen.min(cap)], cap);
    let k = v.len().min(cap);
    std::ptr::copy_nonoverlapping(v.as_ptr(), out, k);
    k
}

#[no_mangle]
pub unsafe extern "C" fn stitch_dict_add(dt: c_int, tok: *const u8, len: usize) {
    rt::dict_add(dt as usize, std::slice::from_raw_parts(tok, len));
}

#[no_mangle]
pub unsafe extern "C" fn stitch_decode_spans(dt: c_int, choices: *const u8, n: usize, out: *mut u32, max_spans: usize) -> usize {
    let v = rt::spans(dt as usize, std::slice::from_raw_parts(choices, n));
    let k = (v.len() / 3).min(max_spans);
    std::ptr::copy_nonoverlapping(v.as_ptr(), out, 3 * k);
    k
}

#[no_mangle]
pub unsafe extern "C" fn stitch_set_plan(path: *const c_char) {
    let p = if path.is_null() { String::new() } else { CStr::from_ptr(path).to_string_lossy().into_owned() };
    rt::set_plan(&p);
}
