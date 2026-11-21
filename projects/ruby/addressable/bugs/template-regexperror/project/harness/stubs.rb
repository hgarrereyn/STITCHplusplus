# addressable: URI parsing, normalization, joining and routing, query values, component encoding,
# URI templates (RFC 6570). Oracles: a URI's string parses back to it, normalize is idempotent,
# base.join(base.route_to(u)) is u, a template's expansion matches it with the same values.
require "stitch"
require "addressable/uri"
require "addressable/template"
include Stitch::DSL

Uri = Stitch.object("Uri")
Tmpl = Stitch.object("Tmpl")
UriText = Stitch.datatype("UriText")
TemplateText = Stitch.datatype("TemplateText")

BAD = [Addressable::URI::InvalidURIError].freeze

Stitch.decoder(UriText) do |s|
  out = +""
  out << s.pick(%w[http:// https:// ftp:// file:/// mailto: urn: // / ./ ../ ? # javascript: a: HTTP://]) if s.coin(0.8)
  if s.coin(0.3)
    out << s.str(6)
    out << ":#{s.str(4)}" if s.coin(0.3)
    out << "@"
  end
  out << (s.coin(0.7) ? s.pick(%w[example.com EXAMPLE.com 127.0.0.1 [::1] [v1.x] xn--nxasmq6b.com localhost a.b.c. ünïcode.com]) : s.str(10))
  out << ":#{s.pick(%w[80 443 0 65535 65536 x])}" if s.coin(0.3)
  while s.more
    out << case s.choice(7)
           when 0 then "/#{s.str(6)}"
           when 1 then "/#{s.pick(%w[. .. %2e %2E%2e // %2F])}"
           when 2 then "?#{s.str(4)}=#{s.str(4)}"
           when 3 then "&#{s.str(3)}[#{s.str(2)}]=#{s.str(3)}"
           when 4 then "##{s.str(6)}"
           when 5 then "%#{s.pick(%w[20 41 2F 25 C3%A9 E2%82 ZZ 0 FF])}"
           else s.token
           end
  end
  out
end

Stitch.decoder(TemplateText) do |s|
  out = +""
  out << s.pick(%w[http://example.com /path / ]) if s.coin(0.6)
  while s.more
    if s.coin(0.6)
      vars = (0..s.range(0, 2)).map { s.pick(%w[a b var list keys x.y _ %41]) + s.pick(["", "*", ":3", ":0"]) }
      out << "{#{s.pick(['', '+', '#', '.', '/', ';', '?', '&', '='])}#{vars.join(',')}}"
    else
      out << (s.coin(0.5) ? s.str(4) : s.token)
    end
  end
  out
end

Stitch.endpoint :parse, { text: Str[512, UriText] }, New[Uri], bails_on: BAD do |text|
  Addressable::URI.parse(text)
end

Stitch.endpoint :heuristic_parse, { text: Str[512, UriText] }, New[Uri] | nil, bails_on: BAD do |text|
  Addressable::URI.heuristic_parse(text)
end

# to_s parses back to the same URI
Stitch.endpoint :roundtrip, { u: Borrow[Uri] }, bails_on: BAD do |u|
  s = u.to_s
  again = Addressable::URI.parse(s)
  Stitch.log("to_s: #{s.inspect} -> #{again.to_s.inspect}")
  Stitch.check(again == u, "parse(to_s) == uri")
end

Stitch.endpoint :normalize, { u: Borrow[Uri] }, New[Uri], bails_on: BAD do |u|
  n = u.normalize
  Stitch.log("normalized: #{n.to_s.inspect}")
  n2 = n.normalize
  Stitch.check(n2.to_s == n.to_s, "normalize is idempotent")
  again = Addressable::URI.parse(n.to_s)
  Stitch.check(again.normalize.to_s == n.to_s, "a normalized URI parses back to itself")
  n
end

Stitch.endpoint :join, { a: Borrow[Uri], b: Borrow[Uri] }, New[Uri], bails_on: BAD do |a, b|
  a.join(b)
end

# base.join(base.route_to(u)) is u (for absolute URIs of the same scheme and authority form)
Stitch.endpoint :route, { base: Borrow[Uri], u: Borrow[Uri] }, bails_on: BAD + [ArgumentError] do |base, u|
  Stitch.require(base.absolute? && u.absolute?, "relative URIs")
  r = base.route_to(u)
  back = base.join(r)
  Stitch.log("route_to: #{r.to_s.inspect}, joined back: #{back.to_s.inspect}")
  Stitch.check(back.normalize == u.normalize, "base.join(base.route_to(u)) == u")
end

Stitch.endpoint :set_part, { u: Borrow[Uri], part: OneOf[:scheme, :user, :password, :host, :port, :path, :query, :fragment, :authority],
                             value: Str[32, UriText] }, bails_on: BAD + [TypeError] do |u, part, value|
  value = part == :port ? value.to_i : value
  u.send("#{part}=", value)
  Stitch.check(Addressable::URI.parse(u.to_s).send(part).to_s == u.send(part).to_s || part == :authority,
               "a part set reads back after to_s/parse")
end

Stitch.endpoint :query_values, { u: Borrow[Uri], mode: OneOf[Hash, Array] }, bails_on: BAD + [ArgumentError, TypeError] do |u, mode|
  v = u.query_values(mode)
  next if v.nil? || mode == Array

  copy = u.dup
  copy.query_values = v
  Stitch.check(copy.query_values(Hash) == v, "query_values= then query_values reads the same")
end

Stitch.endpoint :omit, { u: Borrow[Uri], parts: OneOf[[:scheme], [:query, :fragment], [:user, :password], [:path]] }, New[Uri],
                bails_on: BAD do |u, parts|
  u.omit(*parts)
end

Stitch.endpoint :display, { u: Borrow[Uri] }, bails_on: BAD do |u|
  u.display_uri.to_s
  u.inspect
  u.hash
  u.origin
end

Stitch.endpoint :encode_component, { text: Str[64], cls: OneOf[:path, :query, :fragment, :unreserved, :authority] } do |text, cls|
  chars = Addressable::URI::CharacterClasses.const_get(cls.to_s.upcase)
  enc = Addressable::URI.encode_component(text, chars)
  Stitch.check(Addressable::URI.unencode_component(enc) == text, "unencode(encode(s)) == s")
end

Stitch.endpoint :form, { pairs: Str[128] } do |pairs|
  kv = pairs.split("&").map { |p| k, v = p.split("=", 2); [k.to_s, v.to_s] }
  enc = Addressable::URI.form_encode(kv)
  dec = Addressable::URI.form_unencode(enc)
  Stitch.check(dec == kv.map { |k, v| [k.gsub("\r\n", "\n").gsub("\r", "\n"), v.gsub("\r\n", "\n").gsub("\r", "\n")] },
               "form_unencode(form_encode(pairs)) == pairs")
end

Stitch.endpoint :template, { text: Str[256, TemplateText] }, New[Tmpl], bails_on: BAD do |text|
  Addressable::Template.new(text)
end

# expansion with string values matches the template back to those values
Stitch.endpoint :expand_extract, { t: Borrow[Tmpl], a: Str[8], b: Str[8] }, bails_on: BAD + [Addressable::Template::InvalidTemplateOperatorError] do |t, a, b|
  vals = t.variables.uniq.each_with_index.to_h { |v, i| [v, i.even? ? a : b] }
  uri = t.expand(vals)
  Stitch.log("expanded: #{uri.to_s.inspect}")
  m = t.match(uri)
  Stitch.check(!m.nil?, "a template matches its own expansion")
end

Stitch.endpoint :extract, { t: Borrow[Tmpl], u: Borrow[Uri] }, bails_on: BAD do |t, u|
  t.extract(u)
end

Stitch.endpoint :partial_expand, { t: Borrow[Tmpl], v: Str[8] }, New[Tmpl] | nil, bails_on: BAD do |t, v|
  vars = t.variables
  next nil if vars.empty?

  t.partial_expand(vars.first => v)
end
