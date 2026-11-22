require "stitch"
require "kramdown"
require "kramdown/converter"
include Stitch::DSL

Doc = Stitch.object("Doc")
Elem = Stitch.object("Elem")
KdText = Stitch.datatype("KdText")

Stitch.decoder(KdText) do |s|
  out = +""
  while s.more
    case s.choice(14)
    when 0 then out << "#" * s.range(1, 6) << " " << s.str(10) << " {#" << s.str(5) << "}\n\n"
    when 1 then out << s.token << s.str(12) << "\n"
    when 2 then out << "\n"
    when 3 then out << "*" << s.str(8) << "* **" << s.str(8) << "** `" << s.str(6) << "` [" << s.str(6) << "](" << s.str(10) << ")"
    when 4 then out << "<" << s.pick(%w[div span p table td a pre em]) << " markdown=\"" << s.pick(%w[0 1 block span]) << "\">" << s.str(10) << "</" << s.pick(%w[div span p table td a pre em]) << ">\n"
    when 5 then out << "* " << s.str(8) << "\n    * " << s.str(8) << "\n"
    when 6 then out << "| " << s.str(4) << " | " << s.str(4) << " |\n|---|:-:|\n| " << s.str(4) << " | " << s.str(4) << " |\n"
    when 7 then out << "[^" << s.str(3) << "]: " << s.str(10) << "\n\ntext[^" << s.str(3) << "]\n"
    when 8 then out << "{:" << s.pick(%w[.a #b key=val toc]) << "}\n"
    when 9 then out << "\"" << s.str(6) << "\" '" << s.str(5) << "' -- ... << >>\n"
    when 10 then out << "> " << s.str(10) << "\n> > " << s.str(10) << "\n"
    when 11 then out << "~~~ " << s.pick(%w[ruby html]) << "\n" << s.str(12) << "\n~~~\n"
    when 12 then out << "&" << s.pick(%w[amp lt hellip nbsp #x41 #9999999]) << ";" << s.str(4)
    else out << s.str(20)
    end
  end
  out
end

INPUTS = %w[kramdown html markdown]

def mkdoc(text, input, opts)
  Kramdown::Document.new(text, opts.merge(input: input))
end

OptsT = Stitch.datatype("KdOpts")
Stitch.decoder(OptsT) do |s|
  o = {}
  o[:auto_ids] = s.coin if s.coin(0.3)
  o[:parse_block_html] = s.coin if s.coin(0.5)
  o[:parse_span_html] = s.coin if s.coin(0.3)
  o[:html_to_native] = s.coin if s.coin(0.3)
  o[:smart_quotes] = [s.pick(%w[lsquo rsquo ldquo rdquo]), s.pick(%w[lsquo rsquo apos 39]), s.pick(%w[ldquo rdquo laquo 8220]), s.pick(%w[rdquo raquo 8221])] if s.coin(0.2)
  o[:entity_output] = s.pick([:as_char, :numeric, :symbolic, :as_input]) if s.coin(0.5)
  o[:header_offset] = s.range(-3, 8) if s.coin(0.4)
  o[:toc_levels] = s.coin ? s.range(1, 6)..s.range(1, 6) : [s.range(1, 6), s.range(1, 6)] if s.coin(0.4)
  o[:auto_id_prefix] = s.str(4) if s.coin(0.15)
  o[:auto_id_stripping] = s.coin if s.coin(0.15)
  o[:transliterated_header_ids] = s.coin if s.coin(0.15)
  o[:footnote_nr] = s.range(-2, 5) if s.coin(0.2)
  o[:footnote_prefix] = s.str(3) if s.coin(0.1)
  o[:footnote_backlink] = s.str(6) if s.coin(0.1)
  o[:footnote_backlink_inline] = s.coin if s.coin(0.1)
  o[:line_width] = s.range(1, 120) if s.coin(0.3)
  o[:remove_block_html_tags] = s.coin if s.coin(0.2)
  o[:remove_span_html_tags] = s.coin if s.coin(0.2)
  o[:remove_line_breaks_for_cjk] = s.coin if s.coin(0.1)
  o[:typographic_symbols] = {hellip: s.str(3), mdash: s.str(2)} if s.coin(0.1)
  o[:syntax_highlighter] = s.pick([nil, :rouge, :coderay]) if s.coin(0.2)
  o[:math_engine] = s.pick([nil, :mathjax, :ritex]) if s.coin(0.2)
  o[:link_defs] = {s.str(3) => [s.str(5), s.str(4)]} if s.coin(0.1)
  o[:latex_headers] = %w[chapter section subsection paragraph part a] .first(s.range(1, 6)) if s.coin(0.1)
  o[:template] = s.str(6) if s.coin(0.05)
  o
end

# all of these are invalid-option / bad input errors
BAILS = [Kramdown::Error]

Stitch.endpoint :parse, { text: Str[3000, KdText], input: OneOf[*INPUTS], opts: Val[OptsT, 256] }, New[Doc], bails_on: BAILS do |text, input, opts|
  mkdoc(text, input, opts)
end

Stitch.endpoint :parse_default, { text: Str[3000, KdText] }, New[Doc], bails_on: BAILS do |text|
  Kramdown::Document.new(text)
end

Stitch.endpoint :parse_raw, { text: Str[300], input: OneOf[*INPUTS], opts: Val[OptsT, 256] }, New[Doc], bails_on: BAILS do |text, input, opts|
  mkdoc(text, input, opts)
end

Stitch.endpoint :to_html, { d: Borrow[Doc] }, bails_on: BAILS do |d|
  a = d.to_html
  Stitch.check(a.is_a?(String), "html is a String")
  b = d.to_html
  Stitch.check(a == b, "to_html deterministic on same doc")
end

Stitch.endpoint :to_latex, { d: Borrow[Doc] }, bails_on: BAILS do |d|
  d.to_latex
end

Stitch.endpoint :to_man, { d: Borrow[Doc] }, bails_on: BAILS do |d|
  d.to_man
end

Stitch.endpoint :to_toc, { d: Borrow[Doc] }, bails_on: BAILS do |d|
  t = d.to_toc
  Stitch.check(t.is_a?(Kramdown::Element), "toc is an Element")
end

Stitch.endpoint :to_hash_ast, { d: Borrow[Doc] }, bails_on: BAILS do |d|
  h = d.to_hash_ast
  Stitch.check(h.is_a?(Hash) && h[:type] == :root, "hash ast root")
end

Stitch.endpoint :to_remove_html_tags, { d: Borrow[Doc] }, bails_on: BAILS do |d|
  d.to_remove_html_tags
end

Stitch.endpoint :to_kramdown, { d: Borrow[Doc] }, bails_on: BAILS do |d|
  d.to_kramdown
end

Stitch.endpoint :warnings, { d: Borrow[Doc] } do |d|
  Stitch.check(d.warnings.all? { |w| w.is_a?(String) }, "warnings are strings")
  d.warnings.size
end

def with_time(&b) = Stitch.time_limit(1.0, &b)

# Round trip: kramdown output of a doc re-parses to the same HTML
Stitch.endpoint :roundtrip_kramdown, { text: Str[2000, KdText], pbh: Bool }, bails_on: BAILS do |text, pbh|
  o = { parse_block_html: pbh, auto_ids: false }
  d1 = Kramdown::Document.new(text, o)
  h1 = d1.to_html
  k = d1.to_kramdown
  d2 = Kramdown::Document.new(k, o)
  h2 = d2.to_html
  Stitch.check(h1 == h2, "to_kramdown re-parses to the same HTML")
end

Stitch.endpoint :html_deterministic, { text: Str[2000, KdText], input: OneOf[*INPUTS], opts: Val[OptsT, 256] }, bails_on: BAILS do |text, input, opts|
  a = mkdoc(text, input, opts).to_html
  b = mkdoc(text, input, opts).to_html
  Stitch.check(a == b, "to_html deterministic across parses")
end

Stitch.endpoint :hash_ast_matches_tree, { text: Str[1000, KdText] }, bails_on: BAILS do |text|
  d = Kramdown::Document.new(text)
  h = d.to_hash_ast
  count = ->(e) { 1 + e[:children].sum { |c| count.(c) } }
  count2 = ->(e) { 1 + e.children.sum { |c| count2.(c) } }
  Stitch.check(count.(h) == count2.(d.root), "hash_ast has same node count as tree")
end

# Element tree
Stitch.endpoint :root, { d: Borrow[Doc] }, New[Elem] do |d|
  d.root
end

Stitch.endpoint :child, { e: Borrow[Elem], s: Src[4] }, New[Elem] do |e, s|
  Stitch.require(!e.children.empty?, "no children")
  s.pick(e.children)
end

Stitch.endpoint :elem_inspect, { e: Borrow[Elem] } do |e|
  Stitch.check(e.type.is_a?(Symbol), "type is a symbol")
  e.attr.to_a
  e.value.to_s
  e.options.to_a
  e.inspect
end

Stitch.endpoint :elem_to_html, { e: Borrow[Elem], opts: Val[OptsT, 256] }, bails_on: BAILS do |e, opts|
  Stitch.require(e.type != :root, "root handled elsewhere")
  Kramdown::Converter::Html.convert(e, Kramdown::Options.merge(opts)) rescue raise if false
  Kramdown::Converter::Html.convert(e, Kramdown::Options.merge(opts))
end

Stitch.endpoint :tree_to_kramdown, { d: Borrow[Doc] }, bails_on: BAILS do |d|
  Kramdown::Converter::Kramdown.convert(d.root, d.options)
end

Stitch.endpoint :add_para, { d: Borrow[Doc], t: Str[40] }, bails_on: BAILS do |d, t|
  p = Kramdown::Element.new(:p)
  p.children << Kramdown::Element.new(:text, t)
  d.root.children << p
end

Stitch.endpoint :add_header, { d: Borrow[Doc], t: Str[40], lvl: Int[1, 6] }, bails_on: BAILS do |d, t, lvl|
  h = Kramdown::Element.new(:header, nil, nil, level: lvl, raw_text: t)
  h.children << Kramdown::Element.new(:text, t)
  d.root.children << h
end

Stitch.endpoint :clear_children, { d: Borrow[Doc], s: Src[4] } do |d, s|
  Stitch.require(!d.root.children.empty?, "no children")
  d.root.children.delete(s.pick(d.root.children))
end

Stitch.endpoint :parse_options_str, { text: Str[200, KdText] } do |text|
  # in-text options and kramdown parse_block_html interplay
  Stitch.require(true, "")
  Kramdown::Document.new("{::options parse_block_html=\"true\" /}\n" + text).to_html
end
