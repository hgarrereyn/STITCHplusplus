#include "../objects.hpp"

New<Rt> new_runtime(Range<1 << 20, 256 << 20> mem_limit, Range<0, 1 << 22> gc_threshold, Bool set_gc) {
    JSRuntime* rt = JS_NewRuntime();
    stitch::require(rt, "JS_NewRuntime failed");
    JS_SetMemoryLimit(rt, (size_t)mem_limit);
    if (set_gc) JS_SetGCThreshold(rt, (size_t)gc_threshold);
    JS_SetInterruptHandler(rt, qj::interrupt, nullptr);  // runaway scripts throw instead of hanging
    return rt;
}
STITCH_ENDPOINT(new_runtime);

// 0 disables the stack check (documented): keep a limit so deep recursion throws instead of
// overflowing the C stack.
void set_max_stack_size(Borrow<Rt> rt, Range<64 << 10, 1 << 22> size) { JS_SetMaxStackSize(rt, (size_t)size); }
STITCH_ENDPOINT(set_max_stack_size);

void run_gc(Borrow<Rt> rt) { JS_RunGC(rt); }
STITCH_ENDPOINT(run_gc);

void memory_usage(Borrow<Rt> rt) {
    JSMemoryUsage u;
    JS_ComputeMemoryUsage(rt, &u);
    stitch::check(u.malloc_size >= 0 && u.memory_used_size >= 0, "memory usage is non-negative");
}
STITCH_ENDPOINT(memory_usage);

void execute_pending_jobs(Borrow<Rt> rt, Range<0, 64> max) {
    qj::arm();
    for (int i = 0; i < (int)max && JS_IsJobPending(rt); i++) {
        JSContext* c = nullptr;
        if (JS_ExecutePendingJob(rt, &c) < 0 && c) JS_FreeValue(c, JS_GetException(c));
    }
}
STITCH_ENDPOINT(execute_pending_jobs);

void free_runtime(Borrow<Rt> rt) {
    stitch::require(stitch::get(rt, "contexts", 0) == 0, "runtime still has contexts");
    // jobs keep values alive; run or drop them first (JS_FreeRuntime asserts on leaks)
    while (JS_IsJobPending(rt)) {
        JSContext* c = nullptr;
        if (JS_ExecutePendingJob(rt, &c) < 0 && c) JS_FreeValue(c, JS_GetException(c));
    }
    JS_FreeRuntime(rt);
    rt.consume();
}
STITCH_ENDPOINT(free_runtime);

New<Ctx> new_context(Borrow<Rt> rt) {
    JSContext* c = JS_NewContext(rt);
    stitch::require(c, "JS_NewContext failed");
    stitch::set(rt, "contexts", stitch::get(rt, "contexts", 0) + 1);
    New<Ctx> r(CtxT{c, *rt});
    stitch::set(r, "rt", rt);
    return r;
}
STITCH_ENDPOINT(new_context);

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
STITCH_ENDPOINT(new_context_raw);

void free_context(Borrow<Ctx> c) {
    stitch::require(stitch::get(c, "values", 0) == 0 && stitch::get(c, "atoms", 0) == 0, "context still has live values");
    if (JSRuntime** rt = stitch::get_obj<Rt>(c, "rt")) stitch::set(*rt, "contexts", stitch::get(*rt, "contexts", 0) - 1);
    JS_FreeContext(c->ctx);
    c.consume();
}
STITCH_ENDPOINT(free_context);
