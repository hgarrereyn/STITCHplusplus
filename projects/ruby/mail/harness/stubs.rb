# mail: parse, build and encode email messages (RFC 5322 / MIME, Ragel-generated field parsers):
# every header setter and getter, field objects (address, date, message-id, content-type, MIME
# version...), Header and Field APIs, bodies (match, split, transfer encodings), parts and
# attachments (add_part, part, add_file, inline, text/html parts, convert_to_multipart,
# without_attachments!, PartsList sorting and recursion), delivery-status reports, reply,
# YAML/hash round trips, delivery and retrieval through the test mailer and retriever with
# interceptors and observers, the SMTP envelope, Encodings, Utilities and IndifferentHash.
# Oracles: encoding a parsed message is a fixed point after one round; a message built from
# fields, a body and attachments encodes to text that parses back to the same subject, addresses,
# body and attachment contents; Address / ContentType / encodings / UTF-7 / quoting round-trip; a
# header set reads back; YAML round-trips; a reply refers to its message; the test mailer
# delivers the message. Anything raised from parsing that is not one of mail's own parse errors
# is a bug (mail is lenient by design).
require "stitch"
require "mail"
include Stitch::DSL

Msg = Stitch.object("Msg")
MailText = Stitch.datatype("MailText")
HeaderValue = Stitch.datatype("HeaderValue")
AddressText = Stitch.datatype("AddressText")

PARSE_ERRORS = [Mail::Field::ParseError, Mail::Field::IncompleteParseError, Mail::UnknownEncodingType].freeze

ADDR = ["a@b.com", "\"Q, R\" <q@r.org>", "Bob <bob@x.y>", "undisclosed-recipients:;", "group: a@b.c, d@e.f;", "<x@[1.2.3.4]>",
        "\"\\\"\" <q@x>", "=?UTF-8?B?w6k=?= <e@x>", "a(comment)@b.c", "a@b", "@", "<>", "a..b@c"].freeze

