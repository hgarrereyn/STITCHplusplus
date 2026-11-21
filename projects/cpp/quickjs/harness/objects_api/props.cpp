#include "../objects.hpp"

static std::string key_or_existing(Borrow<Val>& o, stitch::Source& s, const std::string& fresh) {
    // mostly an existing own property name so lookups hit
    JSPropertyEnum* tab = nullptr;
    uint32_t n = 0;
    std::string k = fresh;
    if (JS_IsObject(o->v) && s.coin(0.7) &&
        JS_GetOwnPropertyNames(o->ctx, &tab, &n, o->v, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) == 0) {
        if (n) {
            const char* c = JS_AtomToCString(o->ctx, tab[s.choice(n)].atom);
            if (c) {
                k = c;
                JS_FreeCString(o->ctx, c);
            }
        }
        JS_FreePropertyEnum(o->ctx, tab, n);
    }
    if (JS_HasException(o->ctx)) JS_FreeValue(o->ctx, JS_GetException(o->ctx));
    return k;
}

New<Val> get_property(Borrow<Val> o, Str<64, JsKey> fresh, Src<8> s) {
    std::string k = key_or_existing(o, *s, fresh.s);
    qj::arm();
    return qj::wrap_like(o, JS_GetPropertyStr(o->ctx, o->v, k.c_str()));
}
STITCH_ENDPOINT(get_property);

// JS_SetProperty* takes ownership of the value.
void set_property(Borrow<Val> o, Str<64, JsKey> fresh, Src<8> s, Take<Val> v) {
    qj::same_ctx(o, v);
    std::string k = key_or_existing(o, *s, fresh.s);
    qj::arm();
    if (CtxT* c = stitch::get_obj<Ctx>(v, "ctx")) stitch::set(c, "values", stitch::get(c, "values", 0) - 1);
    if (JS_SetPropertyStr(o->ctx, o->v, k.c_str(), v->v) < 0) qj::exception(o->ctx);
}
STITCH_ENDPOINT(set_property);

New<Val> get_index(Borrow<Val> o, Range<0, 4096> idx) {
    qj::arm();
    return qj::wrap_like(o, JS_GetPropertyUint32(o->ctx, o->v, (uint32_t)idx));
}
STITCH_ENDPOINT(get_index);

void set_index(Borrow<Val> o, Range<0, 4096> idx, Take<Val> v) {
    qj::same_ctx(o, v);
    qj::arm();
    if (CtxT* c = stitch::get_obj<Ctx>(v, "ctx")) stitch::set(c, "values", stitch::get(c, "values", 0) - 1);
    if (JS_SetPropertyUint32(o->ctx, o->v, (uint32_t)idx, v->v) < 0) qj::exception(o->ctx);
}
STITCH_ENDPOINT(set_index);

void define_property(Borrow<Val> o, Borrow<Atom> key, Borrow<Val> v, Flags<JS_PROP_CONFIGURABLE, JS_PROP_WRITABLE, JS_PROP_ENUMERABLE, JS_PROP_THROW> flags) {
    qj::same_rt(o, key);
    qj::same_ctx(o, v);
    qj::arm();
    if (JS_DefinePropertyValue(o->ctx, o->v, key->a, JS_DupValue(v->ctx, v->v), (int)flags) < 0) qj::exception(o->ctx);
}
STITCH_ENDPOINT(define_property);

void delete_property(Borrow<Val> o, Borrow<Atom> key, Bool throw_flag) {
    qj::same_rt(o, key);
    qj::arm();
    bool pending_before = JS_HasException(o->ctx);
    if (JS_DeleteProperty(o->ctx, o->v, key->a, throw_flag ? JS_PROP_THROW : 0) < 0) qj::exception(o->ctx);
    if (!pending_before) stitch::check(throw_flag || !JS_HasException(o->ctx), "no pending exception after a successful delete");
}
STITCH_ENDPOINT(delete_property);

void has_property(Borrow<Val> o, Borrow<Atom> key) {
    qj::same_rt(o, key);
    qj::arm();
    if (JS_HasProperty(o->ctx, o->v, key->a) < 0) qj::exception(o->ctx);
}
STITCH_ENDPOINT(has_property);

void own_property_names(Borrow<Val> o, Flags<JS_GPN_STRING_MASK, JS_GPN_SYMBOL_MASK, JS_GPN_PRIVATE_MASK, JS_GPN_ENUM_ONLY> flags) {
    JSPropertyEnum* tab = nullptr;
    uint32_t n = 0;
    qj::arm();
    if (JS_GetOwnPropertyNames(o->ctx, &tab, &n, o->v, (int)flags) < 0) qj::exception(o->ctx);
    JS_FreePropertyEnum(o->ctx, tab, n);
}
STITCH_ENDPOINT(own_property_names);

