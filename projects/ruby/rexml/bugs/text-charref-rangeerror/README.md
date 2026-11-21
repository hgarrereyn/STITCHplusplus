# rexml: RangeError in REXML::Text.expand

| | |
|---|---|
| Kind | `RangeError` |
| Location | `REXML::Text.expand text.rb:410`, `block in REXML::Text.unnormalize text.rb:395`, `String#gsub text.rb:394` |
| Library source | [https://github.com/ruby/rexml](https://github.com/ruby/rexml) @ `a622645e980e` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `text-charref-rangeerror` |

## Call sequence (3 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 new_doc(root="r")
#1 root(doc=o0.0)
#2 set_text(elem=o1.0, text=Str[26]"u&#1111111111111110;&e1;\"\"")
```

## Stubs used

What each call does (from `project/harness/`):

`new_doc` · harness/stubs.rb

```ruby
Stitch.endpoint :new_doc, { root: OneOf["r", "a", "b"] }, New[Doc] do |root|
  d = REXML::Document.new
  d.add_element(root)
  d
end
```

`root` · harness/stubs.rb

```ruby
Stitch.endpoint :root, { doc: Borrow[Doc] }, New[Elem] | nil do |doc|
  doc.root
end
```

`set_text` · harness/stubs.rb

```ruby
Stitch.endpoint :set_text, { elem: Borrow[Elem], text: Str[32] }, bails_on: [RuntimeError] do |elem, text|
  elem.text = text
  plain = !text.include?("&") && !text.include?("\r") && text.valid_encoding?
  Stitch.check(elem.text == text || text.empty? || !plain, "text reads back")
end
```

## Report

```
[stitch] EXCEPTION in set_text: RangeError: pack(U): value out of range
    #0 0x0 in REXML::Text.expand /src/lib/rexml/text.rb:410
    #1 0x0 in block in REXML::Text.unnormalize /src/lib/rexml/text.rb:395
    #2 0x0 in String#gsub /src/lib/rexml/text.rb:394
    #3 0x0 in REXML::Text.unnormalize /src/lib/rexml/text.rb:394
    #4 0x0 in REXML::Text#value /src/lib/rexml/text.rb:251
    #5 0x0 in REXML::Element#text /src/lib/rexml/element.rb:1031
    #6 0x0 in block in <top (required)> .stitch/build/app/harness/stubs.rb:145
    #7 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

