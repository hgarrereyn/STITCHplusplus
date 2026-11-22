# Ruby harnesses

Stubs are blocks registered with `Stitch.endpoint`. A hash of argument specs is the signature.
Ruby 3.4 (YJIT) runs in the image; the library is installed as a gem. The fuzzer talks to a Ruby
worker process.

## Project setup

```sh
stitch init rexml --lang ruby --git https://github.com/ruby/rexml --rev v3.2.5
cd rexml
```

`Dockerfile`: install the library as a gem.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && gem build *.gemspec && gem install --local ./*.gem
```

If the gem has dependencies, drop `--local` so `gem install` fetches them.

`stitch.toml`:

```toml
[project]
name = "rexml"
language = "ruby"

[source]
git = "https://github.com/ruby/rexml"
rev = "v3.2.5"

[ruby]
include = ["rexml"]       # gem names (or path prefixes) of the code under test; empty: all but the standard library
# exclude = ["rexml/xpath_parser"]

[fuzz]
timeout_ms = 2000
```

`include` decides which code gets coverage and comparison tracing. Set it to the library's gem
name.

## Files

```
harness/
  _common.rb           shared object types, data types and helpers (sorts first)
  parse.rb             stubs; every harness/**/*.rb file is loaded, in sorted order
  data/XmlText/        seeds/, *.dict for the XmlText data type
```

Files share constants such as `Doc` directly. Put shared definitions in a file whose name sorts
first, such as `_common.rb`.

## A complete example

```ruby
require "stitch"
require "rexml/document"
include Stitch::DSL                           # Borrow, New, Str, Int, ... without the Stitch:: prefix

Doc = Stitch.object("Doc")                    # object type
XmlText = Stitch.datatype("XmlText")          # data type: harness/data/XmlText/

# name, argument specs (in block argument order), outputs, exceptions that are not bugs
Stitch.endpoint :parse, { text: Str[4096, XmlText] }, New[Doc], bails_on: [REXML::ParseException] do |text|
  REXML::Document.new(text)
end

Stitch.endpoint :add, { doc: Borrow[Doc], name: Str[16], n: Int[0, 9] } do |doc, name, n|
  Stitch.require(doc.root, "no root")
  n.times { doc.root.add_element(name) }
end

Stitch.endpoint :remove, { doc: Borrow[Doc], s: Src[8] } do |doc, s|
  kids = doc.root ? doc.root.elements.to_a : []
  Stitch.require(!kids.empty?, "no children")
  doc.root.delete_element(s.pick(kids))       # pick an existing element
end

Stitch.endpoint :roundtrip, { doc: Borrow[Doc] }, bails_on: [REXML::ParseException] do |doc|
  out = +""
  doc.write(output: out)
  again = REXML::Document.new(out)
  Stitch.check(again.root&.name == doc.root&.name, "write/parse keeps the root")
end
```

## Objects

```ruby
Doc = Stitch.object("Doc")
Conn = Stitch.object("Conn", drop: ->(c) { c.close })   # drop: frees objects left at the end
```

| Spec | Meaning |
|---|---|
| `Borrow[T]` (or just `T`) | uses an object; it stays available |
| `Take[T]` | consumes the object |
| `Stitch.consume(x)` | on a borrowed input: the library took ownership in this call |
| outputs `New[T]` | the block returns a new object; `nil` is a bail |
| outputs `New[T] \| nil` | optional output: `nil` means no object, not a bail |
| outputs `[New[A], New[B] \| nil]` | several outputs: return an Array |
| no outputs argument | the stub produces nothing |

```ruby
Elem = Stitch.object("Elem")

# two outputs, the second optional
Stitch.endpoint :parse_root, { text: Str[4096, XmlText] }, [New[Doc], New[Elem] | nil],
                bails_on: [REXML::ParseException] do |text|
  doc = REXML::Document.new(text)
  [doc, doc.root]
end

Stitch.endpoint :detach, { e: Take[Elem] } do |e|
  Stitch.require(e.parent, "already detached")
  e.remove
end
```

## Fuzzed parameters

| Spec | Value |
|---|---|
| `U8`..`U64`, `I8`..`I64`, `Integer` | Integer (`Integer` is `I64`) |
| `F32`, `F64`, `Float` | Float |
| `Bool` | true or false |
| `Int[lo, hi]` | Integer in lo..hi |
| `OneOf["a", nil, :sym]` | one of the values (any Ruby objects); `OneOf[*LIST]` for a list |
| `Flags[1, 2, 4]` | OR of a subset |
| `Str[max, Tag]`, `String` | UTF-8 String of at most `max` bytes (invalid bytes replaced); `Tag` is an optional data type |
| `Bytes[max, Tag]` | binary String |
| `FilePath[max, Tag]` | path of a temporary file holding the data |
| `Val[Tag, max = 1024]` | the value `Tag`'s decoder returns (any object) |
| `Src[max]` | a `Stitch::Source` for choices inside the stub |

The names differ from other languages because Ruby already has `Range` and `File`: use `Int`
and `FilePath`.

```ruby
Stitch.endpoint :format, { doc: Borrow[Doc], indent: Int[-1, 8], mode: OneOf[:pretty, :compact] } do |doc, indent, mode|
  out = +""
  formatter = mode == :pretty ? REXML::Formatters::Pretty.new(indent) : REXML::Formatters::Default.new
  formatter.write(doc, out)
end

