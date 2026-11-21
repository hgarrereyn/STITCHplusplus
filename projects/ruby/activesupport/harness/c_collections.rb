# Array, Hash, Enumerable, Object and Range core extensions.
ARRAY_M = {
  to_sentence: ->(a, s) { a.to_sentence(words_connector: s.pick([", ", " ", ""]), last_word_connector: s.pick([", and ", " & "]), locale: s.pick([:en, :xx])) },
  in_groups: ->(a, s) { a.in_groups(s.range(1, 5), s.pick([nil, false, 0])) },
  in_groups_of: ->(a, s) { a.in_groups_of(s.range(1, 5), s.pick([nil, false, 0])) },
  split: ->(a, s) { a.split(s.pick([nil, 1, "a"])) },
  excluding: ->(a, s) { a.excluding(s.pick([1, nil, "a"])) },
  including: ->(a, s) { a.including(s.pick([1, nil, "a"])) },
  from: ->(a, s) { a.from(s.range(-5, 5)) },
  to: ->(a, s) { a.to(s.range(-5, 5)) },
  second: ->(a, _) { [a.second, a.third, a.fourth, a.fifth, a.forty_two, a.second_to_last, a.third_to_last] },
  extract_options: ->(a, _) { a.dup.extract_options! },
  deep_dup: ->(a, _) { a.deep_dup },
  to_param: ->(a, _) { a.to_param },
  to_query: ->(a, s) { a.to_query(s.pick(["k", "a[b]", ""])) },
  to_fs_format: ->(a, _) { a.to_fs(:default) },  # (:db calls #id on each element: for records)
  inquiry: ->(a, _) { a.inquiry.any?(:a) },
  compact_blank: ->(a, _) { a.compact_blank },
  index_by: ->(a, _) { a.index_by(&:to_s) },
  index_with: ->(a, _) { a.index_with(&:to_s) },
  many: ->(a, _) { a.many? },
  pluck: ->(a, s) { a.select { |x| x.is_a?(Hash) }.pluck(s.pick(["a", :b])) },
  pick: ->(a, s) { a.select { |x| x.is_a?(Hash) }.pick(s.pick(["a", :b])) },
  sum: ->(a, _) { a.select { |x| x.is_a?(Numeric) }.sum },
  in_order_of: ->(a, s) { a.map(&:to_s).in_order_of(:itself, [s.str(2), "a", "1"]) },
  maximum: ->(a, _) { a.select { |x| x.is_a?(Hash) }.maximum("a") },
  sole: ->(a, _) { a.sole },
  exclude: ->(a, s) { a.exclude?(s.pick([1, nil])) },
  as_json: ->(a, _) { a.as_json },
  to_xml: ->(a, _) { a.select { |x| x.is_a?(Hash) }.to_xml },
  wrap: ->(a, _) { Array.wrap(a.first) },
}
ARRAY_M.each do |m, fn|
  register :"array_#{m}", { a: Borrow[Arr], s: Src[16] }, New[Val] | nil, bails: ARG + [Enumerable::SoleItemExpectedError] do |a, s|
    Stitch.require(a.is_a?(Array), "not an array")
    r = fn.call(a, s)
    r.nil? || r == true || r == false ? nil : r
  end
end

