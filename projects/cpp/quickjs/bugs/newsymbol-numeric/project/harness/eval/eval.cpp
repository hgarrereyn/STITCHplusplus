#include "../objects.hpp"

using EvalFlags = OneOf<JS_EVAL_TYPE_GLOBAL, JS_EVAL_TYPE_MODULE, JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT,
                        JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_BACKTRACE_BARRIER, JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_ASYNC>;

New<Val> eval(Borrow<Ctx> c, Str<8192, JsSource> src, EvalFlags flags) {
    qj::arm();
    JSValue v = JS_Eval(c->ctx, src.c_str(), src.size(), "<fuzz>", (int)flags);
    return qj::wrap(c, v);
}
STITCH_ENDPOINT(eval);

New<Val> eval_this(Borrow<Val> self, Str<4096, JsSource> src) {
    qj::arm();
    JSValue v = JS_EvalThis(self->ctx, self->v, src.c_str(), src.size(), "<fuzz>", JS_EVAL_TYPE_GLOBAL);
    return qj::wrap_like(self, v);
}
STITCH_ENDPOINT(eval_this);

New<Compiled> compile(Borrow<Ctx> c, Str<8192, JsSource> src, Bool module) {
    qj::arm();
    JSValue v = JS_Eval(c->ctx, src.c_str(), src.size(), "<fuzz>",
                        (module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL) | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(v)) qj::exception(c->ctx);
    New<Compiled> r(ValT{c->ctx, v});
    stitch::set(r, "ctx", c);
    stitch::set(c, "values", stitch::get(c, "values", 0) + 1);
    return r;
}
STITCH_ENDPOINT(compile);

// Run compiled code (JS_EvalFunction takes ownership of it).
New<Val> eval_function(Take<Compiled> fn) {
    CtxT* c = stitch::get_obj<Ctx>(fn, "ctx");
    stitch::require(c, "compiled code's context is gone");
    stitch::set(c, "values", stitch::get(c, "values", 0) - 1);
    qj::arm();
    JSValue r = JS_EvalFunction(fn->ctx, fn->v);
    return qj::wrap(c, c->ctx, r);
}
STITCH_ENDPOINT(eval_function);

// JS_DetectModule reads past input_len when the input is not NUL-terminated (recorded in
// notes.md as a finding); pass a terminated copy so fuzzing can go past it.
void detect_module(Bytes<4096, JsSource> src) {
    std::string copy(src.data(), src.data() + src.size());
    JS_DetectModule(copy.c_str(), copy.size());
}
STITCH_ENDPOINT(detect_module);

New<Val> json_parse(Borrow<Ctx> c, Str<4096, JsonText> text) {
    return qj::wrap(c, JS_ParseJSON(c->ctx, text.c_str(), text.size(), "<json>"));
}
STITCH_ENDPOINT(json_parse);

New<Val> json_stringify(Borrow<Val> v, Range<0, 12> space) {
    JSValue sp = JS_NewInt32(v->ctx, (int)space);
    qj::arm();
    JSValue r = JS_JSONStringify(v->ctx, v->v, JS_UNDEFINED, sp);
    JS_FreeValue(v->ctx, sp);
    return qj::wrap_like(v, r);
}
STITCH_ENDPOINT(json_stringify);

// Serialize with JS_WriteObject, then read back (never with bytecode from untrusted input).
New<Val> write_read_object(Borrow<Val> v, Flags<JS_WRITE_OBJ_REFERENCE, JS_WRITE_OBJ_SAB, JS_WRITE_OBJ_BYTECODE> wflags) {
    size_t size = 0;
    uint8_t* buf = JS_WriteObject(v->ctx, &size, v->v, (int)wflags);
    if (!buf) qj::exception(v->ctx);
    int rflags = 0;
    if ((int)wflags & JS_WRITE_OBJ_BYTECODE) rflags |= JS_READ_OBJ_BYTECODE;  // our own bytecode: trusted
    if ((int)wflags & JS_WRITE_OBJ_REFERENCE) rflags |= JS_READ_OBJ_REFERENCE;
    if ((int)wflags & JS_WRITE_OBJ_SAB) rflags |= JS_READ_OBJ_SAB;
    JSValue r = JS_ReadObject(v->ctx, buf, size, rflags);
    js_free(v->ctx, buf);
    return qj::wrap_like(v, r);
}
STITCH_ENDPOINT(write_read_object);

// Untrusted serialized data: allowed without JS_READ_OBJ_BYTECODE.
New<Val> read_object(Borrow<Ctx> c, Bytes<4096> data, Flags<JS_READ_OBJ_REFERENCE, JS_READ_OBJ_SAB> flags) {
    return qj::wrap(c, JS_ReadObject(c->ctx, data.data(), data.size(), (int)flags));
}
STITCH_ENDPOINT(read_object);
