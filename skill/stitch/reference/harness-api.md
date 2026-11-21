# Harness API

## Project layout

```
stitch.toml          source, build flags, fuzzing options
Dockerfile           builds the library
harness/
  objects.hpp        object and data type declarations
  **/*.cpp           stubs, any folder depth (folders group stubs in reports and `--only`)
  data/<Type>/       seeds/, *.dict and decoder for a data type
  data/global.dict   tokens for buffers without a data type
.stitch/             generated: builds, campaigns, reproducers
```

## stitch.toml

```toml
[project]
name = "cjson"

[source]                  # git or path
git = "https://github.com/DaveGamble/cJSON"
rev = "v1.7.18"           # default version; `stitch build --rev X` overrides it
submodules = false        # check out git submodules
keep_git = false          # keep .git in the source (for builds that run `git describe`)
# path = "../cJSON"       # local checkout, used as-is

[docker]
dockerfile = "Dockerfile"
build_args = {}           # extra --build-arg values
context_files = []        # project files the Dockerfile COPYs (also copied into reproducers)
# platform = "linux/amd64"

[build]                   # how stubs compile and link, paths inside the image
cflags = ["-I/src"]
ldflags = ["/src/build/libcjson.a", "-lm"]
sanitizers = ["address"]  # "address", "undefined"
std = "c++17"

[fuzz]
timeout_ms = 2000         # per program
max_nodes = 24            # calls per program
jobs = 1                  # workers for `stitch fuzz`
exceptions = "finding"    # uncaught C++ exception: "finding" (a crash) or "bail"
persistent = 200          # programs per worker process before it restarts
```

## Objects

```cpp
STITCH_OBJECT(Name, Handle);                    // Handle: usually a pointer; a struct also works
STITCH_DROP(Name, [](Handle h) { free_it(h); });  // optional: frees objects left at the end
```

| In the signature | Meaning |
|---|---|
| `Borrow<T> x` | uses an object; it stays available. `*x` is the handle by reference (assign to it for realloc-style APIs); `x` converts to the handle; `x->field` |
| `x.consume()` | on a `Borrow`: the library took ownership in this call |
| `Take<T> x` | always consumes the object |
| return `New<T>` | produces an object: `return handle;` |
| return `std::tuple<New<A>, std::optional<New<B>>>` | several outputs; return `std::nullopt` for an absent optional one |

- Inputs are never null. Returning a null handle counts as a bail.
- An object is never used after it was consumed.
- If the stub that produces an object bails, calls that need the object are skipped.

## Fuzzed parameters

| Type | Use as |
|---|---|
| `U8 U16 U32 U64 I8 I16 I32 I64` | integer |
| `F32 F64` | float |
| `Bool` | bool |
| `Range<Lo, Hi>` | integer in [Lo, Hi] |
| `OneOf<A, B, ...>` | one of the constants |
| `Flags<A, B, ...>` | OR of any subset |
| `Bytes<Max, Tag = void>` | `.data()`, `.size()`, `.str()` |
| `Str<Max, Tag = void>` | `.c_str()`, `.size()`; converts to `const char*` |
| `File<Max, Tag = void>` | `.path()` of a temporary file holding the data; `.data()` |
| `Raw<T>` | a `T` (plain struct), `->`, `*` |
| `stitch::Val<Tag, Max = 1024>` | the value `Tag`'s decoder returns (any type): `->`, `*`, converts to it |
| `Src<Max>` | a `stitch::Source` for choices inside the stub |

Values start random, not zero. When the library compares an argument against a constant, the
fuzzer finds the constant on its own.

## Control

```cpp
stitch::bail("reason");               // stop this call; not a bug. The program continues.
stitch::require(cond, "reason");      // bail unless cond
stitch::check(cond, "what");          // reported as a crash if false: "check failed: what"
stitch::log("x=%d", x);               // printed by `stitch exec --trace`
STITCH_INIT { ... }                   // runs once per process
```

## Attributes

State stored on an object. It belongs to the object, not its address: a consumed object loses its
attributes, and a new object at a reused address starts empty.

```cpp
stitch::set(obj, "count", 1);          // obj: Borrow<T>, Take<T>, New<T>, or a handle pointer
stitch::set(obj, "name", "text");
stitch::set(chan, "session", sess);    // link to another object
stitch::get(obj, "count", 0);          // int64_t; 0 if missing
stitch::get(obj, "count");             // missing reads are reported by `probe`
stitch::get_str(obj, "name", "");
Session::handle* s = stitch::get_obj<Session>(chan, "session");  // nullptr if missing or consumed
stitch::has(obj, "count");
stitch::del(obj, "count");
stitch::global_set("k", 1);
stitch::global_get("k", 0);
```

## Data types and decoders

```cpp
STITCH_DATATYPE(JsonText);       // folder: harness/data/JsonText/
STITCH_DECODER(JsonText, fn);    // fn: (stitch::Source&) -> std::string or std::vector<uint8_t>
```

A decoder can return any type. Such a data type is a parameter as `stitch::Val<Tag, Max>`, and decoders
build on each other with `s.draw<Tag>()`. Write it `stitch::Val` (it is not exported globally, so a
harness may have its own `Val`):

```cpp
struct Point { int x, y; };
STITCH_DATATYPE(AnyPoint);
STITCH_DECODER(AnyPoint, [](stitch::Source& s) { return Point{(int)s.range(-100, 100), (int)s.range(-100, 100)}; });
STITCH_DATATYPE(Polygon);
STITCH_DECODER(Polygon, [](stitch::Source& s) {
    std::vector<Point> v;
    while (s.more()) v.push_back(s.draw<AnyPoint>());
    return v;
});

void fill(Borrow<Canvas> c, stitch::Val<Polygon, 512> poly) { canvas_fill(c, poly->data(), poly->size()); }
```

Put a typed decoder where the stubs that use it can see it (a header). Its values are always
stored as choices: the engine mutates single values, and inserts, deletes, duplicates or swaps the
iterations of its `more()` loops (here: whole points). Seeds (`seeds/`) are for string and byte data
types only.

`stitch::Source` methods:

| Method | Returns |
|---|---|
| `u8()` .. `u64()`, `i8()` .. `i64()`, `f32()`, `f64()` | a number |
| `range(lo, hi)` | integer in [lo, hi] |
| `choice(n)` | index in [0, n) |
| `pick({a, b, c})` | one of the values |
| `coin(p = 0.5)` | true with probability about p |
| `more()` | loop condition: `while (s.more()) { ... }` |
| `f64(lo, hi)`, `f32(lo, hi)` | finite float in [lo, hi] |
| `str(max)`, `bytes(max)` | literal text or bytes |
| `fixed(n)` | exactly n bytes |
| `rest()` | every byte that is left |
| `token()` | a token from the data type's `.dict` files |
| `draw<Tag>()` | a value from data type `Tag`'s decoder |

Running out of choices is fine: later draws decode from zero bytes and `more()` returns false, so
every input decodes to some value.

A data type's values come from the decoder, `seeds/`, and byte-level mutations of decoded values.

## Programs

`.stp` files store calls by stub name, so corpora survive rebuilds and new stubs.
`stitch exec FILE --trace` prints a program call by call.
`.stitch/build/manifest.json` lists the stubs as the fuzzer sees them: inputs, outputs and parameters.
`stitch cov [FILES]` runs programs on a coverage build and writes `lcov.info` and an HTML report.