HASH_M = {
  deep_merge: ->(h, s) { h.deep_merge(s.pick([{ "a" => { "x" => 1 } }, { b: nil }, {}])) },
  deep_transform_keys: ->(h, _) { h.deep_transform_keys(&:to_s) },
  deep_transform_values: ->(h, _) { h.deep_transform_values(&:to_s) },
  deep_symbolize_keys: ->(h, _) { h.deep_symbolize_keys },
  deep_stringify_keys: ->(h, _) { h.deep_stringify_keys },
  symbolize_keys: ->(h, _) { h.symbolize_keys },
  stringify_keys: ->(h, _) { h.stringify_keys },
  reverse_merge: ->(h, _) { h.reverse_merge("a" => 0, "z" => 1) },
  except: ->(h, s) { h.except(s.pick(["a", :b, 1])) },
  slice: ->(h, s) { h.slice(s.pick(["a", :b, 1])) },
  extract: ->(h, s) { h.dup.extract!(s.pick(["a", :b])) },
  compact_blank: ->(h, _) { h.compact_blank },
  to_query: ->(h, s) { h.to_query(s.pick([nil, "ns"])) },
  to_param: ->(h, _) { h.to_param },
  with_indifferent_access: ->(h, _) { h.with_indifferent_access },
  assert_valid_keys: ->(h, _) { h.assert_valid_keys("a", :b, "c d", 1, "x[y]") },
  deep_dup: ->(h, _) { h.deep_dup },
  as_json: ->(h, _) { h.as_json },
  to_xml: ->(h, s) { h.to_xml(skip_types: s.coin, dasherize: s.coin, root: s.pick(["hash", "r o o t", "x"])) },
  to_json: ->(h, _) { h.to_json },
}
HASH_M.each do |m, fn|
  register :"hash_#{m}", { h: Borrow[Hsh], s: Src[8] }, New[Val] | nil do |h, s|
    Stitch.require(h.is_a?(Hash), "not a hash")
    r = fn.call(h, s)
    r.is_a?(Hash) || r.is_a?(Array) || r.is_a?(String) ? r : nil
  end
end

OBJECT_M = %i[blank? present? presence deep_dup to_param as_json duplicable? html_safe?].freeze
OBJECT_M.each do |m|
  register :"object_#{m.to_s.delete('?')}", { x: Borrow[Val] } do |x|
    x.public_send(m)
  end
end

register :object_try, { x: Borrow[Val], m: OneOf[:to_s, :length, :upcase, :nope, :first, :keys, :to_i] } do |x, m|
  a = x.try(m)
  b = x.respond_to?(m) ? x.public_send(m) : nil
  Stitch.check(a == b || (a.respond_to?(:nan?) && a.nan?), "try(m) == m when the object responds")
end
register :object_acts_like, { x: Borrow[Val], what: OneOf[:time, :date, :string] } do |x, what|
  x.acts_like?(what)
end
# (String#include? takes strings only: in? on a string with another type is a TypeError by design)
register :object_in, { x: Borrow[Val], y: Borrow[Val] }, nil, bails: ARG + [TypeError] do |x, y|
  x.in?(y) if y.respond_to?(:include?)
end
register :object_with_options, { x: Borrow[Val] } do |x|
  x.to_query("k") if x.respond_to?(:to_query)
end

register :blank_consistent, { x: Borrow[Val] } do |x|
  Stitch.check(x.blank? == !x.present?, "blank? == !present?")
  Stitch.check((x.presence.nil?) == x.blank? || x == false, "presence is nil exactly when blank")
end

RANGE_M = {
  overlaps: ->(a, b) { a.overlaps?(b) },
  include_range: ->(a, b) { a.include?(b) },
  sum_range: ->(a, _) { a.sum },
  to_fs: ->(a, _) { a.to_fs(:db) },
  step: ->(a, _) { a.step(2).first(5) },
  each_entry: ->(a, _) { a.first(3) },
}
RANGE_M.each do |m, fn|
  register :"range_#{m}", { lo: Int[-5, 5], hi: Int[-5, 5], lo2: Int[-5, 5], hi2: Int[-5, 5], excl: Bool } do |lo, hi, lo2, hi2, excl|
    fn.call(Range.new(lo, hi, excl), Range.new(lo2, hi2))
  end
end
register :range_overlaps_oracle, { lo: Int[-5, 5], hi: Int[-5, 5], lo2: Int[-5, 5], hi2: Int[-5, 5], e1: Bool, e2: Bool } do |lo, hi, lo2, hi2, e1, e2|
  a = Range.new(lo, hi, e1)
  b = Range.new(lo2, hi2, e2)
  brute = a.to_a.intersect?(b.to_a)
  Stitch.require(a.to_a.any? && b.to_a.any?, "empty range")
  Stitch.check(a.overlaps?(b) == brute, "Range#overlaps? agrees with the elements (#{a} #{b})")
end
