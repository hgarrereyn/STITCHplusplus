// Lua C API harness. Lua values are kept as registry references (Ref); each stub pushes what it
// needs, runs the operation inside lua_pcall (errors from metamethods must not escape
// unprotected), stores results as new refs, and checks the stack is balanced afterwards.
#pragma once
#include <stitch.h>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

// main state + a memory cap (a custom allocator: scripts can allocate a lot)
struct StateT {
    lua_State* L;
    size_t used = 0, limit = 64u << 20;
};
STITCH_OBJECT(State, StateT*);
STITCH_DROP(State, [](StateT* s) { lua_close(s->L); delete s; });

struct RefT {
    lua_State* L;  // main state
    int ref;
};
STITCH_OBJECT(Ref, RefT);  // any value; freed by lua_close (no drop: the state may be closed first)
// Typed views of the same value (own registry refs), produced next to the generic Ref when the
// runtime type matches, so consumers can ask for the type they need.
STITCH_OBJECT(Table, RefT);
STITCH_OBJECT(Func, RefT);
STITCH_OBJECT(Num, RefT);
STITCH_OBJECT(UData, RefT);

struct ThreadT {
    lua_State* L;  // main state
    lua_State* co;
    int ref;  // anchors the thread in the registry
};
STITCH_OBJECT(Thread, ThreadT);

STITCH_DATATYPE(LuaSource);
STITCH_DATATYPE(LuaPattern);

namespace lh {
inline void* alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* s = static_cast<StateT*>(ud);
    if (ptr == nullptr) osize = 0;
    if (nsize == 0) {
        free(ptr);
        s->used -= osize;
        return nullptr;
    }
    if (s->used - osize + nsize > s->limit) return nullptr;
    void* p = realloc(ptr, nsize);
    if (p) s->used = s->used - osize + nsize;
    return p;
}

// Runs `f` inside lua_pcall with `nargs` values already pushed; leaves `nres` results (or bails
// after popping the error). Budget: an instruction-count hook stops runaway code.
inline int budget = 0;
inline void hook(lua_State* L, lua_Debug*) {
    if (--budget < 0) luaL_error(L, "instruction budget exhausted");
}
inline std::function<int(lua_State*)>* g_fn = nullptr;
inline int tramp(lua_State* L) { return (*g_fn)(L); }

inline void protect(lua_State* L, int nargs, int nres, std::function<int(lua_State*)> f) {
    luaL_checkstack(L, nargs + 2, "protect");
    lua_pushcfunction(L, tramp);
    lua_insert(L, -(nargs + 1));
    auto* saved = g_fn;
    g_fn = &f;
    budget = 1000;
    lua_sethook(L, hook, LUA_MASKCOUNT, 1000);
    int rc = lua_pcall(L, nargs, nres, 0);
    lua_sethook(L, nullptr, 0, 0);
    g_fn = saved;
    if (rc != LUA_OK) {
        // bail reason = the error message without its position prefix and quoted names
        static std::string reason;
        const char* m = lua_tostring(L, -1);
        std::string msg = m ? m : "(non-string error)";
        stitch::log("lua error: %s", msg.c_str());
        size_t p = msg.find(": ");
        if (msg.rfind("[string", 0) == 0 && p != std::string::npos) msg = msg.substr(p + 2);
        reason.clear();
        bool quoted = false;
        for (char ch : msg) {
            if (ch == '\'') { quoted = !quoted; reason += quoted ? "'?" : "'"; continue; }
            if (!quoted) reason += ch;
        }
        reason = "Lua: " + reason.substr(0, 70);
        lua_pop(L, 1);
        stitch::bail(reason.c_str());
    }
}

template <class R>
inline StateT* state_of(const R& r) {
    StateT** s = stitch::get_obj<State>(r, "state");
    stitch::require(s != nullptr, "state is gone");
    return *s;
}
template <class R>
inline void push(const R& r) { lua_rawgeti(r->L, LUA_REGISTRYINDEX, r->ref); }

template <class S>
New<Ref> ref_top(const S& state_obj, lua_State* L) {  // pops the top value into a new Ref
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    New<Ref> r(RefT{L, ref});
    stitch::set(r, "state", state_obj);
    return r;
}
template <class S>
std::tuple<New<Ref>, std::optional<New<Table>>, std::optional<New<Func>>, std::optional<New<Num>>, std::optional<New<UData>>> out(const S& st, lua_State* L);

// stack must be balanced when a stub returns
struct Balanced {
    lua_State* L;
    int top;
    explicit Balanced(lua_State* L) : L(L), top(lua_gettop(L)) {}
    void ok() { stitch::check(lua_gettop(L) == top, "stack balanced after the call"); }
};

template <class A, class B>
inline void same_state(const A& a, const B& b) { stitch::require(a->L == b->L, "refs from different states"); }

// Pop the top value into a Ref, plus a typed view of it.
using Out = std::tuple<New<Ref>, std::optional<New<Table>>, std::optional<New<Func>>, std::optional<New<Num>>, std::optional<New<UData>>>;
template <class S>
Out out(const S& st, lua_State* L) {
    int t = lua_type(L, -1);
    std::optional<New<Table>> tab;
    std::optional<New<Func>> fn;
    std::optional<New<Num>> num;
    std::optional<New<UData>> ud;
    if (t == LUA_TTABLE || t == LUA_TFUNCTION || t == LUA_TNUMBER || t == LUA_TUSERDATA) {
        lua_pushvalue(L, -1);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        if (t == LUA_TTABLE) { tab.emplace(RefT{L, ref}); stitch::set(*tab, "state", st); }
        if (t == LUA_TFUNCTION) { fn.emplace(RefT{L, ref}); stitch::set(*fn, "state", st); }
        if (t == LUA_TNUMBER) { num.emplace(RefT{L, ref}); stitch::set(*num, "state", st); }
        if (t == LUA_TUSERDATA) { ud.emplace(RefT{L, ref}); stitch::set(*ud, "state", st); }
    }
    return {ref_top(st, L), std::move(tab), std::move(fn), std::move(num), std::move(ud)};
}
}  // namespace lh
