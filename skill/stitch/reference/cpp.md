# C/C++ harnesses

Stubs are C++ functions registered with `STITCH_ENDPOINT`. The function signature is the spec.
The library is built in Docker with clang, AddressSanitizer and coverage instrumentation, and the
fuzzer runs the stubs in its own process.

## Project setup

```sh
stitch init cjson --git https://github.com/DaveGamble/cJSON --rev v1.7.18
cd cjson
```

`Dockerfile`: build the library as a static library. The source is copied in from the `source`
build context.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN cmake -B build -DBUILD_SHARED_LIBS=OFF -DENABLE_CJSON_UTILS=ON -DENABLE_CJSON_TEST=OFF \
 && cmake --build build -j$(nproc)
```

- `CC` and `CXX` already add coverage and AddressSanitizer. Do not override them.
- Other build systems: `RUN ./configure --disable-shared && make -j$(nproc)`.
- System packages: `RUN apt-get update && apt-get install -y libfoo-dev` (the image is Ubuntu 24.04).
- A build tool that must not be instrumented: `RUN STITCH_NO_INSTR=1 make gen-tool`.

`stitch.toml`: tell the harness how to compile and link. Paths are inside the image.

```toml
[project]
name = "cjson"

[source]
git = "https://github.com/DaveGamble/cJSON"
rev = "v1.7.18"            # default version; `stitch build --rev X` builds another one
# path = "../cJSON"        # instead of git: a local checkout, uncommitted changes included
# submodules = true        # check out git submodules
# keep_git = true          # keep .git in /src (for builds that run `git describe`)

[build]
cflags = ["-I/src"]
ldflags = ["/src/build/libcjson_utils.a", "/src/build/libcjson.a", "-lm"]
sanitizers = ["address"]   # add "undefined" for UBSan
std = "c++17"

[docker]
# build_args = { WITH_ZLIB = "1" }   # extra --build-arg values
# context_files = ["patch.diff"]     # project files the Dockerfile COPYs

[fuzz]
timeout_ms = 2000          # per program; slower programs are reported as timeouts
max_nodes = 24             # calls per program
jobs = 1                   # workers for `stitch fuzz`
exceptions = "finding"     # an uncaught C++ exception is a bug; "bail" makes it a failed call
```

Check the library build alone with `stitch build --image-only`. If it fails, the log path is
printed; `stitch shell` opens a shell in the image to try commands.

## Files

```
harness/
  objects.hpp          object types and data types, included by every stub file
  parse/parse.cpp      stubs; any folder depth (folders group stubs in reports)
  tree/edit.cpp
  data/JsonText/       seeds/, *.dict for the JsonText data type
  data/global.dict     tokens for strings and bytes without a data type
```

## A complete example

```cpp
// harness/objects.hpp
#pragma once
#include <stitch.h>
#include <cJSON.h>

STITCH_OBJECT(Json, cJSON*);                            // object type Json, handle type cJSON*
STITCH_DROP(Json, [](cJSON* j) { cJSON_Delete(j); });   // frees Json objects left at the end
STITCH_DATATYPE(JsonText);                              // data type: harness/data/JsonText/
```

```cpp
// harness/parse/parse.cpp
#include "../objects.hpp"

New<Json> parse(Str<4096, JsonText> text) {
    cJSON* j = cJSON_Parse(text.c_str());
    stitch::require(j, "parse error");          // not a bug: invalid JSON
    return j;
}
STITCH_ENDPOINT(parse);

New<Json> new_number(F64 v) { return cJSON_CreateNumber(v); }
STITCH_ENDPOINT(new_number);

void add_item(Borrow<Json> obj, Str<64> key, Take<Json> item) {
    if (!cJSON_IsObject(obj) || !cJSON_AddItemToObject(obj, key.c_str(), item)) {
        cJSON_Delete(item);                     // we still own `item`: free it before bailing
        stitch::bail("add failed");
    }
}
STITCH_ENDPOINT(add_item);

void delete_member(Borrow<Json> obj, Src<16> s) {
    int n = cJSON_GetArraySize(obj);
    stitch::require(cJSON_IsObject(obj) && n > 0, "no members");
    cJSON_DeleteItemFromArray(obj, (int)s->choice(n));   // pick an existing member
}
STITCH_ENDPOINT(delete_member);

