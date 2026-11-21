# rexml: SystemStackError in Kernel#clone

| | |
|---|---|
| Kind | `SystemStackError` |
| Location | `Kernel#clone <internal:kernel>:48 `, `REXML::Entity#value entity.rb:137`, `REXML::Entity#unnormalized entity.rb:75` |
| Library source | [https://github.com/ruby/rexml](https://github.com/ruby/rexml) @ `a622645e980e` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `entity-recursion-text` |

## Call sequence (5 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 parse(text=Str[134,decoded]"<!DOCTYPE r [<!ENTITY e0 \"&e0;&e0;\"><!ENTITY e1 \"&e0;&e0;\"><!ENTITY e2 \"&e1;&e1;\"><!ATTLIST r a CDATA \"d\">]><a c=\"qQ:/kE{@nC\">&e0;</a>")
#1 root(doc=o0.0)
#2 add_element(elem=o1.0, name="a.b")
#3 deep_clone(elem=o1.0)
#4 text_of(elem=o1.0)
```

## Stubs used

What each call does (from `project/harness/`):

`parse` · harness/stubs.rb

```ruby
Stitch.endpoint :parse, { text: Str[2048, XmlText] }, New[Doc], bails_on: PARSE_ERRORS do |text|
  REXML::Document.new(text)
end
```

`root` · harness/stubs.rb

```ruby
Stitch.endpoint :root, { doc: Borrow[Doc] }, New[Elem] | nil do |doc|
  doc.root
end
```

`add_element` · harness/stubs.rb

```ruby
Stitch.endpoint :add_element, { elem: Borrow[Elem], name: OneOf[*PLAIN] }, New[Elem] do |elem, name|
  e = elem.add_element(name)
  Stitch.check(e.parent.equal?(elem), "add_element sets the parent")
  e
end
```

`deep_clone` · harness/stubs.rb

```ruby
Stitch.endpoint :deep_clone, { elem: Borrow[Elem] }, New[Elem] do |elem|
  c = elem.deep_clone
  Stitch.check(write(c) == write(elem), "deep_clone writes the same")
  c
end
```

`text_of` · harness/stubs.rb

```ruby
Stitch.endpoint :text_of, { elem: Borrow[Elem] } do |elem|
  elem.text
  elem.texts.each(&:value)
  elem.attributes.each_attribute(&:value)
end
```

## Report

```
[stitch] EXCEPTION in text_of: SystemStackError: stack level too deep
    #0 0x0 in Kernel#clone <internal:kernel>:48
    #1 0x0 in REXML::Entity#value /src/lib/rexml/entity.rb:137
    #2 0x0 in REXML::Entity#unnormalized /src/lib/rexml/entity.rb:75
    #3 0x0 in REXML::DocType#entity /src/lib/rexml/doctype.rb:182
    #4 0x0 in REXML::Text.expand /src/lib/rexml/text.rb:417
    #5 0x0 in block in REXML::Text.unnormalize /src/lib/rexml/text.rb:395
    #6 0x0 in String#gsub /src/lib/rexml/text.rb:394
    #7 0x0 in REXML::Text.unnormalize /src/lib/rexml/text.rb:394
    #8 0x0 in REXML::Entity#unnormalized /src/lib/rexml/entity.rb:77
    #9 0x0 in REXML::DocType#entity /src/lib/rexml/doctype.rb:182
    #10 0x0 in REXML::Text.expand /src/lib/rexml/text.rb:417
    #11 0x0 in block in REXML::Text.unnormalize /src/lib/rexml/text.rb:395
    #12 0x0 in String#gsub /src/lib/rexml/text.rb:394
    #13 0x0 in REXML::Text.unnormalize /src/lib/rexml/text.rb:394
    #14 0x0 in REXML::Entity#unnormalized /src/lib/rexml/entity.rb:77
    #15 0x0 in REXML::DocType#entity /src/lib/rexml/doctype.rb:182
    #16 0x0 in REXML::Text.expand /src/lib/rexml/text.rb:417
    #17 0x0 in block in REXML::Text.unnormalize /src/lib/rexml/text.rb:395
    #18 0x0 in String#gsub /src/lib/rexml/text.rb:394
    #19 0x0 in REXML::Text.unnormalize /src/lib/rexml/text.rb:394
    #20 0x0 in REXML::Entity#unnormalized /src/lib/rexml/entity.rb:77
    #21 0x0 in REXML::DocType#entity /src/lib/rexml/doctype.rb:182
    #22 0x0 in REXML::Text.expand /src/lib/rexml/text.rb:417
    #23 0x0 in block in REXML::Text.unnormalize /src/lib/rexml/text.rb:395
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

