//! The engine's own heap, outside AddressSanitizer.
//!
//! C/C++ (and Swift, Objective-C) harnesses link the engine into an ASan binary, where `malloc` is
//! ASan's: every engine allocation would pay for redzones, shadow poisoning, stack depot lookups and
//! the free quarantine. Worse, the quarantine cycles through megabytes of heap, and the fuzzer
//! process re-forks its persistent worker every few hundred executions: each page it writes after a
//! fork is copied (copy-on-write), so the quarantine turned into a steady stream of page faults.
//!
//! The engine is not the code under test, so its Rust allocations go straight to glibc's allocator
//! through `__libc_malloc` and friends (exported by glibc, not intercepted by ASan). The library
//! under test keeps ASan's allocator. Memory never crosses between the two: the runtime only reads
//! or fills engine buffers it is handed, and the engine never frees runtime memory.
use std::alloc::{GlobalAlloc, Layout};

extern "C" {
    fn __libc_malloc(size: usize) -> *mut u8;
    fn __libc_calloc(n: usize, size: usize) -> *mut u8;
    fn __libc_realloc(p: *mut u8, size: usize) -> *mut u8;
    fn __libc_free(p: *mut u8);
}

/// glibc's malloc returns 16-byte aligned memory on 64-bit targets.
const MALLOC_ALIGN: usize = 16;

pub struct EngineAlloc;

#[inline]
fn plain(l: &Layout) -> bool {
    l.align() <= MALLOC_ALIGN && l.align() <= l.size()
}

/// Over-aligned allocation: allocate extra, align up, keep the raw pointer just below.
unsafe fn alloc_aligned(l: Layout, zero: bool) -> *mut u8 {
    let Some(total) = l.size().checked_add(l.align()) else { return std::ptr::null_mut() };
    let raw = if zero { __libc_calloc(1, total) } else { __libc_malloc(total) };
    if raw.is_null() {
        return raw;
    }
    // at least `align` (>= 8) bytes above raw: room for the back pointer
    let p = ((raw as usize + l.align()) & !(l.align() - 1)) as *mut u8;
    (p as *mut *mut u8).sub(1).write_unaligned(raw);
    p
}

unsafe impl GlobalAlloc for EngineAlloc {
    #[inline]
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        if plain(&l) { __libc_malloc(l.size()) } else { alloc_aligned(l, false) }
    }
    #[inline]
    unsafe fn alloc_zeroed(&self, l: Layout) -> *mut u8 {
        if plain(&l) { __libc_calloc(1, l.size()) } else { alloc_aligned(l, true) }
    }
    #[inline]
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        if plain(&l) { __libc_free(p) } else { __libc_free((p as *mut *mut u8).sub(1).read_unaligned()) }
    }
    #[inline]
    unsafe fn realloc(&self, p: *mut u8, l: Layout, new_size: usize) -> *mut u8 {
        if plain(&l) && l.align() <= new_size {
            return __libc_realloc(p, new_size);
        }
        let nl = Layout::from_size_align_unchecked(new_size, l.align());
        let q = self.alloc(nl);
        if !q.is_null() {
            std::ptr::copy_nonoverlapping(p, q, l.size().min(new_size));
            self.dealloc(p, l);
        }
        q
    }
}
