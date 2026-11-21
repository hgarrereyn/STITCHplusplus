# Number helpers (to_fs formats, NumberHelper), numeric extensions, BigDecimal.
NUM_FMT = %i[phone currency percentage delimited rounded human human_size]
NUM_FMT.each do |f|
  register :"number_to_fs_#{f}", { x: Borrow[Num], s: Src[16] } do |x, s|
    Stitch.require(x.is_a?(Integer) || x.is_a?(Float) || x.is_a?(BigDecimal), "not a number with to_fs")
    opts = { precision: s.pick([nil, 0, 2, 10]), significant: s.coin, strip_insignificant_zeros: s.coin,
             delimiter: s.pick([nil, ",", "", " "]), separator: s.pick([nil, ".", ","]), locale: s.pick([:en, :xx]) }.compact
    opts[:unit] = s.pick(["$", "€", ""]) if f == :currency
    opts[:units] = { unit: "", thousand: "K" } if f == :human && s.coin
    x.to_fs(f, opts)
  end
end

NH = ActiveSupport::NumberHelper
%i[number_to_phone number_to_currency number_to_percentage number_to_delimited number_to_rounded number_to_human number_to_human_size].each do |m|
  register :"helper_#{m}", { text: Str[24], s: Src[8] } do |text, s|
    NH.public_send(m, s.coin ? text : text.to_f, precision: s.pick([1, 3, 20]), raise: s.coin)
  end
end

register :number_rounded_oracle, { n: Int[-10**9, 10**9], d: Int[0, 6], p: Int[0, 8] } do |n, d, p|
  x = BigDecimal(n) / BigDecimal(10**d)
  r = NH.number_to_rounded(x, precision: p)
  Stitch.log("#{x.to_s('F')} -> #{r}")
  Stitch.check((BigDecimal(r) - x).abs <= BigDecimal("0.5") / BigDecimal(10**p) + BigDecimal("1e-30"), "number_to_rounded is within half a unit of the last place")
  dl = NH.number_to_delimited(n)
  Stitch.check(dl.delete(",").to_i == n, "number_to_delimited keeps the digits")
  hs = NH.number_to_human_size(n.abs)
  Stitch.check(hs.match?(/\A-?[\d.]+ (Bytes|Byte|KB|MB|GB|TB|PB|EB|ZB)\z/), "human size format (#{hs})")
end

NUMERIC_M = %i[ordinalize ordinal bytes kilobytes megabytes gigabytes terabytes petabytes exabytes seconds minutes hours days weeks fortnights
               in_milliseconds html_safe? to_formatted_s].freeze
NUMERIC_M.each do |m|
  register :"numeric_#{m.to_s.delete('?')}", { x: Borrow[Num] } do |x|
    Stitch.require(x.is_a?(Numeric) && x.respond_to?(m), "not applicable")
    x.public_send(m)
  end
end
register :numeric_multiple_of, { a: Int[-100, 100], b: Int[-10, 10] } do |a, b|
  Stitch.check(a.multiple_of?(b) == (b.zero? ? a.zero? : (a % b).zero?), "multiple_of?")
end
register :bigdecimal_conversions, { text: Str[32] } do |text|
  b = BigDecimal(text)
  Stitch.check(BigDecimal(b.to_s) == b || b.nan?, "BigDecimal to_s round trip")
  b.to_fs
  b.as_json
  b.to_d
end
register :integer_months_years, { n: Int[-10**6, 10**6] } do |n|
  n.months.inspect
  n.years.inspect
  n.multiple_of?(3)
end
