# Objective-C harnesses

Stubs are C functions declared with `STITCH_ENDPOINT`. The declaration is the spec. clang builds
the library and the harness against libobjc2 and GNUstep Base (Foundation) with ARC,
AddressSanitizer and coverage instrumentation, and the fuzzer runs the stubs in its own process.
An Objective-C exception that escapes a stub is a bug.

## Project setup

```sh
stitch init kvstore --lang objc --git https://github.com/example/KVStore --rev 1.2.0
cd kvstore
```

`Dockerfile`: build the library as a static library. The source is copied in from the `source`
build context.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN for f in Sources/*.m; do $CC -fobjc-arc -ISources -c "$f" -o "${f%.m}.o"; done \
 && ar rcs libkvstore.a Sources/*.o
```

- `CC` and `CXX` add coverage and AddressSanitizer, and for `.m` / `.mm` files the Objective-C
  runtime flags (libobjc2 ABI, blocks, exceptions, Foundation headers). Do not override them.
  Pass `-fobjc-arc` if the library uses ARC (most do).
- The runtime is libobjc2 with GNUstep Base 1.30 and libdispatch (Foundation and GCD; no AppKit,
  UIKit or CoreFoundation-only APIs). Code that uses Apple-only frameworks or types does not build
  as is: define small stand-ins in the build (`-DFloat64=double`) or skip those files.
- CMake, Meson and Makefile builds work when they use `$CC`. Link flags for Objective-C tools
  the build makes: `$(cat /stitch/objc/link-flags)`.
- System packages: `RUN apt-get update && apt-get install -y libfoo-dev` (the image is Ubuntu 24.04).

`stitch.toml`: tell the harness how to compile and link. Paths are inside the image. Foundation
is linked already.

```toml
[project]
name = "kvstore"
language = "objc"

[source]
git = "https://github.com/example/KVStore"
rev = "1.2.0"

[build]
cflags = ["-I/src/Sources"]
ldflags = ["/src/libkvstore.a"]
sanitizers = ["address"]   # add "undefined" for UBSan

# [objc]
# arc = false              # compile the harness without ARC (default: ARC)

[fuzz]
timeout_ms = 2000
exceptions = "finding"     # an escaped Objective-C exception is a bug; "bail" makes it a failed call
```

Check the library build alone with `stitch build --image-only`; `stitch shell` opens a shell in
the image.

## Files

```
harness/
  objects.h            object types and data types, imported by every stub file
  parse/parse.m        stubs; every harness/**/*.m (and .mm, .c) file is compiled
  bridge/glue.mm       Objective-C++ works too (std:: types next to Foundation)
  tree/edit.m
  data/KvText/         seeds/, *.dict for the KvText data type
```

## A complete example

```objc
// harness/objects.h
#import <Stitch/Stitch.h>
#import "KVStore.h"

STITCH_OBJECT(Store, KVStore *);      // object type Store, handle type KVStore *
STITCH_DATATYPE(KvText);              // data type: harness/data/KvText/
```

```objc
// harness/stubs.m
#import "objects.h"

STITCH_ENDPOINT(New(Store), new_store) {
    return [KVStore new];
}

STITCH_ENDPOINT(New(Store), load, Str(256, KvText) text) {
    NSError *err = nil;
    KVStore *st = [KVStore storeWithText:text error:&err];
    STRequire(st != nil, @"invalid text");          // not a bug: invalid input
    return st;
}

STITCH_ENDPOINT(void, put, Borrow(Store) st, Str(16) key, Str(32) value) {
    STRequire([st putValue:value forKey:key error:NULL], @"invalid key or value");
}

STITCH_ENDPOINT(void, remove_any, Borrow(Store) st, Src(8) s) {
    NSArray *keys = [[st dictionary] allKeys];
    STRequire(keys.count > 0, @"empty");
    [st removeKey:[s pick:keys]];                   // pick an existing key
}

