# addressable: RegexpError in Regexp#initialize

| | |
|---|---|
| Kind | `RegexpError` |
| Location | `Regexp#initialize template.rb:1041`, `Class#new template.rb:1041`, `Addressable::Template#parse_template_pattern template.rb:1041` |
| Library source | [https://github.com/sporkmonger/addressable](https://github.com/sporkmonger/addressable) @ `598aea6b1b6c` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `template-regexperror` |

## Call sequence (4 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 template(text=Str[16,decoded]"/path{=%41,list}")
#1 expand_extract(t=o0.0, a=Str[4]"(\x97g\x1e", b=Str[1]"A")
#2 template(text=Str[113,decoded]"/pathx}&a?x={?a*,_:0,keys}_.yPqIk%^G-G^Or*B6#=CT)=|i~AQ_vN*tTv}gGq2\"DVHP{()o'a.@$cg6nI Yf*{\\5k}a]Mct`H6Wb&F.14W{.")
#3 partial_expand(t=o2.0, v=Str[3]"q~4")
```

## Stubs used

What each call does (from `project/harness/`):

`template` · harness/stubs.rb

```ruby
Stitch.endpoint :template, { text: Str[256, TemplateText] }, New[Tmpl], bails_on: BAD do |text|
  Addressable::Template.new(text)
end
```

`expand_extract` · harness/stubs.rb

```ruby
# expansion with string values matches the template back to those values
Stitch.endpoint :expand_extract, { t: Borrow[Tmpl], a: Str[8], b: Str[8] }, bails_on: BAD + [Addressable::Template::InvalidTemplateOperatorError] do |t, a, b|
  vals = t.variables.uniq.each_with_index.to_h { |v, i| [v, i.even? ? a : b] }
  uri = t.expand(vals)
  Stitch.log("expanded: #{uri.to_s.inspect}")
  m = t.match(uri)
  Stitch.check(!m.nil?, "a template matches its own expansion")
end
```

`partial_expand` · harness/stubs.rb

```ruby
Stitch.endpoint :partial_expand, { t: Borrow[Tmpl], v: Str[8] }, New[Tmpl] | nil, bails_on: BAD do |t, v|
  vars = t.variables
  next nil if vars.empty?

  t.partial_expand(vars.first => v)
end
```

## Report

```
[stitch] EXCEPTION in partial_expand: RegexpError: numbered backref/call is not allowed. (use name): /^\/pathx\}&a\?x=(?:|\?(?<a>(?:[a-zA-Z0-9\-\.\_\~]|%[a-fA-F0-9][a-fA-F0-9])*=(?:[a-zA-Z0-9\-\.\_\~]|%[a-fA-F0-9][a-fA-F0-9])*?(?:&?(?:[a-zA-Z0-9\-\.\_\~]|%[a-fA-F0-9][a-fA-F0-9])*=(?:[a-zA-Z0-9\-\.\_\~]|%[a-fA-F0-9][a-fA-F0-9])*?)*)?&?(?<_>(?:[a-zA-Z0-9\-\.\_\~]|%[a-fA-F0-9][a-fA-F0-9])*=(?:[a-zA-Z0-9\-\.\_\~]|%[a-fA-F0-9][a-fA-F0-9])*?)?&?(?<keys>(?:[a-zA-Z0-9\-\.\_\~]|%[a-fA-F0-9][a-fA-F0-9])*=(?:[a-zA-Z0-9\-\.\_\~]|%[a-fA-F0-9][a-fA-F0-9])*?)?
    #0 0x0 in Regexp#initialize /src/lib/addressable/template.rb:1041
    #1 0x0 in Class#new /src/lib/addressable/template.rb:1041
    #2 0x0 in Addressable::Template#parse_template_pattern /src/lib/addressable/template.rb:1041
    #3 0x0 in Addressable::Template#ordered_variable_defaults /src/lib/addressable/template.rb:693
    #4 0x0 in Addressable::Template#variables /src/lib/addressable/template.rb:609
    #5 0x0 in block in <top (required)> .stitch/build/app/harness/stubs.rb:154
    #6 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