void roundtrip(Borrow<Json> j) {
    char* out = cJSON_PrintUnformatted(j);
    stitch::require(out, "print failed");
    cJSON* again = cJSON_Parse(out);
    free(out);
    stitch::check(again != nullptr, "printed JSON parses");   // a bug if false
    cJSON_Delete(again);
}
STITCH_ENDPOINT(roundtrip);
```

## Objects

```cpp
STITCH_OBJECT(Json, cJSON*);                            // the handle is usually a pointer
STITCH_DROP(Json, [](cJSON* j) { cJSON_Delete(j); });   // optional: frees objects left at the end

struct Buffer { char* data; size_t len; };
STITCH_OBJECT(Buf, Buffer);                             // a struct works too
STITCH_DROP(Buf, [](Buffer b) { free(b.data); });
```

| In the signature | Meaning |
|---|---|
| `Borrow<T> x` | uses an object; it stays available for later calls |
| `Take<T> x` | consumes the object: free it or hand it to the library, also on bail paths |
| `x.consume()` | on a `Borrow`: the library took ownership in this call |
| return `New<T>` | produces an object: `return handle;` |
| return `std::tuple<New<A>, std::optional<New<B>>>` | several outputs; return `std::nullopt` for a missing optional one |

Using a `Borrow<T>` or `Take<T>`:

- `x` converts to the handle: `cJSON_IsObject(x)`.
- `x->field` reads or writes a field of a pointer or struct handle.
- `*x` is a reference to the stored handle. Assign to it when the library replaces the handle
  (realloc-style APIs): `*v = vec_grow(*v, n);`.

```cpp
New<Buf> new_buf(Range<1, 64> n) { return Buffer{(char*)calloc(n, 1), (size_t)n}; }
STITCH_ENDPOINT(new_buf);

void grow(Borrow<Buf> b, Range<1, 128> n) {
    b->data = (char*)realloc(b->data, n);       // updates the stored handle
    b->len = n;
}
STITCH_ENDPOINT(grow);
```

Rules the runtime guarantees:

- An input is never null. Returning a null handle counts as a bail.
- A consumed object is never used again.
- When the producer of an object bails, the calls that need that object are skipped.
- An object input cannot be optional. Write two stubs, one with the input and one without.

Conditional ownership:

```cpp
void add_item_ref(Borrow<Json> obj, Str<64> key, Borrow<Json> item) {
    stitch::require(cJSON_AddItemToObject(obj, key.c_str(), item), "add failed");
    item.consume();                           // only consumed when the call succeeded
}
STITCH_ENDPOINT(add_item_ref);
```

Several outputs, one of them optional:

```cpp
STITCH_OBJECT(Item, cJSON*);

std::tuple<New<Json>, std::optional<New<Item>>> parse_first(Str<256, JsonText> text) {
    cJSON* j = cJSON_Parse(text.c_str());
    stitch::require(j, "parse error");
    if (!j->child) return {j, std::nullopt};
    return {j, New<Item>(j->child)};
}
STITCH_ENDPOINT(parse_first);
```

## Fuzzed parameters

| Type | Value | Use |
|---|---|---|
| `U8 U16 U32 U64 I8 I16 I32 I64` | integer | converts to the integer |
| `F32 F64` | float | converts to the float |
| `Bool` | bool | converts to bool |
| `Range<Lo, Hi>` | integer in [Lo, Hi] | converts to the integer |
| `OneOf<A, B, ...>` | one of the constants | converts to the constant's type |
| `Flags<A, B, ...>` | OR of any subset of the flags | converts to the integer |
| `Str<Max, Tag>` | string of at most `Max` bytes; `Tag` is an optional data type | `.c_str()`, `.size()`, converts to `const char*` |
| `Bytes<Max, Tag>` | byte buffer | `.data()`, `.size()`, `.str()` |
| `File<Max, Tag>` | temporary file holding the data | `.path()`, `.data()`, `.size()` |
| `Raw<T>` | the bytes of a plain struct `T` | `p->field`, `*p` |
| `stitch::Val<Tag, Max = 1024>` | the value `Tag`'s decoder returns (any type) | `v->`, `*v` |
| `Src<Max>` | a `stitch::Source` for choices inside the stub | `s->choice(n)`, `(*s).str(8)` |

```cpp
void print_opts(Borrow<Json> j, OneOf<0, 1> formatted, Range<1, 64> prebuffer) {
    char* s = cJSON_PrintBuffered(j, prebuffer, formatted);
    stitch::require(s, "print failed");
    free(s);
}
STITCH_ENDPOINT(print_opts);