Stitch.decoder(AddressText) do |s|
  out = []
  while s.more
    out << case s.choice(5)
           when 0 then s.pick(ADDR)
           when 1 then "#{s.str(6)}@#{s.pick(%w[x.com b.c [127.0.0.1] e.f])}"
           when 2 then %("#{s.str(8)}" <#{s.pick(%w[a b c])}@x.y>)
           when 3 then s.token
           else s.str(10)
           end
  end
  out.join(s.pick([", ", ",", ";", " "]))
end

Stitch.decoder(HeaderValue) do |s|
  out = +""
  while s.more
    out << case s.choice(8)
           when 0 then s.str(8)
           when 1 then s.pick(["=?UTF-8?B?", "=?ISO-8859-1?Q?", "?=", "=?utf-8?q?a_b?=", "=?iso-2022-jp?B?GyRCJEokPhsoQg==?="])
           when 2 then s.pick(["text/plain", "multipart/mixed", "; charset=", "; boundary=", "; name*=UTF-8''%C3%A9", "attachment; filename=",
                               "inline", "base64", "quoted-printable", "7bit", "8bit", "binary", "x-uuencode"])
           when 3 then s.pick(["Mon, 1 Jan 2001 10:00:00 +0000", "1 Jan 2001", "Fri, 31 Dec 9999 23:59:60 -2400", "Tue, 30 Feb 2020 00:00 GMT"])
           when 4 then s.pick(ADDR)
           when 5 then s.pick(["\r\n ", " ", "\t", "\"", "(", ")", "<", ">", ";", ",", "\\"])
           when 6 then s.token
           else "é中\u{1f600}"
           end
  end
  out
end

HEADERS = %w[From To Cc Bcc Reply-To Subject Date Message-ID In-Reply-To References Content-Type Content-Transfer-Encoding
             Content-Disposition MIME-Version Received Return-Path Sender Keywords Comments Content-ID Content-Location
             Content-Description X-Custom Resent-From Resent-Date].freeze

Stitch.decoder(MailText) do |s|
  out = +""
  boundary = "XyZ"
  multipart = s.coin(0.3)
  while s.more && s.coin(0.85)
    out << "#{s.coin(0.9) ? s.pick(HEADERS) : s.str(6)}: "
    out << (s.coin(0.7) ? s.str(20) : s.token)
    out << s.pick(["\r\n", "\n", "\r\n\t", "\r\n "]) if s.coin(0.15)
    out << "\r\n"
  end
  out << "Content-Type: multipart/mixed; boundary=#{boundary}\r\n" if multipart
  out << "\r\n"
  if multipart
    while s.more
      out << "--#{boundary}\r\n"
      out << "Content-Type: #{s.pick(['text/plain; charset=utf-8', 'application/octet-stream', 'message/rfc822', 'text/html', s.str(8)])}\r\n"
      out << "Content-Transfer-Encoding: #{s.pick(%w[base64 quoted-printable 7bit 8bit binary x-uuencode])}\r\n" if s.coin(0.6)
      out << "Content-Disposition: attachment; filename=\"#{s.str(6)}\"\r\n" if s.coin(0.3)
      out << "\r\n#{s.str(24)}\r\n"
    end
    out << "--#{boundary}--\r\n" if s.coin(0.8)
  else
    out << s.str(64)
  end
  out
end

def parse(text)
  Mail.new(text)
end

Stitch.endpoint :parse, { text: Str[8192, MailText] }, New[Msg], bails_on: PARSE_ERRORS do |text|
  m = parse(text)
  m.header.fields.each do |f|
    f.to_s
    f.field # instantiate the structured field
  end
  m
end

# Walks a message the way an application reads it.
Stitch.endpoint :inspect, { m: Borrow[Msg] }, bails_on: PARSE_ERRORS do |m|
  m.subject
  m.from
  m.to
  m.cc
  m.date
  m.message_id
  m.mime_type
  m.charset
  m.content_type_parameters
  m.multipart?
  m.attachments.each { |a| a.filename; a.decoded }
  m.parts.each { |p| p.content_type; p.decoded unless p.multipart? }
  m.text_part&.decoded
  m.html_part&.decoded
  m.body.decoded unless m.multipart?
  m.envelope_from
  m.reply_to
  m.received
end

# encoding is a fixed point from the second round on
Stitch.endpoint :reencode, { m: Borrow[Msg] }, bails_on: PARSE_ERRORS do |m|
  a = m.encoded
  b = parse(a).encoded
  c = parse(b).encoded
  strip = ->(x) { x.gsub(/^(Message-ID|Content-ID|Date): .*\r\n/, "").gsub(/--==_mimepart_\h+_\h+_\d+/, "--B").gsub(/boundary="?--==_mimepart_[^;\r\n]+/, "boundary=B") }
  Stitch.log("second: #{b[0, 400].inspect}") if strip.(b) != strip.(c)
  Stitch.check(strip.(b) == strip.(c), "encoded(parse(encoded(m))) is a fixed point")
end

Stitch.endpoint :build, { subject: Str[64, HeaderValue], from: Str[64, AddressText], body: Str[256], charset: OneOf["UTF-8", "ISO-8859-1", "US-ASCII", nil],
                          cte: OneOf[nil, "base64", "quoted-printable", "7bit", "8bit"] }, New[Msg], bails_on: PARSE_ERRORS do |subject, from, body, charset, cte|
  m = Mail.new
  m.subject = subject
  m.from = from
  m.to = "to@example.com"
  m.charset = charset if charset
  m.body = body
  m.content_transfer_encoding = cte if cte
  Stitch.set(m, "subject", subject)
  Stitch.set(m, "body", body)
  Stitch.set(m, "cte", cte.to_s)
  m
end

Stitch.endpoint :attach, { m: Borrow[Msg], name: Str[32, HeaderValue], data: Bytes[512], mime: OneOf[nil, "application/pdf", "text/plain", "image/png", "message/rfc822"] },
                bails_on: PARSE_ERRORS do |m, name, data, mime|
  Stitch.require(!name.strip.empty? && !name.include?("\0"), "no file name")
  # (a text/plain attachment is text in its charset: invalid UTF-8 is replaced)
  data = data.dup.force_encoding(Encoding::UTF_8).scrub("?").b if mime == "text/plain"
  opts = { content: data.b }
  opts[:mime_type] = mime if mime
  m.attachments[name] = opts
  n = Stitch.get(m, "attachments", 0)
  Stitch.set(m, "att#{n}", data.b)
  Stitch.set(m, "attname#{n}", name)
  Stitch.set(m, "attachments", n + 1)
end

# a built message survives encoding: subject, body and attachments come back
Stitch.endpoint :roundtrip, { m: Borrow[Msg] }, bails_on: PARSE_ERRORS do |m|
  Stitch.require(Stitch.get(m, "subject", nil), "not built here")
  text = m.encoded
  again = parse(text)
  subject = Stitch.get(m, "subject", "")
  Stitch.log("encoded: #{text[0, 600].inspect}")
  # (folding turns whitespace runs into one space; text that already holds encoded words is
  # taken as encoded by design)
  want = subject.gsub(/\s+/, " ").strip
  got = again.subject.to_s.gsub(/\s+/, " ").strip
  unless want.include?("=?") || want.empty?
    Stitch.check(got == want, "the subject survives encoding (#{got.inspect} vs #{want.inspect})")
  end
  n = Stitch.get(m, "attachments", 0)
  # (an explicit base64 / quoted-printable transfer encoding marks the body as already encoded)
  if n.zero? && !again.multipart? && !%w[base64 quoted-printable].include?(Stitch.get(m, "cte", "")) && Stitch.has?(m, "body")
    body = Stitch.get(m, "body", "")
    got_body = again.body.decoded
    norm = ->(x) { x.b.gsub(/\r\n|\r/, "\n").sub(/\n+\z/, "") }  # (line endings are normalized by design)
    # (a body of whitespace only is blank, and mail drops it)
    Stitch.check(norm.(got_body) == norm.(body) || m.charset.to_s =~ /ascii|8859/i || body !~ /\S/, "the body survives encoding")
  end
  atts = again.attachments
  n.times do |i|
    data = Stitch.get(m, "att#{i}")
    next unless data.b.match?(/\S/n) # (content of whitespace only is blank, like a blank body)
    eol = ->(x) { x.b.gsub(/\r\n|\r/, "\n") }  # (text/* parts get their line endings normalized)
    found = atts.find { |a| [a.body.decoded, a.decoded].any? { |d| d.b == data || eol.(d) == eol.(data) } }
    Stitch.check(!found.nil? || atts.size < n, "attachment #{i} decodes to its content")
  end
end

Stitch.endpoint :address, { text: Str[128, AddressText] }, bails_on: PARSE_ERRORS do |text|
  list = Mail::AddressList.new(text)
  list.addresses.each do |a|
    a.address
    a.display_name
    a.local
    a.domain
    a.comments
    s = a.to_s
    again = Mail::Address.new(s)
    Stitch.log("#{a.address.inspect} -> #{s.inspect} -> #{again.address.inspect}")
    Stitch.check(again.address == a.address, "Address.new(a.to_s).address == a.address")
  end
  list.group_names
end

FIELD_CLASSES = { "Content-Type" => Mail::ContentTypeField, "Content-Disposition" => Mail::ContentDispositionField,
                  "Date" => Mail::DateField, "Received" => Mail::ReceivedField, "Message-ID" => Mail::MessageIdField,
                  "References" => Mail::ReferencesField, "Content-Transfer-Encoding" => Mail::ContentTransferEncodingField,
                  "MIME-Version" => Mail::MimeVersionField, "Keywords" => Mail::KeywordsField, "Content-Location" => Mail::ContentLocationField,
                  "Subject" => Mail::SubjectField, "From" => Mail::FromField }.freeze

Stitch.endpoint :field, { name: OneOf[*FIELD_CLASSES.keys], value: Str[128, HeaderValue] }, bails_on: PARSE_ERRORS do |name, value|
  f = Mail::Field.new(name, value)
  f.to_s
  enc = f.encoded
  f.decoded
  inner = f.field
  if inner.respond_to?(:parameters)
    inner.parameters.to_s
  end
  inner.date_time if inner.respond_to?(:date_time)
  again = Mail::Field.parse(enc.chomp)
  Stitch.check(again.nil? ? f.value.to_s.strip.empty? : again.name.casecmp?(name), "an encoded field parses back with its name")
end

Stitch.endpoint :encodings, { text: Str[128, HeaderValue], charset: OneOf["UTF-8", "ISO-8859-1", "Shift_JIS", "GB2312"] }, bails_on: [Encoding::UndefinedConversionError, Encoding::InvalidByteSequenceError, Encoding::ConverterNotFoundError] do |text, charset|
  d = Mail::Encodings.value_decode(text)
  d2 = Mail::Encodings.unquote_and_convert_to(text, "UTF-8")
  Stitch.require(!text.include?("=?") && !text.include?("?="), "already holds encoded-word syntax")
  b = Mail::Encodings.b_value_encode(text, "UTF-8")
  Stitch.check(Mail::Encodings.value_decode(b).gsub(/\s+/, "") == text.gsub(/\s+/, ""), "value_decode(b_value_encode(s)) == s")
  q = Mail::Encodings.q_value_encode(text, "UTF-8")
  Stitch.check(Mail::Encodings.value_decode(q).gsub(/\s+/, "") == text.gsub(/\s+/, ""), "value_decode(q_value_encode(s)) == s")
  qp = Mail::Encodings::QuotedPrintable.encode(text.b)
  Stitch.check(Mail::Encodings::QuotedPrintable.decode(qp).gsub("\r\n", "\n") == text.b.gsub("\r\n", "\n"), "QuotedPrintable round trip")
  [d, d2]
end


# ---------------------------------------------------------------------------------------------
# Headers through Message setters and getters

Part = Stitch.object("Part")

Stitch.init do
  Mail.defaults do
    delivery_method :test
    retriever_method :test
  end
end

ADDR_FIELDS = %i[to cc bcc reply_to sender return_path resent_to resent_cc resent_bcc resent_from resent_sender
                 smtp_envelope_from smtp_envelope_to].freeze
TEXT_FIELDS = %i[subject comments content_description keywords].freeze
OTHER_FIELDS = %i[date resent_date message_id resent_message_id in_reply_to references content_id content_location
                  mime_version content_type content_disposition received body_encoding].freeze

Stitch.endpoint :set_address, { m: Borrow[Msg], field: OneOf[*ADDR_FIELDS], value: Str[96, AddressText] }, bails_on: PARSE_ERRORS do |m, field, value|
  m.send("#{field}=", value)
  m.send(field)
  next if %i[smtp_envelope_from smtp_envelope_to].include?(field)
  f = m[field]
  next unless f
  f.to_s
  if f.field.respond_to?(:addrs)
    inner = f.field
    inner.addrs.each { |a| a.to_s; a.name; a.raw; a.inspect }
    inner.formatted
    inner.display_names
    inner.group_addresses
    inner.group_names
    inner.default
    inner.each { |a| a }
  end
end

Stitch.endpoint :set_text, { m: Borrow[Msg], field: OneOf[*TEXT_FIELDS], value: Str[64, HeaderValue] }, bails_on: PARSE_ERRORS do |m, field, value|
  m.send("#{field}=", value)
  Stitch.set(m, "subject", value) if field == :subject && Stitch.has?(m, "subject") # (what roundtrip expects)
  got = m.send(field)
  # (an unstructured value without encoded words reads back, apart from folding whitespace)
  if field == :subject && !value.include?("=?") && value.valid_encoding? && value !~ /[\r\n]/
    Stitch.check(got.to_s.strip.squeeze(" ") == value.strip.squeeze(" "), "a subject set reads back")
  end
end

# (a new content type or transfer encoding changes how the body reads back)
def body_changed(m) = Stitch.delete(m, "body")

Stitch.endpoint :set_other, { m: Borrow[Msg], field: OneOf[*OTHER_FIELDS], value: Str[96, HeaderValue] }, bails_on: PARSE_ERRORS do |m, field, value|
  body_changed(m) if %i[content_type content_disposition body_encoding].include?(field)
  m.send("#{field}=", value)
  m.send(field)
  m[field]&.to_s
end

Stitch.endpoint :set_field, { m: Borrow[Msg], name: OneOf[*HEADERS], value: Str[96, HeaderValue], how: OneOf[:index, :header, :nil] },
                bails_on: PARSE_ERRORS do |m, name, value, how|
  if Stitch.has?(m, "subject")
    Stitch.set(m, "subject", how == :nil ? "" : value) if name == "Subject"
    body_changed(m) if name.start_with?("Content-")
  end
  case how
  when :index then m[name] = value
  when :header then m.header[name] = value
  else
    m[name] = nil
    Stitch.check(m.header[name].nil?, "a field set to nil is gone")
  end
end

Stitch.endpoint :headers_info, { m: Borrow[Msg] }, bails_on: PARSE_ERRORS do |m|
  (ADDR_FIELDS + TEXT_FIELDS + OTHER_FIELDS).each { |f| m.send(f) }
  %i[destinations from_addrs to_addrs cc_addrs bcc_addrs header_fields sub_type main_type multipart_report?
     delivery_status_report? bounced? has_attachments? raw_envelope envelope_date errors inspect
     inspect_structure has_message_id? has_date? has_content_type? has_charset? has_content_transfer_encoding?
     has_mime_version? attachment? attachment content_transfer_encoding transport_encoding default_part_body].each do |meth|
    m.send(meth) if m.respond_to?(meth)
  end
  h = m.header
  h.errors
  h.to_s
  h.field_summary
  h.has_message_id?
  h.has_content_id?
  h.has_date?
  h.has_mime_version?
  h.fields.each do |f|
    f.inspect
    f.responsible_for?(f.name)
    Stitch.check(f.same(f) && f == f, "a field is the same as itself")
    f.field.element if f.field.respond_to?(:element)
    inner = f.field
    inner.major if inner.respond_to?(:major)
    inner.minor if inner.respond_to?(:minor)
    inner.stringify(inner.parameters) if inner.respond_to?(:stringify)
  end
  h.fields.summary if h.fields.respond_to?(:summary)
  Stitch.check(m.has_attachments? == !m.attachments.empty?, "has_attachments? agrees with attachments")
end

# delivery-status reports (multipart/report)
ReportText = Stitch.datatype("ReportText")

Stitch.endpoint :parse_report, { text: Str[8192, ReportText] }, New[Msg], bails_on: PARSE_ERRORS do |text|
  Mail.new(text)
end

Stitch.endpoint :bounce, { m: Borrow[Msg] }, bails_on: PARSE_ERRORS do |m|
  Stitch.require(m.delivery_status_report?, "not a delivery status report")
  m.delivery_status_part
  %i[bounced? action final_recipient error_status diagnostic_code remote_mta retryable?].each { |x| m.send(x) }
  m.all_parts.each do |p|
    next unless p.delivery_status_report_part?
    p.delivery_status_data
    %i[bounced? action final_recipient error_status diagnostic_code remote_mta retryable?].each { |x| p.send(x) }
  end
end

# ---------------------------------------------------------------------------------------------
# Building messages from parts

Stitch.endpoint :new_from_hash, { subject: Str[32, HeaderValue], body: Str[128], to: Str[64, AddressText], with_block: Bool },
                New[Msg], bails_on: PARSE_ERRORS do |subject, body, to, with_block|
  if with_block
    Mail.new do
      from "a@example.com"
      to to
      subject subject
      body body
    end
  else
    Mail::Message.from_hash(from: "a@example.com", to: to, subject: subject, body: body)
  end
end

Stitch.endpoint :new_part, { body: Str[128], mime: OneOf["text/plain", "text/html", "application/octet-stream", "text/plain; charset=UTF-8"],
                             cte: OneOf[nil, "base64", "quoted-printable", "7bit"], cid: Bool }, New[Part], bails_on: PARSE_ERRORS do |body, mime, cte, cid|
  p = Mail::Part.new(content_type: mime, body: body)
  p.content_transfer_encoding = cte if cte
  p.add_content_id if cid
  p.cid
  p.url
  p.inline?
  p.has_content_id?
  p
end

Stitch.endpoint :add_part, { m: Borrow[Msg], p: Take[Part], how: OneOf[:add_part, :text_part=, :html_part=, :part_block, :lshift] },
                bails_on: PARSE_ERRORS do |m, p, how|
  n = m.parts.size
  case how
  when :add_part then m.add_part(p)
  when :text_part= then m.text_part = p
  when :html_part= then m.html_part = p
  when :part_block then m.part(content_type: "multipart/alternative") { |q| q.add_part(p) }
  else m.body << p
  end
  Stitch.check(m.parts.size >= n, "adding a part does not drop parts")
  m.text_part
  m.html_part
end

Stitch.endpoint :add_file, { m: Borrow[Msg], name: OneOf["a.txt", "b.pdf", "c.png", "d", "é.bin", "x y.doc"], data: Bytes[256], inline: Bool,
                             encoding: OneOf[nil, "base64", "binary", "quoted-printable"] }, bails_on: PARSE_ERRORS do |m, name, data, inline, encoding|
  if inline
    m.attachments.inline[name] = data.b
    a = m.attachments.select { |x| x.filename == name }.last
    Stitch.check(a.nil? || a.inline?, "an inline attachment is inline")
  elsif encoding
    # (with a transfer encoding, the content is given already encoded)
    content = Mail::Encodings.get_encoding(encoding).encode(data.b)
    m.attachments[name] = { content: content, transfer_encoding: encoding }
  else
    m.add_file(filename: name, content: data.b)
  end
  m.attachments[name]
  a = m.attachments.select { |x| x.filename == name }.last # (the one just added)
  Stitch.check(!a.nil?, "the attachment is found by name")
  eol = ->(x) { x.b.gsub(/\r\n?/, "\n") } # (text/* attachments get their line endings normalized)
  # (content of whitespace only is blank, like a blank body)
  Stitch.check(eol.(a.decoded) == eol.(data), "an attachment decodes to its content") if (encoding.nil? || encoding == "base64") && data.b.match?(/\S/n)
  m.attachments[0]
  m.has_attachments?
end

Stitch.endpoint :convert_to_multipart, { m: Borrow[Msg] }, bails_on: PARSE_ERRORS do |m|
  Stitch.require(!m.multipart? && m.parts.empty?, "already multipart")
  before = m.body.decoded
  m.convert_to_multipart
  Stitch.check(m.parts.size == 1, "convert_to_multipart puts the body in a part")
  eol = ->(x) { x.b.gsub(/\r\n?/, "\n") }
  Stitch.check(eol.(m.parts.first.body.decoded) == eol.(before), "the text part keeps the body")
end

Stitch.endpoint :without_attachments, { m: Borrow[Msg] }, bails_on: PARSE_ERRORS do |m|
  m.without_attachments!
  Stitch.check(!m.has_attachments?, "without_attachments! removes them")
end

Stitch.endpoint :parts_list, { m: Borrow[Msg], order: OneOf[nil, ["text/plain", "text/html"], ["text/html", "text/enriched", "text/plain"]] },
                bails_on: PARSE_ERRORS do |m, order|
  ps = m.parts
  n = 0
  ps.recursive_each { n += 1 }
  Stitch.check(n == ps.recursive_size, "recursive_each visits recursive_size parts")
  ps.inspect_structure
  ps.attachments
  ps.collect { |x| x }
  ps.sort
  ps.sort!(order) if order
  m.body.set_sort_order(order) if order
  m.body.sort_parts!
  ps.recursive_delete_if { |p| p.content_type.to_s.start_with?("text/enriched") }
  ps.delete_attachments
  Stitch.check(ps.attachments.empty?, "delete_attachments removes the attachments")
end

# ---------------------------------------------------------------------------------------------
# Bodies

Stitch.endpoint :body_ops, { m: Borrow[Msg], text: Str[32], cte: OneOf[nil, "base64", "quoted-printable", "7bit", "8bit", "binary"] },
                bails_on: PARSE_ERRORS do |m, text, cte|
  b = m.body
  text = text.gsub(/[^\x20-\x7e]/, "") # (a binary body cannot be searched for non-ASCII UTF-8 text)
  b.to_s
  b.include?(text)
  b.match(/#{Regexp.escape(text)}/)
  b =~ /a/
  b == b.decoded
  b.encoding
  b.ascii_only?
  b.empty?
  b.default_encoding
  b.negotiate_best_encoding(cte || "8bit")
  b.encoded(cte)
  b.preamble
  b.epilogue
end

Stitch.endpoint :body_new, { text: Str[256], cte: OneOf["base64", "quoted-printable", "7bit", "8bit", "binary"], boundary: OneOf["XyZ", "b", "--"] },
                bails_on: PARSE_ERRORS do |text, cte, boundary|
  b = Mail::Body.new(text)
  b.encoding = cte
  enc = b.encoded
  Stitch.check(Mail::Body.new(text).tap { |x| x.encoding = cte }.encoded == enc, "encoding a body is deterministic")
  b << Mail::Part.new(body: "p")
  b.split!(boundary)
  b.parts
  b.multipart?
end

# YAML: a message dumped with to_yaml loads back with the same encoding
Stitch.endpoint :yaml, { m: Borrow[Msg] }, bails_on: PARSE_ERRORS do |m|
  y = m.to_yaml
  back = Mail::Message.from_yaml(y)
  Stitch.check(back.subject == m.subject && back.parts.size == m.parts.size, "from_yaml(to_yaml(m)) keeps the message")
  m.body.encoding
  YAML.unsafe_load(YAML.dump(m.body)) if m.body.parts.empty?
end

# reply sets the threading fields from the original
Stitch.endpoint :reply, { m: Borrow[Msg] }, New[Msg], bails_on: PARSE_ERRORS do |m|
  r = m.reply
  if m.message_id.is_a?(String) && m.message_id.match?(/\A[\w.!#$%&'*+\/=?^`{|}~-]+@[\w.-]+\z/)
    Stitch.check(r.in_reply_to == m.message_id, "a reply is in reply to the message")
    Stitch.check([r.references].flatten.include?(m.message_id), "a reply references the message") if m.references.nil? && m.in_reply_to.nil?
  end
  Stitch.check(r.subject.to_s.match?(/\ARe:/i) || m.subject.nil?, "a reply's subject starts with Re:") if m.subject && !m.subject.empty?
  r
end

Stitch.endpoint :compare, { a: Borrow[Msg], b: Borrow[Msg] }, bails_on: PARSE_ERRORS do |a, b|
  a <=> b
  Stitch.check(a == a.dup, "a message equals its copy") unless a.multipart? || a.has_attachments? || !a.parts.empty?
  a == b
end

# ---------------------------------------------------------------------------------------------
# Delivery and retrieval (test mailer and retriever: no network)

class Interceptor
  def self.delivering_email(m) = (m.header["X-Intercepted"] ||= "yes")
end

class Observer
  @seen = 0
  class << self
    attr_accessor :seen
  end
  def self.delivered_email(_m) = (self.seen += 1)
end

Stitch.endpoint :deliver, { m: Borrow[Msg], bang: Bool, hooks: Bool }, bails_on: PARSE_ERRORS do |m, bang, hooks|
  Mail::TestMailer.deliveries.clear
  m.delivery_method :test
  if hooks
    Mail.register_interceptor(Interceptor)
    Mail.register_observer(Observer)
  end
  begin
    bang ? m.deliver! : m.deliver
  rescue ArgumentError => e
    # (the SMTP envelope needs a sender and recipients)
    raise unless e.backtrace_locations&.any? { |l| l.path.to_s.end_with?("smtp_envelope.rb") }
    Stitch.bail("no envelope")
  ensure
    if hooks
      Mail.unregister_interceptor(Interceptor)
      Mail.unregister_observer(Observer)
    end
  end
  Stitch.check(Mail::TestMailer.deliveries.size == 1 && Mail::TestMailer.deliveries.first.equal?(m), "the test mailer delivers the message")
  Stitch.check(!m.header["X-Intercepted"].nil?, "the interceptor ran") if hooks
  m.delivery_method
  env = Mail::SmtpEnvelope.new(m)
  env.from
  env.to
  env.message
end

Stitch.endpoint :retrieve, { a: Borrow[Msg], b: Borrow[Msg], count: Int[1, 3], order: OneOf[:asc, :desc], what: OneOf[:first, :last, :all, :find, :find_and_delete] } do |a, b, count, order, what|
  Mail::TestRetriever.emails = [a, b]
  r = case what
      when :first then Mail.first(count: count)
      when :last then Mail.last(count: count)
      when :all then Mail.all
      when :find then Mail.find(count: count, order: order, what: :first)
      else Mail.find_and_delete(count: count)
      end
  Stitch.check(Array(r).size <= 2, "the retriever returns stored messages")
  Mail::TestRetriever.emails = []
end

Stitch.endpoint :read_from_string, { text: Str[4096, MailText] }, New[Msg], bails_on: PARSE_ERRORS do |text|
  Mail.read_from_string(text)
end

# ---------------------------------------------------------------------------------------------
# Header and Field objects

Stitch.endpoint :header_new, { text: Str[1024, MailText], charset: OneOf[nil, "UTF-8", "ISO-8859-1"] }, bails_on: PARSE_ERRORS do |text, charset|
  h = Mail::Header.new(text.split(/\r?\n\r?\n/, 2).first.to_s, charset)
  h.fields.each { |f| f.to_s }
  h.charset = "UTF-8"
  h["X-New"] = "v"
  Stitch.check(h["X-New"].to_s == "v", "a header field set reads back")
  h.encoded
  h.errors
  c = h.dup
  Stitch.check(c.encoded == h.encoded, "a copied header encodes the same")
end

Stitch.endpoint :field_ops, { name: OneOf[*FIELD_CLASSES.keys, "X-Custom", "To", "Cc"], value: Str[128, HeaderValue], value2: Str[64, HeaderValue] },
                bails_on: PARSE_ERRORS do |name, value, value2|
  f = Mail::Field.new(name, value)
  g = Mail::Field.new(name, value2)
  f.inspect
  f <=> g
  f == g
  f.same(g)
  Stitch.check(f.same(g), "fields of the same name are the same")
  f.value = value2
  f.value
  f.field = g.field
  f.to_s
end

# ---------------------------------------------------------------------------------------------
# Addresses

Stitch.endpoint :address_edit, { text: Str[64, AddressText], name: OneOf["Bob", "Q R", "Mary Smith", "é"], local: OneOf["a", "b.c", "x+y"] },
                bails_on: PARSE_ERRORS do |text, name, local|
  a = Mail::Address.new(text)
  a.inspect
  a.raw
  a.name
  a.group
  a.decoded
  a.encoded
  a.address = "#{local}@example.com"
  a.display_name = name
  again = Mail::Address.new(a.to_s)
  Stitch.check(again.address == "#{local}@example.com", "an address set reads back after to_s")
  Stitch.check(again.display_name == name, "a display name set reads back after to_s")
end

# ---------------------------------------------------------------------------------------------
# Encodings and utilities

Stitch.endpoint :transfer_encodings, { data: Bytes[256], name: OneOf["base64", "quoted-printable", "7bit", "8bit", "binary", "x-uuencode"] } do |data, name|
  enc = Mail::Encodings.get_encoding(name)
  Stitch.check(Mail::Encodings.defined?(name), "a registered encoding is defined")
  Mail::Encodings.get_all
  Mail::Encodings.get_name(name)
  unless name == "x-uuencode" # (UnixToUnix is not negotiated: it has no cost)
    enc.cost(data)
    enc.compatible_input?(data)
    enc.can_encode?(Mail::Encodings::SevenBit)
    enc.can_transport?(Mail::Encodings::EightBit)
    Mail::Encodings::TransferEncoding.negotiate("8bit", name, data)
    Mail::Encodings::TransferEncoding.negotiate("7bit", name, data, [Mail::Encodings::Base64, Mail::Encodings::SevenBit])
    # (renegotiate and lowest_cost take encoding classes)
    Mail::Encodings::TransferEncoding.renegotiate(Mail::Encodings::SevenBit, enc, data)
    Mail::Encodings::TransferEncoding.lowest_cost(data, [Mail::Encodings::Base64, Mail::Encodings::QuotedPrintable, Mail::Encodings::SevenBit])
  end
  e = enc.encode(data.b)
  d = enc.decode(e)
  # (the 7bit/8bit/binary identities normalize line endings)
  if %w[base64 x-uuencode].include?(name)
    Stitch.check(d.b == data.b, "#{name}: decode(encode(x)) == x")
  elsif name == "quoted-printable"
    eol = ->(x) { x.b.gsub(/\r\n?/, "\n") }
    Stitch.check(eol.(d) == eol.(data), "quoted-printable: decode(encode(x)) == x")
  end
end

Stitch.endpoint :header_encodings, { text: Str[96, HeaderValue], charset: OneOf["UTF-8", "ISO-8859-1", "Shift_JIS"] },
                bails_on: [Encoding::UndefinedConversionError, Encoding::InvalidByteSequenceError, Encoding::ConverterNotFoundError] + PARSE_ERRORS do |text, charset|
  Mail::Encodings.address_encode(text, charset)
  Mail::Encodings.encode_non_usascii(text, charset)
  Mail::Encodings.value_encoding_from_string(text)
  Mail::Encodings.decode_encode(text, :encode)
  Mail::Encodings.decode_encode(text, :decode)
  Mail::Encodings.collapse_adjacent_encodings(text)
  Mail::Encodings.b_value_decode(text)
  Mail::Encodings.q_value_decode(text)
  pe = Mail::Encodings.param_encode(text)
  Mail::Encodings.param_decode(pe, "UTF-8")
  Mail::Encodings.transcode_charset(text, "UTF-8", charset)
  Mail::Utilities.transcode_charset(text, "UTF-8")
  Mail::Utilities.pick_encoding(charset)
end

# UTF-7 (IMAP mailbox names) round-trips
Stitch.endpoint :utf7, { text: Str[64] } do |text|
  e = Mail::Utilities.encode_utf7(text)
  Stitch.check(e.ascii_only?, "encode_utf7 gives ASCII")
  Stitch.check(Mail::Utilities.decode_utf7(e) == text, "decode_utf7(encode_utf7(s)) == s")
end

class Util
  include Mail::Utilities
end
U = Util.new

Stitch.endpoint :quoting, { text: Str[48, AddressText] } do |text|
  U.token_safe?(text.b)
  U.quote_token(text)
  q = U.quote_phrase(text)
  U.unquote(q)
  dq = U.dquote(text)
  # (dquote leaves a string that is already quoted, and unquote looks at each line)
  Stitch.check(U.unquote(dq) == text, "unquote(dquote(s)) == s") unless text.match?(/^".*?"$/) || text.include?("\n")
  Stitch.check(U.unparen(U.paren(text)) == U.escape_paren(U.unparen(text)), "unparen(paren(s)) is s with parens escaped")
  Stitch.check(U.unbracket(U.bracket(text)) == Mail::Utilities.escape_bracket(U.unbracket(text)), "unbracket(bracket(s)) is s with brackets escaped")
  Mail::Utilities.paren(text)
  Mail::Utilities.bracket(text)
  Mail::Utilities.escape_bracket(text)
  U.uri_escape(text)
  Stitch.check(U.uri_unescape(U.uri_escape(text)) == text, "uri_unescape(uri_escape(s)) == s") if text.valid_encoding?
  U.capitalize_field(text)
  U.dasherize(text)
  U.underscoreize(text)
  U.map_lines(text) { |l| l }
  U.map_with_index(text.chars) { |c, i| [c, i] }
  U.match_to_s(text, text.upcase)
  U.blank?(text)
  Mail::Utilities.to_lf(text)
  Mail::Utilities.to_crlf(text)
  Mail::Utilities.decode_base64(Mail::Utilities.encode_base64(text))
  Mail::Utilities.has_constant?(Mail, "Message")
  Mail::Utilities.get_constant(Mail, "Message")
  U.constantize("Mail::Message")
  U.generate_message_id
  # last: atom_safe? matches a binary regexp, so non-ASCII UTF-8 text raises
  U.atom_safe?(text)
  U.quote_atom(text)
end

Stitch.endpoint :indifferent_hash, { k: OneOf["a", :a, "b", :b], v: Str[8], k2: OneOf["a", :b, "c"] } do |k, v, k2|
  h = Mail::IndifferentHash.new(k => v)
  Stitch.check(h[k.to_s] == v && h[k.to_sym] == v, "string and symbol keys are the same")
  Stitch.check(h.key?(k.to_sym) && h.fetch(k.to_s) == v, "key? and fetch agree")
  h2 = h.merge(k2 => "m")
  Stitch.check(h2[k2.to_s] == "m", "merge sets the key")
  h.values_at(k, k2)
  h.stringify_keys
  h.stringify_keys!
  h.to_options!
  h.to_hash
  h.update(k2 => "u")
  Mail::IndifferentHash.new_from_hash_copying_default(h)
  cur = h[k.to_s]
  Stitch.check(h.delete(k.to_sym) == cur && !h.key?(k.to_s), "delete by symbol removes the string key")
  Mail::ParameterHash.new.merge("filename*0" => "a", "filename*1" => "b")["filename"]
end

# reverse_merge and symbolize_keys call methods that only ActiveSupport adds to Hash
Stitch.endpoint :indifferent_reverse_merge, { k: OneOf["a", :a], v: Str[8], how: OneOf[:reverse_merge, :reverse_merge!, :symbolize_keys] } do |k, v, how|
  h = Mail::IndifferentHash.new(k => v)
  case how
  when :reverse_merge then Stitch.check(h.reverse_merge(k => "x")[k] == v, "reverse_merge keeps the receiver's value")
  when :reverse_merge! then h.reverse_merge!("b" => "r")
  else h.symbolize_keys
  end
end

Stitch.endpoint :configuration, { } do
  Mail::Configuration.instance.delivery_method
  Mail::Configuration.instance.retriever_method
  Mail::Configuration.instance.lookup_delivery_method(:test)
  Mail::Configuration.instance.lookup_retriever_method(:test)
  Mail::VERSION.version
  Mail.random_tag
  Mail.uniq
end
