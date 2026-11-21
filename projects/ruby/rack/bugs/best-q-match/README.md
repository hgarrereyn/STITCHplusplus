# rack: NoMethodError in Rack::Mime.match?

| | |
|---|---|
| Kind | `NoMethodError` |
| Location | `Rack::Mime.match? mime.rb:32`, `block  utils.rb:149`, `Array#each utils.rb:149` |
| Library source | [https://github.com/rack/rack](https://github.com/rack/rack) @ `1741c580d71c` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `best-q-match` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 q_values(header=Str[27,decoded]",*oL42_?L38p+h::,multipart/", avail=["gzip", "identity"])
```

## Stubs used

What each call does (from `project/harness/`):

`q_values` · harness/stubs.rb

```ruby
Stitch.endpoint :q_values, { header: Str[256, HeaderText], avail: OneOf[%w[gzip identity], %w[text/html */*], %w[en de]] } do |header, avail|
  Rack::Utils.q_values(header)
  Rack::Utils.best_q_match(header, avail)
end
```

## Report

```
[stitch] EXCEPTION in q_values: NoMethodError: undefined method 'split' for nil
    #0 0x0 in Rack::Mime.match? /src/lib/rack/mime.rb:32
    #1 0x0 in block (2 levels) in Rack::Utils.best_q_match /src/lib/rack/utils.rb:149
    #2 0x0 in Array#each /src/lib/rack/utils.rb:149
    #3 0x0 in Enumerable#find /src/lib/rack/utils.rb:149
    #4 0x0 in block in Rack::Utils.best_q_match /src/lib/rack/utils.rb:149
    #5 0x0 in Array#map /src/lib/rack/utils.rb:148
    #6 0x0 in Rack::Utils.best_q_match /src/lib/rack/utils.rb:148
    #7 0x0 in block in <top (required)> .stitch/build/app/harness/stubs.rb:115
    #8 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

