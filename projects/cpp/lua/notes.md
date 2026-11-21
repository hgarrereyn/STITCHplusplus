# Lua (lua/lua @ 104b0fc, 5.5.0-dev, built with LUAI_ASSERT + LUA_USE_APICHECK)

38 endpoints in `state/`, `values/`, `tables/`, `code/`, `coro/`. Lua values are registry
references. Each stub pushes what it needs, runs the operation inside `lua_pcall` (metamethods can
raise errors), stores results as new references, and checks that the stack is balanced. A counting
allocator caps memory, and an instruction-count hook stops runaway code. Text chunks only, since
binary chunks are unsafe by design: only our own `lua_dump` output is loaded as binary.

Decoders: `LuaSource` (standard library, metatables, closures, coroutines, `<close>` variables)
and `LuaPattern`. Seeds: a few files from Lua's `testes/`.

## Findings

No library bugs found (45-minute campaign, 910k executions, 6.6k edges).

Harness fix from triage: `luaL_gsub` is a plain-text replacement and loops forever on an empty
pattern. The guard checked `size() > 0`, but a pattern that starts with NUL is an empty C string.

## Harness design lessons (from `stitch probe`)

- One `new_value(kind)` endpoint hid which producers work. Splitting it into
  `new_number`/`new_table`/... lets the engine learn producer → consumer success rates.
- **Typed views** for dynamically typed results: producers return
  `std::tuple<New<Ref>, optional<New<Table>>, optional<New<Func>>, optional<New<Num>>, optional<New<UData>>>`,
  chosen by `lua_type`, and consumers ask for the type they need. This took calls that bail from
  22% to 10%, programs containing a bail from 86% to 48%, and raised coverage.
- Bail reasons carry the normalized Lua error message ("attempt to index a nil value"), so the probe
  report shows *why* calls fail.
- `get_global` mostly picks an existing global, since random names are all nil.