void load_file(File<4096, JsonText> f) {
    FILE* fp = fopen(f.path(), "rb");
    stitch::require(fp, "open failed");
    fclose(fp);
}
STITCH_ENDPOINT(load_file);

struct Opts { int depth; unsigned flags; };
void configure(Raw<Opts> o) { lib_configure(o->depth, o->flags); }
STITCH_ENDPOINT(configure);
```

Values start random, not zero. When the library compares an argument against a constant
(`if (mode == 0x1234)`), the fuzzer finds the constant on its own.

## Bail, require, check

```cpp
stitch::bail("reason");               // stop this call; not a bug. The program continues.
stitch::require(cond, "reason");      // bail unless cond
stitch::check(cond, "what");          // a bug if false, reported as "check failed: what"
stitch::log("n=%d", n);               // printed by `stitch exec --trace`

STITCH_INIT { lib_global_init(); }    // runs once per process, before any stub
```

These are bugs: AddressSanitizer reports (and UBSan with `sanitizers = ["address", "undefined"]`),
other crashes (segfault, abort, failed `assert`), uncaught C++ exceptions (unless
`exceptions = "bail"`), failed `stitch::check`, and programs that run past `timeout_ms`.

## Attributes

Attributes store state on an object that the library does not expose. They belong to the object:
a consumed object loses them.

```cpp
stitch::set(obj, "refs", stitch::get(obj, "refs", 0) + 1);       // integer, with a default
stitch::require(stitch::get(obj, "refs", 0) == 0, "object is referenced");
stitch::set(obj, "name", "text");                                // string
std::string name = stitch::get_str(obj, "name", "");
stitch::has(obj, "refs");
stitch::del(obj, "refs");
stitch::global_set("mode", 1);                                   // program-wide; reset for every program
stitch::global_get("mode", 0);
```

`obj` is a `Borrow<T>`, `Take<T>`, `New<T>` or a handle pointer. Without a default,
`stitch::get(obj, "k")` reports a missing key in `stitch probe` ("read missing attr 'k'"); use it
when the key must have been set.

Links to other objects: store a link to an input, read it back with `get_obj`. It returns
`nullptr` once the linked object was consumed.

```cpp
New<Item> first_child(Borrow<Json> root) {
    stitch::require(root->child, "no children");
    New<Item> it(root->child);
    stitch::set(it, "root", root);              // link the new Item to its root
    return it;
}
STITCH_ENDPOINT(first_child);

void print_item(Borrow<Item> it) {
    cJSON** root = stitch::get_obj<Json>(it, "root");
    stitch::require(root != nullptr, "root is gone");   // the root was deleted: the item is dangling
    char* s = cJSON_Print(it);
    free(s);
}
STITCH_ENDPOINT(print_item);
```

The link target must already exist when you set it: an input of the call (`Borrow`/`Take`).
A link to another output of the same call does not work and reads as missing.

## Data types and decoders

A data type gives a `Str`, `Bytes` or `File` parameter better inputs. Declare it, tag the
parameter, and add any of these to `harness/data/<Name>/`:

- `seeds/`: sample inputs, one per file. Copy them from the library's tests.
- `*.dict`: tokens, one per line, quoted: `"true"`, `"\x00\x01"`.
- a decoder: a function that builds a value from fuzzer choices, so values stay well formed.

```cpp
STITCH_DATATYPE(JsonText);
New<Json> parse(Str<4096, JsonText> text);    // tagged parameter
```

A decoder takes a `stitch::Source&` and returns `std::string` or `std::vector<uint8_t>`:

```cpp
static std::string value(stitch::Source& s, int depth) {
    switch (s.choice(depth > 4 ? 4 : 6)) {
        case 0: return "null";
        case 1: return s.coin() ? "true" : "false";
        case 2: return std::to_string(s.range(-1000, 1000000));
        case 3: return "\"" + s.str(16) + "\"";
        case 4: { std::string a = "["; while (s.more()) a += value(s, depth + 1) + ","; return a + "]"; }
        default: { std::string o = "{"; while (s.more()) o += "\"" + s.token() + "\":" + value(s, depth + 1) + ","; return o + "}"; }
    }
}
STITCH_DECODER(JsonText, [](stitch::Source& s) { return value(s, 0); });
```

`stitch sample JsonText -n 5` prints values the decoder makes.

### Typed values

A decoder can return any type. Take its value as `stitch::Val<Tag, Max>`, and build decoders on
each other with `s.draw<Tag>()`. Put typed decoders in a header the stubs include.

```cpp
struct Point { int x, y; };
inline std::ostream& operator<<(std::ostream& o, const Point& p) { return o << "(" << p.x << "," << p.y << ")"; }

