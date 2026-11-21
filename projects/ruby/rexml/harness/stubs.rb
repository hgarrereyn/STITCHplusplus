# rexml: DOM parsing and serialization, the pull and SAX2 parsers, XPath, tree edits, entities.
# Oracles: a written document parses again and writes the same text; XPath results belong to the
# document; edits keep the tree consistent (parents, attribute lookups).
require "stitch"
require "rexml/document"
require "rexml/parsers/pullparser"
require "rexml/parsers/sax2parser"
require "rexml/streamlistener"
include Stitch::DSL

Doc = Stitch.object("Doc")
Elem = Stitch.object("Elem")
XmlText = Stitch.datatype("XmlText")
XPathExpr = Stitch.datatype("XPathExpr")

PARSE_ERRORS = [REXML::ParseException, REXML::UndefinedNamespaceException].freeze
NAMES = %w[a b c d root item x:y ns:a xmlns e1 E _a a-b a.b].freeze
# names for elements built through the API: a prefix without a namespace declaration is the
# caller's mistake (REXML writes it, then refuses to read it back)
PLAIN = %w[a b c d root item e1 E _a a-b a.b].freeze

Stitch.decoder(XmlText) do |s|
  out = +""
  out << '<?xml version="1.0"?>' if s.coin(0.3)
  if s.coin(0.3)
    ents = (0...s.range(0, 3)).map { |i| %(<!ENTITY e#{i} "#{s.coin(0.5) ? "&e#{[i - 1, 0].max};&e#{[i - 1, 0].max};" : s.str(8)}">) }
    out << "<!DOCTYPE r [#{ents.join}#{s.coin(0.3) ? '<!ATTLIST r a CDATA "d">' : ''}#{s.coin(0.2) ? '<!ELEMENT r ANY>' : ''}]>"
  end
  depth = 0
  stack = []
  while s.more
    case s.choice(9)
    when 0, 1
      n = s.coin(0.7) ? s.pick(NAMES) : s.str(6)
      attrs = (0...s.range(0, 2)).map { %( #{s.coin(0.8) ? s.pick(NAMES) : s.token}="#{s.coin(0.7) ? s.str(10) : s.token}") }.join
      if s.coin(0.2)
        out << "<#{n}#{attrs}/>"
      else
        out << "<#{n}#{attrs}>"
        stack << n
        depth += 1
      end
    when 2
      out << "</#{stack.pop || 'r'}>" if s.coin(0.9) || stack.empty?
    when 3 then out << s.str(16)
    when 4 then out << "&#{s.coin(0.5) ? "e#{s.range(0, 3)}" : s.pick(%w[amp lt gt quot apos #60 #x3c #1114112])};"
    when 5 then out << "<![CDATA[#{s.str(12)}]]>"
    when 6 then out << "<!--#{s.str(10)}-->"
    when 7 then out << "<?#{s.pick(%w[pi xml-stylesheet x])} #{s.str(8)}?>"
    else out << s.token
    end
  end
  out << "</#{stack.pop}>" until stack.empty?
  out
end

Stitch.decoder(XPathExpr) do |s|
  out = +""
  out << s.pick(["/", "//", ".", "..", "", "/*/"])
  while s.more
    out << case s.choice(6)
           when 0 then s.pick(NAMES + %w[* node() text() comment() @* @a])
           when 1 then "[#{s.pick(%w[1 2 last() position()=1 @a @a='x' text()='y' count(*)>1 contains(.,'a') not(b)])}]"
           when 2 then s.pick(%w[/ // :: | and or + - div mod])
           when 3 then s.pick(%w[child:: descendant:: parent:: ancestor:: following-sibling:: preceding:: attribute:: self::])
           when 4 then s.token
           else s.str(4)
           end
  end
  out
end

def write(node)
  out = +""
  node.write(out)
  out
end

Stitch.endpoint :parse, { text: Str[2048, XmlText] }, New[Doc], bails_on: PARSE_ERRORS do |text|
  REXML::Document.new(text)
end

Stitch.endpoint :new_doc, { root: OneOf["r", "a", "b"] }, New[Doc] do |root|
  d = REXML::Document.new
  d.add_element(root)
  d
end

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

Stitch.endpoint :pretty, { doc: Borrow[Doc], indent: Int[0, 4] } do |doc, indent|
  out = +""
  REXML::Formatters::Pretty.new(indent).write(doc, out)
end

Stitch.endpoint :root, { doc: Borrow[Doc] }, New[Elem] | nil do |doc|
  doc.root
end

Stitch.endpoint :xpath, { doc: Borrow[Doc], expr: Str[64, XPathExpr] }, New[Elem] | nil,
                bails_on: [REXML::ParseException] do |doc, expr|
  r = REXML::XPath.match(doc, expr)
  els = r.select { |x| x.is_a?(REXML::Element) && !x.is_a?(REXML::Document) } # (a Document is an Element too)
  els.each { |e| Stitch.check(e.document.equal?(doc) || e.document.nil?, "XPath results belong to the document") }
  els.first
end

Stitch.endpoint :xpath_first, { elem: Borrow[Elem], expr: Str[64, XPathExpr] }, bails_on: [REXML::ParseException] do |elem, expr|
  REXML::XPath.first(elem, expr)
end

Stitch.endpoint :add_element, { elem: Borrow[Elem], name: OneOf[*PLAIN] }, New[Elem] do |elem, name|
  e = elem.add_element(name)
  Stitch.check(e.parent.equal?(elem), "add_element sets the parent")
  e
end

# REXML reports characters that XML does not allow with RuntimeError ("Illegal character ...")
Stitch.endpoint :add_attribute, { elem: Borrow[Elem], name: OneOf[*NAMES], value: Str[16] },
                bails_on: [RuntimeError] do |elem, name, value|
  elem.add_attribute(name, value)
  # (attribute values are normalized: entity references, CR, invalid UTF-8)
  plain = !value.include?("&") && !value.include?("\r") && value.valid_encoding?
  Stitch.check(elem.attributes[name] == value || !plain || name.include?(":") || name == "xmlns", "attribute reads back")
end

Stitch.endpoint :add_text, { elem: Borrow[Elem], text: Str[32] }, bails_on: [RuntimeError] do |elem, text|
  elem.add_text(text)
end

Stitch.endpoint :set_text, { elem: Borrow[Elem], text: Str[32] }, bails_on: [RuntimeError] do |elem, text|
  elem.text = text
  plain = !text.include?("&") && !text.include?("\r") && text.valid_encoding?
  Stitch.check(elem.text == text || text.empty? || !plain, "text reads back")
end

Stitch.endpoint :cdata, { elem: Borrow[Elem], text: Str[32] } do |elem, text|
  elem.add(REXML::CData.new(text))
end

Stitch.endpoint :comment, { elem: Borrow[Elem], text: Str[16] } do |elem, text|
  elem.add(REXML::Comment.new(text))
end

Stitch.endpoint :delete_child, { elem: Borrow[Elem], s: Src[8] } do |elem, s|
  kids = elem.elements.to_a
  Stitch.require(!kids.empty?, "no children")
  k = s.pick(kids)
  elem.delete_element(k)
  Stitch.check(k.parent.nil?, "a deleted element has no parent")
end

Stitch.endpoint :children, { elem: Borrow[Elem], s: Src[8] }, New[Elem] | nil do |elem, s|
  kids = elem.elements.to_a
  kids.empty? ? nil : s.pick(kids)
end

Stitch.endpoint :deep_clone, { elem: Borrow[Elem] }, New[Elem] do |elem|
  c = elem.deep_clone
  Stitch.check(write(c) == write(elem), "deep_clone writes the same")
  c
end

Stitch.endpoint :text_of, { elem: Borrow[Elem] } do |elem|
  elem.text
  elem.texts.each(&:value)
  elem.attributes.each_attribute(&:value)
end

Stitch.endpoint :pull, { text: Str[2048, XmlText] }, bails_on: PARSE_ERRORS do |text|
  pp = REXML::Parsers::PullParser.new(text)
  n = 0
  while pp.has_next? && n < 10_000
    pp.pull
    n += 1
  end
end

class Count
  include REXML::StreamListener
  attr_reader :starts, :ends

  def initialize
    @starts = @ends = 0
  end

  def tag_start(*) = (@starts += 1)
  def tag_end(*) = (@ends += 1)
end

Stitch.endpoint :stream, { text: Str[2048, XmlText] }, bails_on: PARSE_ERRORS do |text|
  l = Count.new
  REXML::Document.parse_stream(text, l)
  Stitch.check(l.starts == l.ends, "stream: every start tag has an end")
end

Stitch.endpoint :sax2, { text: Str[2048, XmlText] }, bails_on: PARSE_ERRORS do |text|
  p = REXML::Parsers::SAX2Parser.new(text)
  p.listen(:start_element) { |*| nil }
  p.listen(:characters) { |*| nil }
  p.parse
end
