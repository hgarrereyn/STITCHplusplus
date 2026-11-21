# rack: NoMethodError in Rack::Multipart::Parser#tag_multipart_encoding

| | |
|---|---|
| Kind | `NoMethodError` |
| Location | `Rack::Multipart::Parser#tag_multipart_encoding parser.rb:340`, `block  parser.rb:194`, `Rack::Multipart::Parser::Collector::MimePart#get_data parser.rb:104` |
| Library source | [https://github.com/rack/rack](https://github.com/rack/rack) @ `1741c580d71c` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `multipart-empty-content-type` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 multipart(body=Str[94,decoded]"--AaB03x\x0d\nContent-Disposition: form-data; name=\"b\"\x0d\nContent-Type: \x0d\n\x0d\n\xef\xbf\xbd~\xef\xbf\xbd9l?\x0d\n--AaB03x--\x0d\n", boundary="AaB03x")
```

## Stubs used

What each call does (from `project/harness/`):

`multipart` · harness/stubs.rb

```ruby
Stitch.endpoint :multipart, { body: Str[2048, MultipartBody], boundary: OneOf["AaB03x", "x", ""] },
                bails_on: [EOFError, Rack::Multipart::MultipartPartLimitError] + PARAM_ERRORS do |body, boundary|
  env = Rack::MockRequest.env_for("/", "CONTENT_TYPE" => "multipart/form-data; boundary=#{boundary}",
                                       "CONTENT_LENGTH" => body.bytesize.to_s, :input => StringIO.new(body))
  Rack::Multipart.parse_multipart(env)
end
```

## Report

```
[stitch] EXCEPTION in multipart: NoMethodError: undefined method 'strip!' for nil
    #0 0x0 in Rack::Multipart::Parser#tag_multipart_encoding /src/lib/rack/multipart/parser.rb:340
    #1 0x0 in block (2 levels) in Rack::Multipart::Parser#result /src/lib/rack/multipart/parser.rb:194
    #2 0x0 in Rack::Multipart::Parser::Collector::MimePart#get_data /src/lib/rack/multipart/parser.rb:104
    #3 0x0 in block in Rack::Multipart::Parser#result /src/lib/rack/multipart/parser.rb:193
    #4 0x0 in block in Rack::Multipart::Parser::Collector#each /src/lib/rack/multipart/parser.rb:127
    #5 0x0 in Array#each /src/lib/rack/multipart/parser.rb:127
    #6 0x0 in Rack::Multipart::Parser::Collector#each /src/lib/rack/multipart/parser.rb:127
    #7 0x0 in Rack::Multipart::Parser#result /src/lib/rack/multipart/parser.rb:192
    #8 0x0 in Rack::Multipart::Parser.parse /src/lib/rack/multipart/parser.rb:81
    #9 0x0 in Rack::Multipart.extract_multipart /src/lib/rack/multipart.rb:54
    #10 0x0 in Rack::Multipart.parse_multipart /src/lib/rack/multipart.rb:42
    #11 0x0 in block in <top (required)> .stitch/build/app/harness/stubs.rb:122
    #12 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

