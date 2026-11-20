#include "../objects.hpp"

// One constructor per kind of value, so the engine can learn which producers work for which
// consumers (arith wants numbers, iterate wants tables, ...).
template <class F>
static lh::Out make(Borrow<State>& s, F push) {
    lh::Balanced b(s->L);
    lh::protect(s->L, 0, 1, [&](lua_State* L) {
        push(L);
        return 1;
    });
    auto r = lh::out(s, s->L);
    b.ok();
    return r;
}

lh::Out new_number(Borrow<State> s, F64 d) { return make(s, [&](lua_State* L) { lua_pushnumber(L, d); }); }
STITCH_ENDPOINT(new_number);
lh::Out new_integer(Borrow<State> s, I64 i) { return make(s, [&](lua_State* L) { lua_pushinteger(L, i); }); }
STITCH_ENDPOINT(new_integer);
lh::Out new_string(Borrow<State> s, Bytes<1024> str) {
    return make(s, [&](lua_State* L) { lua_pushlstring(L, (const char*)str.data(), str.size()); });
}
STITCH_ENDPOINT(new_string);
lh::Out new_table(Borrow<State> s, Range<0, 256> narr, Range<0, 256> nrec) {
    return make(s, [&](lua_State* L) { lua_createtable(L, (int)narr, (int)nrec); });
}
STITCH_ENDPOINT(new_table);
lh::Out new_boolean(Borrow<State> s, Bool v) { return make(s, [&](lua_State* L) { lua_pushboolean(L, v); }); }
STITCH_ENDPOINT(new_boolean);
lh::Out new_userdata(Borrow<State> s, Range<0, 256> size, Range<0, 4> nuv) {
    return make(s, [&](lua_State* L) { lua_newuserdatauv(L, (size_t)size, (int)nuv); });
}
STITCH_ENDPOINT(new_userdata);
lh::Out new_formatted(Borrow<State> s, Range<0, 4> fmt, Str<64> str, I64 i, F64 d) {
    return make(s, [&](lua_State* L) {
        switch ((int)fmt) {  // lua_pushfstring with matching arguments only
            case 0: lua_pushfstring(L, "%s:%d", str.c_str(), (int)i); break;
            case 1: lua_pushfstring(L, "%f %%", d); break;
            case 2: lua_pushfstring(L, "%I|%c", (LUAI_UACINT)i, (int)(i & 0x7f)); break;
            case 3: lua_pushfstring(L, "%U", (long)(i & 0x10ffff)); break;
            default: lua_pushfstring(L, "%p", (void*)&s); break;
        }
    });
}
STITCH_ENDPOINT(new_formatted);

void free_ref(Take<Ref> r) {
    if (stitch::get_obj<State>(r, "state")) luaL_unref(r->L, LUA_REGISTRYINDEX, r->ref);
}
STITCH_ENDPOINT(free_ref);