STITCH_ENDPOINT(void, roundtrip, Borrow(Store) st) {
    KVStore *again = [KVStore storeWithText:[st dump] error:NULL];
    STCheck([[again dictionary] isEqual:[st dictionary]], @"dump/load roundtrip");   // a bug if false
}
```

`STITCH_ENDPOINT(ReturnType, name, parameters...)` defines the function `stub_<name>`; `name` is
the stub's id in programs and reports. `stitch build` reads the declarations, so the types must
be written as below (macros and plain C types, not your own typedefs).

## Objects

```objc
STITCH_OBJECT(Store, KVStore *);           // a class
STITCH_OBJECT(Item, id<KVItem>);           // `id` and protocols work too
```

The handle is an Objective-C object pointer. The pool retains it and releases it when the object
is consumed or the program ends. Wrap a C handle in a small class whose `-dealloc` frees it.

| In the declaration | Meaning |
|---|---|
| `Borrow(T) x` | uses an object; it stays available for later calls |
| `Take(T) x` | consumes the object: it leaves the pool after the call |
| `STConsume(x)` | on a `Borrow`: the library took ownership in this call |
| return `New(T)` | produces an object: `return obj;` (`nil` counts as a bail) |
| return `Maybe(T)` | produces an object or nothing: `return nil;` is not a bail |
| return `Outputs(New(A), Maybe(B))` | several outputs: `return @[a, b ?: [NSNull null]];` |
| return `void` | produces nothing |

```objc
STITCH_OBJECT(Entry, KVEntry *);

STITCH_ENDPOINT(Outputs(New(Store), Maybe(Entry)), load_first, Str(256, KvText) text) {
    KVStore *st = [KVStore storeWithText:text error:NULL];
    STRequire(st != nil, @"invalid text");
    KVEntry *first = [st firstEntry];
    return @[st, first ?: [NSNull null]];
}

STITCH_ENDPOINT(void, close_store, Take(Store) st) {
    [st close];                                // later calls never see this store
}
```

Rules the runtime guarantees:

- An input is never nil. A required output that is nil (or missing) makes the call a bail.
- A consumed object is never used again.
- When the producer of an object bails, the calls that need that object are skipped.

## Fuzzed parameters

| Type | Value in the stub |
|---|---|
| `int8_t` .. `uint64_t`, `int`, `unsigned`, `long`, `NSInteger`, `NSUInteger`, `size_t`, `char`, `unichar` | integer |
| `float`, `double`, `CGFloat`, `BOOL`, `bool` | float, bool |
| `Range(lo, hi)` | `long long` in [lo, hi] |
| `OneOf(A, B, ...)` | `long long`: one of the constants (enum cases work) |
| `Flags(A, B, ...)` | `long long`: OR of a subset of the flags |
| `Str(max)`, `Str(max, Tag)` | `NSString *` of at most `max` UTF-8 bytes (invalid bytes become U+FFFD); `Tag` is a data type |
| `CStr(max)`, `CStr(max, Tag)` | `const char *`, NUL-terminated (valid during the call) |
| `Bytes(max)`, `Bytes(max, Tag)` | `NSData *` |
| `File(max)`, `File(max, Tag)` | `NSString *`: path of a temporary file holding the data |
| `Raw(T)` | the bytes of a plain C struct `T` |
| `Val(Tag)`, `Val(Tag, max)` | `id`: the object `Tag`'s decoder returns |
| `Src(max)` | `STSource *` for choices inside the stub |

```objc
typedef NS_ENUM(NSInteger, KVFormat) { KVFormatPlain, KVFormatJSON };

STITCH_ENDPOINT(void, export_as, Borrow(Store) st, OneOf(KVFormatPlain, KVFormatJSON) fmt, Range(0, 8) indent) {
    (void)[st exportWithFormat:(KVFormat)fmt indent:(NSUInteger)indent];
}

STITCH_ENDPOINT(New(Store), load_file, File(4096, KvText) path) {
    KVStore *st = [KVStore storeWithContentsOfFile:path error:NULL];
    STRequire(st != nil, @"invalid file");
    return st;
}
```

Values start random, not zero. When the library compares an argument against a constant, the
fuzzer finds the constant on its own: integer comparisons and `strcmp` / `memcmp` in compiled
code, and `NSString` comparisons (`isEqualToString:`, `hasPrefix:`, `compare:`, `rangeOfString:`, ...).

## Bail, require, check

```objc
STBail(@"reason");                     // stop this call; not a bug. The program continues.
STRequire(cond, @"reason");            // bail unless cond
STCheck(cond, @"what");                // a bug if false, reported as "check failed: what"
STLog(@"n=%lu", (unsigned long)n);     // printed by `stitch exec --trace`

