# Stubs: one Stitch.endpoint per API call. The specs hash is the signature.
#
# Borrow[T] uses an object, Take[T] consumes it, New[T] (outputs) produces one.
# Every other parameter is fuzzed: Str[64], Bytes[256], I32, Int[0, 9], OneOf["a", "b"], ...
# An exception that is not listed in bails_on: is a bug.
require "stitch"
include Stitch::DSL

# require "foo"
# Doc = Stitch.object("Doc")
#
# Stitch.endpoint :parse, { text: Str[4096] }, New[Doc], bails_on: [Foo::ParseError] do |text|
#   Foo.parse(text)
# end
#
# Stitch.endpoint :render, { doc: Borrow[Doc] } do |doc|
#   doc.render
# end
