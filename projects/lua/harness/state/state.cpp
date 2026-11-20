#include "../objects.hpp"

New<State> new_state(Bool openlibs, U32 seed, Range<1 << 20, 256 << 20> limit) {
    auto* s = new StateT();
    s->limit = (size_t)limit;
    s->L = lua_newstate(lh::alloc, s, (unsigned)seed);
    if (!s->L) {
        delete s;
        stitch::bail("lua_newstate failed");
    }
    if (openlibs) {
        lua_pushcfunction(s->L, [](lua_State* L) {
            luaL_openlibs(L);
            return 0;
        });
        if (lua_pcall(s->L, 0, 0, 0) != LUA_OK) lua_pop(s->L, 1);
    }
    return s;
}
STITCH_ENDPOINT(new_state);

void close_state(Borrow<State> s) {
    lua_close(s->L);
    delete *s;
    s.consume();
}
STITCH_ENDPOINT(close_state);

void gc(Borrow<State> s, OneOf<LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT, LUA_GCCOUNT, LUA_GCCOUNTB, LUA_GCSTEP, LUA_GCISRUNNING, LUA_GCGEN, LUA_GCINC> what,
        Range<0, 4096> arg) {
    lh::Balanced b(s->L);
    if ((int)what == LUA_GCSTEP) lua_gc(s->L, LUA_GCSTEP, (int)arg);
    else lua_gc(s->L, (int)what);
    b.ok();
}
STITCH_ENDPOINT(gc);

void gc_param(Borrow<State> s, Range<0, LUA_GCPN - 1> param, Range<-1, 10000> value) {
    lua_gc(s->L, LUA_GCPARAM, (int)param, (int)value);
}
STITCH_ENDPOINT(gc_param);

void set_global(Borrow<Ref> v, Str<32> name) {
    lh::state_of(v);
    lh::Balanced b(v->L);
    lh::push(v);
    lh::protect(v->L, 1, 0, [&](lua_State* L) {
        lua_setglobal(L, name.c_str());
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(set_global);

lh::Out get_global(Borrow<State> s, Str<32> fresh, Src<8> pick) {
    lh::Balanced b(s->L);
    lh::protect(s->L, 0, 1, [&](lua_State* L) {
        // mostly an existing global (string, table, math, _G, ...), sometimes a fresh name
        std::string name = fresh.s;
        if (pick->coin(0.85)) {
            lua_pushglobaltable(L);
            std::vector<std::string> names;
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_type(L, -2) == LUA_TSTRING) names.push_back(lua_tostring(L, -2));
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            if (!names.empty()) name = names[pick->choice(names.size())];
        }
        lua_getglobal(L, name.c_str());
        return 1;
    });
    auto r = lh::out(s, s->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(get_global);