void object_ops(Borrow<Val> o, OneOf<0, 1, 2, 3, 4> op) {
    qj::arm();
    int64_t len;
    int rc = 0;
    switch ((int)op) {
        case 0: rc = JS_PreventExtensions(o->ctx, o->v); break;
        case 1: rc = JS_SealObject(o->ctx, o->v); break;
        case 2: rc = JS_FreezeObject(o->ctx, o->v); break;
        case 3: rc = JS_IsExtensible(o->ctx, o->v); break;
        default: rc = JS_GetLength(o->ctx, o->v, &len); break;
    }
    if (rc < 0) qj::exception(o->ctx);
}
STITCH_ENDPOINT(object_ops);

void set_prototype(Borrow<Val> o, Borrow<Val> proto) {
    qj::same_ctx(o, proto);
    qj::arm();
    if (JS_SetPrototype(o->ctx, o->v, proto->v) < 0) qj::exception(o->ctx);
}
STITCH_ENDPOINT(set_prototype);

New<Val> get_prototype(Borrow<Val> o) {
    qj::arm();
    return qj::wrap_like(o, JS_GetPrototype(o->ctx, o->v));
}
STITCH_ENDPOINT(get_prototype);

void instance_of(Borrow<Val> v, Borrow<Val> ctor) {
    qj::same_ctx(v, ctor);
    qj::arm();
    if (JS_IsInstanceOf(v->ctx, v->v, ctor->v) < 0) qj::exception(v->ctx);
}
STITCH_ENDPOINT(instance_of);

New<Val> call(Borrow<Val> fn, Borrow<Val> self, Borrow<Val> arg, Range<0, 2> argc, Bool construct) {
    qj::same_ctx(fn, self);
    qj::same_ctx(fn, arg);
    JSValueConst argv[2] = {arg->v, self->v};
    qj::arm();
    JSValue r = construct ? JS_CallConstructor(fn->ctx, fn->v, (int)argc, argv) : JS_Call(fn->ctx, fn->v, self->v, (int)argc, argv);
    return qj::wrap_like(fn, r);
}
STITCH_ENDPOINT(call);

New<Val> invoke(Borrow<Val> self, Borrow<Atom> method, Borrow<Val> arg, Range<0, 1> argc) {
    qj::same_rt(self, method);
    qj::same_ctx(self, arg);
    JSValueConst argv[1] = {arg->v};
    qj::arm();
    return qj::wrap_like(self, JS_Invoke(self->ctx, self->v, method->a, (int)argc, argv));
}
STITCH_ENDPOINT(invoke);

// Promises: capability = (promise, resolve, reject)
std::tuple<New<Val>, New<Val>, New<Val>> new_promise(Borrow<Ctx> c) {
    JSValue funcs[2];
    JSValue p = JS_NewPromiseCapability(c->ctx, funcs);
    if (JS_IsException(p)) qj::exception(c->ctx);
    return {qj::wrap(c, p), qj::wrap(c, funcs[0]), qj::wrap(c, funcs[1])};
}
STITCH_ENDPOINT(new_promise);

void promise_state(Borrow<Val> p) {
    JSPromiseStateEnum st = JS_PromiseState(p->ctx, p->v);
    if (st == JS_PROMISE_FULFILLED || st == JS_PROMISE_REJECTED) JS_FreeValue(p->ctx, JS_PromiseResult(p->ctx, p->v));
}
STITCH_ENDPOINT(promise_state);

// Atoms
New<Atom> new_atom(Borrow<Ctx> c, Str<64, JsKey> name) {
    JSAtom a = JS_NewAtom(c->ctx, name.c_str());
    stitch::require(a != JS_ATOM_NULL, "JS_NewAtom failed");
    stitch::set(c, "atoms", stitch::get(c, "atoms", 0) + 1);
    New<Atom> r(AtomT{c->ctx, a});
    stitch::set(r, "ctx", c);
    return r;
}
STITCH_ENDPOINT(new_atom);

New<Val> atom_to_string(Borrow<Atom> a) {
    CtxT* c = stitch::get_obj<Ctx>(a, "ctx");
    stitch::require(c, "atom's context is gone");
    return qj::wrap(c, a->ctx, JS_AtomToString(a->ctx, a->a));
}
STITCH_ENDPOINT(atom_to_string);

void free_atom(Take<Atom> a) {
    JS_FreeAtom(a->ctx, a->a);
    if (CtxT* c = stitch::get_obj<Ctx>(a, "ctx")) stitch::set(c, "atoms", stitch::get(c, "atoms", 0) - 1);
}
STITCH_ENDPOINT(free_atom);
