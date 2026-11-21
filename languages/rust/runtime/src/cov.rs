//! SanitizerCoverage callbacks (edges and comparisons). The harness and the library are compiled
//! with `-Cpasses=sancov-module` (trace-pc-guard, trace-compares) by the stitch-rustc wrapper;
//! this crate is not instrumented. Under ASan, the memcmp/strcmp interceptors call the weak hooks.
//!
//! The edge map and (in-process mode) the comparison log live in shared memory, so the engine's
//! fork server reads what a forked worker wrote (see abi.rs).
#![allow(static_mut_refs, clippy::missing_safety_doc)]

static mut EDGES: *mut u8 = std::ptr::null_mut();
static mut EDGES_LEN: usize = 0;
static mut N_GUARDS: u32 = 0;
/// In-process mode: comparisons go straight into the engine-visible log (abi::cmplog_shm()).
static mut NATIVE: bool = false;
static mut CMPLOG: bool = false;
static mut IN_STUB: bool = false;
static mut NODE: u16 = 0;
static mut CMPS: Vec<u8> = Vec::new();
static mut NCMPS: u32 = 0;
/// Comparisons already logged in this run: a bitmap of hashes (no allocation; cleared per run).
static mut SEEN: [u64; 4096] = [0; 4096];
static mut SEEN_DIRTY: bool = false;
/// Set while logging: the logger's own comparisons (memcmp, ...) are not logged.
static mut RECORDING: bool = false;

pub fn map_size() -> usize {
    unsafe { (N_GUARDS as usize + 1).max(1) }
}

/// Shared anonymous memory (visible to the parent after fork).
pub fn shm(n: usize) -> *mut u8 {
    extern "C" {
        fn mmap(addr: *mut u8, len: usize, prot: i32, flags: i32, fd: i32, off: i64) -> *mut u8;
    }
    // PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS (Linux)
    let p = unsafe { mmap(std::ptr::null_mut(), n.max(1), 3, 0x01 | 0x20, -1, 0) };
    if p as isize == -1 {
        eprintln!("[stitch] mmap of {n} bytes failed");
        std::process::abort();
    }
    p
}

/// The edge map: one counter per instrumented edge, index 0 unused.
pub fn edges() -> (*mut u8, usize) {
    unsafe {
        if EDGES.is_null() {
            EDGES_LEN = map_size();
            EDGES = shm(EDGES_LEN);
        }
        (EDGES, EDGES_LEN)
    }
}

pub fn set_native(on: bool) {
    unsafe { NATIVE = on }
}

pub fn set_cmplog(on: bool) {
    unsafe { CMPLOG = on }
}

pub fn set_in_stub(on: bool) {
    unsafe { IN_STUB = on }
}

pub fn set_node(i: usize) {
    unsafe { NODE = i as u16 }
}

pub fn begin_exec() {
    unsafe {
        // in-process, the engine clears the map before each run
        if !NATIVE && !EDGES.is_null() {
            std::ptr::write_bytes(EDGES, 0, EDGES_LEN);
        }
        CMPS.clear();
        NCMPS = 0;
        if SEEN_DIRTY {
            SEEN.fill(0);
            SEEN_DIRTY = false;
        }
    }
}

pub fn edges_bytes() -> Vec<u8> {
    let mut out = vec![0u8; 4];
    let mut n = 0u32;
    unsafe {
        if !EDGES.is_null() {
            let m = std::slice::from_raw_parts_mut(EDGES, EDGES_LEN);
            // 8 bytes at a time: most of the map is zero
            let mut i = 0;
            while i < m.len() {
                if i + 8 <= m.len() && u64::from_ne_bytes(m[i..i + 8].try_into().unwrap()) == 0 {
                    i += 8;
                    continue;
                }
                if m[i] != 0 {
                    out.extend_from_slice(&(i as u32).to_le_bytes());
                    out.push(m[i]);
                    m[i] = 0;
                    n += 1;
                }
                i += 1;
            }
        }
    }
    out[..4].copy_from_slice(&n.to_le_bytes());
    out
}

pub fn cmps_bytes() -> Vec<u8> {
    unsafe {
        let mut out = NCMPS.to_le_bytes().to_vec();
        out.extend_from_slice(&CMPS);
        out
    }
}

#[inline]
fn on() -> bool {
    unsafe { (CMPLOG || (NATIVE && crate::abi::cmplog_enabled())) && IN_STUB }
}

