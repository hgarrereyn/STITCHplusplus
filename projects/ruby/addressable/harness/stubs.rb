# addressable: URI parsing (parse, heuristic_parse, new from components, YAML), component getters
# and their normalized forms, setters (scheme to fragment, userinfo, hostname, tld, authority,
# origin, site, request_uri, query values), normalization, joining, merging, routing and omitting
# (copying and in place), class-level encoding (encode, normalized_encode, unencode, escape,
# encode/normalize_component, normalize_path, convert_path, form encoding), IDNA, and URI
# templates (RFC 6570): expansion, partial expansion, matching, extraction, MatchData.
# Oracles: a URI's string parses back to it, normalize is idempotent, base.join(base.route_to(u))
# is u, a component set reads back, encode/unencode and form_encode/form_unencode round-trip,
# YAML round-trips, merge keeps what it is not given, a template's expansion matches it with the
# same values, MatchData agrees with its mapping.
require "stitch"
require "yaml"
require "addressable/uri"
require "addressable/template"
require "addressable/idna"
include Stitch::DSL

Uri = Stitch.object("Uri")
Tmpl = Stitch.object("Tmpl")
Match = Stitch.object("Match")
UriText = Stitch.datatype("UriText")
TemplateText = Stitch.datatype("TemplateText")

BAD = [Addressable::URI::InvalidURIError].freeze
TBAD = [Addressable::URI::InvalidURIError, Addressable::Template::InvalidTemplateError,
        Addressable::Template::InvalidTemplateValueError].freeze
CC = Addressable::URI::CharacterClasses

