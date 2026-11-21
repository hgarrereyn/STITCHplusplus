# quickjs: stack-buffer-overflow READ in js_typed_array_constructor

| | |
|---|---|
| Sanitizer | `stack-buffer-overflow READ` |
| Location | `js_typed_array_constructor quickjs.c:56945:17`, `JS_NewTypedArray quickjs.c:55132:12`, `new_typed_array values.cpp:45:41` |
| Library source | [https://github.com/quickjs-ng/quickjs](https://github.com/quickjs-ng/quickjs) @ `97d23e51dcc8` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `typedarray-argv` |

## Standalone reproducer

`standalone/min.c` (8 lines of code) needs only the library: no harness, no STITCH runtime.

```c
#include <quickjs.h>

int main(void) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    JSValue argv[1] = {JS_NewArrayBufferCopy(ctx, NULL, 0)};
    /* argc is 1, but with an ArrayBuffer js_typed_array_constructor reads argv[1] and argv[2] */
    JS_NewTypedArray(ctx, 1, argv, JS_TYPED_ARRAY_UINT8C);
    return 0;
}
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with AddressSanitizer, runs it). Build line: `clang $(stitch-flags --no-cov) -I. -I/src min.c /src/build/libqjs.a -lm -ldl -lpthread -lm -lpthread -o /tmp/repro`

`standalone/generated.cpp` (115 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (4 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 new_runtime(mem_limit=13627785, gc_threshold=46, set_gc=false)
#1 new_context_raw(rt=o0.0, which=128)
#2 new_arraybuffer(c=o1.0, data=Bytes[0]"", uint8array=false)
#3 new_typed_array(buffer_or_len=o2.0, type=0)
```

## Stubs used

What each call does (from `project/harness/`):

`new_runtime` · harness/runtime/runtime.cpp

```cpp
New<Rt> new_runtime(Range<1 << 20, 256 << 20> mem_limit, Range<0, 1 << 22> gc_threshold, Bool set_gc) {
    JSRuntime* rt = JS_NewRuntime();
    stitch::require(rt, "JS_NewRuntime failed");
    JS_SetMemoryLimit(rt, (size_t)mem_limit);
    if (set_gc) JS_SetGCThreshold(rt, (size_t)gc_threshold);
    JS_SetInterruptHandler(rt, qj::interrupt, nullptr);  // runaway scripts throw instead of hanging
    return rt;
}
```

`new_context_raw` · harness/runtime/runtime.cpp

```cpp
// A raw context with a chosen subset of intrinsics.
New<Ctx> new_context_raw(Borrow<Rt> rt, Flags<1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024> which) {
    JSContext* c = JS_NewContextRaw(rt);
    stitch::require(c, "JS_NewContextRaw failed");
    JS_AddIntrinsicBaseObjects(c);
    int w = (int)which;
    if (w & 1) JS_AddIntrinsicDate(c);
    if (w & 2) JS_AddIntrinsicEval(c);
    if (w & 4) JS_AddIntrinsicRegExpCompiler(c);
    if (w & 8) JS_AddIntrinsicRegExp(c);
    if (w & 16) JS_AddIntrinsicJSON(c);
    if (w & 32) JS_AddIntrinsicProxy(c);
    if (w & 64) JS_AddIntrinsicMapSet(c);
    if (w & 128) JS_AddIntrinsicTypedArrays(c);
    if (w & 256) JS_AddIntrinsicPromise(c);
    if (w & 512) JS_AddIntrinsicBigInt(c);
    if (w & 1024) JS_AddIntrinsicWeakRef(c);
    stitch::set(rt, "contexts", stitch::get(rt, "contexts", 0) + 1);
    New<Ctx> r(CtxT{c, *rt});
    stitch::set(r, "rt", rt);
    return r;
}
```

`new_arraybuffer` · harness/values/values.cpp

```cpp
New<Val> new_arraybuffer(Borrow<Ctx> c, Bytes<4096> data, Bool uint8array) {
    JSValue v = uint8array ? JS_NewUint8ArrayCopy(c->ctx, data.data(), data.size())
                           : JS_NewArrayBufferCopy(c->ctx, data.data(), data.size());
    return qj::wrap(c, v);
}
```

`new_typed_array` · harness/values/values.cpp

```cpp
New<Val> new_typed_array(Borrow<Val> buffer_or_len, Range<0, 12> type) {
    JSValueConst argv[1] = {buffer_or_len->v};
    qj::arm();
    return qj::wrap_like(buffer_or_len, JS_NewTypedArray(buffer_or_len->ctx, 1, argv, (JSTypedArrayEnum)(int)type));
}
```

## Sanitizer report

```
==7==ERROR: AddressSanitizer: stack-buffer-overflow on address 0xffffeed8b190 at pc 0xaaaac675837c bp 0xffffeed8aaf0 sp 0xffffeed8aae8
READ of size 16 at 0xffffeed8b190 thread T0
    #0 0xaaaac6758378 in js_typed_array_constructor /src/quickjs.c:56945:17
    #1 0xaaaac6757d84 in JS_NewTypedArray /src/quickjs.c:55132:12
    #2 0xaaaac665e690 in new_typed_array(stitch::Borrow<Val>, stitch::Range<0ll, 12ll>) harness/values/values.cpp:45:41
    #3 0xaaaac666a3c0 in void stitch::detail::Endpoint<&new_typed_array(stitch::Borrow<Val>, stitch::Range<0ll, 12ll>)>::call<0ul, 1ul>(stitch::detail::Ctx&, std::integer_sequence<unsigned long, 0ul, 1ul>) /stitch/runtime/include/stitch.h:779:19

Address 0xffffeed8b190 is located in stack of thread T0 at offset 48 in frame
    #0 0xaaaac665e5ac in new_typed_array(stitch::Borrow<Val>, stitch::Range<0ll, 12ll>) harness/values/values.cpp:42

  This frame has 1 object(s):
    [32, 48) 'argv' (line 43) <== Memory access at offset 48 overflows this variable
HINT: this may be a false positive if your program uses some custom stack unwind mechanism, swapcontext or vfork
      (longjmp and C++ exceptions *are* supported)
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH toolchain (base image Dockerfile, runtime, compiler wrappers); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug as a plain C/C++ program (`./run.sh --standalone`), if one was written

