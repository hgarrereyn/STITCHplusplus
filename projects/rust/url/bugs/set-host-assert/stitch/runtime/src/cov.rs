//! SanitizerCoverage callbacks (edges and comparisons). The harness and the library are compiled
//! with `-Cpasses=sancov-module` (trace-pc-guard, trace-compares) by the stitch-rustc wrapper;
//! this crate is not instrumented. Under ASan, the memcmp/strcmp interceptors call the weak hooks.
#![allow(static_mut_refs, clippy::missing_safety_doc)]

static mut EDGES: Vec<u8> = Vec::new();
static mut N_GUARDS: u32 = 0;
static mut CMPLOG: bool = false;
static mut IN_STUB: bool = false;
static mut NODE: u16 = 0;
static mut CMPS: Vec<u8> = Vec::new();
static mut NCMPS: u32 = 0;
static mut SEEN: Option<std::collections::HashSet<(u16, Vec<u8>, Vec<u8>)>> = None;

pub fn map_size() -> usize {
    unsafe { (N_GUARDS as usize + 1).max(1) }
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
        EDGES.iter_mut().for_each(|e| *e = 0);
        CMPS.clear();
        NCMPS = 0;
        SEEN.get_or_insert_with(Default::default).clear();
    }
}

pub fn edges_bytes() -> Vec<u8> {
    let mut out = vec![0u8; 4];
    let mut n = 0u32;
    unsafe {
        for (i, e) in EDGES.iter_mut().enumerate() {
            if *e != 0 {
                out.extend_from_slice(&(i as u32).to_le_bytes());
                out.push(*e);
                *e = 0;
                n += 1;
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
    unsafe { CMPLOG && IN_STUB }
}

fn record(kind: u8, size: u8, a: &[u8], b: &[u8]) {
    unsafe {
        if NCMPS >= 16384 {
            return;
        }
        let (la, lb) = (a.len().min(32), b.len().min(32));
        if la == 0 || lb == 0 {
            return;
        }
        let seen = SEEN.get_or_insert_with(Default::default);
        if !seen.insert((NODE, a[..la].to_vec(), b[..lb].to_vec())) {
            return;
        }
        let mut r = [0u8; 72];
        r[0..2].copy_from_slice(&NODE.to_le_bytes());
        r[2] = kind;
        r[3] = size;
        r[4] = la as u8;
        r[5] = lb as u8;
        r[8..8 + la].copy_from_slice(&a[..la]);
        r[40..40 + lb].copy_from_slice(&b[..lb]);
        CMPS.extend_from_slice(&r);
        NCMPS += 1;
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
    EDGES.resize(N_GUARDS as usize + 1, 0);
}

#[no_mangle]
pub unsafe extern "C" fn __sanitizer_cov_trace_pc_guard(guard: *mut u32) {
    let i = *guard as usize;
    if let Some(e) = EDGES.get_mut(i) {
        *e = e.wrapping_add(1).max(1);
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
