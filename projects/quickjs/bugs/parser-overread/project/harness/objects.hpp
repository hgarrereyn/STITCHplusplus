// quickjs-ng harness.
// Ownership rules from quickjs.h: every JSValue we hold is freed exactly once with JS_FreeValue on
// its context; a context may only be freed when none of its values are alive, a runtime only
// when none of its contexts are (JS_FreeRuntime asserts otherwise). Attributes "values" /
// "atoms" (on Ctx) and "contexts" (on Rt) count what is alive.
#pragma once
#include <stitch.h>
#include <quickjs.h>
#include <string.h>
#include <string>

STITCH_OBJECT(Rt, JSRuntime*);
struct CtxT {
    JSContext* ctx;
    JSRuntime* rt;
};
STITCH_OBJECT(Ctx, CtxT);
struct ValT {
    JSContext* ctx;
    JSValue v;
};
STITCH_OBJECT(Val, ValT);
STITCH_DROP(Val, [](ValT v) { JS_FreeValue(v.ctx, v.v); });
// Output of JS_EVAL_FLAG_COMPILE_ONLY (function bytecode or module): only valid for JS_EvalFunction.
STITCH_OBJECT(Compiled, ValT);
STITCH_DROP(Compiled, [](ValT v) { JS_FreeValue(v.ctx, v.v); });
struct AtomT {
    JSContext* ctx;
    JSAtom a;
};
STITCH_OBJECT(Atom, AtomT);
STITCH_DROP(Atom, [](AtomT a) { JS_FreeAtom(a.ctx, a.a); });

STITCH_DATATYPE(JsSource);
STITCH_DATATYPE(JsKey);
STITCH_DATATYPE(JsonText);

namespace qj {
inline int budget = 0;
// JSInterruptHandler takes a JSRuntime* in older quickjs-ng and a JSContext* in newer ones; the
// template resolves against whichever this version declares.
template <class R>
int interrupt(R*, void*) { return --budget < 0; }
inline void arm(int n = 200000) { budget = n; }


// The Ctx object a value belongs to (relation set at creation).
inline CtxT* ctx_of(const Borrow<Val>& v) {
    CtxT* c = stitch::get_obj<Ctx>(v, "ctx");
    stitch::require(c != nullptr, "value's context is gone");
    return c;
}

// Clear and inspect a pending exception, then bail.
[[noreturn]] inline void exception(JSContext* ctx) {
    JSValue e = JS_GetException(ctx);
    const char* s = JS_ToCString(ctx, e);
    if (s) JS_FreeCString(ctx, s);
    if (JS_HasException(ctx)) JS_FreeValue(ctx, JS_GetException(ctx));  // ToCString may throw
    JS_FreeValue(ctx, e);
    stitch::bail("JS exception");
}

// Wrap a fresh JSValue owned by `c` (an exception value bails).
template <class C>
New<Val> wrap(const C& c, JSContext* ctx, JSValue v) {
    if (JS_IsException(v)) exception(ctx);
    New<Val> r(ValT{ctx, v});
    stitch::set(r, "ctx", c);
    stitch::set(c, "values", stitch::get(c, "values", 0) + 1);
    return r;
}
inline New<Val> wrap(Borrow<Ctx>& c, JSValue v) { return wrap(c, c->ctx, v); }
inline New<Val> wrap_like(const Borrow<Val>& like, JSValue v) { return wrap(ctx_of(like), like->ctx, v); }

// Release a value we own (Take<Val> or a consumed Borrow).
template <class V>
inline void release(const V& v) {
    JS_FreeValue(v->ctx, v->v);
    if (CtxT* c = stitch::get_obj<Ctx>(v, "ctx")) stitch::set(c, "values", stitch::get(c, "values", 0) - 1);
}

inline void same_ctx(const Borrow<Val>& a, const Borrow<Val>& b) {
    stitch::require(a->ctx == b->ctx, "values from different contexts");
}
inline void same_ctx(const Borrow<Val>& a, const Take<Val>& b) {
    stitch::require(a->ctx == b->ctx, "values from different contexts");
}
// atoms are per runtime
inline void same_rt(const Borrow<Val>& v, const Borrow<Atom>& a) {
    stitch::require(JS_GetRuntime(v->ctx) == JS_GetRuntime(a->ctx), "atom from another runtime");
}
}  // namespace qj
