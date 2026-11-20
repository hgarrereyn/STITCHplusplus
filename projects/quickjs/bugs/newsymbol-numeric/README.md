# quickjs: ABRT in JS_NewSymbolFromAtom

| | |
|---|---|
| Sanitizer | `ABRT` |
| Location | `JS_NewSymbolFromAtom quickjs.c:3277:5`, `JS_NewSymbol quickjs.c:3290:12`, `new_symbol values.cpp:31:24` |
| Library source | [https://github.com/quickjs-ng/quickjs](https://github.com/quickjs-ng/quickjs) @ `97d23e51dcc8` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `newsymbol-numeric` |

## Standalone reproducer

`standalone/min.c` (7 lines of code) needs only the library: no harness, no STITCH runtime.

```c
#include <quickjs.h>

int main(void) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    /* "0" becomes a tagged integer atom; JS_NewSymbolFromAtom asserts the atom is not one */
    JS_NewSymbol(ctx, "0", true);
    return 0;
}
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with AddressSanitizer, runs it). Build line: `clang $(stitch-flags --no-cov) -I. -I/src min.c /src/build/libqjs.a -lm -ldl -lpthread -lm -lpthread -o /tmp/repro`

`standalone/generated.cpp` (85 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (3 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 new_runtime(mem_limit=13627801, gc_threshold=110, set_gc=false)
#1 new_context(rt=o0.0)
#2 new_symbol(c=o1.0, desc=Str[1,decoded]"0", global=true)
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

`new_context` · harness/runtime/runtime.cpp

```cpp
New<Ctx> new_context(Borrow<Rt> rt) {
    JSContext* c = JS_NewContext(rt);
    stitch::require(c, "JS_NewContext failed");
    stitch::set(rt, "contexts", stitch::get(rt, "contexts", 0) + 1);
    New<Ctx> r(CtxT{c, *rt});
    stitch::set(r, "rt", rt);
    return r;
}
```

`new_symbol` · harness/values/values.cpp

```cpp
New<Val> new_symbol(Borrow<Ctx> c, Str<64, JsKey> desc, Bool global) {
    return qj::wrap(c, JS_NewSymbol(c->ctx, desc.c_str(), global));
}
```

## Sanitizer report

```
==7==ERROR: AddressSanitizer: ABRT on unknown address 0x000000000007 (pc 0xffff884874b8 bp 0xfffff1a198f0 sp 0xfffff1a198e0 T0)
    #5 0xaaaad563b05c in JS_NewSymbolFromAtom /src/quickjs.c:3277:5
    #6 0xaaaad563af68 in JS_NewSymbol /src/quickjs.c:3290:12
    #7 0xaaaad55e8334 in new_symbol harness/values/values.cpp:31:24
    #8 0xaaaad55e8334 in void stitch::detail::Endpoint<&new_symbol(stitch::Borrow<Ctx>, stitch::Str<64ul, JsKey>, stitch::Bool)>::call<0ul, 1ul, 2ul>(stitch::detail::Ctx&, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul>) /stitch/runtime/include/stitch.h:779:19

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH toolchain (base image Dockerfile, runtime, compiler wrappers); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug as a plain C/C++ program (`./run.sh --standalone`), if one was written

