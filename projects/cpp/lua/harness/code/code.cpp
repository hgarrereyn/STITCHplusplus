#include "../objects.hpp"

// Text chunks only from fuzz input: Lua does not verify binary chunks (loading untrusted
// bytecode is unsafe by design), so binary loading only uses our own lua_dump output.
lh::Out load(Borrow<State> s, Str<8192, LuaSource> src, Str<16> chunkname) {
    lh::Balanced b(s->L);
    int rc = luaL_loadbufferx(s->L, src.data(), src.size(), chunkname.c_str(), "t");
    if (rc != LUA_OK) {
        lua_pop(s->L, 1);
        stitch::bail("syntax error");
    }
    auto r = lh::out(s, s->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(load);

static int writer(lua_State*, const void* p, size_t sz, void* ud) {
    static_cast<std::string*>(ud)->append((const char*)p, sz);
    return 0;
}

// dump a Lua function and load it back (binary chunk produced by this state: trusted)
lh::Out dump_and_reload(Borrow<Func> fn, Bool strip) {
    StateT* s = lh::state_of(fn);
    lh::Balanced b(fn->L);
    lh::push(fn);
    stitch::require(lua_type(fn->L, -1) == LUA_TFUNCTION && !lua_iscfunction(fn->L, -1), "not a Lua function");
    std::string chunk;
    int rc = lua_dump(fn->L, writer, &chunk, strip);
    lua_pop(fn->L, 1);
    stitch::require(rc == 0, "dump failed");
    rc = luaL_loadbufferx(fn->L, chunk.data(), chunk.size(), "=dumped", "b");
    stitch::check(rc == LUA_OK || rc == LUA_ERRMEM, "a dumped chunk loads");
    if (rc != LUA_OK) {
        lua_pop(fn->L, 1);
        stitch::bail("reload failed");
    }
    auto r = lh::out(s, fn->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(dump_and_reload);

// Call with 0-2 arguments; returns the first result (if any).
lh::Out call(Borrow<Func> fn, Borrow<Ref> a, Borrow<Ref> c, Range<0, 2> nargs) {
    lh::same_state(fn, a);
    lh::same_state(fn, c);
    StateT* s = lh::state_of(fn);
    lh::Balanced b(fn->L);
    lh::push(fn);
    lh::push(a);
    lh::push(c);
    lh::protect(fn->L, 3, 1, [&](lua_State* L) {
        lua_settop(L, 1 + (int)nargs);
        lua_call(L, (int)nargs, 1);
        return 1;
    });
    auto r = lh::out(s, fn->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(call);

// Debug API on a function value: getinfo, upvalue names/values, upvalueid/join.
void debug_info(Borrow<Func> fn, Borrow<Ref> other, Src<16> sel) {
    lh::same_state(fn, other);
    lh::state_of(fn);
    lh::Balanced b(fn->L);
    lh::push(fn);
    lh::push(other);
    lh::protect(fn->L, 2, 0, [&](lua_State* L) {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_Debug ar;
        lua_pushvalue(L, 1);
        lua_getinfo(L, ">Snlu", &ar);
        int n1 = 0;
        while (n1 < 300 && lua_getupvalue(L, 1, n1 + 1)) { lua_pop(L, 1); n1++; }
        stitch::check(n1 == ar.nups, "getupvalue count == nups");
        if (n1 > 0) {
            int k = 1 + (int)sel->choice((size_t)n1);
            (void)lua_upvalueid(L, 1, k);
            if (sel->coin()) {
                lua_pushnil(L);
                lua_setupvalue(L, 1, k);
            }
            if (lua_type(L, 2) == LUA_TFUNCTION && !lua_iscfunction(L, 1) && !lua_iscfunction(L, 2)) {
                int n2 = 0;
                while (n2 < 300 && lua_getupvalue(L, 2, n2 + 1)) { lua_pop(L, 1); n2++; }
                if (n2 > 0) lua_upvaluejoin(L, 1, k, 2, 1 + (int)sel->choice((size_t)n2));
            }
        }
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(debug_info);

void traceback(Borrow<State> s, Str<64> msg, Range<0, 8> level) {
    lh::Balanced b(s->L);
    luaL_traceback(s->L, s->L, msg.c_str(), (int)level);
    lua_pop(s->L, 1);
    b.ok();
}
STITCH_ENDPOINT(traceback);
