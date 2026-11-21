# rack: the request-parsing utilities: nested queries, escaping, byte ranges, cookies, media types,
# q-values, multipart bodies, host / forwarded headers. Oracles: parse(build(parse(q))) == parse(q),
# unescape(escape(s)) == s, byte ranges stay inside the body; slow parsing shows up as timeouts
# (timeout_ms = 1000).
require "stitch"
require "stringio"
require "rack"
include Stitch::DSL

Params = Stitch.object("Params")
QueryText = Stitch.datatype("QueryText")
HeaderText = Stitch.datatype("HeaderText")
MultipartBody = Stitch.datatype("MultipartBody")

PARAM_ERRORS = [Rack::Utils::ParameterTypeError, Rack::Utils::InvalidParameterError, RangeError].freeze # RangeError: too deep (2.2)

Stitch.decoder(QueryText) do |s|
  parts = []
  while s.more
    key = s.coin(0.6) ? s.pick(%w[a b user x y]) : s.str(4)
    key += s.pick(["", "[]", "[a]", "[][b]", "[a][]", "[a][b][c]", "[", "]", "[[]]"]) if s.coin(0.6)
    parts << (s.coin(0.9) ? "#{key}=#{s.coin(0.7) ? s.str(6) : s.token}" : key)
  end
  parts.join(s.pick(["&", ";", "&&", "&;"]))
end

Stitch.decoder(HeaderText) do |s|
  out = +""
  while s.more
    out << case s.choice(6)
           when 0 then s.pick(["bytes=", "text/html", "multipart/form-data", "gzip", "*/*", "en-US"])
           when 1 then s.pick([";", ",", "=", "-", " ", "\t", ";q=", ";charset=", ";boundary=", "\""])
           when 2 then s.range(0, 1000).to_s
           when 3 then s.str(6)
           when 4 then s.token
           else " " * s.range(0, 40)
           end
  end
  out
end

Stitch.decoder(MultipartBody) do |s|
  b = "AaB03x"
  out = +""
  while s.more
    out << "--#{b}\r\n"
    disp = %(Content-Disposition: form-data; name="#{s.coin(0.7) ? s.pick(%w[a b file x[] x[y]]) : s.str(6)}")
    disp << %(; filename="#{s.str(8)}") if s.coin(0.4)
    disp << %(; filename*=#{s.str(10)}) if s.coin(0.2)
    out << disp << "\r\n"
    out << "Content-Type: #{s.coin(0.6) ? s.pick(%w[text/plain application/octet-stream]) : s.str(12)}\r\n" if s.coin(0.5)
    out << s.str(8) << "\r\n" if s.coin(0.2)
    out << "\r\n" << s.str(32) << "\r\n"
  end
  out << "--#{b}--\r\n" if s.coin(0.9)
  out
end

Stitch.endpoint :parse_nested, { qs: Str[512, QueryText] }, New[Params], bails_on: PARAM_ERRORS do |qs|
  Rack::Utils.parse_nested_query(qs)
end

# parse(build(params)) == params
Stitch.endpoint :nested_roundtrip, { p: Borrow[Params] }, bails_on: PARAM_ERRORS do |p|
  q = Rack::Utils.build_nested_query(p)
  again = Rack::Utils.parse_nested_query(q)
  Stitch.log("built: #{q.inspect}")
  Stitch.check(again == p, "parse_nested_query(build_nested_query(p)) == p")
end

Stitch.endpoint :parse_query, { qs: Str[512, QueryText], sep: OneOf[nil, "&", ";", "&;"] }, bails_on: PARAM_ERRORS do |qs, sep|
  h = Rack::Utils.parse_query(qs, sep)
  q = Rack::Utils.build_query(h)
  Stitch.check(Rack::Utils.parse_query(q) == h, "parse_query(build_query(h)) == h")
end

Stitch.endpoint :escape, { s: Str[64], mode: OneOf[:escape, :escape_path] } do |s, mode|
  e = Rack::Utils.send(mode, s)
  back = mode == :escape ? Rack::Utils.unescape(e) : Rack::Utils.unescape_path(e)
  Stitch.check(back == s.b.force_encoding(back.encoding) || back == s, "unescape(#{mode}(s)) == s")
end

Stitch.endpoint :unescape, { s: Str[64, QueryText] }, bails_on: [ArgumentError] do |s|
  Rack::Utils.unescape(s)
  Rack::Utils.escape_html(s)
end

Stitch.endpoint :byte_ranges, { header: Str[256, HeaderText], size: Int[0, 1000] } do |header, size|
  r = Rack::Utils.get_byte_ranges(header, size)
  (r || []).each { |x| Stitch.check(x.begin >= 0 && x.end < size && x.begin <= x.end, "byte ranges lie inside the body") }
end

Stitch.endpoint :cookies, { header: Str[256, HeaderText] } do |header|
  c = Rack::Utils.parse_cookies_header(header)
  c.each do |k, v|
    out = Rack::Utils.set_cookie_header!({}, k, v) rescue nil # rubocop:disable Style/RescueModifier
    out
  end
end

Stitch.endpoint :set_cookie, { key: Str[16], value: Str[32], path: OneOf[nil, "/", "/a"], delete: Bool } do |key, value, path, delete|
  h = {}
  Rack::Utils.set_cookie_header!(h, key, { value: value, path: path })
  Rack::Utils.delete_cookie_header!(h, key, { path: path }) if delete
  h
end

Stitch.endpoint :media_type, { ct: Str[256, HeaderText] } do |ct|
  Rack::MediaType.type(ct)
  Rack::MediaType.params(ct)
end

Stitch.endpoint :q_values, { header: Str[256, HeaderText], avail: OneOf[%w[gzip identity], %w[text/html */*], %w[en de]] } do |header, avail|
  Rack::Utils.q_values(header)
  Rack::Utils.best_q_match(header, avail)
end

Stitch.endpoint :multipart, { body: Str[2048, MultipartBody], boundary: OneOf["AaB03x", "x", ""] },
                bails_on: [EOFError, Rack::Multipart::MultipartPartLimitError] + PARAM_ERRORS do |body, boundary|
  env = Rack::MockRequest.env_for("/", "CONTENT_TYPE" => "multipart/form-data; boundary=#{boundary}",
                                       "CONTENT_LENGTH" => body.bytesize.to_s, :input => StringIO.new(body))
  Rack::Multipart.parse_multipart(env)
end

Stitch.endpoint :request_headers, { host: Str[128, HeaderText], fwd: Str[128, HeaderText], accept: Str[128, HeaderText] },
                bails_on: [ArgumentError] do |host, fwd, accept|
  env = Rack::MockRequest.env_for("http://example.com/a?b=c", "HTTP_HOST" => host, "HTTP_X_FORWARDED_HOST" => fwd,
                                  "HTTP_X_FORWARDED_FOR" => fwd, "HTTP_ACCEPT_ENCODING" => accept, "HTTP_FORWARDED" => fwd)
  r = Rack::Request.new(env)
  r.host
  r.port
  r.ip
  r.accept_encoding
  r.forwarded_for
end

Stitch.endpoint :clean_path, { path: Str[128, QueryText] } do |path|
  c = Rack::Utils.clean_path_info(path)
  Stitch.check(!c.split("/").include?(".."), "clean_path_info removes ..")
end
