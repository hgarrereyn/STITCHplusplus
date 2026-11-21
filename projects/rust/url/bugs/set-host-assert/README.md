# url: assertion failed in url::Url::set_host

| | |
|---|---|
| Kind | `assertion failed` |
| Location | `url::Url::set_host lib.rs:1670`, `stitch_harness::stubs::clear (stub)` |
| Library source | [https://github.com/servo/rust-url](https://github.com/servo/rust-url) @ `1182d5d5b91c` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `set-host-assert` |

## Call sequence (3 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse(text=Str[8,decoded]"mailto:/")
#1 join(base=o0.0, rel=Str[18,decoded]"//\xef\xbf\xbdP\xef\xbf\xbd%4}kQQPw?")
#2 clear(u=o1.0, what=2)
```

## Stubs used

What each call does (from `project/harness/`):

`parse` · harness/stubs.rs

```rust
    fn parse(text: Str<256, UrlText>) -> Result<New<U>, url::ParseError> {
        let u = Url::parse(&text)?;
        roundtrip(&u, "parse");
        Ok(New::new(u))
    }
```

`join` · harness/stubs.rs

```rust
    fn join(base: Borrow<U>, rel: Str<128, UrlText>) -> Result<New<U>, url::ParseError> {
        let u = base.join(&rel)?;
        roundtrip(&u, "join");
        Ok(New::new(u))
    }
```

`clear` · harness/stubs.rs

```rust
    fn clear(u: Borrow<U>, what: Range<0, 3>) {
        match *what {
            0 => u.set_query(None),
            1 => u.set_fragment(None),
            2 => { let _ = u.set_host(None); }
            _ => { let _ = u.set_password(None); }
        }
        roundtrip(&u, "clear");
    }
```

## Report

```
#0 0x0 in url::Url::set_host /src/url/src/lib.rs:1670
    #1 0x0 in stitch_harness::stubs::clear harness/stubs.rs:73
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH rust toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