STITCH_INIT { [KVStore setDefaultCapacity:16]; }    // runs once per process, before any stub
```

Cocoa APIs report expected failures with `nil` / `NO` and an `NSError`: turn those into bails
with `STRequire`. An exception means a programming error (`NSRangeException`,
`NSInvalidArgumentException`, ...), so one that escapes the stub is a bug, reported with the
stack of the `@throw` or `-raise`. If the library throws for invalid input, catch that exception
in the stub and bail, or set `exceptions = "bail"` in `[fuzz]`:

```objc
STITCH_ENDPOINT(New(Store), unarchive, Bytes(1024) data) {
    @try {
        return [KVStore storeWithArchive:data];
    } @catch (NSException *e) {
        if (![e.name isEqualToString:NSInvalidArchiveOperationException]) @throw;
        STBail(@"invalid archive");
    }
}
```

These are bugs: escaped Objective-C exceptions (a failed `NSAssert` raises
`NSInternalInconsistencyException`), AddressSanitizer reports (and UBSan with
`sanitizers = ["address", "undefined"]`), other crashes (segfault, abort), a failed `STCheck`, and
programs that run past `timeout_ms`. The crash kind is the exception name or the sanitizer's;
frames name methods as `-[KVStore items]`. ARC is not exception-safe (as on Apple platforms): a
bail or an exception that unwinds a stub leaks the objects its locals hold, which is harmless
while fuzzing.

## Attributes

Attributes store state on an object that the library does not expose. They belong to the object:
a consumed object loses them. Keys are strings.

```objc
STSetInt(st, @"puts", STGetIntOr(st, @"puts", 0) + 1);   // integer, with a default
STRequire(STGetIntOr(st, @"puts", 0) < 100, @"store is full");
STSetStr(st, @"name", @"text");                            // string
NSString *name = STGetStrOr(st, @"name", @"");
STHas(st, @"puts");
STDel(st, @"puts");
STGlobalSet(@"mode", 1);                                   // program-wide; reset for every program
STGlobalGetOr(@"mode", 0);
```

Without a default, `STGetInt(st, @"k")` and `STGetStr(st, @"k")` report a missing key in
`stitch probe` ("read missing attr 'k'"); use them when the key must have been set. An attribute
set on the object a stub returns stays with the new object.

Links to other objects: store a link to an input, read it back with `STGetObj(obj, key, Type)`.
It returns nil once the linked object was consumed.

```objc
STITCH_ENDPOINT(New(Entry), first_entry, Borrow(Store) st) {
    KVEntry *e = [st firstEntry];
    STRequire(e != nil, @"empty");
    STSetObj(e, @"store", st);                 // link the new Entry to its store
    return e;
}

STITCH_ENDPOINT(void, entry_value, Borrow(Entry) e) {
    KVStore *st = STGetObj(e, @"store", Store);
    STRequire(st != nil, @"store is gone");    // the store was closed: the entry is dangling
    (void)[e value];
}
```

The link target must already exist when you set it: an input of the call (`Borrow`/`Take`). A
link to another output of the same call does not work and reads as missing. Attributes follow the
object pointer: when a stub returns an object that is already in the pool (the same instance from
an earlier call), `STSet*` inside the stub reaches the earlier one.

## Data types and decoders

A data type gives a `Str`, `CStr`, `Bytes` or `File` parameter better inputs. Declare it, tag
the parameter, and add any of these to `harness/data/<Name>/`:

- `seeds/`: sample inputs, one per file. Copy them from the library's tests.
- `*.dict`: tokens, one per line, quoted: `"true"`, `"\x00\x01"`.
- a decoder: a function that builds a value from fuzzer choices, so values stay well formed.

```objc
STITCH_DATATYPE(KvText);
STITCH_ENDPOINT(New(Store), load, Str(256, KvText) text) { ... }   // tagged parameter
```

A decoder's body sees `STSource *s` and returns an `NSString` (for `Str`, `CStr`, `File`) or
`NSData` (for `Bytes`):

```objc
STITCH_DECODER(KvText) {
    NSMutableArray *lines = [NSMutableArray array];
    while ([s more]) {
        NSString *key = [s coin:0.3] ? [s token] : [s str:8];
        [lines addObject:[NSString stringWithFormat:@"%@=%@", key, [s str:12]]];
    }
    return [lines componentsJoinedByString:@"\n"];
}
```

`stitch sample KvText -n 5` prints values the decoder makes.

A decoder can return any object. Take it as `Val(Tag, max)`, and build decoders on each other with
`STDraw(s, Tag)` (`[s draw:@"Tag"]`). `stitch sample` and traces show its `-description`.

```objc
STITCH_DECODER(Pair) {
    return @[[s coin:0.3] ? [s token] : [s str:8], [s str:12]];
}