Stitch.decoder(UriText) do |s|
  out = +""
  out << s.pick(%w[http:// https:// ftp:// file:/// mailto: urn: // / ./ ../ ? # javascript: a: HTTP:// ssh+svn:// feed:]) if s.coin(0.8)
  if s.coin(0.3)
    out << s.str(6)
    out << ":#{s.str(4)}" if s.coin(0.3)
    out << "@"
  end
  out << (s.coin(0.7) ? s.pick(%w[example.com EXAMPLE.com www.example.co.uk 127.0.0.1 [::1] [v1.x] xn--nxasmq6b.com localhost a.b.c. ünïcode.com bücher.de]) : s.str(10))
  out << ":#{s.pick(%w[80 443 0 65535 65536 x 8080])}" if s.coin(0.3)
  while s.more
    out << case s.choice(7)
           when 0 then "/#{s.str(6)}"
           when 1 then "/#{s.pick(%w[. .. %2e %2E%2e // %2F index.html a.tar.gz])}"
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

# a value for a template variable: a string, a list or an associative array
Value = Stitch.datatype("Value")
Stitch.decoder(Value) do |s|
  case s.choice(4)
  when 0, 1 then s.str(8)
  when 2 then (0...s.range(0, 3)).map { s.str(4) }
  else (0...s.range(0, 3)).to_h { [s.pick(%w[a b k x]), s.str(4)] }
  end
end

# PublicSuffix reads its list with the default external encoding: make it UTF-8, as in a UTF-8 locale
Stitch.init { Encoding.default_external = Encoding::UTF_8 }

PARTS = %i[scheme user password userinfo host hostname authority port path query fragment site origin request_uri].freeze

# characters a component may hold without changing how the URI parses
COMPONENT_CLASS = { user: CC::UNRESERVED + CC::SUB_DELIMS, password: CC::UNRESERVED + CC::SUB_DELIMS,
                    path: CC::PATH, query: CC::QUERY, fragment: CC::FRAGMENT }.freeze

# ---------------------------------------------------------------------------------------------
# Producers

Stitch.endpoint :parse, { text: Str[512, UriText] }, New[Uri], bails_on: BAD do |text|
  Addressable::URI.parse(text)
end

Stitch.endpoint :heuristic_parse, { text: Str[512, UriText], scheme: OneOf[nil, "http", "https", "ftp"] }, New[Uri] | nil, bails_on: BAD do |text, scheme|
  scheme ? Addressable::URI.heuristic_parse(text, scheme: scheme) : Addressable::URI.heuristic_parse(text)
end

# components given separately: the URI they make parses back to the same components
Stitch.endpoint :from_parts, { scheme: OneOf[nil, "http", "https", "ftp", "mailto", "urn", "x-y"], user: OneOf[nil, "u", "us%40er", ""],
                               password: OneOf[nil, "p", "p%3Aw"], host: OneOf[nil, "example.com", "127.0.0.1", "[::1]", "", "Bücher.de"],
                               port: OneOf[nil, 80, 8080, 0, 65535], path: Str[16], query: OneOf[nil, "a=1", "", "x=%20&y"],
                               fragment: OneOf[nil, "f", ""] }, New[Uri], bails_on: BAD do |scheme, user, password, host, port, path, query, fragment|
  path = Addressable::URI.encode_component(path, CC::PATH)
  u = Addressable::URI.new(scheme: scheme, user: user, password: password, host: host, port: port, path: path, query: query, fragment: fragment)
  again = Addressable::URI.parse(u.to_s)
  Stitch.log("to_s: #{u.to_s.inspect}")
  Stitch.check(again == u, "URI.new(parts) parses back to itself")
  u
end

Stitch.endpoint :convert_path, { path: Str[64, UriText] }, New[Uri] | nil, bails_on: BAD do |path|
  Addressable::URI.convert_path(path.delete("\0")) # (a file path has no NUL: File.exist? raises)
end

Stitch.endpoint :join_all, { a: Str[128, UriText], b: Str[128, UriText], c: Str[64, UriText] }, New[Uri], bails_on: BAD do |a, b, c|
  r = Addressable::URI.join(a, b, c)
  Stitch.check(r == Addressable::URI.join(Addressable::URI.join(a, b), c), "URI.join(a, b, c) joins left to right")
  r
end

# ---------------------------------------------------------------------------------------------
# Round trips and inspection

# to_s parses back to the same URI
Stitch.endpoint :roundtrip, { u: Borrow[Uri] }, bails_on: BAD do |u|
  s = u.to_s
  again = Addressable::URI.parse(s)
  Stitch.log("to_s: #{s.inspect} -> #{again.to_s.inspect}")
  Stitch.check(again == u, "parse(to_s) == uri")
  Stitch.check(u.to_str == s, "to_str is to_s")
end

# encode_with/init_with: a URI dumped to YAML loads back equal
Stitch.endpoint :yaml, { u: Borrow[Uri] }, bails_on: BAD do |u|
  y = YAML.dump(u)
  back = YAML.unsafe_load(y)
  Stitch.check(back.to_s == u.to_s && back == u, "YAML.load(YAML.dump(uri)) == uri")
end

Stitch.endpoint :inspect_uri, { u: Borrow[Uri] }, bails_on: BAD do |u|
  %i[scheme normalized_scheme user normalized_user password normalized_password userinfo normalized_userinfo
     host normalized_host hostname authority normalized_authority port normalized_port inferred_port default_port
     site normalized_site path normalized_path basename extname query normalized_query fragment normalized_fragment
     request_uri origin display_uri to_hash inspect hash ip_based? absolute? relative? empty?].each { |m| u.send(m) }
  Stitch.check(u.absolute? != u.relative?, "absolute? is not relative?")
  Stitch.check(u.eql?(u.dup) && u.dup.hash == u.hash, "a copy is eql? and has the same hash")
  Stitch.check(u === u.to_s, "uri === its string")
  h = u.to_hash
  Stitch.check(h[:host] == u.host && h[:path] == u.path && h[:query] == u.query, "to_hash holds the components")
  Stitch.check(u.inferred_port == (u.port.to_i.zero? ? u.default_port : u.port), "inferred_port is the port or the default")
  Stitch.check(u.default_port == (u.scheme && Addressable::URI.port_mapping[u.scheme.strip.downcase]), "default_port follows port_mapping")
  Addressable::URI.ip_based_schemes
  Stitch.check(u.empty? == u.to_s.empty?, "empty? is an empty string")
end

# the public suffix of the host; PublicSuffix rejects names it cannot split
Stitch.endpoint :domain, { u: Borrow[Uri], tld: OneOf[nil, "com", "org", "co.uk", "de"] }, bails_on: BAD + [PublicSuffix::Error] do |u, tld|
  Stitch.require(u.host && !u.host.empty?, "no host")
  u.tld
  u.domain
  u.tld = tld if tld && !u.frozen?
end

# ---------------------------------------------------------------------------------------------
# Normalization, joining, routing, merging

Stitch.endpoint :normalize, { u: Borrow[Uri] }, New[Uri], bails_on: BAD do |u|
  n = u.normalize
  Stitch.log("normalized: #{n.to_s.inspect}")
  n2 = n.normalize
  Stitch.check(n2.to_s == n.to_s, "normalize is idempotent")
  again = Addressable::URI.parse(n.to_s)
  Stitch.check(again.normalize.to_s == n.to_s, "a normalized URI parses back to itself")
  Stitch.check(u == n, "a URI is == to its normalized form")
  n
end

Stitch.endpoint :normalize_in_place, { u: Borrow[Uri] }, bails_on: BAD do |u|
  Stitch.require(!u.frozen?, "frozen")
  expect = u.normalize.to_s
  u.normalize!
  Stitch.check(u.to_s == expect, "normalize! does what normalize returns")
end

Stitch.endpoint :join, { a: Borrow[Uri], b: Borrow[Uri], in_place: Bool }, New[Uri], bails_on: BAD do |a, b, in_place|
  r = a.join(b)
  if in_place
    Stitch.require(!a.frozen?, "frozen")
    c = a.dup
    c.join!(b)
    Stitch.check(c == r, "join! does what join returns")
  end
  r
end

# base.join(base.route_to(u)) is u (for absolute URIs of the same scheme and authority form)
Stitch.endpoint :route, { base: Borrow[Uri], u: Borrow[Uri], from: Bool }, bails_on: BAD + [ArgumentError] do |base, u, from|
  Stitch.require(base.absolute? && u.absolute?, "relative URIs")
  r = from ? u.route_from(base) : base.route_to(u)
  back = base.join(r)
  Stitch.log("route: #{r.to_s.inspect}, joined back: #{back.to_s.inspect}")
  Stitch.check(back.normalize == u.normalize, "base.join(base.route_to(u)) == u")
end

# merge replaces the components it is given and keeps the others
Stitch.endpoint :merge, { u: Borrow[Uri], part: OneOf[:scheme, :host, :port, :path, :query, :fragment, :user, :password],
                          value: OneOf[nil, "http", "example.org", 8080, "/p/q", "a=b", "frag", "x"], in_place: Bool },
                New[Uri], bails_on: BAD + [TypeError] do |u, part, value, in_place|
  m = u.merge(part => value)
  %i[scheme host port path query fragment user password].each do |k|
    next if k == part
    # (a URI with an authority has an absolute path: a rootless one gains a "/")
    next if k == :path && %i[host port user password].include?(part)
    next if k == :user && part == :password # (a password needs a user: it becomes "")
    Stitch.check(m.send(k) == u.send(k), "merge keeps the components it is not given")
  end
  if in_place && !u.frozen?
    c = u.dup
    c.merge!(part => value)
    Stitch.check(c == m, "merge! does what merge returns")
  end
  m
end

Stitch.endpoint :omit, { u: Borrow[Uri], parts: OneOf[[:scheme], [:query, :fragment], [:user, :password], [:path], [:authority], [:port, :fragment]],
                         in_place: Bool }, New[Uri], bails_on: BAD do |u, parts, in_place|
  o = u.omit(*parts)
  parts.each { |p| Stitch.check(o.send(p).nil? || (p == :path && o.path.empty?), "omit removes the component") }
  if in_place && !u.frozen?
    c = u.dup
    c.omit!(*parts)
    Stitch.check(c == o, "omit! does what omit returns")
  end
  o
end

Stitch.endpoint :freeze_uri, { u: Borrow[Uri] }, New[Uri], bails_on: BAD do |u|
  f = u.dup.freeze
  f.normalize
  %i[normalized_scheme normalized_authority normalized_path normalized_query normalized_fragment normalized_site].each { |m| f.send(m) }
  Stitch.check(f.frozen? && f == u, "a frozen copy equals the URI")
  f
end

# ---------------------------------------------------------------------------------------------
# Setters

# a component, pct-encoded where it would otherwise change how the URI parses, reads back after
# to_s/parse (request_uri= takes its path and query as they are: only set_part_raw sets it)
Stitch.endpoint :set_part, { u: Borrow[Uri], part: OneOf[*(PARTS - [:request_uri])], value: Str[32, UriText] }, bails_on: BAD + [TypeError] do |u, part, value|
  Stitch.require(!u.frozen?, "frozen")
  value = Addressable::URI.encode_component(value, COMPONENT_CLASS[part]) if COMPONENT_CLASS[part]
  value = value.to_i if part == :port && value.match?(/\A\d+\z/)
  u.send("#{part}=", value)
  if %i[scheme user password host port path query fragment].include?(part)
    again = Addressable::URI.parse(u.to_s)
    Stitch.log("to_s: #{u.to_s.inspect}")
    Stitch.check(again.send(part).to_s == u.send(part).to_s, "a part set reads back after to_s/parse")
  end
end

# a raw component: setters do not escape delimiters, so a value with them makes a URI whose
# string reads differently; set it on a copy that no other stub sees
Stitch.endpoint :set_part_raw, { u: Borrow[Uri], part: OneOf[*PARTS], value: Str[32, UriText] }, bails_on: BAD + [TypeError] do |u, part, value|
  c = u.dup
  c.send("#{part}=", part == :port && value.match?(/\A\d+\z/) ? value.to_i : value)
  c.to_s
  c.normalize
end

Stitch.endpoint :set_nil, { u: Borrow[Uri], part: OneOf[:scheme, :user, :password, :userinfo, :host, :authority, :port, :query, :fragment, :site, :origin] },
                bails_on: BAD do |u, part|
  Stitch.require(!u.frozen?, "frozen")
  u.send("#{part}=", nil)
  # (user stays "" while there is a password)
  Stitch.check(u.send(part).nil?, "a part set to nil reads nil") unless %i[site origin user].include?(part)
end

Stitch.endpoint :defer_validation, { u: Borrow[Uri], host: OneOf[nil, "example.com", ""], path: OneOf["", "a", "/a", "//a"], scheme: OneOf[nil, "http"] },
                bails_on: BAD do |u, host, path, scheme|
  Stitch.require(!u.frozen?, "frozen")
  before = u.to_s
  begin
    u.defer_validation do
      u.host = host
      u.path = path
      u.scheme = scheme
    end
  rescue Addressable::URI::InvalidURIError
    Stitch.check(u.to_s == before, "a failed defer_validation restores the URI")
    raise
  end
end

Stitch.endpoint :query_values, { u: Borrow[Uri], mode: OneOf[Hash, Array] }, bails_on: BAD + [ArgumentError, TypeError] do |u, mode|
  v = u.query_values(mode)
  next if v.nil? || mode == Array

  copy = u.dup
  copy.query_values = v
  Stitch.check(copy.query_values(Hash) == v, "query_values= then query_values reads the same")
end

QV = Stitch.datatype("QV")
Stitch.decoder(QV) do |s|
  if s.coin(0.3)
    (0...s.range(0, 3)).map { [s.str(4), s.coin(0.2) ? nil : s.str(4)] }
  else
    (0...s.range(0, 3)).to_h { [s.pick(%w[a b c key]), s.coin(0.2) ? s.pick([nil, true, ["x", "y"], { "n" => "1" }]) : s.str(6)] }
  end
end

Stitch.endpoint :set_query_values, { u: Borrow[Uri], v: Val[QV, 128] }, bails_on: BAD + [TypeError] do |u, v|
  Stitch.require(!u.frozen?, "frozen")
  u.query_values = v
  next unless v.is_a?(Hash) && v.values.all? { |x| x.is_a?(String) }
  Stitch.check(u.query_values(Hash) == v.transform_keys(&:to_s), "query_values= with strings reads back")
end

# ---------------------------------------------------------------------------------------------
# Class-level encoding

Stitch.endpoint :encode_component, { text: Str[64], cls: OneOf[:path, :query, :fragment, :unreserved, :authority, :scheme, :reserved, :sub_delims] } do |text, cls|
  chars = CC.const_get(cls.to_s.upcase)
  enc = Addressable::URI.encode_component(text, chars)
  Stitch.check(Addressable::URI.unencode_component(enc) == text, "unencode(encode(s)) == s")
  Stitch.check(Addressable::URI.escape_component(text, chars) == enc, "escape_component is encode_component")
  Stitch.check(Addressable::URI.unescape_component(enc) == text, "unescape_component is unencode_component")
end

Stitch.endpoint :normalize_component, { text: Str[64, UriText], cls: OneOf[:path, :query, :fragment, :unreserved], leave: OneOf["", "/", "%2F"] } do |text, cls, leave|
  chars = CC.const_get(cls.to_s.upcase)
  n = Addressable::URI.normalize_component(text, chars, leave)
  Stitch.check(Addressable::URI.normalize_component(n, chars, leave) == n, "normalize_component is idempotent")
end

Stitch.endpoint :unencode, { text: Str[64, UriText], as_uri: Bool, leave: OneOf["", "/"] }, bails_on: BAD do |text, as_uri, leave|
  Addressable::URI.unencode(text, as_uri ? Addressable::URI : String, leave)
  Addressable::URI.unescape(text)
end

Stitch.endpoint :encode_uri, { text: Str[128, UriText], as_uri: Bool }, bails_on: BAD do |text, as_uri|
  t = as_uri ? Addressable::URI : String
  e = Addressable::URI.encode(text, t)
  Addressable::URI.escape(text)
  n = Addressable::URI.normalized_encode(text, t)
  Stitch.check(Addressable::URI.normalized_encode(n.to_s) == n.to_s, "normalized_encode is idempotent")
  e
end

Stitch.endpoint :normalize_path, { path: Str[64, UriText] } do |path|
  n = Addressable::URI.normalize_path(path)
  Stitch.check(Addressable::URI.normalize_path(n) == n, "normalize_path is idempotent")
  Stitch.check(!n.split("/").include?("..") || !path.start_with?("/"), "an absolute normalized path has no .. segment")
end

Stitch.endpoint :form, { pairs: Str[128], sort: Bool } do |pairs, sort|
  kv = pairs.split("&").map { |p| k, v = p.split("=", 2); [k.to_s, v.to_s] }
  enc = Addressable::URI.form_encode(kv, sort)
  dec = Addressable::URI.form_unencode(enc)
  want = kv.map { |k, v| [k.gsub("\r\n", "\n").gsub("\r", "\n"), v.gsub("\r\n", "\n").gsub("\r", "\n")] }
  want = want.sort if sort
  Stitch.check(dec == want, "form_unencode(form_encode(pairs)) == pairs")
end

Stitch.endpoint :form_unencode, { text: Str[128, UriText] } do |text|
  Addressable::URI.form_unencode(text)
end

# IDNA: to_ascii gives ASCII, and is stable through to_unicode
Stitch.endpoint :idna, { text: Str[64, UriText] }, bails_on: [Addressable::IDNA::InvalidLabel, Addressable::IDNA::PunycodeBigOutput,
                                                             Addressable::IDNA::PunycodeBadInput, Addressable::IDNA::PunycodeOverflow] do |text|
  a = Addressable::IDNA.to_ascii(text)
  u = Addressable::IDNA.to_unicode(a)
  # (for a host name: letters, digits, hyphens and dots)
  Stitch.check(a.ascii_only?, "to_ascii gives ASCII") if text.match?(/\A[\p{L}\p{M}\p{N}.-]*\z/)
  Stitch.check(Addressable::IDNA.to_ascii(u) == a, "to_ascii(to_unicode(to_ascii(s))) == to_ascii(s)") if a.match?(/\A[a-z0-9.-]*\z/)
end

# ---------------------------------------------------------------------------------------------
# Templates

Stitch.endpoint :template, { text: Str[256, TemplateText] }, New[Tmpl], bails_on: BAD do |text|
  Addressable::Template.new(text)
end

Stitch.endpoint :inspect_template, { t: Borrow[Tmpl] }, bails_on: TBAD do |t|
  t.pattern
  t.inspect
  t.hash
  t.names
  t.keys
  t.variable_defaults
  Stitch.check(t.variables == t.names && t.keys == t.names, "variables, keys and names agree")
  c = Addressable::Template.new(t.pattern)
  Stitch.check(c == t && c.eql?(t) && c.hash == t.hash, "a template from the same pattern is ==")
  f = Addressable::Template.new(t.pattern).freeze
  Stitch.check(f.frozen? && f.variables == t.variables, "a frozen template keeps its variables")
  t.to_regexp
end

# expansion with string values matches the template back to those values
Stitch.endpoint :expand_extract, { t: Borrow[Tmpl], a: Str[8], b: Str[8] }, bails_on: TBAD do |t, a, b|
  vals = t.variables.uniq.each_with_index.to_h { |v, i| [v, i.even? ? a : b] }
  uri = t.expand(vals)
  Stitch.log("expanded: #{uri.to_s.inspect}")
  m = t.match(uri)
  Stitch.check(!m.nil?, "a template matches its own expansion")
end

Stitch.endpoint :expand, { t: Borrow[Tmpl], vals: Val[Value, 64], vals2: Val[Value, 64], normalize: Bool }, New[Uri], bails_on: TBAD do |t, vals, vals2, normalize|
  names = t.variables.uniq
  mapping = names.each_with_index.to_h { |n, i| [n, i.even? ? vals : vals2] }
  t.expand(mapping, nil, normalize)
end

# a processor validates and transforms values
class Proc1
  def self.validate(_name, value) = !value.to_s.include?("!")
  def self.transform(_name, value) = value.is_a?(String) ? value.upcase : value
  def self.restore(_name, value) = value.is_a?(String) ? value.downcase : value
  def self.match(_name) = ".*?"
end

Stitch.endpoint :expand_processor, { t: Borrow[Tmpl], v: Str[8] }, New[Uri], bails_on: TBAD do |t, v|
  mapping = t.variables.to_h { |n| [n, v] }
  begin
    t.expand(mapping, Proc1)
  rescue Addressable::Template::InvalidTemplateValueError
    Stitch.check(v.include?("!"), "the processor rejects only values it does not validate")
    Stitch.bail("value rejected")
  end
end

Stitch.endpoint :partial_expand, { t: Borrow[Tmpl], v: Val[Value, 64], which: Int[0, 3] }, New[Tmpl] | nil, bails_on: TBAD do |t, v, which|
  vars = t.variables.uniq
  next nil if vars.empty?

  name = vars[which % vars.size]
  p = t.partial_expand(name => v)
  # expanding the rest gives what expanding everything at once gives
  rest = vars.reject { |n| n == name }.to_h { |n| [n, "r"] }
  full = t.expand(rest.merge(name => v)).to_s
  Stitch.check(p.expand(rest).to_s == full, "partial_expand then expand is expand") if v.is_a?(String) && v.match?(/\A[a-zA-Z0-9]*\z/)
  p
end

Stitch.endpoint :match_uri, { t: Borrow[Tmpl], u: Borrow[Uri], with_processor: Bool }, New[Match] | nil, bails_on: TBAD do |t, u, with_processor|
  m = with_processor ? t.match(u, Proc1) : t.match(u)
  e = with_processor ? t.extract(u, Proc1) : t.extract(u)
  Stitch.check(m.nil? == e.nil?, "match and extract agree on matching")
  Stitch.check(m.mapping == e, "extract is the match's mapping") if m
  m
end

Stitch.endpoint :match_expansion, { t: Borrow[Tmpl], v: Str[8] }, New[Match] | nil, bails_on: TBAD do |t, v|
  t.match(t.expand(t.variables.to_h { |n| [n, v] }))
end

Stitch.endpoint :match_data, { m: Borrow[Match], s: Src[8] } do |m, s|
  keys = m.keys
  Stitch.check(keys == m.names && keys == m.variables, "keys, names and variables agree")
  Stitch.check(m.values == keys.map { |k| m.mapping[k] } && m.captures == m.values, "values follow the mapping")
  Stitch.check(m.to_a == [m.to_s, *m.captures], "to_a is the string then the captures")
  Stitch.check(m.string == m.uri.to_s && m.to_s == m.uri.to_s, "string is the URI")
  m.template
  m.inspect
  m.pre_match
  m.post_match
  next if keys.empty?
  i = s.range(0, keys.size - 1)
  Stitch.check(m[keys[i]] == m.values[i], "[name] is the value")
  Stitch.check(m[i + 1] == m.values[i] && m[0] == m.to_s, "[index] is the capture")
  Stitch.check(m.values_at(i + 1) == [m.values[i]], "values_at reads captures")
end

Stitch.endpoint :extract, { t: Borrow[Tmpl], u: Borrow[Uri] }, bails_on: TBAD do |t, u|
  t.extract(u)
end
