require "kramdown"

Kramdown::Document.new("&#9999999;").to_kramdown   # ArgumentError: invalid byte sequence in UTF-8