STITCH_DATATYPE(AnyPoint);
STITCH_DECODER(AnyPoint, [](stitch::Source& s) { return Point{(int)s.range(-100, 100), (int)s.range(-100, 100)}; });

STITCH_DATATYPE(Polygon);
STITCH_DECODER(Polygon, [](stitch::Source& s) {
    std::vector<Point> v;
    while (s.more()) v.push_back(s.draw<AnyPoint>());
    return v;
});

void fill(Borrow<Canvas> c, stitch::Val<Polygon, 512> poly) {
    canvas_fill(c, poly->data(), poly->size());
}
STITCH_ENDPOINT(fill);
```

`operator<<` is optional: with it, `stitch sample Polygon` and traces print
`[(85,4), (28,-100)]` instead of `[<value>, <value>]`. Write `stitch::Val`, not `Val`.

### Source methods

| Method | Returns |
|---|---|
| `u8()` .. `u64()`, `i8()` .. `i64()`, `f32()`, `f64()` | a number |
| `range(lo, hi)` | integer in [lo, hi] |
| `choice(n)` | index in [0, n) |
| `pick({a, b, c})` | one of the values |
| `coin(p = 0.5)` | true with probability about p |
| `more()` | loop condition: `while (s.more()) { ... }` |
| `f64(lo, hi)`, `f32(lo, hi)` | finite float in [lo, hi] |
| `str(max)`, `bytes(max)` | text or bytes of at most `max` bytes |
| `fixed(n)` | exactly n bytes |
| `rest()` | every byte that is left |
| `token()` | a token from the data type's `.dict` files |
| `draw<Tag>()` | a value from data type `Tag`'s decoder |

Running out of choices is fine: later draws return zeros and `more()` returns false.

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/repro.cpp`: every call of the crashing program,
with the stub code pasted in and the arguments filled in. Shrink a copy to plain library calls.
Name it `.c` if the library is C:

```sh
cp .stitch/standalone/c1/repro.cpp .stitch/standalone/c1/min.c
stitch try .stitch/standalone/c1/min.c        # SAME CRASH after each edit
```

```c
#include <cJSON.h>
#include <cJSON_Utils.h>

int main(void) {
    cJSON *from = cJSON_Parse("{\"a\":1,\"c\":2,\"b\":3}");
    cJSON *to = cJSON_Parse("{\"a\":1,\"c\":2,\"d\":4}");
    /* sorts `from`, but the head's prev (the tail pointer) still points to "b" */
    cJSON *patches = cJSONUtils_GeneratePatchesCaseSensitive(from, to);
    /* removing "b" frees it; adding "d" then writes to the freed tail */
    cJSONUtils_ApplyPatchesCaseSensitive(from, patches);
    return 0;
}
```

```
SAME CRASH: .stitch/standalone/c1/min.c -> heap-use-after-free WRITE in suffix_object
```

`stitch try` compiles the file with the `cflags` and `ldflags` from `stitch.toml`. Details and
pitfalls: `stitch guide reproducers`.

## Coverage

`stitch cov` rebuilds the library with clang source-based coverage and no sanitizers, and replays
the corpus. Memory bugs that need ASan to crash run to completion there.
