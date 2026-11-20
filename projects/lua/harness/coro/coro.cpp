#include "../objects.hpp"

New<Thread> new_thread(Borrow<State> s) {
    lh::Balanced b(s->L);
    lua_State* co = nullptr;
    lh::protect(s->L, 0, 1, [&](lua_State* L) {
        co = lua_newthread(L);
        return 1;
    });
    int ref = luaL_ref(s->L, LUA_REGISTRYINDEX);
    New<Thread> t(ThreadT{s->L, co, ref});
    stitch::set(t, "state", s);
    b.ok();
    return t;
}
STITCH_ENDPOINT(new_thread);

// Resume the thread: on a fresh/finished thread start `fn`, on a suspended one pass `arg`.
std::optional<New<Ref>> resume(Borrow<Thread> t, Borrow<Func> fn, Borrow<Ref> arg) {
    StateT** sp = stitch::get_obj<State>(t, "state");
    stitch::require(sp, "state is gone");
    stitch::require(t->L == fn->L && t->L == arg->L, "refs from another state");
    lua_State* co = t->co;
    int st = lua_status(co);
    stitch::require(st == LUA_OK || st == LUA_YIELD, "thread is dead (error)");
    int nargs = 0;
    stitch::require(lua_checkstack(co, 3), "no stack space");
    if (st == LUA_OK && lua_gettop(co) == 0) {
        lua_rawgeti(co, LUA_REGISTRYINDEX, fn->ref);
        if (lua_type(co, -1) != LUA_TFUNCTION) {
            lua_pop(co, 1);
            stitch::bail("not a function");
        }
    }
    lua_rawgeti(co, LUA_REGISTRYINDEX, arg->ref);
    nargs = 1;
    int nres = 0;
    lh::budget = 1000;
    lua_sethook(co, lh::hook, LUA_MASKCOUNT, 1000);
    int rc = lua_resume(co, t->L, nargs, &nres);
    lua_sethook(co, nullptr, 0, 0);
    if (rc != LUA_OK && rc != LUA_YIELD) {
        lua_closethread(co, t->L);  // reset the errored thread so it can be reused
        stitch::bail("coroutine error");
    }
    std::optional<New<Ref>> out;
    if (nres > 0) {
        lua_xmove(co, t->L, 1);  // first result -> main stack
        if (nres > 1) lua_pop(co, nres - 1);
        out = lh::ref_top(*sp, t->L);
    }
    if (rc == LUA_OK) lua_settop(co, 0);
    return out;
}
STITCH_ENDPOINT(resume);

void close_thread(Borrow<Thread> t) {
    StateT** sp = stitch::get_obj<State>(t, "state");
    stitch::require(sp, "state is gone");
    lua_closethread(t->co, t->L);
}
STITCH_ENDPOINT(close_thread);
