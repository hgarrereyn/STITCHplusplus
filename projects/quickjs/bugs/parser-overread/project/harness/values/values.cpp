#include "../objects.hpp"

New<Val> new_number(Borrow<Ctx> c, F64 d, I32 i, OneOf<0, 1, 2> kind) {
    JSValue v = (int)kind == 0 ? JS_NewFloat64(c->ctx, d) : (int)kind == 1 ? JS_NewInt32(c->ctx, i) : JS_NewInt64(c->ctx, (int64_t)d);
    return qj::wrap(c, v);
}
STITCH_ENDPOINT(new_number);

New<Val> new_bigint(Borrow<Ctx> c, I64 v) { return qj::wrap(c, JS_NewBigInt64(c->ctx, v)); }
STITCH_ENDPOINT(new_bigint);

New<Val> new_string(Borrow<Ctx> c, Bytes<1024, JsKey> s) {
    return qj::wrap(c, JS_NewStringLen(c->ctx, (const char*)s.data(), s.size()));
}
STITCH_ENDPOINT(new_string);

New<Val> new_object(Borrow<Ctx> c, OneOf<0, 1, 2, 3, 4> kind, F64 date) {
    JSValue v;
    switch ((int)kind) {
        case 0: v = JS_NewObject(c->ctx); break;
        case 1: v = JS_NewArray(c->ctx); break;
        case 2: v = JS_NewError(c->ctx); break;
        case 3: v = JS_NewDate(c->ctx, date); break;
        default: v = JS_GetGlobalObject(c->ctx); break;
    }
    return qj::wrap(c, v);
}
STITCH_ENDPOINT(new_object);

New<Val> new_symbol(Borrow<Ctx> c, Str<64, JsKey> desc, Bool global) {
    return qj::wrap(c, JS_NewSymbol(c->ctx, desc.c_str(), global));
}
STITCH_ENDPOINT(new_symbol);

New<Val> new_arraybuffer(Borrow<Ctx> c, Bytes<4096> data, Bool uint8array) {
    JSValue v = uint8array ? JS_NewUint8ArrayCopy(c->ctx, data.data(), data.size())
                           : JS_NewArrayBufferCopy(c->ctx, data.data(), data.size());
    return qj::wrap(c, v);
}
STITCH_ENDPOINT(new_arraybuffer);

New<Val> new_typed_array(Borrow<Val> buffer_or_len, Range<0, 12> type) {
    JSValueConst argv[1] = {buffer_or_len->v};
    qj::arm();
    return qj::wrap_like(buffer_or_len, JS_NewTypedArray(buffer_or_len->ctx, 1, argv, (JSTypedArrayEnum)(int)type));
}
STITCH_ENDPOINT(new_typed_array);

void inspect_buffer(Borrow<Val> v) {
    size_t size = 0;
    if (JS_IsArrayBuffer(v->v)) {
        uint8_t* p = JS_GetArrayBuffer(v->ctx, &size, v->v);
        if (p && size) stitch::check(p[size - 1] == p[size - 1], "buffer readable");
    }
    uint8_t* p = JS_GetUint8Array(v->ctx, &size, v->v);
    if (p && size) (void)p[size - 1];
    if (JS_HasException(v->ctx)) JS_FreeValue(v->ctx, JS_GetException(v->ctx));
}
STITCH_ENDPOINT(inspect_buffer);

New<Val> dup(Borrow<Val> v) { return qj::wrap_like(v, JS_DupValue(v->ctx, v->v)); }
STITCH_ENDPOINT(dup);

void free_value(Take<Val> v) { qj::release(v); }
STITCH_ENDPOINT(free_value);

// Conversions: each may run user code (valueOf/toString/Symbol.toPrimitive).
void convert(Borrow<Val> v, OneOf<0, 1, 2, 3, 4, 5, 6> how) {
    JSContext* ctx = v->ctx;
    qj::arm();
    int32_t i32;
    int64_t i64;
    double d;
    size_t len = 0;
    int rc = 0;
    switch ((int)how) {
        case 0: rc = JS_ToInt32(ctx, &i32, v->v); break;
        case 1: rc = JS_ToInt64(ctx, &i64, v->v); break;
        case 2: rc = JS_ToFloat64(ctx, &d, v->v); break;
        case 3: rc = JS_ToBool(ctx, v->v) < 0 ? -1 : 0; break;
        case 4: rc = JS_ToBigInt64(ctx, &i64, v->v); break;
        case 5: {
            const char* s = JS_ToCStringLen2(ctx, &len, v->v, false);
            if (s) {
                stitch::check(strlen(s) <= len, "C string length");
                JS_FreeCString(ctx, s);
            } else rc = -1;
            break;
        }
        default: {
            JSValue s = JS_ToString(ctx, v->v);
            rc = JS_IsException(s) ? -1 : 0;
            JS_FreeValue(ctx, s);
        }
    }
    if (rc < 0) qj::exception(ctx);
}
STITCH_ENDPOINT(convert);

New<Val> to_string(Borrow<Val> v) {
    qj::arm();
    return qj::wrap_like(v, JS_ToString(v->ctx, v->v));
}
STITCH_ENDPOINT(to_string);

void compare(Borrow<Val> a, Borrow<Val> b) {
    qj::same_ctx(a, b);
    qj::arm();
    bool strict = JS_IsStrictEqual(a->ctx, a->v, b->v);
    bool same = JS_IsSameValue(a->ctx, a->v, b->v);
    bool zero = JS_IsSameValueZero(a->ctx, a->v, b->v);
    if (same) stitch::check(zero, "SameValue implies SameValueZero");
    stitch::check(JS_IsSameValue(a->ctx, a->v, a->v), "SameValue(x, x)");
    int eq = JS_IsEqual(a->ctx, a->v, b->v);
    if (eq < 0) qj::exception(a->ctx);
    (void)strict;
}
STITCH_ENDPOINT(compare);

void inspect(Borrow<Val> v) {
    JSValueConst x = v->v;
    int n = JS_IsNumber(x) + JS_IsBigInt(x) + JS_IsBool(x) + JS_IsNull(x) + JS_IsUndefined(x) + JS_IsString(x) +
            JS_IsSymbol(x) + JS_IsObject(x);
    stitch::check(n <= 1 || JS_IsUninitialized(x), "at most one primitive type");
    (void)JS_IsFunction(v->ctx, x);
    (void)JS_IsArray(x);
    (void)JS_IsError(x);
    (void)JS_IsDate(x);
}
STITCH_ENDPOINT(inspect);

void throw_and_catch(Borrow<Val> v) {
    JS_Throw(v->ctx, JS_DupValue(v->ctx, v->v));
    stitch::check(JS_HasException(v->ctx), "exception pending after JS_Throw");
    JSValue e = JS_GetException(v->ctx);
    stitch::check(JS_IsSameValue(v->ctx, e, v->v), "caught value is the thrown value");
    JS_FreeValue(v->ctx, e);
}
STITCH_ENDPOINT(throw_and_catch);