fn record(kind: u8, size: u8, a: &[u8], b: &[u8]) {
    unsafe {
        if RECORDING || NCMPS >= 16384 || (NATIVE && crate::abi::cmplog_full()) {
            return;
        }
        let (la, lb) = (a.len().min(32), b.len().min(32));
        if la == 0 || lb == 0 {
            return;
        }
        // FNV-1a over (node, a, b): a run logs each distinct operand pair once
        let mut h: u32 = 2166136261;
        for &c in NODE.to_le_bytes().iter().chain(&a[..la]).chain([0xffu8].iter()).chain(&b[..lb]) {
            h = (h ^ c as u32).wrapping_mul(16777619);
        }
        let bit = (h as usize) & (4096 * 64 - 1);
        if SEEN[bit / 64] & (1 << (bit % 64)) != 0 {
            return;
        }
        SEEN[bit / 64] |= 1 << (bit % 64);
        SEEN_DIRTY = true;
        RECORDING = true;
        let mut r = [0u8; 72];
        r[0..2].copy_from_slice(&NODE.to_le_bytes());
        r[2] = kind;
        r[3] = size;
        r[4] = la as u8;
        r[5] = lb as u8;
        r[8..8 + la].copy_from_slice(&a[..la]);
        r[40..40 + lb].copy_from_slice(&b[..lb]);
        if NATIVE {
            crate::abi::cmplog_push(&r);
        } else {
            CMPS.extend_from_slice(&r);
            NCMPS += 1;
        }
        RECORDING = false;
    }
}

fn rec_int(a: u64, b: u64, size: u8) {
    if a != b && on() {
        record(0, size, &a.to_le_bytes(), &b.to_le_bytes());
    }
}

#[no_mangle]
pub unsafe extern "C" fn __sanitizer_cov_trace_pc_guard_init(start: *mut u32, stop: *mut u32) {
    if start == stop || *start != 0 {
        return;
    }
    let mut p = start;
    while p < stop {
        N_GUARDS += 1;
        *p = N_GUARDS;
        p = p.add(1);
    }
    // (constructors, before main: grow the map, keeping counts already recorded)
    let n = N_GUARDS as usize + 1;
    let m = shm(n);
    if !EDGES.is_null() {
        std::ptr::copy_nonoverlapping(EDGES, m, EDGES_LEN.min(n));
    }
    EDGES = m;
    EDGES_LEN = n;
}

#[no_mangle]
pub unsafe extern "C" fn __sanitizer_cov_trace_pc_guard(guard: *mut u32) {
    let i = *guard as usize;
    if i < EDGES_LEN {
        let e = EDGES.add(i);
        *e = (*e).wrapping_add(1).max(1);
    }
}

#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_cmp1(a: u8, b: u8) { rec_int(a as u64, b as u64, 1) }
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_cmp2(a: u16, b: u16) { rec_int(a as u64, b as u64, 2) }
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_cmp4(a: u32, b: u32) { rec_int(a as u64, b as u64, 4) }
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_cmp8(a: u64, b: u64) { rec_int(a, b, 8) }
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_const_cmp1(a: u8, b: u8) { rec_int(a as u64, b as u64, 1) }
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_const_cmp2(a: u16, b: u16) { rec_int(a as u64, b as u64, 2) }
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_const_cmp4(a: u32, b: u32) { rec_int(a as u64, b as u64, 4) }
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_const_cmp8(a: u64, b: u64) { rec_int(a, b, 8) }

#[no_mangle]
pub unsafe extern "C" fn __sanitizer_cov_trace_switch(val: u64, cases: *const u64) {
    if !on() {
        return;
    }
    let n = *cases;
    let bits = *cases.add(1);
    let size = (bits / 8).max(1) as u8;
    for i in 0..n.min(64) {
        rec_int(*cases.add(2 + i as usize), val, size);
    }
}

#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_div4(_: u32) {}
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_div8(_: u64) {}
#[no_mangle]
pub extern "C" fn __sanitizer_cov_trace_gep(_: usize) {}

#[no_mangle]
pub unsafe extern "C" fn __sanitizer_weak_hook_memcmp(_pc: *const u8, s1: *const u8, s2: *const u8, n: usize, result: i32) {
    if result != 0 && on() && !s1.is_null() && !s2.is_null() {
        let k = n.min(32);
        record(1, 0, std::slice::from_raw_parts(s1, k), std::slice::from_raw_parts(s2, k));
    }
}

#[no_mangle]
pub unsafe extern "C" fn __sanitizer_weak_hook_bcmp(pc: *const u8, s1: *const u8, s2: *const u8, n: usize, result: i32) {
    __sanitizer_weak_hook_memcmp(pc, s1, s2, n, result)
}
