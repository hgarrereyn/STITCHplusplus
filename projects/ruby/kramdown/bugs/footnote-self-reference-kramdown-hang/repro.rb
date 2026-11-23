require "kramdown"

Kramdown::Document.new("[^1]: a[^1]\n\nx[^1]\n").to_kramdown
