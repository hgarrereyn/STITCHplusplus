# Rust harnesses

Stubs are functions declared inside `endpoint! { ... }`. Their parameter and return types are the
signature. Nightly Rust builds the library and the harness crate with AddressSanitizer and
coverage instrumentation, and the fuzzer runs the stubs in its own process.

## Project setup

```sh
stitch init semver --lang rust --git https://github.com/dtolnay/semver --rev 1.0.0
cd semver
```

`Dockerfile`: put the source at `/src`. The harness crate depends on it by path.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
```

`stitch.toml`: `stitch init` adds a dependency on `/src` named after the project. Rename it to
the library's crate name (`name` in the library's `Cargo.toml`):

```toml
[project]
name = "semver"
language = "rust"

[source]
git = "https://github.com/dtolnay/semver"
rev = "1.0.0"

[rust.dependencies]          # the harness crate's dependencies, as in Cargo.toml
semver = { path = "/src" }
# serde_json = "1"

[rust]
# overflow_checks = false    # default true: integer overflow panics

[fuzz]
timeout_ms = 2000
```

## Files

```
harness/
  objects.rs           object types, data types, decoders
  version.rs           stubs; every harness/*.rs file is a module of the crate stitch_harness
  req.rs
  data/VersionText/    seeds/, *.dict for the VersionText data type
```

Only files directly in `harness/` are compiled. Start each stub module with
`pub use crate::objects::*;` (the `pub` lets `stitch write` reproducers see the types).

## A complete example

```rust
// harness/objects.rs
use stitch::*;
use semver::{Version, VersionReq};

object!(Ver, Version);                       // object type Ver, handle type Version
object!(Req, VersionReq);
datatype!(VersionText);                      // data type: harness/data/VersionText/
```

```rust
// harness/version.rs
pub use crate::objects::*;
use semver::Version;
use stitch::*;

endpoint! {
    fn parse_version(text: Str<64, VersionText>) -> Result<New<Ver>, semver::Error> {
        Ok(New::new(Version::parse(&text)?))      // Err is a bail
    }
}

endpoint! {
    fn new_version(major: u64, minor: Range<0, 1000>, patch: u64) -> New<Ver> {
        New::new(Version::new(major, *minor as u64, patch))
    }
}

endpoint! {
    fn version_roundtrip(v: Borrow<Ver>) {
        let back = Version::parse(&v.to_string());
        check(back.ok().as_ref() == Some(&*v), "Display -> parse round trip");   // a bug if false
    }
}

endpoint! {
    fn version_compare(a: Borrow<Ver>, b: Borrow<Ver>) {
        check(a.cmp(&b) == b.cmp(&a).reverse(), "cmp antisymmetric");
    }
}
```

## Objects

```rust
object!(Ver, Version);
object!(Req, VersionReq, drop = |r: VersionReq| log(&format!("drop {}", r)));   // drop: runs on objects left at the end
```

| Type | Meaning |
|---|---|
| `Borrow<T>` | uses an object; it stays available. Derefs (mutably) to the handle |
| `Take<T>` | consumes the object: `x.into_inner()` gives the handle |
| `consume(&x)` | on a `Borrow`: the library took ownership in this call |
| `-> New<T>` | produces an object: `New::new(handle)` |
| `-> Option<New<T>>` | optional output: `None` means no object |
| `-> (New<A>, Option<New<B>>)` | several outputs |
| `-> Result<R, E>` | `Err` is a bail; `R` is any of the above or `()` |

Plain values (`bool`, `String`) are not valid return types.

```rust
endpoint! {
    fn attach_build(v: Borrow<Ver>, b: Take<Build>) {
        v.build = b.into_inner();
    }
}

endpoint! {
    fn req_absorb(dst: Borrow<Req>, src: Borrow<Req>) {
        let cs = std::mem::take(&mut src.comparators);
        dst.comparators.extend(cs);
        consume(&src);                           // src is gone after this call
    }
}

endpoint! {
    fn split(v: Borrow<Ver>) -> (New<Pre>, Option<New<Build>>) {
        let b = if v.build.is_empty() { None } else { Some(New::new(v.build.clone())) };
        (New::new(v.pre.clone()), b)
    }
}
```

## Fuzzed parameters

| Type | Value | Use |
|---|---|---|
| `u8`..`u64`, `i8`..`i64`, `usize`, `f32`, `f64`, `bool` | the value | as is |
| `Range<LO, HI>` | integer in [LO, HI] | `*r` is an `i64` |
| `OneOf<C>` | one of the values of `choices!(C: T = [...])` | `*o` is a `T` |
| `Flags<F>` | OR of a subset of `flags!(F = [...])` | `f.bits()` is an `i64` |
| `Str<MAX, Tag>` | String of at most `MAX` bytes; `Tag` is an optional data type | derefs to `str`: `&text`, `text.as_str()` |
| `Bytes<MAX, Tag>` | bytes | derefs to `[u8]` |
| `File<MAX, Tag>` | temporary file holding the data | derefs to `PathBuf` |
| `Val<Tag, MAX = 1024>` | the value `Tag`'s decoder returns | derefs to it |
| `Src<MAX>` | a `Source` for choices inside the stub | derefs to `Source` |

`choices!` takes any `Clone + Debug` values, enum variants included:

```rust
choices!(OpKind: Op = [Op::Exact, Op::Greater, Op::Less, Op::Tilde, Op::Caret, Op::Wildcard]);
flags!(Parts = [1, 2, 4]);

endpoint! {
    fn build_comparator(op: OneOf<OpKind>, parts: Flags<Parts>, major: Range<0, 50>,
                        minor: Range<0, 50>) -> New<Cmp> {
        let has_minor = parts.bits() & 1 != 0;
        New::new(Comparator {
            op: *op,
            major: *major as u64,
            minor: if has_minor { Some(*minor as u64) } else { None },
            patch: None,
            pre: Prerelease::EMPTY,
        })
    }
}

endpoint! {
    fn pick_comparator(req: Borrow<Req>, s: Src<8>) -> Option<New<Cmp>> {
        if req.comparators.is_empty() { return None; }
        let i = s.choice(req.comparators.len());          // pick an existing element
        Some(New::new(req.comparators[i].clone()))
    }
}
```

## Bail, require, check

```rust
bail("reason")                       // stop this call; not a bug. Returns `!`
require(cond, "reason");             // bail unless cond
check(cond, "what");                 // a bug if false: "check failed: what"
log(&format!("n={}", n));            // printed by `stitch exec --trace`
```

`bail` returns `!`, so it fits in a `match` arm:

```rust
let s = match std::str::from_utf8(&b) { Ok(s) => s, Err(_) => bail("utf8") };
```

There is no init hook. For one-time setup, use a `static INIT: std::sync::Once` inside the stubs
that need it.

These are bugs: panics (including integer overflow, `unwrap` on `None`, index out of bounds),
AddressSanitizer reports in `unsafe` code (heap-buffer-overflow, use-after-free, ...), a failed
`check`, and programs that run past `timeout_ms`.

## Attributes

```rust
set_int(&v, "bumps", 1);
let n = get_int(&v, "bumps");            // Option<i64>; a miss is reported by `stitch probe`
set_str(&v, "name", "text");
let s = get_str(&v, "name");             // Option<String>
has(&v, "bumps");
del(&v, "bumps");
global_set("mode", 1);                   // program-wide i64; reset for every program
global_get("mode");                      // Option<i64>
```

Every `get_*` reports a missing key. When a missing key is normal, test with `has` first:

```rust
let n = if has(&v, "bumps") { get_int(&v, "bumps").unwrap() } else { 0 };
set_int(&v, "bumps", n + 1);
```

Links to other objects:

```rust
endpoint! {
    fn pick_comparator(req: Borrow<Req>, s: Src<8>) -> Option<New<Cmp>> {
        if req.comparators.is_empty() { return None; }
        let out = New::new(req.comparators[s.choice(req.comparators.len())].clone());
        set_obj(&out, "origin", &req);           // link the new Cmp to its Req
        Some(out)
    }
}

endpoint! {
    fn comparator_in_origin(c: Borrow<Cmp>) {
        let origin = get_obj::<Req>(&c, "origin");   // Option<Borrow<Req>>; None once Req was consumed
        require(origin.is_some(), "no origin");
        check(origin.unwrap().comparators.contains(&c), "picked comparator still in origin");
    }
}
```

## Data types and decoders

Tag a `Str`, `Bytes` or `File` parameter with a data type and create `harness/data/<Name>/` with
`seeds/` (sample inputs) and `*.dict` files (one quoted token per line). A decoder builds values
from fuzzer choices:

```rust
datatype!(Ident);                            // declare the data type, then its decoder
const ALNUM: [char; 8] = ['a', 'z', 'A', 'Z', '0', '9', '-', 'x'];

decoder!(Ident, fn ident(s: &mut Source) -> String {
    let mut out = String::new();
    loop {
        if !out.is_empty() { out.push('.'); }
        match s.choice(3) {
            0 => out.push_str(&s.range(0, 1000).to_string()),
            1 => { let n = s.range(1, 6); for _ in 0..n { out.push(s.pick(&ALNUM)); } }
            _ => out.push_str("alpha"),
        }
        if !s.more() { break; }
    }
    out
});
```

```
$ stitch sample Ident -n 4
"0"
"alpha"
"A0"
"alpha.760.alpha.574"
```

A decoder can return any type. Take it as `Val<Tag, MAX>`, and build decoders on each other with
`s.draw::<Tag>()`. Derive `Debug` so `stitch sample` and traces can print the value:

```rust
datatype!(VerParts);

#[derive(Debug)]
pub struct VerFields { pub major: u64, pub minor: u64, pub patch: u64, pub pre: String }

decoder!(VerParts, fn ver_parts(s: &mut Source) -> VerFields {
    VerFields { major: s.range(0, 20) as u64, minor: s.range(0, 20) as u64, patch: s.range(0, 20) as u64,
            pre: if s.coin(0.5) { s.draw::<Ident>() } else { String::new() } }
});

endpoint! {
    fn version_from_parts(p: Val<VerParts, 256>) -> Result<New<Ver>, semver::Error> {
        let mut v = Version::new(p.major, p.minor, p.patch);
        v.pre = Prerelease::new(&p.pre)?;
        Ok(New::new(v))
    }
}
```

`Source` methods:

| Method | Returns |
|---|---|
| `u8()` .. `u64()`, `i8()` .. `i64()`, `f32()`, `f64()` | a number |
| `range(lo: i64, hi: i64) -> i64` | integer in [lo, hi] |
| `choice(n: usize) -> usize` | index in [0, n) |
| `pick(&[T]) -> T` | one element (`T: Clone`) |
| `coin(p: f64) -> bool` | true with probability about p |
| `more() -> bool` | loop condition |
| `f64_range(lo, hi)`, `f32_range(lo, hi)` | finite float in [lo, hi] |
| `str(max) -> String`, `bytes(max) -> Vec<u8>` | text or bytes of at most `max` bytes |
| `fixed(n) -> Vec<u8>` | exactly n bytes |
| `rest() -> Vec<u8>` | every byte that is left |
| `token() -> String` | a token from the data type's `.dict` files |
| `draw::<Tag>()` | a value from data type `Tag`'s decoder |

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/repro.rs`, a binary of the harness crate that
calls each stub with its recorded arguments:

```rust
        // #0 parse_version(text)
        let mut r0 = stitch::call(0, "parse_version", || parse_version(Str::from("1.2.3")));
        let mut ver0 = r0.get::<Ver>(0);
```

Shrink a copy to plain library code and check it after each edit. End with `assert!` for a
failed check:

```sh
cp .stitch/standalone/c1/repro.rs .stitch/standalone/c1/min.rs
stitch try .stitch/standalone/c1/min.rs
```

```rust
use semver::Version;

fn main() {
    let a = Version::parse("1.2.3").unwrap();
    let b = Version::parse("1.2.3-a").unwrap();
    // a pre-release sorts before its release (1.2.3-a < 1.2.3), but "1.2.3" < "1.2.3-a" as strings
    assert!(a.cmp(&b) == a.to_string().cmp(&b.to_string()), "version order equals string order");
}
```

```
SAME CRASH: .stitch/standalone/c1/min.rs -> check failed: version order equals string order
  6 code lines, plain library code
```

(This check is false by design; it shows the workflow.) Details: `stitch guide reproducers`.

## Coverage

`stitch cov` builds the crate again with `-Cinstrument-coverage` and reports lines and functions.
