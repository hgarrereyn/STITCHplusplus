# addressable: check failed: normalize is idempotent in normalize

| | |
|---|---|
| Kind | `check failed: normalize is idempotent` |
| Location | – |
| Library source | [https://github.com/sporkmonger/addressable](https://github.com/sporkmonger/addressable) @ `598aea6b1b6c` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `normalize-idempotent` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 heuristic_parse(text=Str[23,decoded]"ftp://aW@example.com%41")
#1 normalize(u=o0.0)
```

## Stubs used

What each call does (from `project/harness/`):

`heuristic_parse` · harness/stubs.rb

```ruby
Stitch.endpoint :heuristic_parse, { text: Str[512, UriText] }, New[Uri] | nil, bails_on: BAD do |text|
  Addressable::URI.heuristic_parse(text)
end
```

`normalize` · harness/stubs.rb

```ruby
Stitch.endpoint :normalize, { u: Borrow[Uri] }, New[Uri], bails_on: BAD do |u|
  n = u.normalize
  Stitch.log("normalized: #{n.to_s.inspect}")
  n2 = n.normalize
  Stitch.check(n2.to_s == n.to_s, "normalize is idempotent")
  again = Addressable::URI.parse(n.to_s)
  Stitch.check(again.normalize.to_s == n.to_s, "a normalized URI parses back to itself")
  n
end
```

## Report

```
[stitch] CHECK FAILED: normalize is idempotent (node 1)
    #0 0x0 in block in <top (required)> .stitch/build/app/harness/stubs.rb:74
    #1 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

