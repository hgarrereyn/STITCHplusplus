# rexml: NoMethodError in REXML::Parsers::XPathParser#FunctionCall

| | |
|---|---|
| Kind | `NoMethodError` |
| Location | `REXML::Parsers::XPathParser#FunctionCall xpathparser.rb:616`, `REXML::Parsers::XPathParser#PrimaryExpr xpathparser.rb:591`, `REXML::Parsers::XPathParser#FilterExpr xpathparser.rb:561` |
| Library source | [https://github.com/ruby/rexml](https://github.com/ruby/rexml) @ `a622645e980e` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `xpath-nomethoderror` |

## Call sequence (6 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 new_doc(root="r")
#1 root(doc=o0.0)
#2 children(elem=o1.0, s=Src[8]c4ef6e6f74617469)
#3 add_element(elem=o1.0, name="e1")
#4 roundtrip(doc=o0.0)
#5 xpath_first(elem=o1.0, expr=Str[13,decoded]"/or/*/-last(\"")
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

`children` · harness/stubs.rb

```ruby
Stitch.endpoint :children, { elem: Borrow[Elem], s: Src[8] }, New[Elem] | nil do |elem, s|
  kids = elem.elements.to_a
  kids.empty? ? nil : s.pick(kids)
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

`roundtrip` · harness/stubs.rb

```ruby
# a written document parses again and writes the same text
Stitch.endpoint :roundtrip, { doc: Borrow[Doc] } do |doc|
  text = write(doc)
  again = begin
    REXML::Document.new(text)
  rescue *PARSE_ERRORS => e
    Stitch.log("written: #{text.inspect}")
    Stitch.check(false, "written document parses (#{e.class})")
  end
  Stitch.log("written: #{text.inspect}")
  Stitch.check(write(again) == text, "write/parse/write is stable")
end
```

`xpath_first` · harness/stubs.rb

```ruby
Stitch.endpoint :xpath_first, { elem: Borrow[Elem], expr: Str[64, XPathExpr] }, bails_on: [REXML::ParseException] do |elem, expr|
  REXML::XPath.first(elem, expr)
end
```

## Report

```
[stitch] EXCEPTION in xpath_first: NoMethodError: undefined method 'each' for nil
    #0 0x0 in REXML::Parsers::XPathParser#FunctionCall /src/lib/rexml/parsers/xpathparser.rb:616
    #1 0x0 in REXML::Parsers::XPathParser#PrimaryExpr /src/lib/rexml/parsers/xpathparser.rb:591
    #2 0x0 in REXML::Parsers::XPathParser#FilterExpr /src/lib/rexml/parsers/xpathparser.rb:561
    #3 0x0 in REXML::Parsers::XPathParser#PathExpr /src/lib/rexml/parsers/xpathparser.rb:543
    #4 0x0 in REXML::Parsers::XPathParser#UnionExpr /src/lib/rexml/parsers/xpathparser.rb:523
    #5 0x0 in REXML::Parsers::XPathParser#UnaryExpr /src/lib/rexml/parsers/xpathparser.rb:514
    #6 0x0 in REXML::Parsers::XPathParser#MultiplicativeExpr /src/lib/rexml/parsers/xpathparser.rb:480
    #7 0x0 in REXML::Parsers::XPathParser#AdditiveExpr /src/lib/rexml/parsers/xpathparser.rb:465
    #8 0x0 in REXML::Parsers::XPathParser#RelationalExpr /src/lib/rexml/parsers/xpathparser.rb:432
    #9 0x0 in REXML::Parsers::XPathParser#EqualityExpr /src/lib/rexml/parsers/xpathparser.rb:409
    #10 0x0 in REXML::Parsers::XPathParser#AndExpr /src/lib/rexml/parsers/xpathparser.rb:390
    #11 0x0 in REXML::Parsers::XPathParser#OrExpr /src/lib/rexml/parsers/xpathparser.rb:371
    #12 0x0 in REXML::Parsers::XPathParser#parse /src/lib/rexml/parsers/xpathparser.rb:25
    #13 0x0 in REXML::XPathParser#parse /src/lib/rexml/xpath_parser.rb:80
    #14 0x0 in REXML::XPath.first /src/lib/rexml/xpath.rb:39
    #15 0x0 in block in <top (required)> .stitch/build/app/harness/stubs.rb:120
    #16 0x0 in <main> /stitch/ruby/bin/stitch-rb:8
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH ruby toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

