# mail 2.7.1 (large project)

mikel/mail from source on the load path (Ruby 3.4 with YJIT), with mini_mime. Its header field
parsers are Ragel-generated Ruby.

## Harness

- **Parsing.** `parse` (Mail.new on generated messages: headers, folding, encoded words, multipart
  bodies with transfer encodings; seeded from the gem's 90 fixture emails), `inspect` (the accessors
  an application uses: addresses, dates, parts, attachments, decoded bodies), `field` (each structured
  field class on its own), `address`.
- **Oracles.** Encoding a parsed message is a fixed point from the second round on; a message built
  from a subject, sender, body, charset, transfer encoding and attachments encodes to text that parses
  back to the same subject, body and attachment contents; `Address.new(a.to_s).address ==
  a.address`; B / Q encoded words and quoted-printable round-trip.
- **Bugs.** Anything raised from parsing that is not one of mail's parse errors (mail is lenient by
  design and keeps unparsable fields as unstructured).

## Harness fixes during probing

- **Encoded-word syntax in input.** Setting a subject that already contains `=?...?=` is taken as
  encoded by design; such inputs are excluded from the subject and encoding oracles.
- **Folding and line endings** normalize whitespace runs and CR / LF; an explicit base64 or
  quoted-printable transfer encoding marks the body as already encoded.

## Results

22 buckets. Checked at 2.9.1: fixed upstream: a NameError in the generated phrase-list parser,
Date::Error from Date headers in one path, `q_value_encode` leaving `_` unencoded. Still
reproducing: ArgumentError / RangeError from DateTime.parse escaping Mail.new on bad Date /
Resent-Date headers, Encoding::CompatibilityError in Message#encoded, subjects with CR/LF decoding
to a literal `=0D=0A`, and re-encoding that is not a fixed point.