STITCH_DECODER(Pairs) {
    NSMutableArray *out = [NSMutableArray array];
    while ([s more]) [out addObject:STDraw(s, Pair)];
    return out;
}

STITCH_ENDPOINT(void, put_all, Borrow(Store) st, Val(Pairs, 256) kvs) {
    for (NSArray *kv in kvs) STRequire([st putValue:kv[1] forKey:kv[0] error:NULL], @"invalid pair");
}
```

`STSource` methods:

| Method | Returns |
|---|---|
| `u8` .. `u64`, `i8` .. `i64`, `f32`, `f64` | a number |
| `range:lo to:hi` | `int64_t` in [lo, hi] |
| `choice:n` | index in [0, n) |
| `pick:array` | one element |
| `coin`, `coin:p` | `YES` with probability about p (default 0.5) |
| `more` | loop condition: `while ([s more]) { ... }` |
| `f64:lo to:hi`, `f32:lo to:hi` | finite float in [lo, hi] |
| `str:max`, `bytes:max` | `NSString` or `NSData` of at most `max` bytes |
| `fixed:n` | exactly n bytes |
| `rest` | every byte that is left |
| `token` | often a token from the data type's `.dict` files; otherwise (and once mutated) any string, like `str`: check it when the input must be one of the tokens (or use `pick`) |
| `draw:@"Tag"` (`STDraw(s, Tag)`) | a value from data type `Tag`'s decoder |

Running out of choices is fine: later draws return zeros and `more` returns `NO`.

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/repro.m`: a program that includes the harness
sources and calls each stub function with its recorded arguments.

```objc
#include "harness/stubs.m"

int main(void) {
    return STReproMain(^{
        // #0 load(text)
        stitch_obj_Store store0 = STCall(0, "load", ^id { return stub_load(@"a=1\ntmp=2"); });
        // #1 remove(st=store0, key)
        STCall(1, "remove", ^id { stub_remove(store0, @"tmp"); return nil; });
        // #2 roundtrip(st=store0)  <-- crashes here in the original run
        STCall(2, "roundtrip", ^id { stub_roundtrip(store0); return nil; });
    });
}
```

Calls that bailed in the original run use `STCallBails`; outputs are variables named after their
type (`store0`); enum constants of `OneOf` parameters appear by name. When the harness has `.mm`
files the file is `repro.mm`, and every harness file is compiled as Objective-C++.

Shrink a copy to plain library code and check it after each edit:

```sh
cp .stitch/standalone/c1/repro.m .stitch/standalone/c1/min.m
stitch try .stitch/standalone/c1/min.m
```

A finished reproducer:

```objc
#import "KVStore.h"

int main(void) {
    KVStore *st = [KVStore storeWithText:@"a=1\ntmp=2" error:NULL];
    [st removeKey:@"tmp"];          // a tmp key stays in the order list
    [st dump];                      // -[KVStore items] puts nil into an array
    return 0;
}
```

`stitch try` compiles the file with the `cflags` and `ldflags` from `stitch.toml` and the
STITCH++ runtime (an exception nothing catches is reported in the fuzzer's format), runs it in the
project image, and compares the crash with the original. Details: `stitch guide reproducers`.

## Coverage

`stitch cov` rebuilds the library and the harness with clang source-based coverage and no
sanitizers, and replays the corpus. Memory bugs that need ASan to crash run to completion there.
