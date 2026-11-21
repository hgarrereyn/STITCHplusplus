# String core extensions and the inflector.
STRING_1 = %i[squish strip_heredoc pluralize singularize camelize underscore dasherize titleize humanize parameterize tableize classify
              foreign_key demodulize deconstantize upcase_first safe_constantize inquiry html_safe to_date to_time to_datetime
              blank? present? presence mb_chars is_utf8? remove_possible_method?].freeze
STRING_1.each do |m|
  next if m == :remove_possible_method?
  register :"str_#{m.to_s.delete('?')}", { s: Str[64, Word] }, New[Val] | nil do |s|
    r = s.public_send(m)
    r.is_a?(String) || r.is_a?(Array) || r.is_a?(Hash) ? r : nil
  end
end

STRING_ARGS = {
  truncate: ->(s, src) { s.truncate(src.range(0, 40), omission: src.pick(["...", "", "…", "very long omission"]), separator: src.pick([nil, " ", /\s/, ","])) },
  truncate_words: ->(s, src) { s.truncate_words(src.range(0, 8), separator: src.pick([nil, " ", ","])) },
  truncate_bytes: ->(s, src) { s.truncate_bytes(src.range(0, 40), omission: src.pick(["…", "", "..."])) },
  at: ->(s, src) { s.at(src.pick([src.range(-10, 10), src.range(-3, 3)..src.range(-3, 8)])) },
  from: ->(s, src) { s.from(src.range(-10, 10)) },
  to: ->(s, src) { s.to(src.range(-10, 10)) },
  first: ->(s, src) { s.first(src.range(0, 10)) },
  last: ->(s, src) { s.last(src.range(0, 10)) },
  indent: ->(s, src) { s.indent(src.range(0, 4), src.pick([" ", "\t", nil]), src.coin) },
  remove: ->(s, src) { s.remove(src.pick(["a", / +/, "o", ""])) },
  exclude?: ->(s, src) { s.exclude?(src.str(3)) },
  camelize_lower: ->(s, _) { s.camelize(:lower) },
  parameterize_sep: ->(s, src) { s.parameterize(separator: src.pick(["_", "", "--", "."]), preserve_case: src.coin) },
  titleize_keep_id: ->(s, _) { s.titleize(keep_id_suffix: true) },
  humanize_opts: ->(s, src) { s.humanize(capitalize: src.coin, keep_id_suffix: src.coin) },
  pluralize_count: ->(s, src) { s.pluralize(src.range(-2, 3), src.pick([:en, :fr])) },
  to_time_local: ->(s, _) { s.to_time(:local) },
  in_time_zone: ->(s, src) { s.in_time_zone(src.pick(ZONES)) },
  starts_with: ->(s, src) { s.starts_with?(src.str(2)) },
}
STRING_ARGS.each do |m, fn|
  register :"str_#{m.to_s.delete('?')}", { s: Str[64, Word], src: Src[16] }, New[Val] | nil do |s, src|
    r = fn.call(s, src)
    r.is_a?(String) || r.is_a?(Array) ? r : nil
  end
end

INFL = ActiveSupport::Inflector
INFLECTOR = %i[pluralize singularize camelize underscore humanize titleize tableize classify dasherize demodulize deconstantize
               foreign_key ordinal ordinalize transliterate parameterize upcase_first].freeze
INFLECTOR.each do |m|
  register :"inflector_#{m}", { s: Str[64, Word] } do |s|
    arg = %i[ordinal ordinalize].include?(m) ? s.to_i : s
    INFL.public_send(m, arg)
  end
end

register :inflector_oracles, { w: Str[32, Word] } do |w|
  t = INFL.transliterate(w)
  Stitch.check(t.ascii_only?, "transliterate gives ASCII")
  pm = INFL.parameterize(w)
  Stitch.check(pm.match?(/\A[a-z0-9\-_]*\z/), "parameterize gives [a-z0-9-_] (got #{pm.inspect})")
  Stitch.check(!pm.start_with?("-") && !pm.end_with?("-"), "parameterize strips leading and trailing separators")
  if w.match?(/\A[a-z]+(_[a-z]+)*\z/)
    Stitch.check(INFL.underscore(INFL.camelize(w)) == w, "underscore(camelize(w)) == w for snake_case words")
  end
  if w.match?(/\A[a-z]+\z/)
    s = INFL.singularize(INFL.pluralize(w))
    Stitch.check(INFL.pluralize(s) == INFL.pluralize(w), "pluralize(singularize(pluralize(w))) == pluralize(w)")
  end
  Stitch.check(INFL.ordinalize(w.length).start_with?(w.length.to_s), "ordinalize keeps the number")
end

register :inflections_custom, { a: Str[16, Word], b: Str[16, Word] } do |a, b|
  Stitch.require(a.match?(/\A[a-z]+\z/) && b.match?(/\A[a-z]+\z/) && a != b, "two words")
  ActiveSupport::Inflector.inflections(:xx) do |inf|
    inf.clear
    inf.irregular(a, b)
  end
  Stitch.check(a.pluralize(:xx) == b, "an irregular plural is used")
  Stitch.check(b.singularize(:xx) == a, "an irregular singular is used")
end

register :string_inquirer, { s: Str[16, Word], q: Str[16, Word] } do |s, q|
  Stitch.require(q.match?(/\A[a-z_]+\z/), "not a method name")
  Stitch.check(s.inquiry.public_send("#{q}?") == (s == q), "StringInquirer compares")
end

register :safe_buffer, { a: Str[32], b: Str[32] } do |a, b|
  buf = a.html_safe + b
  Stitch.check(buf.html_safe?, "SafeBuffer + String stays html_safe")
  Stitch.check(buf.to_s.end_with?(ERB::Util.html_escape(b)), "the unsafe part is escaped")
  esc = ERB::Util.html_escape(a)
  Stitch.check(!esc.match?(/[<>"']|&(?!amp;|lt;|gt;|quot;|#39;)/), "html_escape leaves no markup characters")
  Stitch.check(ERB::Util.html_escape(esc) == esc, "escaping an html_safe string again changes nothing")
  j = ERB::Util.json_escape(a.to_json)
  Stitch.check(!j.match?(/[<>&]| | /), "json_escape leaves no <, >, & or line separators")
  ERB::Util.url_encode(a)
  ERB::Util.xml_name_escape(a)
end

register :mb_chars_ops, { s: Str[64, Word], src: Src[8] } do |s, src|
  c = s.mb_chars
  c.reverse
  c.limit(src.range(0, 20))
  c.compose
  c.decompose
  c.grapheme_length
  c.tidy_bytes
  c.titleize
end
