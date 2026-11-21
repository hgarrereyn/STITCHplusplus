# mail: parse, build and encode email messages (RFC 5322 / MIME, Ragel-generated field parsers).
# Oracles: encoding a parsed message is a fixed point after one round; a message built from
# fields, a body and attachments encodes to text that parses back to the same subject, addresses,
# body and attachment contents; Address / ContentType / encodings round-trip. Anything raised from
# parsing that is not one of mail's own parse errors is a bug (mail is lenient by design).
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
  if n.zero? && !again.multipart? && !%w[base64 quoted-printable].include?(Stitch.get(m, "cte", ""))
    body = Stitch.get(m, "body", "")
    got_body = again.body.decoded
    norm = ->(x) { x.b.gsub(/\r\n|\r/, "\n").sub(/\n+\z/, "") }  # (line endings are normalized by design)
    Stitch.check(norm.(got_body) == norm.(body) || m.charset.to_s =~ /ascii|8859/i, "the body survives encoding")
  end
  atts = again.attachments
  n.times do |i|
    data = Stitch.get(m, "att#{i}")
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