// Conversions and type queries (luaL_tolstring runs __tostring / __name).
void inspect(Borrow<Ref> v) {
    lh::state_of(v);
    lh::Balanced b(v->L);
    lh::push(v);
    lh::protect(v->L, 1, 0, [](lua_State* L) {
        int isnum = 0;
        lua_tonumberx(L, 1, &isnum);
        lua_tointegerx(L, 1, &isnum);
        lua_toboolean(L, 1);
        size_t len = 0;
        const char* s = luaL_tolstring(L, 1, &len);
        stitch::check(s && strlen(s) <= len, "tolstring length");
        char buf[LUA_N2SBUFFSZ];
        if (lua_type(L, 1) == LUA_TNUMBER) {
            unsigned n = lua_numbertocstring(L, 1, buf);
            stitch::check(n < sizeof buf, "numbertocstring fits");
        }
        (void)lua_rawlen(L, 1);
        (void)lua_typename(L, lua_type(L, 1));
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(inspect);

lh::Out arith(Borrow<Num> a, Borrow<Num> c, OneOf<LUA_OPADD, LUA_OPSUB, LUA_OPMUL, LUA_OPMOD, LUA_OPPOW, LUA_OPDIV, LUA_OPIDIV, LUA_OPBAND, LUA_OPBOR, LUA_OPBXOR, LUA_OPSHL, LUA_OPSHR, LUA_OPUNM, LUA_OPBNOT> op) {
    lh::same_state(a, c);
    StateT* s = lh::state_of(a);
    lh::Balanced b(a->L);
    lh::push(a);
    lh::push(c);
    lh::protect(a->L, 2, 1, [&](lua_State* L) {
        if ((int)op == LUA_OPUNM || (int)op == LUA_OPBNOT) lua_pop(L, 1);  // unary: one operand
        lua_arith(L, (int)op);
        return 1;
    });
    auto r = lh::out(s, a->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(arith);

void compare(Borrow<Ref> a, Borrow<Ref> c, OneOf<LUA_OPEQ, LUA_OPLT, LUA_OPLE> op) {
    lh::same_state(a, c);
    lh::state_of(a);
    lh::Balanced b(a->L);
    lh::push(a);
    lh::push(c);
    lh::protect(a->L, 2, 0, [&](lua_State* L) {
        int r = lua_compare(L, 1, 2, (int)op);
        if ((int)op == LUA_OPEQ && lua_rawequal(L, 1, 2)) stitch::check(r == 1 || lua_getmetatable(L, 1) || lua_type(L, 1) == LUA_TNUMBER, "raw equal implies equal");
        return 0;
    });
    b.ok();
}
STITCH_ENDPOINT(compare);

lh::Out concat(Borrow<Ref> a, Borrow<Ref> c, Range<0, 2> n) {
    lh::same_state(a, c);
    StateT* s = lh::state_of(a);
    lh::Balanced b(a->L);
    lh::push(a);
    lh::push(c);
    lh::protect(a->L, 2, 1, [&](lua_State* L) {
        lua_settop(L, (int)n);
        lua_concat(L, (int)n);
        return 1;
    });
    auto r = lh::out(s, a->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(concat);

lh::Out string_to_number(Borrow<State> s, Str<64> text) {
    lh::Balanced b(s->L);
    lh::protect(s->L, 0, 1, [&](lua_State* L) {
        size_t n = lua_stringtonumber(L, text.c_str());
        if (n == 0) lua_pushnil(L);
        else stitch::check(n == strlen(text.c_str()) + 1, "stringtonumber consumed the whole string");
        return 1;
    });
    auto r = lh::out(s, s->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(string_to_number);

// luaL_Buffer: interleave adds of strings, chars, values and buffer prep.
lh::Out buffer_build(Borrow<State> s, Src<512> ops) {
    lh::Balanced b(s->L);
    lh::protect(s->L, 0, 1, [&](lua_State* L) {
        luaL_Buffer buf;
        luaL_buffinit(L, &buf);
        size_t total = 0;
        while (ops->more() && total < (1 << 16)) {
            switch (ops->choice(4)) {
                case 0: { std::string x = ops->str(64); luaL_addlstring(&buf, x.data(), x.size()); total += x.size(); break; }
                case 1: luaL_addchar(&buf, (char)ops->u8()); total++; break;
                case 2: { size_t n = (size_t)ops->range(0, 4096); char* p = luaL_prepbuffsize(&buf, n); memset(p, 'x', n); luaL_addsize(&buf, n); total += n; break; }
                default: lua_pushinteger(L, (lua_Integer)ops->u32()); luaL_addvalue(&buf); break;
            }
        }
        luaL_pushresult(&buf);
        return 1;
    });
    auto r = lh::out(s, s->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(buffer_build);

lh::Out gsub(Borrow<State> s, Str<256> text, Str<32, LuaPattern> pat, Str<16> rep) {
    lh::Balanced b(s->L);
    lh::protect(s->L, 0, 1, [&](lua_State* L) {
        stitch::require(strlen(pat.c_str()) > 0, "empty pattern loops forever (luaL_gsub is plain-text)");
        luaL_gsub(L, text.c_str(), pat.c_str(), rep.c_str());
        return 1;
    });
    auto r = lh::out(s, s->L);
    b.ok();
    return r;
}
STITCH_ENDPOINT(gsub);
