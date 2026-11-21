# Ruby harnesses

Stubs are blocks registered with `Stitch.endpoint`. A hash of argument specs is the signature.
Ruby 3.4 (YJIT) runs in the image; the library is installed as a gem.

## Project

```sh
stitch init rexml --lang ruby --git https://github.com/ruby/rexml --rev v3.2.5
```

```toml
# stitch.toml
[project]
language = "ruby"

[ruby]
include = ["rexml"]     # gem names (or path prefixes) of the code under test
```

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && gem build *.gemspec && gem install --local ./*.gem
```

## Stubs

Every `harness/**/*.rb` file is loaded, in sorted order.

```ruby
require "stitch"
require "rexml/document"
include Stitch::DSL

Doc = Stitch.object("Doc")                    # object type
XmlText = Stitch.datatype("XmlText")          # data type: harness/data/XmlText/

# specs hash (argument order), outputs, exceptions that are not bugs
Stitch.endpoint :parse, { text: Str[4096, XmlText] }, New[Doc], bails_on: [REXML::ParseException] do |text|
  REXML::Document.new(text)
end

Stitch.endpoint :add, { doc: Borrow[Doc], name: Str[16], n: Int[0, 9] } do |doc, name, n|
  n.times { doc.root&.add_element(name) }
end

Stitch.endpoint :remove, { doc: Borrow[Doc], s: Src[8] } do |doc, s|
  kids = doc.root ? doc.root.elements.to_a : []
  Stitch.require(!kids.empty?, "no children")
  doc.root.delete_element(s.pick(kids))       # pick an existing element
end
```

| Spec | Value |
|---|---|
| `Borrow[T]`, `T` | an object; it stays available. `Stitch.consume(x)` if the library took it |
| `Take[T]` | an object the call consumes |
| outputs `New[T]` | returns an object; `nil` is a bail |
| outputs `New[T] \| nil` | optional output |
| outputs `[New[A], New[B] \| nil]` | several outputs (return an Array) |
| `U8`..`U64`, `I8`..`I64`, `Integer` | Integer (`Integer` is `I64`) |
| `F32`, `F64`, `Float` | Float |
| `Bool` | true / false |
| `Int[lo, hi]` | Integer in lo..hi (not `Range`: a top-level `Range` is Ruby's) |
| `OneOf["a", nil, :sym]` | one of the values (any Ruby objects) |
| `Flags[1, 2, 4]` | OR of a subset |
| `Str[max, Tag]`, `String` | UTF-8 String (invalid bytes replaced) |
| `Bytes[max, Tag]` | binary String |
| `FilePath[max, Tag]` | path of a temporary file (not `File`, for the same reason) |
| `Src[max]` | `Stitch::Source` |

## Bugs

Any exception a stub does not list in `bails_on:` is a bug (`NoMethodError`, `TypeError`,
`ArgumentError`, `SystemStackError`, `NoMemoryError` beyond 4 GB, ...), and so is a failed
`Stitch.check(cond, "what")`. `Stitch.bail("reason")` and `Stitch.require(cond, "reason")` stop the
call without a bug. They raise `Exception`s, not `StandardError`s: a library's `rescue => e` does
not swallow them.

```ruby
Stitch.time_limit(0.05) { out = rule.first(n) }   # bails ("time limit") instead of a timeout
```

## Decoders and attributes

```ruby
Stitch.decoder(XmlText) do |s|                # s: Stitch::Source
  out = +"<r>"
  out << "<#{s.token}>#{s.str(8)}</#{s.token}>" while s.more
  out << "</r>"
end

Stitch.set(doc, "count", 1)
Stitch.get(doc, "count", 0)
Stitch.get_obj(doc, "parent")                 # a linked object, or nil
```

`Stitch::Source`: `u8`..`u64`, `i8`..`i64`, `f32`, `f64`, `range(lo, hi)`, `choice(n)`,
`pick(list)`, `coin(p)`, `more`, `str(max)`, `bytes(max)`, `token`.

## Coverage and comparisons

While fuzzing, files of the gems in `include` (and the harness) are compiled from rewritten
source (Prism, through `RubyVM::InstructionSequence.load_iseq`): a byte counter at the start of
every statement list and on the right of `&&`/`||`, and `==`, `!=`, `<`..., `eql?`,
`start_with?`, `include?`, `key?` and `case`/`when` literals logged for the fuzzer's comparison
feedback. No line is added, so backtraces keep their line numbers. `stitch cov` uses Ruby's
`Coverage` (lines and branches) and genhtml.

## Reproducers

`stitch write c1` writes `repro.rb`: the harness code followed by `main`, which calls each stub
with its recorded arguments (`Stitch.call(n, :name) { ... }`), and `stitch.rb`, a stand-in for
the runtime. Shrink it to plain library code; `stitch try min.rb` compares the report with the
original crash.
