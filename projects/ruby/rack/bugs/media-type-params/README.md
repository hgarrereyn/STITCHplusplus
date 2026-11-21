# rack: NoMethodError in Rack::MediaType.params

| | |
|---|---|
| Kind | `NoMethodError` |
| Location | `Rack::MediaType.params media_type.rb:29`, `block in <top (required)> (stub)`, `<main> stitch-rb:8` |
| Library source | [https://github.com/rack/rack](https://github.com/rack/rack) @ `1741c580d71c` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `media-type-params` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 request_headers(host=Str[11,decoded]";boundary=\x09", fwd=Str[42,decoded]"\x01\x07\x00\x00\x00\x00\x00=\xef\xbf\xbd\x00\x00\x00\x00\xef\xbf\xbd\x00\x00\x07\x00\x00\x00\x00\x00\x01\x00\x05\x00\xef\xbf\xbd\x00\x00\x00\x00\x00\x00\xef\xbf\xbd", accept=Str[0,decoded]"")
#1 media_type(ct=Str[1,decoded]";")
```

## Stubs used

What each call does (from `project/harness/`):

`request_headers` · harness/stubs.rb

```ruby
Stitch.endpoint :request_headers, { host: Str[128, HeaderText], fwd: Str[128, HeaderText], accept: Str[128, HeaderText] },
                bails_on: [ArgumentError] do |host, fwd, accept|
  env = Rack::MockRequest.env_for("http://example.com/a?b=c", "HTTP_HOST" => host, "HTTP_X_FORWARDED_HOST" => fwd,
                                  "HTTP_X_FORWARDED_FOR" => fwd, "HTTP_ACCEPT_ENCODING" => accept, "HTTP_FORWARDED" => fwd)
  r = Rack::Request.new(env)
  r.host
  r.port
  r.ip
  r.accept_encoding
  r.forwarded_for
end
```

`media_type` · harness/stubs.rb

```ruby
Stitch.endpoint :media_type, { ct: Str[256, HeaderText] } do |ct|
  Rack::MediaType.type(ct)
  Rack::MediaType.params(ct)
end
```

## Report

```
[stitch] EXCEPTION in media_type: NoMethodError: undefined method 'each_with_object' for nil
    #0 0x0 in Rack::MediaType.params /src/lib/rack/media_type.rb:29
    #1 0x0 in block in <top (required)> .stitch/build/app/harness/stubs.rb:110
    #2 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

