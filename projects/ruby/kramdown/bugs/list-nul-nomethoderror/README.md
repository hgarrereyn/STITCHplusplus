# kramdown: NoMethodError in block

| | |
|---|---|
| Kind | `NoMethodError` |
| Location | `block in Kramdown::Parser::Kramdown#parse_list list.rb:136`, `Array#each list.rb:109`, `Kramdown::Parser::Kramdown#parse_list list.rb:109` |
| Library source | [https://github.com/gettalong/kramdown](https://github.com/gettalong/kramdown) @ `84e305c87c3f` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `list-nul-nomethoderror` |

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse(text=Str[11,decoded]"* \x00\n    * \n", input="markdown", opts=Val<KdOpts>[0 choices] {})
```

## Stubs used

What each call does (from `project/harness/`):

`parse` · harness/kd.rb

```ruby
Stitch.endpoint :parse, { text: Str[3000, KdText], input: OneOf[*INPUTS], opts: Val[OptsT, 256] }, New[Doc], bails_on: BAILS do |text, input, opts|
  mkdoc(text, input, opts)
end
```

## Report

```
[stitch] EXCEPTION in parse: NoMethodError: undefined method 'value' for nil
    #0 0x0 in block in Kramdown::Parser::Kramdown#parse_list /src/lib/kramdown/parser/kramdown/list.rb:136
    #1 0x0 in Array#each /src/lib/kramdown/parser/kramdown/list.rb:109
    #2 0x0 in Kramdown::Parser::Kramdown#parse_list /src/lib/kramdown/parser/kramdown/list.rb:109
    #3 0x0 in block (2 levels) in Kramdown::Parser::Kramdown#parse_blocks /src/lib/kramdown/parser/kramdown.rb:149
    #4 0x0 in Array#any? /src/lib/kramdown/parser/kramdown.rb:147
    #5 0x0 in block in Kramdown::Parser::Kramdown#parse_blocks /src/lib/kramdown/parser/kramdown.rb:147
    #6 0x0 in Kernel#catch /src/lib/kramdown/parser/kramdown.rb:145
    #7 0x0 in Kramdown::Parser::Kramdown#parse_blocks /src/lib/kramdown/parser/kramdown.rb:145
    #8 0x0 in Kramdown::Parser::Kramdown#parse /src/lib/kramdown/parser/kramdown.rb:90
    #9 0x0 in Kramdown::Parser::Base.parse /src/lib/kramdown/parser/base.rb:69
    #10 0x0 in Kramdown::Document#initialize /src/lib/kramdown/document.rb:102
    #11 0x0 in Class#new .stitch/build/app/harness/kd.rb:36
    #12 0x0 in Object#mkdoc .stitch/build/app/harness/kd.rb:36
    #13 0x0 in block in <top (required)> .stitch/build/app/harness/kd.rb:73
    #14 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH++ ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

