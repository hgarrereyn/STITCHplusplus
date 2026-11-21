# addressable: ArgumentError in String#gsub

| | |
|---|---|
| Kind | `ArgumentError` |
| Location | `String#gsub uri.rb:467`, `Addressable::URI.unencode uri.rb:467`, `Addressable::URI.normalize_component uri.rb:563` |
| Library source | [https://github.com/sporkmonger/addressable](https://github.com/sporkmonger/addressable) @ `598aea6b1b6c` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `normalize-invalid-utf8` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 heuristic_parse(text=Str[27,decoded]"https://localhost%41\x00\x00\x00\x1d%FF")
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
[stitch] EXCEPTION in normalize: ArgumentError: invalid byte sequence in UTF-8
    #0 0x0 in String#gsub /src/lib/addressable/uri.rb:467
    #1 0x0 in Addressable::URI.unencode /src/lib/addressable/uri.rb:467
    #2 0x0 in Addressable::URI.normalize_component /src/lib/addressable/uri.rb:563
    #3 0x0 in Addressable::URI#normalized_host /src/lib/addressable/uri.rb:1126
    #4 0x0 in Addressable::URI#normalized_authority /src/lib/addressable/uri.rb:1246
    #5 0x0 in Addressable::URI#normalize /src/lib/addressable/uri.rb:2170
    #6 0x0 in block in <top (required)> .stitch/build/app/harness/stubs.rb:71
    #7 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

