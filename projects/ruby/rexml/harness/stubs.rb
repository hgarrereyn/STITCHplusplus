# rexml: DOM parsing (with parse options, from strings and IO) and serialization (default, pretty,
# transitive, encodings), the pull, stream, SAX2, light and ultra-light parsers, XPath (match/first/
# each, namespaces, variables), tree edits (elements, attributes, namespaces, text, CDATA, comments,
# processing instructions, sibling inserts, replace, delete), DOCTYPE/entity/notation inspection,
# XML declarations, Text normalization, and RELAX NG validation.
# Oracles: a written document parses again and writes the same text; XPath results belong to the
# document; edits keep the tree consistent (parents, indexes, sibling links, attribute lookups);
# Text.normalize/unnormalize round-trip; Elements indexing agrees with to_a; peeking a pull parser
# does not change the event sequence.
require "stitch"
require "stringio"
require "rexml/document"
require "rexml/parsers/pullparser"
require "rexml/parsers/sax2parser"
require "rexml/parsers/streamparser"
require "rexml/parsers/lightparser"
require "rexml/parsers/ultralightparser"
require "rexml/sax2listener"
require "rexml/streamlistener"
require "rexml/formatters/transitive"
require "rexml/validation/relaxng"
include Stitch::DSL

Doc = Stitch.object("Doc")
Elem = Stitch.object("Elem")
Node = Stitch.object("Node")        # any child node: Text, CData, Comment, Instruction, Element
Pull = Stitch.object("Pull")        # a PullParser part way through its input
XmlText = Stitch.datatype("XmlText")
XPathExpr = Stitch.datatype("XPathExpr")
RngText = Stitch.datatype("RngText")
RngDoc = Stitch.datatype("RngDoc")

PARSE_ERRORS = [REXML::ParseException, REXML::UndefinedNamespaceException].freeze
NAMES = %w[a b c d root item x:y ns:a xmlns e1 E _a a-b a.b].freeze
# names for elements built through the API: a prefix without a namespace declaration is the
# caller's mistake (REXML writes it, then refuses to read it back)
PLAIN = %w[a b c d root item e1 E _a a-b a.b].freeze
ATTR_NAMES = (PLAIN + ["xmlns"]).freeze
URIS = ["urn:a", "http://example.com/ns", "urn:x:y", "u"].freeze

# REXML only checks the characters of raw text: other text, attribute values, comments, CDATA and
# processing instructions are written as given, so the caller must pass characters XML allows.
ILLEGAL = /[^\u0009\u000A\u000D -퟿-�\u{10000}-\u{10FFFF}]/
def legal(s) = s.scrub("").gsub(ILLEGAL, "")

