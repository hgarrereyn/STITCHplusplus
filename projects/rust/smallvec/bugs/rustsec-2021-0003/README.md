# smallvec: heap-buffer-overflow WRITE in smallvec::SmallVec$LT$A$GT$::insert_many

| | |
|---|---|
| Kind | `heap-buffer-overflow WRITE` |
| Location | `smallvec::SmallVec$LT$A$GT$::insert_many lib.rs:1048:21` |
| Library source | [https://github.com/servo/rust-smallvec](https://github.com/servo/rust-smallvec) @ `0b2b4e53a5cf` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `rustsec-2021-0003` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 from_slice(data=Bytes[5]"S!CV+")
#1 insert_many(p=o0.0, at=0, data=Bytes[1]"\xa5", lie=3)
```

## Stubs used

What each call does (from `project/harness/`):

`from_slice` · harness/stubs.rs

```rust
    fn from_slice(data: Bytes<32>) -> New<Sv> {
        New::new(Pair { v: SmallVec::from_slice(&data), model: data.to_vec() })
    }
```

`insert_many` · harness/stubs.rs

```rust
    fn insert_many(p: Borrow<Sv>, at: Range<0, 40>, data: Bytes<24>, lie: Range<0, 3>) {
        let i = *at as usize;
        require(i <= p.model.len(), "index > len (documented panic)");
        // an iterator whose size_hint overstates or understates its length (allowed by Iterator)
        let hint = match *lie { 0 => data.len(), 1 => data.len() + 3, 2 => data.len() / 2, _ => 0 };
        struct Lying<I>(I, usize);
        impl<I: Iterator<Item = u8>> Iterator for Lying<I> {
            type Item = u8;
            fn next(&mut self) -> Option<u8> { self.0.next() }
            fn size_hint(&self) -> (usize, Option<usize>) { (self.1, Some(self.1)) }
        }
        p.v.insert_many(i, Lying(data.iter().copied(), hint));
        let tail = p.model.split_off(i);
        p.model.extend_from_slice(&data);
        p.model.extend_from_slice(&tail);
        same(&p, "insert_many");
    }
```

## Report

```
==8==ERROR: AddressSanitizer: heap-buffer-overflow on address 0xfc1f8b420415 at pc 0xaaaae2d7bc70 bp 0xffffe4f1c4c0 sp 0xffffe4f1bcb0
WRITE of size 5 at 0xfc1f8b420415 thread T0
    #0 0xaaaae2d7bc6c in __asan_memmove /rustc/llvm/src/llvm-project/compiler-rt/lib/asan/asan_interceptors_memintrinsics.cpp:71:3
    #1 0xaaaae2dba474 in core::ptr::copy::h3c898420be8a02e8 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/core/src/ptr/mod.rs:633:9
    #2 0xaaaae2dba474 in smallvec::SmallVec$LT$A$GT$::insert_many::h49e8a144868c6d19 /src/src/lib.rs:1048:21
    #3 0xaaaae2dba474 in stitch_harness::stubs::insert_many::h0aa081a35e88b0a3 .stitch/build/app/crate/src/stubs.rs:72:13
    #4 0xaaaae2dbce28 in stitch_harness::stubs::insert_many::__stitch_register::_$u7b$$u7b$closure$u7d$$u7d$::h5c5a7830549eefc2 /stitch/rust/stitch/src/lib.rs:455:42
    #5 0xaaaae2dfbcf8 in __rust_try stitch.63c9b40153e054b-cgu.0
    #6 0xaaaae2df0d8c in std::panicking::catch_unwind::hcae8cffb4dc3d66f /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panicking.rs:552:19
    #7 0xaaaae2df0d8c in std::panic::catch_unwind::hee89e3a9cf010291 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panic.rs:359:14
    #8 0xaaaae2df0d8c in stitch::rt::run_nodes::h633688dc7f11646e /stitch/rust/stitch/src/rt.rs:1143:19
    #9 0xaaaae2deae10 in stitch::rt::run_program::hf6861fe7f4f5df54 /stitch/rust/stitch/src/rt.rs:1049:15
    #10 0xaaaae2df8514 in stitch::rt::repro::h8d38cf89932681ab /stitch/rust/stitch/src/rt.rs:1385:15
    #11 0xaaaae2df8514 in stitch::rt::main::h2af23aa7383a9fc6 /stitch/rust/stitch/src/rt.rs:1402:24
    #12 0xaaaae2daab78 in core::ops::function::FnOnce::call_once::hd339ebed0371f804 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/core/src/ops/function.rs:250:5
    #13 0xaaaae2daab78 in std::sys::backtrace::__rust_begin_short_backtrace::hed0eeaea00b30353 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/sys/backtrace.rs:152:18
    #15 0xaaaae2e10ad0 in core::ops::function::impls::_$LT$impl$u20$core..ops..function..FnOnce$LT$A$GT$$u20$for$u20$$RF$F$GT$::call_once::h4bb62caca4257869 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/core/src/ops/function.rs:284:21
    #16 0xaaaae2e10ad0 in std::panicking::catch_unwind::do_call::h90c1bad1884cedb0 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panicking.rs:589:40
    #17 0xaaaae2e10ad0 in std::panicking::catch_unwind::h781b26f4dcaeeb40 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panicking.rs:552:19
    #18 0xaaaae2e10ad0 in std::panic::catch_unwind::hecc3411200251bc8 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panic.rs:359:14
    #20 0xaaaae2e10ad0 in std::panicking::catch_unwind::do_call::ha61737084ee0ef13 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panicking.rs:589:40
    #21 0xaaaae2e10ad0 in std::panicking::catch_unwind::h3b785820e875d9df /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panicking.rs:552:19
    #22 0xaaaae2e10ad0 in std::panic::catch_unwind::h304af5160566651d /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panic.rs:359:14
    #24 0xaaaae2daad44 in main (.stitch/build/app/worker+0xead44) (BuildId: c5af7866f826f395a72595eeebe42f165c372152)

0xfc1f8b420415 is located 0 bytes after 5-byte region [0xfc1f8b420410,0xfc1f8b420415)
allocated by thread T0 here:
    #0 0xaaaae2d7d86c in malloc /rustc/llvm/src/llvm-project/compiler-rt/lib/asan/asan_malloc_linux.cpp:67:3
    #1 0xaaaae2db2ad8 in alloc::alloc::alloc::hf643c2c8a81cc35b /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/alloc.rs:93:9
    #2 0xaaaae2db2ad8 in alloc::alloc::Global::alloc_impl::h60bdcf85ae38b11a /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/alloc.rs:188:73
    #3 0xaaaae2db2ad8 in _$LT$alloc..alloc..Global$u20$as$u20$core..alloc..Allocator$GT$::allocate::h3e9af07b4668e6df /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/alloc.rs:249:14
    #4 0xaaaae2db2ad8 in alloc::raw_vec::RawVecInner$LT$A$GT$::try_allocate_in::h323a8ef87ee78fb5 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/raw_vec/mod.rs:476:47
    #5 0xaaaae2db2ad8 in alloc::raw_vec::RawVecInner$LT$A$GT$::with_capacity_in::h3685ef9b2f002737 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/raw_vec/mod.rs:422:15
    #6 0xaaaae2db2ad8 in alloc::raw_vec::RawVec$LT$T$C$A$GT$::with_capacity_in::h70fbd40e1149473d /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/raw_vec/mod.rs:190:20
    #7 0xaaaae2db2ad8 in alloc::vec::Vec$LT$T$C$A$GT$::with_capacity_in::h7603fefb5edf7062 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/vec/mod.rs:847:20
    #8 0xaaaae2db2ad8 in _$LT$T$u20$as$u20$alloc..slice..$LT$impl$u20$$u5b$T$u5d$$GT$..to_vec_in..ConvertVec$GT$::to_vec::h3613865b7b7c18cd /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/slice.rs:449:29
    #9 0xaaaae2db5b50 in alloc::slice::_$LT$impl$u20$$u5b$T$u5d$$GT$::to_vec_in::hb1d15bffbd911a47 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/slice.rs:402:16
    #10 0xaaaae2db5b50 in alloc::slice::_$LT$impl$u20$$u5b$T$u5d$$GT$::to_vec::h925a6fe039cc69b3 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/alloc/src/slice.rs:378:14
    #11 0xaaaae2db5b50 in smallvec::SmallVec$LT$A$GT$::from_slice::h57f0ebf399ffc984 /src/src/lib.rs:1357:31
    #12 0xaaaae2db5b50 in stitch_harness::stubs::from_slice::h0f05726662c0bdc9 .stitch/build/app/crate/src/stubs.rs:29:28
    #13 0xaaaae2db6924 in stitch_harness::stubs::from_slice::__stitch_register::_$u7b$$u7b$closure$u7d$$u7d$::h65aa246ac289ee55 /stitch/rust/stitch/src/lib.rs:455:42
    #14 0xaaaae2dfbcf8 in __rust_try stitch.63c9b40153e054b-cgu.0
    #15 0xaaaae2df0d8c in std::panicking::catch_unwind::hcae8cffb4dc3d66f /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panicking.rs:552:19
    #16 0xaaaae2df0d8c in std::panic::catch_unwind::hee89e3a9cf010291 /rustc/49a8ba06848fa8f282fe9055b4178350970bb0ce/library/std/src/panic.rs:359:14
    #17 0xaaaae2df0d8c in stitch::rt::run_nodes::h633688dc7f11646e /stitch/rust/stitch/src/rt.rs:1143:19
...
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH rust toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

