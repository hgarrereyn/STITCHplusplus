# STITCH harness reference

## Layout

```
stitch.toml               source, build flags, fuzzing options
Dockerfile                builds the library (FROM ${STITCH_BASE}; COPY --from=source . /src)
harness/
  objects.hpp             object + data type declarations (convention)
  **/*.cpp                stubs (any folder depth; folders group endpoints for --only / reports)
  data/<Type>/            seeds/, *.dict, decoder sources (any .cpp in harness/ is compiled)
  data/global.dict        tokens for untyped buffers
.stitch/                  generated (build outputs, campaigns, reproducers, bisect results)
```

## stitch.toml

```toml
[project]
name = "cjson"

[source]                  # exactly one of git / path
git = "https://github.com/DaveGamble/cJSON"
rev = "v1.7.18"           # default revision; `stitch build --rev X` overrides
submodules = false        # check out submodules
keep_git = false          # keep .git in the source context (for builds that run `git describe`)
# path = "../cJSON"       # local checkout used as-is

[docker]
dockerfile = "Dockerfile"
build_args = {}           # extra --build-arg values
context_files = []        # files the Dockerfile COPYs from the project dir (copied into reproducers)
# platform = "linux/amd64"

[build]                   # harness compile/link, inside the project image
cflags = ["-I/src"]
ldflags = ["/src/build/libcjson.a", "-lm"]
sanitizers = ["address"]  # "address", "undefined" (library and harness are built with these)
std = "c++17"
# sources = ["harness/**/*.cpp"]

[fuzz]
timeout_ms = 2000         # per program
max_nodes = 24            # calls per program
jobs = 1                  # default workers for `stitch fuzz`
exceptions = "finding"    # uncaught C++ exceptions: "finding" (crash) or "bail"
persistent = 200          # programs per forked worker process (0: fork for every program)
```

## Objects

```cpp
STITCH_OBJECT(Name, HandleType);       // HandleType: a pointer (usual) or a struct bundling several
STITCH_DROP(Name, [](HandleType h) { ... });   // optional finalizer for objects alive at the end
```

| In a signature | Meaning |
|---|---|
| `Borrow<T> x` | use an object, it stays in the pool. `*x` is a reference to the pooled handle (reassign for realloc-style APIs); `x` converts to the handle; `x->f` accesses fields |
| `x.consume()` | (on a `Borrow`) the library took ownership in this call: remove the object from the pool |
| `Take<T> x` | always consumes the object |
| return `New<T>` | produce one object (`return handle;`) |
| return `std::tuple<New<A>, std::optional<New<B>>>` | several outputs; optional ones may be absent (`std::nullopt`) |

Inputs are never null. A null returned handle makes the call a bail. A program never uses an object after a
`Take`. Objects whose producer bailed are missing; calls that need them are skipped.

## Fuzz parameters

| Type | Value | Stored as (mutated bytes) |
|---|---|---|
| `U8 U16 U32 U64 I8 I16 I32 I64` | integer | raw little-endian |
| `F32 F64` | float | raw |
| `Bool` | bool | 1 byte |
| `Range<Lo, Hi>` | integer in [Lo, Hi] | 8 bytes; valid values decode to themselves |
| `OneOf<A, B, ...>` | one of the constants | 8 bytes; the value itself |
| `Flags<A, B, ...>` | OR of a subset | 8 bytes, masked |
| `Bytes<Max, Tag = void>` | `.data() .size() .str()` | bytes, or decoder choices |
| `Str<Max, Tag = void>` | `.c_str() .size()`, converts to `const char*` | bytes, or decoder choices |
| `File<Max, Tag = void>` | `.path()` of a temp file with the bytes; `.data()` | bytes, or decoder choices |
| `Raw<T>` | `T` (trivially copyable), `->`, `*` | `sizeof(T)` bytes |
| `Src<Max>` | a `stitch::Source` for in-stub choices | choice bytes |

All values start random/"interesting" (not zero). Integers compared by the library are solved by cmplog
directly in these buffers.

## Control

```cpp
stitch::bail("reason");               // the call fails legitimately; rest of the program continues
stitch::require(cond, "reason");      // bail unless cond
stitch::check(cond, "what");          // oracle: failure is a crash ("check failed: what")
stitch::log("fmt %d", x);             // printed by `stitch exec --trace`
STITCH_INIT { ... }                   // once per process, before fuzzing
```

## Attributes

Free-form state attached to pooled objects (identity, not address: a consumed object's attributes are gone;
a returned pointer that equals a consumed one is a fresh object).

```cpp
stitch::set(obj, "key", 1);           // obj: Borrow<T>, Take<T>, New<T>, or a pointer handle (also one you
stitch::set(obj, "name", "text");     //      are about to return)
stitch::set(chan, "session", sess);   // relation to another object
stitch::get(obj, "key", def);         // int64; `def` if missing (missing reads are reported by probe)
stitch::get_str(obj, "name", def);
Session::handle* s = stitch::get_obj<Session>(chan, "session");   // nullptr if missing or consumed
stitch::has(obj, "key"); stitch::del(obj, "key");
stitch::global_set("k", 1); stitch::global_get("k", 0); stitch::global_get_str(...); stitch::global_has(...);
```

## Data types and decoders

```cpp
STITCH_DATATYPE(JsonText);                         // folder: harness/data/JsonText/
STITCH_DECODER(JsonText, fn);                      // fn: (stitch::Source&) -> std::string or bytes
```

`stitch::Source` draws (identity-preserving encoding, so LibAFL's havoc/token/I2S mutators work on it):

| Draw | Encoding |
|---|---|
| `u8() u16() u32() u64() i8..i64() f32() f64()` | raw little-endian |
| `range(lo, hi)`, `choice(n)`, `pick({..})` | 8 raw bytes; valid values used as-is |
| `coin(p)` | 1 byte |
| `more()` | 1 byte; loop continuation; false once the buffer is exhausted |
| `str(max)`, `bytes(max)` | length prefix + verbatim bytes |
| `token()` | like `str`; fresh values come from the type's dictionary (+ tokens learned by cmplog) |

When the buffer runs out every draw returns its minimum, so truncated or mutated buffers still decode.
Sources: values for a typed parameter come from the decoder (fresh or mutated choices), `seeds/`, raw-byte
mutations of decoded values, and cross-pollination between parameters of the same type.

## Manifest

`.stitch/build/manifest.json` (from `fuzzer manifest`) is the engine's view: objects, datatypes, endpoints
with inputs (`type`, `mode`), params (`kind`, `size`, `datatype`, `name`, ranges/values) and outputs.

## Programs

`.stp` files: endpoint names (not indices), so corpora survive rebuilds and new stubs. Inputs refer to
`(earlier call, output index)`. `stitch exec FILE --trace` prints a program call by call.
