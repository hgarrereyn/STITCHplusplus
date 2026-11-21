# Rust harnesses

Stubs are functions declared with `endpoint!`. Nightly Rust (for ASan) is in the image; the
library source is at `/src`.

## Project

```sh
stitch init smallvec --lang rust --git https://github.com/servo/rust-smallvec --rev v1.6.0
```

```toml
[project]
language = "rust"

[rust.dependencies]                   # the harness crate's dependencies
smallvec = { path = "/src" }
```

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
```

## Stubs

`harness/*.rs` become modules of the crate `stitch_harness`.

```rust
use stitch::*;
use toml_edit::Document;

object!(Doc, Document);              // object type and its Rust type
datatype!(TomlText);

endpoint! {
    fn parse(text: Str<2048, TomlText>) -> Result<New<Doc>, toml_edit::TomlError> {
        Ok(New::new(text.parse()?))       // Err is a bail
    }
}

endpoint! {
    fn set_int(doc: Borrow<Doc>, key: Str<16>, v: i64) {
        doc[key.as_str()] = toml_edit::value(v);
    }
}
```

| Type | Meaning |
|---|---|
| `Borrow<T>` | an object (derefs to the handle, mutably) |
| `Take<T>` | the call consumes it (`into_inner()`) |
| `New<T>`, `Option<New<T>>`, `(New<A>, Option<New<B>>)`, `Result<R, E>` | returns |
| `u8`..`u64`, `i8`..`i64`, `usize`, `f32`, `f64`, `bool` | fuzzed values |
| `Range<LO, HI>` | integer in [LO, HI] (`*r`) |
| `OneOf<C>` / `Flags<F>` | with `choices!(C: u32 = [1, 2, 4]);` / `flags!(F = [1, 2, 4]);` |
| `Str<MAX, Tag>`, `Bytes<MAX, Tag>`, `File<MAX, Tag>` | String, Vec<u8>, temp file path |
| `Src<MAX>` | a `Source` |

Also `decoder!(Tag, fn f(s: &mut Source) -> String { ... });`, `object!(T, Handle, drop = |h| ...)`,
`bail`, `require`, `check`, `log`, `consume`, `set_int`/`get_int`/`set_obj`/`get_obj`.

## Bugs

Panics, including integer overflow (debug assertions and overflow checks are on), ASan errors in
`unsafe` code (heap-buffer-overflow, use-after-free, ...), and failed `check`.

## Coverage and comparisons

`stitch-rustc` (the `RUSTC_WRAPPER`) adds SanitizerCoverage (edges, comparisons) to the library
and the harness, not to the runtime. `stitch cov` builds with `-Cinstrument-coverage`.

## Reproducers

`stitch write c1` writes `repro.rs`, a binary of the harness crate calling each stub
(`stitch::call(0, "parse", || parse(Str::from("...")))`). A plain minimized file
(`fn main() { ... }` using the library) is compared through the panic or ASan report.