Stitch.endpoint :load_file, { path: FilePath[4096, XmlText] }, New[Doc], bails_on: [REXML::ParseException] do |path|
  File.open(path) { |f| REXML::Document.new(f) }
end
```

## Bail, require, check

```ruby
Stitch.bail("reason")                        # stop this call; not a bug
Stitch.require(cond, "reason")               # bail unless cond
Stitch.check(cond, "what")                   # a bug if false: "check failed: what"
Stitch.log("n=#{n}")                         # printed by `stitch exec --trace`
Stitch.init { REXML::Security.entity_expansion_limit = 1000 }   # once per worker process
Stitch.time_limit(0.05) { out = rule.first(n) }   # bails ("time limit") instead of a timeout
```

`bail` and `require` raise `Exception` subclasses, not `StandardError`s, so a library's
`rescue => e` does not swallow them.

These are bugs: any exception not listed in `bails_on:` (`NoMethodError`, `TypeError`,
`ArgumentError`, `SystemStackError`, `NoMemoryError`, ...), a failed `Stitch.check`, and
programs that run past `timeout_ms`. `bails_on:` takes exception classes; subclasses match too.

Use `Stitch.time_limit` around calls that are slow by design (iterating an endless sequence), so
they bail instead of being reported as timeouts.

## Attributes

```ruby
Stitch.set(doc, "edits", Stitch.get(doc, "edits", 0) + 1)   # any value
Stitch.get(doc, "edits", 0)                 # with a default: a missing key is normal
Stitch.get(doc, "edits")                    # without: a missing key is reported by `stitch probe`
Stitch.has?(doc, "edits")
Stitch.delete(doc, "edits")
Stitch.global_set("mode", 1)                # program-wide; reset for every program
Stitch.global_get("mode", 0)
```

Links to other objects:

```ruby
Stitch.endpoint :root_of, { doc: Borrow[Doc] }, New[Elem] do |doc|
  e = doc.root
  Stitch.require(e, "no root")
  Stitch.set(e, "doc", doc)                   # link the element to its document
  e
end

Stitch.endpoint :elem_text, { e: Borrow[Elem] } do |e|
  Stitch.require(Stitch.get_obj(e, "doc"), "document is gone")   # nil once the Doc was consumed
  e.text
end
```

## Data types and decoders

Tag a `Str`, `Bytes` or `FilePath` parameter with a data type and create
`harness/data/<Name>/` with `seeds/` (sample inputs) and `*.dict` files (one quoted token per
line). A decoder builds values from fuzzer choices:

```ruby
XmlText = Stitch.datatype("XmlText")

Stitch.decoder(XmlText) do |s|                # s: Stitch::Source
  out = +"<r>"
  while s.more
    tag = s.token
    out << "<#{tag}>#{s.str(8)}</#{tag}>"
  end
  out << "</r>"
end
```

`stitch sample XmlText -n 5` prints values the decoder makes.

A decoder can return any object. Take it as `Val[Tag, max]`, and build decoders on each other
with `s.draw(Tag)`:

```ruby
Pair = Stitch.datatype("Pair")
Pairs = Stitch.datatype("Pairs")

Stitch.decoder(Pair) do |s|
  key = s.coin(0.3) ? s.token : s.str(8)
  [key, s.coin ? s.str(12) : format("%.2f", s.f64(0, 100))]
end

Stitch.decoder(Pairs) do |s|
  out = []
  out << s.draw(Pair) while s.more
  out
end

Stitch.endpoint :put_all, { st: Borrow[Store], kvs: Val[Pairs, 256] }, bails_on: [ArgumentError] do |st, kvs|
  kvs.each { |k, v| st.put(k, v) }
end
```

`Stitch::Source` methods:

| Method | Returns |
|---|---|
| `u8` .. `u64`, `i8` .. `i64`, `f32`, `f64` | a number |
| `range(lo, hi)` | Integer in lo..hi |
| `choice(n)` | index in 0...n |
| `pick(list)` | one element of the list |
| `coin(p = 0.5)` | true with probability about p |
| `more` | loop condition: `while s.more` |
| `f64(lo, hi)`, `f32(lo, hi)` | finite Float in lo..hi |
| `str(max)`, `bytes(max)` | text or bytes of at most `max` bytes |
| `fixed(n)` | exactly n bytes |
| `rest` | every byte that is left |
| `token` | a token from the data type's `.dict` files |
| `draw(Tag)` | a value from data type `Tag`'s decoder |

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/repro.rb` (the harness code, then a `main` that
calls each stub with its recorded arguments) and `stitch.rb`, a stand-in for the runtime:

```ruby
def main
  doc0 = Stitch.call(0, :parse) { ... }
  Stitch.call(1, :remove) { ... }
end
```

Shrink a copy to plain library code and check it after each edit:

```sh
cp .stitch/standalone/c1/repro.rb .stitch/standalone/c1/min.rb
stitch try .stitch/standalone/c1/min.rb
```

A finished reproducer:

```ruby
require "rack"

Rack::MediaType.params(";")    # NoMethodError in Rack::MediaType.params
```

`stitch try` compares the uncaught exception with the original crash. Details:
`stitch guide reproducers`.

## Coverage

While fuzzing, files of the gems in `include` (and the harness) are loaded from rewritten source
that counts statements and records comparisons. Line numbers in backtraces are unchanged.
`stitch cov` uses Ruby's `Coverage` (lines and branches).
