require "kramdown"

Kramdown::Document.new("{:a: .x a}\n\npara\n{:a}\n")