Stitch.decoder(XmlText) do |s|
  out = +""
  out << s.pick(['<?xml version="1.0"?>', '<?xml version="1.0" encoding="UTF-8"?>',
                 '<?xml version="1.0" standalone="yes"?>', "<?xml version='1.1' encoding='ISO-8859-1'?>"]) if s.coin(0.3)
  if s.coin(0.35)
    decl = +""
    (0...s.range(0, 3)).each do |i|
      decl << if s.coin(0.75)
                %(<!ENTITY e#{i} "#{s.coin(0.5) ? "&e#{[i - 1, 0].max};&e#{[i - 1, 0].max};" : s.str(8)}">)
              else
                s.pick([%(<!ENTITY x#{i} SYSTEM "x.xml">), %(<!ENTITY p#{i} PUBLIC "-//P//EN" "p.xml">),
                        %(<!ENTITY % pe "#{s.str(6)}">), %(<!ENTITY n#{i} SYSTEM "n.gif" NDATA gif>)])
              end
    end
    decl << '<!ATTLIST r a CDATA "d" b (x|y) #IMPLIED>' if s.coin(0.3)
    decl << '<!ATTLIST r xmlns:ns CDATA #FIXED "urn:a">' if s.coin(0.15)
    decl << s.pick(['<!ELEMENT r ANY>', '<!ELEMENT r (a|b)*>', '<!ELEMENT a (#PCDATA)>']) if s.coin(0.2)
    decl << s.pick(['<!NOTATION gif SYSTEM "image/gif">', '<!NOTATION n PUBLIC "-//N//EN">']) if s.coin(0.2)
    decl << "<!--#{s.str(6)}-->" if s.coin(0.1)
    ext = s.coin(0.3) ? s.pick([' SYSTEM "r.dtd"', ' PUBLIC "-//R//EN" "r.dtd"']) : ""
    out << "<!DOCTYPE r#{ext}#{s.coin(0.9) ? " [#{decl}]" : ''}>"
  end
  stack = []
  while s.more
    case s.choice(9)
    when 0, 1
      n = s.coin(0.7) ? s.pick(NAMES) : s.str(6)
      attrs = (0...s.range(0, 2)).map { %( #{s.coin(0.8) ? s.pick(NAMES) : s.token}="#{s.coin(0.7) ? s.str(10) : s.token}") }.join
      attrs << %( xmlns:#{s.pick(%w[ns x])}="#{s.pick(URIS)}") if s.coin(0.15)
      if s.coin(0.2)
        out << "<#{n}#{attrs}/>"
      else
        out << "<#{n}#{attrs}>"
        stack << n
      end
    when 2
      out << "</#{stack.pop || 'r'}>" if s.coin(0.9) || stack.empty?
    when 3 then out << s.str(16)
    when 4 then out << "&#{s.coin(0.5) ? "e#{s.range(0, 3)}" : s.pick(%w[amp lt gt quot apos #60 #x3c #1114112 x0 n0])};"
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
  out << s.pick(["/", "//", ".", "..", "", "/*/", "("])
  while s.more
    out << case s.choice(7)
           when 0 then s.pick(NAMES + %w[* node() text() comment() processing-instruction() @* @a ns:*])
           when 1 then "[#{s.pick(%w[1 2 last() position()=1 @a @a='x' text()='y' count(*)>1 contains(.,'a') not(b) $v=1 lang('en')])}]"
           when 2 then s.pick(%w[/ // :: | and or + - div mod = != < > ) (])
           when 3 then s.pick(%w[child:: descendant:: descendant-or-self:: parent:: ancestor:: ancestor-or-self:: following-sibling:: preceding-sibling:: following:: preceding:: attribute:: self::])
           when 4 then s.pick(%w[name() local-name() namespace-uri() string() concat('a',.) starts-with(.,'a') substring(.,2) substring-before(.,'a') substring-after(.,'a') string-length() normalize-space() translate(.,'a','b') boolean(.) number(.) sum(*) floor(1.5) ceiling(1.5) round(2.5) true() false() id('x') $v])
           when 5 then s.token
           else s.str(4)
           end
  end
  out
end

# Parent#[]=, #delete_at and #delete_if work like Array's: the child they drop keeps its parent
def attached?(n) = !n.parent.nil? && !n.parent.index(n).nil?

def ns_removed(elem)
  d = elem.document
  Stitch.set(d, "ns_removed", true) if d
end

# the pull, stream and SAX2 parsers report an unknown encoding name with ArgumentError (Document.new
# wraps it in a ParseException)
def bad_encoding
  yield
rescue ArgumentError => e
  raise unless e.backtrace_locations&.any? { |l| l.path.to_s.end_with?("rexml/encoding.rb") }
  Stitch.bail("bad encoding name")
end

# what a node holds, independent of how its attribute values happen to be escaped
def shape(n)
  case n
  when REXML::Element
    [n.expanded_name, n.attributes.to_a.map { |a| [a.expanded_name, a.value] }.sort, n.children.map { |c| shape(c) }]
  when REXML::CData then [:cdata, n.value]
  when REXML::Text then [:text, n.value]
  else n.to_s
  end
end

def empty_text?(n)
  n.is_a?(REXML::Parent) ? n.children.any? { |c| empty_text?(c) } : n.is_a?(REXML::Text) && n.to_s.empty?
end

def write(node)
  out = +""
  node.write(out)
  out
end

def fmt(node)
  out = +""
  REXML::Formatters::Default.new.write(node, out)
  out
end

# ---------------------------------------------------------------------------------------------
# Documents

Stitch.endpoint :parse, { text: Str[2048, XmlText] }, New[Doc], bails_on: PARSE_ERRORS do |text|
  REXML::Document.new(text)
end

# the context options of Document.new; roundtrip reparses with the same context
Stitch.endpoint :parse_with, { text: Str[2048, XmlText], compress: OneOf[nil, :all, ["a"]], ignore: OneOf[nil, :all, ["r"]],
                               respect: OneOf[nil, :all, ["b"]], raw: OneOf[nil, :all, ["a", "r"]], quote: Bool },
                New[Doc], bails_on: PARSE_ERRORS do |text, compress, ignore, respect, raw, quote|
  ctx = {}
  ctx[:compress_whitespace] = compress if compress
  ctx[:ignore_whitespace_nodes] = ignore if ignore
  ctx[:respect_whitespace] = respect if respect
  ctx[:raw] = raw if raw
  ctx[:attribute_quote] = :quote if quote
  d = REXML::Document.new(text, ctx)
  Stitch.set(d, "ctx", ctx)
  d
end

Stitch.endpoint :parse_io, { text: Bytes[2048, XmlText] }, New[Doc], bails_on: PARSE_ERRORS + [EncodingError] do |text|
  REXML::Document.new(StringIO.new(text))
end

Stitch.endpoint :new_doc, { root: OneOf["r", "a", "b"] }, New[Doc] do |root|
  d = REXML::Document.new
  d.add_element(root)
  d
end

# a written document parses again and writes the same text
Stitch.endpoint :roundtrip, { doc: Borrow[Doc] }, bails_on: [EncodingError] do |doc|
  ctx = Stitch.get(doc, "ctx", {})
  text = write(doc)
  again = begin
    REXML::Document.new(text, ctx)
  rescue *PARSE_ERRORS => e
    Stitch.log("written: #{text.inspect}")
    # (the harness removed a namespace declaration that a prefix still used: the caller's mistake)
    Stitch.bail("namespace declaration removed") if e.is_a?(REXML::UndefinedNamespaceException) && Stitch.get(doc, "ns_removed", false)
    Stitch.bail("root removed") if doc.root.nil? # (a document needs a root element)
    Stitch.check(false, "written document parses (#{e.class})")
  end
  Stitch.log("written: #{text.inspect}")
  # (an empty Text node the harness added writes <a></a>, which parses back without it)
  next if empty_text?(doc)
  Stitch.check(write(again) == text, "write/parse/write is stable")
end

Stitch.endpoint :pretty, { doc: Borrow[Doc], indent: Int[0, 4], compact: Bool, width: Int[0, 100] }, bails_on: [EncodingError] do |doc, indent, compact, width|
  out = +""
  f = REXML::Formatters::Pretty.new(indent)
  f.compact = compact
  f.width = width if width > 0
  f.write(doc, out)
end

Stitch.endpoint :transitive, { doc: Borrow[Doc], indent: Int[0, 4], ie_hack: Bool }, bails_on: [EncodingError] do |doc, indent, ie_hack|
  out = +""
  REXML::Formatters::Transitive.new(indent, ie_hack).write(doc, out)
end

# Document#write with options; characters the target encoding cannot represent raise EncodingError
Stitch.endpoint :write_doc, { doc: Borrow[Doc], indent: Int[-1, 3], transitive: Bool, ie_hack: Bool,
                              encoding: OneOf[nil, "UTF-8", "UTF-16", "ISO-8859-1", "Shift_JIS", "US-ASCII"] },
                bails_on: [EncodingError] do |doc, indent, transitive, ie_hack, encoding|
  out = +""
  doc.write(output: out, indent: indent, transitive: transitive, ie_hack: ie_hack, encoding: encoding)
  out
end

Stitch.endpoint :doc_info, { doc: Borrow[Doc] } do |doc|
  doc.version
  doc.encoding
  doc.stand_alone?
  doc.expanded_name
  doc.name
  doc.node_type
  doc.entity_expansion_count
  doc.entity_expansion_text_limit
  REXML::Document.entity_expansion_limit
  REXML::Document.entity_expansion_text_limit
  REXML::Security.entity_expansion_limit
  REXML::Security.entity_expansion_text_limit
  Stitch.check(doc.document.equal?(doc), "a document is its own document")
  x = doc.xml_decl
  x.to_s
  x.inspect
  x.writeencoding
  x.writethis
  x.node_type
  Stitch.check(x.clone == x, "a cloned XMLDecl is equal")
  c = doc.clone
  Stitch.check(c.children.empty?, "Document#clone copies no children")
  doc.each_recursive { |n| n.node_type }
  doc.find_first_recursive { |n| n.is_a?(REXML::Text) }
end

Stitch.endpoint :set_xml_decl, { doc: Borrow[Doc], version: OneOf["1.0", "1.1", nil], encoding: OneOf[nil, "UTF-8", "UTF-16", "ISO-8859-1"],
                                 standalone: OneOf[nil, "yes", "no"], write: Bool, default: Bool } do |doc, version, encoding, standalone, write, default|
  x = default ? REXML::XMLDecl.default : REXML::XMLDecl.new(version || "1.0", encoding, standalone)
  x.xmldecl(version, encoding, standalone) if version && !default
  write ? x.dowrite : x.nowrite
  doc.add(x)
  Stitch.check(doc.xml_decl.equal?(x), "the added XMLDecl is the document's")
  Stitch.check(doc.encoding == x.encoding, "Document#encoding is the declaration's")
end

Stitch.endpoint :doctype_info, { doc: Borrow[Doc], s: Src[8] }, bails_on: PARSE_ERRORS + [RuntimeError] do |doc, s|
  dt = doc.doctype
  Stitch.require(dt, "no doctype")
  dt.name
  dt.external_id
  dt.public
  dt.system
  dt.namespaces
  dt.node_type
  dt.context
  dt.inspect
  fmt(dt)
  dt.clone.to_s
  dt.notations.each do |n|
    n.to_s
    n.name
    n.public
    n.system
    Stitch.check(dt.notation(n.name).equal?(n), "DocType#notation finds each notation")
  end
  dt.attributes_of(doc.root&.name || "r").each(&:to_s)
  dt.attribute_of(doc.root&.name || "r", s.pick(%w[a b xmlns:ns]))
  dt.children.each { |c| c.to_s; fmt(c) }
  ents = dt.entities.keys.sort
  unless ents.empty?
    name = s.pick(ents)
    e = dt.entities[name]
    [e.name, e.external, e.ref, e.ndata, e.pubid, e.value].each(&:to_s)
    e.to_s
    e.normalized
    e.unnormalized
    out = +""
    e.write(out)
    dt.entity(name)
    REXML::Entity.matches?(e.to_s)
  end
end

# ParseException describes where parsing stopped
Stitch.endpoint :parse_error, { text: Str[2048, XmlText] } do |text|
  REXML::Document.new(text)
rescue REXML::ParseException => e
  e.to_s
  e.position
  e.line
  e.context
  Stitch.bail("parse error")
rescue REXML::UndefinedNamespaceException
  Stitch.bail("parse error")
end

Stitch.endpoint :root, { doc: Borrow[Doc] }, New[Elem] | nil do |doc|
  doc.root
end

# ---------------------------------------------------------------------------------------------
# XPath

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

# namespaces and variables; XPath.each visits what XPath.match returns
Stitch.endpoint :xpath_vars, { elem: Borrow[Elem], expr: Str[64, XPathExpr], ns: OneOf[nil, { "ns" => "urn:a" }, { "x" => "urn:x:y", "ns" => "u" }],
                               v: OneOf["1", "a", 2, 1.5, true] },
                bails_on: [REXML::ParseException] do |elem, expr, ns, v|
  all = REXML::XPath.match(elem, expr, ns, { "v" => v })
  seen = []
  REXML::XPath.each(elem, expr, ns, { "v" => v }) { |x| seen << x }
  Stitch.check(seen.size == all.size, "XPath.each yields what XPath.match returns")
  REXML::XPath.first(elem, expr, ns, { "v" => v })
end

# ---------------------------------------------------------------------------------------------
# Elements

Stitch.endpoint :add_element, { elem: Borrow[Elem], name: OneOf[*PLAIN], attrs: OneOf[nil, { "a" => "1" }, { "b" => "<&>", "c" => "x" }] },
                New[Elem] do |elem, name, attrs|
  Stitch.require(!elem.is_a?(REXML::Document) || elem.root.nil?, "document has a root")
  e = elem.add_element(name, attrs)
  Stitch.check(e.parent.equal?(elem), "add_element sets the parent")
  attrs&.each { |k, v| Stitch.check(e.attributes[k] == v, "add_element sets the attributes") }
  e
end

# REXML reports characters that XML does not allow with RuntimeError ("Illegal character ...")
Stitch.endpoint :add_attribute, { elem: Borrow[Elem], name: OneOf[*ATTR_NAMES], value: Str[16] },
                bails_on: [RuntimeError, REXML::ParseException] do |elem, name, value|
  value = legal(value)
  elem.add_attribute(name, value)
  # (attribute values are normalized: entity references, CR)
  plain = !value.include?("&") && !value.include?("\r")
  Stitch.check(elem.attributes[name] == value || !plain, "attribute reads back")
end

Stitch.endpoint :add_attributes, { elem: Borrow[Elem], s: Src[64], as_array: Bool }, bails_on: [RuntimeError, REXML::ParseException] do |elem, s, as_array|
  pairs = (0...s.range(1, 3)).map { [s.pick(PLAIN), legal(s.str(8))] }.uniq(&:first)
  elem.add_attributes(as_array ? pairs : pairs.to_h)
  pairs.each do |k, v|
    Stitch.check(elem.attributes[k] == v || v.include?("&") || v.include?("\r"), "add_attributes reads back")
  end
end

Stitch.endpoint :set_attr, { elem: Borrow[Elem], name: OneOf[*ATTR_NAMES], value: Str[16], as_attr: Bool },
                bails_on: [RuntimeError] do |elem, name, value, as_attr|
  value = legal(value)
  if as_attr
    a = REXML::Attribute.new(name, value)
    elem.attributes.add(a)
    Stitch.check(elem.attributes.get_attribute(name).equal?(a), "Attributes#add stores the attribute")
    Stitch.check(a.element.equal?(elem), "Attributes#add sets the element")
  else
    elem.attributes[name] = value
  end
end

Stitch.endpoint :delete_attr, { elem: Borrow[Elem], s: Src[8], how: OneOf[:delete_attribute, :delete, :delete_obj, :delete_all, :set_nil, :remove] } do |elem, s, how|
  attrs = elem.attributes.to_a
  Stitch.require(!attrs.empty?, "no attributes")
  a = s.pick(attrs)
  name = a.expanded_name
  ns_removed(elem) if a.namespace_declaration?
  case how
  when :delete_attribute then elem.delete_attribute(name)
  when :delete then elem.attributes.delete(name)
  when :delete_obj then elem.attributes.delete(a)
  when :delete_all then elem.attributes.delete_all(name)
  when :set_nil then elem.attributes[name] = nil
  else a.remove
  end
  # (attributes with a DTD default stay)
  Stitch.check(elem.attributes.get_attribute(name).nil?, "a deleted attribute is gone") unless name.include?(":") || name == "xmlns" || elem.document&.doctype
end

Stitch.endpoint :attrs_info, { elem: Borrow[Elem] }, bails_on: [REXML::ParseException] do |elem|
  at = elem.attributes
  list = at.to_a
  Stitch.check(at.length == list.size && at.size == list.size, "Attributes#length counts to_a")
  n = 0
  at.each { |_name, _value| n += 1 }
  Stitch.check(n == list.size, "Attributes#each yields every attribute")
  at.each_attribute(&:value)
  at.prefixes
  at.namespaces
  elem.has_attributes?
  list.each do |a|
    a.prefix
    a.namespace
    a.to_string
    a.to_s
    a.xpath
    a.inspect
    a.doctype
    a.document
    a.node_type
    a.hash
    a.namespace_declaration?
    out = +""
    a.write(out)
    Stitch.check(a.clone == a, "a cloned attribute is equal")
    Stitch.check(at.get_attribute(a.expanded_name).equal?(a), "get_attribute finds each attribute") unless a.prefix.empty? && a.name == "xmlns"
    Stitch.check(elem.attribute(a.expanded_name).equal?(a), "Element#attribute finds each attribute") unless a.prefix.empty? && a.name == "xmlns"
    uri = a.namespace
    at.get_attribute_ns(uri, a.name) if uri
    elem.attribute(a.name, uri) if uri
    elem[a.expanded_name]
  end
end

Stitch.endpoint :normalized_attr, { elem: Borrow[Elem], s: Src[8], value: Str[16] }, bails_on: [RuntimeError, REXML::ParseException] do |elem, s, value|
  attrs = elem.attributes.to_a
  Stitch.require(!attrs.empty?, "no attributes")
  a = s.pick(attrs)
  value = legal(value)
  a.normalized = REXML::Text.normalize(value) # (normalized= takes escaped text)
  Stitch.check(a.value == value || value.include?("\r"), "normalized= reads back")
end

Stitch.endpoint :add_namespace, { elem: Borrow[Elem], prefix: OneOf[nil, "ns", "x", "q"], uri: OneOf[*URIS] } do |elem, prefix, uri|
  prefix.nil? ? elem.add_namespace(uri) : elem.add_namespace(prefix, uri)
  Stitch.check(elem.namespace(prefix) == uri, "add_namespace: namespace(prefix) finds the URI") if prefix || elem.prefix.empty?
  Stitch.check(elem.prefixes.include?(prefix), "add_namespace: prefixes lists the prefix") if prefix
end

Stitch.endpoint :delete_namespace, { elem: Borrow[Elem], prefix: OneOf["xmlns", "ns", "x", "q"] } do |elem, prefix|
  ns_removed(elem)
  elem.delete_namespace(prefix)
end

Stitch.endpoint :add_text, { elem: Borrow[Elem], text: Str[32], as_node: Bool }, bails_on: [RuntimeError] do |elem, text, as_node|
  text = legal(text)
  # (an element parsed with :raw holds escaped text; add_text appends to its last text node as is)
  last = elem.children.last
  text = REXML::Text.normalize(text) if elem.raw || (last.is_a?(REXML::Text) && last.raw)
  elem.add_text(as_node ? REXML::Text.new(text) : text)
end

Stitch.endpoint :set_text, { elem: Borrow[Elem], text: Str[32] }, bails_on: [RuntimeError, REXML::ParseException] do |elem, text|
  text = legal(text)
  elem.text = text
  plain = !text.include?("&") && !text.include?("\r")
  # (whitespace runs are squeezed unless the parse context respects whitespace)
  Stitch.check(elem.text == text || text.empty? || !plain || !elem.whitespace, "text reads back")
end

# a Text node; raw text must already be escaped (RuntimeError otherwise)
Stitch.endpoint :text_node, { elem: Borrow[Elem], text: Str[32], respect: Bool, raw: Bool }, New[Node], bails_on: [RuntimeError] do |elem, text, respect, raw|
  t = REXML::Text.new(legal(text), respect, nil, raw)
  elem.add(t)
  Stitch.check(t.parent.equal?(elem), "add sets the parent")
  t
end

Stitch.endpoint :cdata, { elem: Borrow[Elem], text: Str[32] }, New[Node] do |elem, text|
  text = legal(text)
  Stitch.require(!text.include?("]]>"), "CDATA end in CDATA") # (written as is)
  elem.add(REXML::CData.new(text))
end

Stitch.endpoint :comment, { elem: Borrow[Elem], text: Str[16] }, New[Node] do |elem, text|
  text = legal(text)
  Stitch.require(!text.include?("--") && !text.end_with?("-"), "-- in a comment") # (written as is)
  elem.add(REXML::Comment.new(text))
end

Stitch.endpoint :instruction, { elem: Borrow[Elem], target: OneOf["pi", "xml-stylesheet", "php"], content: Str[16] }, New[Node] do |elem, target, content|
  content = legal(content)
  Stitch.require(!content.include?("?>"), "?> in an instruction") # (written as is)
  pi = REXML::Instruction.new(target, content.empty? ? nil : content)
  elem.add(pi)
end

Stitch.endpoint :delete_child, { elem: Borrow[Elem], s: Src[8], by_xpath: Bool } do |elem, s, by_xpath|
  kids = elem.elements.to_a
  Stitch.require(!kids.empty?, "no children")
  k = s.pick(kids)
  if by_xpath
    elem.elements.delete(k)
  else
    elem.delete_element(k)
  end
  Stitch.check(k.parent.nil?, "a deleted element has no parent")
end

Stitch.endpoint :delete_all, { elem: Borrow[Elem], expr: Str[64, XPathExpr] }, bails_on: [REXML::ParseException] do |elem, expr|
  gone = elem.elements.delete_all(expr)
  gone.each { |g| Stitch.check(g.parent.nil?, "delete_all detaches what it returns") }
end

Stitch.endpoint :children, { elem: Borrow[Elem], s: Src[8] }, New[Elem] | nil do |elem, s|
  kids = elem.elements.to_a
  kids.empty? ? nil : s.pick(kids)
end

# Elements: 1-based indexes, by name, and the enumerators agree with to_a
Stitch.endpoint :elements_index, { elem: Borrow[Elem], s: Src[8], name: OneOf[nil, *PLAIN] }, New[Elem] | nil do |elem, s, name|
  els = elem.elements
  kids = els.to_a
  Stitch.check(els.size == kids.size, "Elements#size counts to_a")
  Stitch.check(els.empty? == kids.empty?, "Elements#empty? agrees with to_a")
  Stitch.check(els.collect { |e| e }.size == kids.size, "Elements#collect visits every element")
  Stitch.check(els.inject(nil, 0) { |n, _| n + 1 } == kids.size, "Elements#inject visits every element")
  n = 0
  els.each { n += 1 }
  Stitch.check(n == kids.size, "Elements#each visits every element")
  Stitch.check(els.parent.equal?(elem), "Elements#parent")
  Stitch.check(elem.has_elements? == !kids.empty?, "has_elements? agrees with to_a")
  next nil if kids.empty?
  i = s.range(0, kids.size - 1)
  Stitch.check(els[i + 1].equal?(kids[i]), "Elements#[i] is the i-th element")
  Stitch.check(els.index(kids[i]) == i + 1, "Elements#index finds the element")
  if name
    named = kids.select { |k| k.name == name }
    Stitch.check(els[1, name].equal?(named.first), "Elements#[1, name] is the first with that name")
  end
  kids[i]
end

Stitch.endpoint :elements_set, { elem: Borrow[Elem], s: Src[8], name: OneOf[*PLAIN] }, New[Elem] do |elem, s, name|
  Stitch.require(!elem.is_a?(REXML::Document), "a document has one root")
  n = elem.elements.size
  i = s.range(1, n + 1)
  e = REXML::Element.new(name, nil, elem.context)
  elem.elements[i] = e
  Stitch.check(e.parent.equal?(elem), "Elements#[]= sets the parent")
  Stitch.check(elem.elements.index(e) == [i, n + 1].min, "Elements#[]= puts the element at the index")
  e
end

Stitch.endpoint :find_elements, { elem: Borrow[Elem], how: OneOf[:with_attribute, :with_text, :each_element, :get_elements, :to_a, :xpath_index],
                                  key: OneOf["a", "b", "xmlns", "id"], value: OneOf[nil, "1", "x", ""], max: Int[0, 3],
                                  name: OneOf[nil, *PLAIN], expr: Str[32, XPathExpr] },
                New[Elem] | nil, bails_on: [REXML::ParseException] do |elem, how, key, value, max, name, expr|
  found = []
  case how
  when :with_attribute then elem.each_element_with_attribute(key, value, max, name) { |e| found << e }
  when :with_text then elem.each_element_with_text(value, max, name) { |e| found << e }
  when :each_element then elem.each_element(expr) { |e| found << e }
  when :get_elements then found = elem.get_elements(expr)
  when :to_a then found = elem.elements.to_a(expr)
  else found = [elem.elements[expr]].compact
  end
  if %i[with_attribute with_text].include?(how)
    Stitch.check(max.zero? || found.size <= max, "each_element_with_*: at most max elements")
    found.each { |e| Stitch.check(e.parent.equal?(elem), "each_element_with_*: children only") }
    found.each { |e| Stitch.check(name.nil? || e.name == name, "each_element_with_*: matches the name") }
  end
  found.grep(REXML::Element).reject { |e| e.is_a?(REXML::Document) }.first
end

Stitch.endpoint :deep_clone, { elem: Borrow[Elem] }, New[Elem], bails_on: [REXML::ParseException] do |elem|
  c = elem.deep_clone
  # (a detached clone has no DOCTYPE, whose entities and attribute defaults change the values)
  Stitch.check(shape(c) == shape(elem) || elem.document&.doctype, "deep_clone copies the element")
  c
end

Stitch.endpoint :clone_into, { elem: Borrow[Elem], other: Borrow[Elem] } do |elem, other|
  Stitch.require(!other.is_a?(REXML::Document), "a document has one root")
  Stitch.require(elem.context == other.context, "different parse contexts")
  c = elem.deep_clone
  Stitch.require(!c.is_a?(REXML::Document), "documents are not moved")
  ns_removed(other) # (the clone leaves its namespace declarations behind)
  other.add_element(c)
  Stitch.check(c.parent.equal?(other), "add_element(element) sets the parent")
end

Stitch.endpoint :text_of, { elem: Borrow[Elem] }, bails_on: [REXML::ParseException] do |elem|
  elem.text
  elem.get_text
  elem.texts.each(&:value)
  elem.attributes.each_attribute(&:value)
end

Stitch.endpoint :elem_info, { elem: Borrow[Elem], name: OneOf[*NAMES] } do |elem, name|
  elem.has_text?
  elem.cdatas.each(&:value)
  elem.comments.each(&:string)
  elem.instructions.each(&:content)
  elem.namespaces
  elem.prefixes
  elem.namespace
  elem.prefix
  elem.expanded_name
  elem.fully_expanded_name
  elem.has_name?(name)
  elem.has_name?(name, "urn:a")
  elem.inspect
  elem.to_s
  elem.root
  elem.root_node
  elem.document
  elem.node_type
  elem.whitespace
  elem.ignore_whitespace_nodes
  elem.raw
  elem.context
  elem.parent?
  elem.size
  elem.bytes
  elem.each_index { |i| elem[i] }
  elem.each_recursive(&:node_type)
  elem.find_first_recursive { |n| n.is_a?(REXML::Comment) }
  elem.xpath unless elem.is_a?(REXML::Document)
  if attached?(elem)
    nx = elem.next_element
    pv = elem.previous_element
    Stitch.check(nx.previous_element.equal?(elem), "next_element/previous_element are inverse") if nx
    Stitch.check(pv.next_element.equal?(elem), "previous_element/next_element are inverse") if pv
    i = elem.index_in_parent
    Stitch.check(elem.parent[i - 1].equal?(elem), "index_in_parent is 1-based")
    ns = elem.next_sibling_node
    ps = elem.previous_sibling_node
    Stitch.check(ns.nil? || ns.previous_sibling_node.equal?(elem), "sibling links are inverse")
    # (Document#xml_decl puts a default XMLDecl first without setting its parent)
    Stitch.check(ps.nil? || ps.is_a?(REXML::XMLDecl) || ps.next_sibling_node.equal?(elem), "sibling links are inverse")
  end
end

# ---------------------------------------------------------------------------------------------
# Child nodes of any kind

Stitch.endpoint :child_node, { elem: Borrow[Elem], s: Src[8] }, New[Node] | nil do |elem, s|
  kids = elem.children.reject { |c| c.is_a?(REXML::DocType) || c.is_a?(REXML::XMLDecl) }
  kids.empty? ? nil : s.pick(kids)
end

# Text#value and Attribute#value expand the entity references in the stored string: one the
# harness wrote as plain text ("&#0;") raises ParseException there
Stitch.endpoint :node_info, { node: Borrow[Node] }, bails_on: [REXML::ParseException] do |node|
  node.node_type
  node.to_s
  node.inspect
  node.document
  node.parent?
  fmt(node)
  c = node.is_a?(REXML::Parent) ? node.deep_clone : node.clone
  Stitch.check(shape(c) == shape(node) || node.document&.doctype, "a clone holds the same")
  case node
  when REXML::CData
    node.value
    Stitch.check(node.to_s == node.value, "CData#to_s is the value")
  when REXML::Text
    node.value
    node.empty?
    node.raw
    node.doctype
    node.xpath if node.parent
    Stitch.check((node <=> c).zero?, "a cloned Text compares equal")
  when REXML::Comment
    Stitch.check(node == c && (node <=> c).zero?, "a cloned Comment is equal")
  when REXML::Instruction
    node.target
    node.content
    Stitch.check(node == c, "a cloned Instruction is equal")
  end
  if attached?(node)
    i = node.index_in_parent
    Stitch.check(node.parent[i - 1].equal?(node), "index_in_parent is 1-based")
    ns = node.next_sibling
    ps = node.previous_sibling
    Stitch.check(ns.nil? || ns.previous_sibling_node.equal?(node), "sibling links are inverse")
    # (Document#xml_decl puts a default XMLDecl first without setting its parent)
    Stitch.check(ps.nil? || ps.is_a?(REXML::XMLDecl) || ps.next_sibling_node.equal?(node), "sibling links are inverse")
  end
end

Stitch.endpoint :text_edit, { node: Borrow[Node], text: Str[32], how: OneOf[:value=, :<<] }, bails_on: [RuntimeError, REXML::ParseException] do |node, text, how|
  Stitch.require(node.is_a?(REXML::Text) && !node.is_a?(REXML::CData), "not a text node")
  text = legal(text)
  before = node.value
  if how == :value=
    node.value = text
    Stitch.check(node.value == text || text.include?("\r") || text.include?("&"), "Text#value= reads back") unless node.raw
  else
    node << (node.raw ? REXML::Text.normalize(text) : text) # (raw text is appended as is: it must be escaped)
    Stitch.check(node.value.start_with?(before) || before.include?("\r"), "Text#<< appends") unless node.raw
  end
end

Stitch.endpoint :node_remove, { node: Borrow[Node] } do |node|
  Stitch.require(attached?(node), "detached")
  parent = node.parent
  node.remove
  Stitch.check(node.parent.nil?, "a removed node has no parent")
  Stitch.check(parent.index(node).nil?, "a removed node is not a child")
end

def fresh(kind, s, context)
  case kind
  when :elem then REXML::Element.new(s.pick(PLAIN), nil, context)
  when :text then REXML::Text.new(legal(s.str(8)))
  when :comment then REXML::Comment.new(s.pick(%w[c note x]))
  else REXML::Instruction.new("pi", s.pick(%w[a b]))
  end
end

# insert a new node next to an existing one, or replace it
Stitch.endpoint :node_insert, { node: Borrow[Node], how: OneOf[:before, :after, :next_sibling=, :previous_sibling=, :replace_with, :replace_child],
                                kind: OneOf[:elem, :text, :comment, :pi], s: Src[16] }, New[Node] do |node, how, kind, s|
  parent = node.parent
  Stitch.require(attached?(node), "detached")
  Stitch.require(!parent.is_a?(REXML::Document), "children of a document are not edited") # (a second root)
  n = fresh(kind, s, parent.context)
  case how
  when :before then parent.insert_before(node, n)
  when :after then parent.insert_after(node, n)
  when :next_sibling= then node.next_sibling = n
  when :previous_sibling= then node.previous_sibling = n
  when :replace_with then node.replace_with(n)
  else parent.replace_child(node, n)
  end
  Stitch.check(n.parent.equal?(parent), "the inserted node has the parent")
  i = parent.index(n)
  Stitch.check(!i.nil?, "the inserted node is a child")
  case how
  when :before, :previous_sibling= then Stitch.check(parent.index(node) == i + 1, "inserted before")
  when :after, :next_sibling= then Stitch.check(parent.index(node) == i - 1, "inserted after")
  when :replace_with, :replace_child then Stitch.check(parent.index(node).nil? && node.parent.nil?, "the replaced node is detached")
  end
  n
end

Stitch.endpoint :parent_edit, { elem: Borrow[Elem], how: OneOf[:unshift, :delete_at, :delete_if, :each_index, :set_index], kind: OneOf[:elem, :text, :comment, :pi], s: Src[16] } do |elem, how, kind, s|
  Stitch.require(!elem.is_a?(REXML::Document), "children of a document are not edited")
  case how
  when :unshift
    n = fresh(kind, s, elem.context)
    elem.unshift(n)
    Stitch.check(elem[0].equal?(n) && n.parent.equal?(elem), "unshift puts the node first")
  when :delete_at
    Stitch.require(elem.size > 0, "no children")
    size = elem.size
    elem.delete_at(s.range(0, size - 1))
    Stitch.check(elem.size == size - 1, "delete_at removes one child")
  when :delete_if
    t = s.pick(%i[text comment element])
    elem.delete_if { |c| c.node_type == t }
    Stitch.check(elem.children.none? { |c| c.node_type == t }, "delete_if removes the matching children")
  when :set_index
    Stitch.require(elem.size > 0, "no children")
    n = fresh(kind, s, elem.context)
    i = s.range(0, elem.size - 1)
    elem[i] = n
    Stitch.check(elem[i].equal?(n) && n.parent.equal?(elem), "[]= puts the node at the index")
  else
    n = 0
    elem.each_index { n += 1 }
    Stitch.check(n == elem.size, "each_index visits every child")
  end
end

# ---------------------------------------------------------------------------------------------
# Text normalization

# normalize escapes, unnormalize reverses it (line ends are normalized to LF)
Stitch.endpoint :text_normalize, { text: Str[64], filter: OneOf[nil, ["amp"], ["lt", "gt"]] } do |text, filter|
  n = REXML::Text.normalize(text, nil, filter)
  u = REXML::Text.unnormalize(n, nil, filter)
  Stitch.check(u == text, "unnormalize(normalize(s)) == s") unless text.include?("\r") || filter
end

Stitch.endpoint :text_unnormalize, { text: Str[64, XmlText], doc: Borrow[Doc] }, bails_on: [REXML::ParseException, RuntimeError] do |text, doc|
  REXML::Text.unnormalize(text, doc.doctype)
  REXML::Text.read_with_substitution(text) if text.valid_encoding?
end

# Text.check raises RuntimeError for characters raw text may not hold
Stitch.endpoint :text_check, { text: Str[64, XmlText] }, bails_on: [RuntimeError] do |text|
  REXML::Text.check(text, REXML::Text::NEEDS_A_SECOND_CHECK)
end

Stitch.endpoint :text_layout, { text: Str[64], width: Int[1, 20], level: Int[0, 3], first: Bool } do |text, width, level, first|
  t = REXML::Text.new("")
  t.wrap(text, width, first)
  t.indent_text(text, level, " ", first)
  out = +""
  t.write_with_substitution(out, text)
  out
end

# ---------------------------------------------------------------------------------------------
# Pull, stream, SAX2 and light parsers

Stitch.endpoint :pull, { text: Str[2048, XmlText] }, bails_on: PARSE_ERRORS do |text|
  bad_encoding do
    pp = REXML::Parsers::PullParser.new(text)
    n = 0
    while pp.has_next? && n < 10_000
      pp.pull
      n += 1
    end
  end
end

def event_info(ev)
  ev.event_type
  ev.inspect
  ev[0]
  ev[0, 2]
  ev[0..1]
  %i[start_element? end_element? text? instruction? comment? doctype? attlistdecl? elementdecl?
     entitydecl? notationdecl? entity? cdata? xmldecl? error?].count { |m| ev.send(m) }
end

Stitch.endpoint :pull_new, { text: Str[2048, XmlText] }, New[Pull] do |text|
  pp = REXML::Parsers::PullParser.new(text)
  pp
end

Stitch.endpoint :pull_next, { pp: Borrow[Pull], n: Int[1, 16] }, bails_on: PARSE_ERRORS do |pp, n|
  bad_encoding do
    n.times do
      break unless pp.has_next?
      ev = pp.pull
      event_info(ev)
    end
    pp.entity_expansion_count
    pp.empty?
  end
end

# peeking ahead does not change the events pull returns
Stitch.endpoint :pull_peek, { pp: Borrow[Pull], depth: Int[0, 4] }, bails_on: PARSE_ERRORS do |pp, depth|
  bad_encoding do
    Stitch.require(pp.has_next?, "at the end")
    peeked = (0..depth).map { |d| pp.peek(d) }
    peeked.each { |e| e && event_info(e) }
    got = peeked.map { pp.pull }
    Stitch.check(got.map(&:event_type) == peeked.map(&:event_type), "pull returns the peeked events")
  end
end

Stitch.endpoint :pull_unshift, { pp: Borrow[Pull] }, bails_on: PARSE_ERRORS do |pp|
  bad_encoding do
    Stitch.require(pp.has_next?, "at the end")
    ev = pp.pull
    pp.unshift(ev)
    Stitch.check(pp.pull.equal?(ev), "pull returns the unshifted event")
  end
end

Stitch.endpoint :pull_reset, { pp: Borrow[Pull] } do |pp|
  pp.reset
end

# exceeding the entity expansion limits raises RuntimeError
Stitch.endpoint :pull_each, { pp: Borrow[Pull], limit: Int[0, 8] }, bails_on: PARSE_ERRORS + [RuntimeError] do |pp, limit|
  bad_encoding do
    pp.entity_expansion_limit = limit * 2 if limit > 0
    pp.entity_expansion_text_limit = limit * 64 if limit > 0
    n = 0
    pp.each do |ev|
      event_info(ev)
      n += 1
      break if n > 10_000
    end
  end
end

class Count
  include REXML::StreamListener
  attr_reader :starts, :ends

  def initialize
    @starts = @ends = 0
  end

  def tag_start(*args)
    super
    @starts += 1
  end

  def tag_end(*args)
    super
    @ends += 1
  end
end

Stitch.endpoint :stream, { text: Str[2048, XmlText] }, bails_on: PARSE_ERRORS do |text|
  bad_encoding do
    l = Count.new
    REXML::Document.parse_stream(text, l)
    Stitch.check(l.starts == l.ends, "stream: every start tag has an end")
  end
end

Stitch.endpoint :stream_parser, { text: Str[2048, XmlText], limit: Int[0, 8] }, bails_on: PARSE_ERRORS + [RuntimeError] do |text, limit|
  bad_encoding do
    l = Count.new
    p = REXML::Parsers::StreamParser.new(text, l)
    p.entity_expansion_limit = limit * 2 if limit > 0
    p.entity_expansion_text_limit = limit * 64 if limit > 0
    p.parse
    p.entity_expansion_count
    Stitch.check(l.starts == l.ends, "stream: every start tag has an end")
  end
end

class Sax2Count
  include REXML::SAX2Listener
  attr_reader :starts, :ends

  def initialize
    @starts = @ends = 0
  end

  def start_element(*args)
    super
    @starts += 1
  end

  def end_element(*args)
    super
    @ends += 1
  end
end

SAX_EVENTS = %i[start_document end_document start_prefix_mapping end_prefix_mapping start_element end_element
                characters processing_instruction doctype attlistdecl elementdecl entitydecl notationdecl cdata
                xmldecl comment progress].freeze

Stitch.endpoint :sax2, { text: Str[2048, XmlText], filter: OneOf[nil, ["a"], [/^x/, "root"]], with_listener: Bool, deafen: Bool },
                bails_on: PARSE_ERRORS do |text, filter, with_listener, deafen|
  bad_encoding do
    p = REXML::Parsers::SAX2Parser.new(text)
    starts = ends = 0
    SAX_EVENTS.each { |ev| p.listen(ev) { |*| nil } }
    p.listen(:start_element) { |*| starts += 1 }
    p.listen(:end_element) { |*| ends += 1 }
    p.listen(:start_element, filter) { |*| nil } if filter
    p.listen(filter) { |*| nil } if filter
    l = Sax2Count.new
    p.listen(l) if with_listener
    p.deafen(l) if with_listener && deafen
    p.source
    p.parse
    p.entity_expansion_count
    Stitch.check(starts == ends, "sax2: every start element has an end")
    Stitch.check(l.starts == l.ends, "sax2 listener: every start element has an end")
  end
end

Stitch.endpoint :light, { text: Str[2048, XmlText], ultra: Bool, again: Bool }, bails_on: PARSE_ERRORS do |text, ultra, again|
  bad_encoding do
    io = StringIO.new(text)
    p = ultra ? REXML::Parsers::UltraLightParser.new(io) : REXML::Parsers::LightParser.new(io)
    first = p.parse
    if again
      p.rewind
      second = p.parse
      Stitch.check(second.size == first.size, "a rewound parser returns the same tree") unless ultra
    end
  end
end

# ---------------------------------------------------------------------------------------------
# RELAX NG

Stitch.endpoint :relaxng, { schema: Str[2048, RngText], doc: Str[512, RngDoc] },
                bails_on: PARSE_ERRORS + [REXML::Validation::ValidationException] do |schema, doc|
  bad_encoding do
    v = REXML::Validation::RelaxNG.new(schema)
    p = REXML::Parsers::TreeParser.new(doc)
    p.add_listener(v.reset)
    p.parse
  end
end
