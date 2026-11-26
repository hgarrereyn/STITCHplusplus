//! C ABI of the STITCH++ runtime (see languages/cpp/runtime/include/stitch_abi.h).
#![allow(dead_code)]
use std::ffi::{c_char, c_int, CStr};

pub const MAX_NODES: usize = 256;
pub const STRINGS_SIZE: usize = 65536;
pub const CMPLOG_RECORDS: usize = 16384;
pub const EDGES_SIZE: usize = 1 << 21;

pub const ST_NOT_RUN: u8 = 0;
pub const ST_OK: u8 = 1;
pub const ST_BAIL: u8 = 2;
pub const ST_SKIP_MISSING: u8 = 3;
pub const ST_SKIP_INVALID: u8 = 4;

/// stitch_exec() return value: the runtime caught a bug (panic, failed check) and can keep running.
/// The fork server counts it as a crash without replacing the worker.
pub const EXEC_FINDING: c_int = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
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
#[derive(Clone, Copy)]
pub struct CmpRecord {
    pub node: u16,
    pub kind: u8,
    pub size: u8,
    pub len_a: u8,
    pub len_b: u8,
    pub a_const: u8,
    pub pad: u8,
    pub a: [u8; 32],
    pub b: [u8; 32],
}

#[repr(C)]
pub struct Cmplog {
    pub enabled: u32,
    pub count: u32,
    pub records: [CmpRecord; CMPLOG_RECORDS],
}

extern "C" {
    pub fn stitch_manifest() -> *const c_char;
    pub fn stitch_init_once();
    pub fn stitch_exec(prog: *const u8, len: usize, flags: u32) -> c_int;
    pub fn stitch_edges(n: *mut usize) -> *mut u8;
    pub fn stitch_results() -> *mut Results;
    pub fn stitch_cmplog() -> *mut Cmplog;
    pub fn stitch_cmplog_reset();
    pub fn stitch_datatype_count() -> c_int;
    pub fn stitch_has_decoder(dt: c_int) -> c_int;
    pub fn stitch_decode(dt: c_int, choices: *const u8, n: usize, out: *mut u8, cap: usize) -> usize;
    pub fn stitch_generate(dt: c_int, seed: u64, prefix: *const u8, plen: usize, out: *mut u8, cap: usize) -> usize;
    pub fn stitch_dict_add(dt: c_int, tok: *const u8, len: usize);
    pub fn stitch_decode_spans(dt: c_int, choices: *const u8, n: usize, out: *mut u32, max_spans: usize) -> usize;
    pub fn stitch_decode_items(dt: c_int, choices: *const u8, n: usize, out: *mut u32, max_items: usize) -> usize;
    pub fn stitch_run_file(path: *const c_char, flags: u32) -> c_int;
    pub fn stitch_set_plan(path: *const c_char);
}

pub fn manifest_json() -> String {
    unsafe { CStr::from_ptr(stitch_manifest()).to_string_lossy().into_owned() }
}

pub fn results() -> &'static Results {
    unsafe { &*stitch_results() }
}

pub fn cmplog() -> &'static mut Cmplog {
    unsafe { &mut *stitch_cmplog() }
}

/// Parse the string table written by the child for this execution.
pub fn result_strings(r: &Results) -> Vec<(u32, String)> {
    let mut out = vec![];
    let used = (r.str_used as usize).min(STRINGS_SIZE);
    let mut off = 0;
    while off + 6 <= used {
        let h = u32::from_le_bytes(r.strings[off..off + 4].try_into().unwrap());
        let l = u16::from_le_bytes(r.strings[off + 4..off + 6].try_into().unwrap()) as usize;
        if off + 6 + l > used {
            break;
        }
        out.push((h, String::from_utf8_lossy(&r.strings[off + 6..off + 6 + l]).into_owned()));
        off += 6 + l;
    }
    out
}

pub fn decode(dt: usize, choices: &[u8], cap: usize) -> Vec<u8> {
    let mut out = vec![0u8; cap.max(1)];
    let n = unsafe { stitch_decode(dt as c_int, choices.as_ptr(), choices.len(), out.as_mut_ptr(), cap) };
    out.truncate(n);
    out
}

pub fn generate(dt: usize, seed: u64, prefix: &[u8], cap: usize) -> Vec<u8> {
    let mut out = vec![0u8; cap.max(1)];
    let n = unsafe { stitch_generate(dt as c_int, seed, prefix.as_ptr(), prefix.len(), out.as_mut_ptr(), cap) };
    out.truncate(n);
    out
}

pub fn dict_add(dt: usize, tok: &[u8]) {
    unsafe { stitch_dict_add(dt as c_int, tok.as_ptr(), tok.len()) }
    // workers forked by the persistent executor's zygote get it too
    crate::persistent::forward_dict(dt, tok);
}

/// (prefix offset, prefix width, content length) of every string drawn by the decoder.
pub fn decode_spans(dt: usize, choices: &[u8]) -> Vec<(usize, usize, usize)> {
    let mut out = vec![0u32; 3 * 512];
    let k = unsafe { stitch_decode_spans(dt as c_int, choices.as_ptr(), choices.len(), out.as_mut_ptr(), 512) };
    (0..k).map(|i| (out[3 * i] as usize, out[3 * i + 1] as usize, out[3 * i + 2] as usize)).collect()
}

/// One `more()` loop iteration found by replaying the decoder: bytes [start, end) of the choice
/// buffer, starting with the iteration's `more()` byte. `site` identifies the loop in the decoder
/// source (stable for the run); `inst` groups the iterations of one execution of the loop. A record
/// with start == end marks where that loop execution ended (its final `more()` byte, or the end of
/// the buffer): new iterations can be inserted there.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Item {
    pub site: u32,
    pub inst: u32,
    pub start: usize,
    pub end: usize,
}

pub fn decode_items(dt: usize, choices: &[u8]) -> Vec<Item> {
    let mut out = vec![0u32; 4 * 1024];
    let k = unsafe { stitch_decode_items(dt as c_int, choices.as_ptr(), choices.len(), out.as_mut_ptr(), 1024) };
    (0..k)
        .map(|i| Item { site: out[4 * i], inst: out[4 * i + 1], start: out[4 * i + 2] as usize, end: out[4 * i + 3] as usize })
        .filter(|it| it.start <= it.end && it.end <= choices.len())
        .collect()
}
