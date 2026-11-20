#include "../objects.hpp"

lh::Out get(Borrow<Table> t, Borrow<Ref> k, Bool raw) {
    lh::same_state(t, k);
    StateT* s = lh::state_of(t);
    lh::Balanced b(t->L);
    lh::push(t);
    lh::push(k);
    lh::protect(t->L, 2, 1, [&](lua_State* L) {
        if (raw) luaL_checktype(L, 1, LUA_TTABLE);
        if (raw) lua_rawget(L, 1);
        else lua_gettable(L, 1);
        return 1;
    });
    auto r = lh::out(s, t->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(get);

void set(Borrow<Table> t, Borrow<Ref> k, Borrow<Ref> v, Bool raw) {
    lh::same_state(t, k);
    lh::same_state(t, v);
    lh::state_of(t);
    lh::Balanced b(t->L);
    lh::push(t);
    lh::push(k);
    lh::push(v);
    lh::protect(t->L, 3, 0, [&](lua_State* L) {
        if (raw) {
            luaL_checktype(L, 1, LUA_TTABLE);
            luaL_argcheck(L, !lua_isnil(L, 2), 2, "nil key");
            lua_rawset(L, 1);
        } else lua_settable(L, 1);
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(set);

void set_index(Borrow<Table> t, I64 idx, Borrow<Ref> v, Bool raw) {
    lh::same_state(t, v);
    lh::state_of(t);
    lh::Balanced b(t->L);
    lh::push(t);
    lh::push(v);
    lh::protect(t->L, 2, 0, [&](lua_State* L) {
        if (raw) {
            luaL_checktype(L, 1, LUA_TTABLE);
            lua_rawseti(L, 1, idx);
        } else lua_seti(L, 1, idx);
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(set_index);

lh::Out get_index(Borrow<Table> t, I64 idx, Bool raw) {
    StateT* s = lh::state_of(t);
    lh::Balanced b(t->L);
    lh::push(t);
    lh::protect(t->L, 1, 1, [&](lua_State* L) {
        if (raw) {
            luaL_checktype(L, 1, LUA_TTABLE);
            lua_rawgeti(L, 1, idx);
        } else lua_geti(L, 1, idx);
        return 1;
    });
    auto r = lh::out(s, t->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(get_index);

// Iterate with lua_next; the count must match a second traversal.
void iterate(Borrow<Table> t) {
    lh::state_of(t);
    lh::Balanced b(t->L);
    lh::push(t);
    lh::protect(t->L, 1, 0, [](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        int n1 = 0, n2 = 0;
        lua_pushnil(L);
        while (lua_next(L, 1)) { n1++; lua_pop(L, 1); }
        lua_pushnil(L);
        while (lua_next(L, 1)) { n2++; lua_pop(L, 1); }
        stitch::check(n1 == n2, "lua_next visits the same number of keys twice");
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(iterate);

void length(Borrow<Ref> t) {
    lh::state_of(t);
    lh::Balanced b(t->L);
    lh::push(t);
    lh::protect(t->L, 1, 0, [](lua_State* L) {
        lua_len(L, 1);
        lua_pop(L, 1);
        (void)luaL_len(L, 1);
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(length);

void set_metatable(Borrow<Ref> t, Borrow<Table> mt) {
    lh::same_state(t, mt);
    lh::state_of(t);
    lh::Balanced b(t->L);
    lh::push(t);
    lh::push(mt);
    lh::protect(t->L, 2, 0, [](lua_State* L) {
        luaL_argcheck(L, lua_istable(L, 2) || lua_isnil(L, 2), 2, "metatable must be a table or nil");
        lua_setmetatable(L, 1);
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(set_metatable);

std::optional<New<Ref>> get_metatable(Borrow<Ref> v) {
    StateT* s = lh::state_of(v);
    lh::Balanced b(v->L);
    lh::push(v);
    bool has = false;
    lh::protect(v->L, 1, 1, [&](lua_State* L) {
        has = lua_getmetatable(L, 1);
        if (!has) lua_pushnil(L);
        return 1;
    });
    if (!has) {
        lua_pop(v->L, 1);
        b.ok();
        return std::nullopt;
    }
    auto r = lh::ref_top(s, v->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(get_metatable);

void user_value(Borrow<UData> u, Borrow<Ref> v, Range<0, 4> n, Bool set) {
    lh::same_state(u, v);
    lh::state_of(u);
    lh::Balanced b(u->L);
    lh::push(u);
    lh::push(v);
    lh::protect(u->L, 2, 0, [&](lua_State* L) {
        luaL_checktype(L, 1, LUA_TUSERDATA);
        if (set) lua_setiuservalue(L, 1, (int)n);
        else { lua_getiuservalue(L, 1, (int)n); lua_pop(L, 1); }
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(user_value);
