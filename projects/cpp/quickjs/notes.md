# quickjs-ng (quickjs-ng/quickjs @ 97d23e5, 0.11.0-dev)

52 endpoints in `runtime/`, `eval/`, `values/`, `objects_api/`. Object types: `Rt`, `Ctx`, `Val`
(JSValue + its context), `Compiled` (output of `JS_EVAL_FLAG_COMPILE_ONLY`), and `Atom`. Live-object
counters (`values`/`atoms` on a context, `contexts` on a runtime) enforce the free order that
`JS_FreeRuntime` asserts on. An interrupt budget stops runaway scripts. Decoders: `JsSource`
(statements over a few variables exercising builtins), `JsKey`, `JsonText`.

## Library bugs

| # | Finding | Status at upstream HEAD (dce8c0b) |
|---|---|---|
| 1 | **`JS_NewSymbol(ctx, "123", …)` aborts**: numeric descriptions become tagged-int atoms, and `JS_NewSymbolFromAtom` asserts `!__JS_AtomIsTaggedInt(descr)`. Without asserts, it indexes `rt->atom_array` with a tagged value (out of bounds). | still crashes |
| 2 | **`JS_NewTypedArray(ctx, 1, argv, type)` reads past `argv`** when `argv[0]` is an ArrayBuffer: the constructor reads `argv[1]`/`argv[2]` regardless of `argc` (stack-buffer-overflow). | fixed: `stitch bisect --fixed` → 3f57bfb "Fix out-of-bound read in JS_NewTypedArray (#1704)" |
| 3 | **BigInt values in a raw context without the BigInt intrinsic**: any property access on them SEGVs in `find_own_property` (missing prototype). Reachable from script alone: `JS_NewContextRaw` + eval intrinsic, `eval("(1n).foo")`. | fixed |
| 4 | **Leak on `JS_NewPromiseCapability` failure** in a context without the Promise intrinsic: `JS_FreeRuntime` later asserts `list_empty(&rt->gc_obj_list)`. | fixed |
| 5 | **Parser heap overread in `js_parse_for_in_of`** (quickjs.c:27088 at the pin, :29370 at HEAD) when compiling nested `for … of` loops in a module under a small memory limit (`JS_SetMemoryLimit` 1 MB). It looks like parsing continues after a failed bytecode-buffer reallocation. | still crashes with the original program |
| 6 | **Out-of-memory error paths** with a small memory limit: `JS_NewContextRaw` frees a half-built context but leaves it linked (heap-use-after-free WRITE in `add_gc_object`); backtraces walk freed objects after a failed `JS_EvalFunction` (UAF READ in `can_add_backtrace`); further UAFs in `JS_RunGC` and `JS_ComputeMemoryUsage`; SEGVs in `JS_NewContext`. | none reproduce at HEAD (fixed or behavior changed) |
| 7 | **`JS_DetectModule(input, len)` reads past `len`** unless the input is NUL-terminated. The header's "must be zero terminated" comment sits under `JS_Eval`, not `JS_DetectModule`, so this is an undocumented precondition (questionable). | not checked |

These came from a 45-minute campaign with persistent workers. The fuzzed `mem_limit` (down to 1 MB)
is what reaches the allocation-failure paths.

## Harness bugs found and fixed through triage

- **`JS_SetMaxStackSize(rt, 0)` disables the stack check** (documented), so deep recursion
  overflowed the C stack. The stub now keeps a ≥64 KB limit.
- **The "no pending exception after delete" oracle** fired when an *earlier* call had left an
  exception pending. It now checks only from a clean state.

- **Compile-only eval results** (function bytecode / modules) were put in the generic `Val` pool.
  Property access on bytecode SEGVs. They're now a separate `Compiled` type, only usable with
  `JS_EvalFunction`.
- **The interrupt handler signature changed upstream** (`JSRuntime*` → `JSContext*`). A template
  callback keeps the harness compiling across versions, so `--rev HEAD` and bisect work.
- **The JS decoder produced mostly syntax errors** (unquoted keys, `let` redeclarations, `await`
  outside async, `super` without `extends`). It's now escaped and scoped.
