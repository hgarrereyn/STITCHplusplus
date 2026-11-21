# ActiveSupport 7.0: core extensions, inflector, durations, time zones, number helpers, JSON / XML,
# message verifiers and encryptors, caches, SafeBuffer.
#
# Bugs: exceptions other than the ones a method documents for bad arguments (ArgumentError and the
# library's own parse errors are bails); failed checks. Values (`Val`) are Ruby objects of any type.
require "stitch"
require "logger"  # (Rails 7.0 expects it loaded: concurrent-ruby >= 1.3.5 no longer requires it)
require "active_support"
require "active_support/all"
require "active_support/core_ext"
require "bigdecimal"
include Stitch::DSL

Val = Stitch.object("Val")
# typed handles, so stubs for one class are only ever given values of that class
Arr = Stitch.object("Arr")
Hsh = Stitch.object("Hsh")
Num = Stitch.object("Num")
Tm = Stitch.object("Tm")
Dur = Stitch.object("Dur")
Word = Stitch.datatype("Word")
Time.zone_default = Time.find_zone!("UTC")
I18n.enforce_available_locales = false

# documented failures for bad arguments
ARG = [ArgumentError, I18n::ArgumentError, ActiveSupport::Duration::ISO8601Parser::ParsingError, TZInfo::InvalidTimezoneIdentifier,
       Date::Error, JSON::ParserError, JSON::GeneratorError, ActiveSupport::MessageVerifier::InvalidSignature, ActiveSupport::MessageEncryptor::InvalidMessage].freeze

WORDS = %w[person people octopus ox oxen child mouse news equipment status bus axis matrix index sheep fish quiz
           ActiveRecord::Base active_record UserProfile HTMLParser api_v2 user_id x 2fa Ünïcödé café naïve 東京 HTTPS].freeze

Stitch.decoder(Word) do |s|
  case s.choice(6)
  when 0 then s.pick(WORDS)
  when 1 then s.pick(WORDS) + s.pick(%w[s es _id Controller ::Name ies x])
  when 2 then s.token
  when 3 then s.str(12)
  when 4 then "#{s.pick(%w[Foo foo_bar Admin::User HTML])}#{s.pick(['', '::', '_', '-', ' ', '.'])}#{s.pick(WORDS)}"
  else " #{s.pick(WORDS)}  #{s.pick(WORDS)}\t"
  end
end

def make_value(s, depth = 0)
  case s.choice(depth > 1 ? 9 : 12)
  when 0 then s.range(-1000, 1000)
  when 1 then s.pick([0, -0.0, 1.5, 1e20, -1e-10, Float::INFINITY, Float::NAN, 2**70, BigDecimal("1.25"), BigDecimal("-0.001"), Rational(1, 3)])
  when 2 then s.str(12)
  when 3 then s.pick(WORDS)
  when 4 then s.pick([nil, true, false, :sym, "", " ", "\t\n"])
  when 5 then Time.utc(1970 + s.range(0, 100), s.range(1, 12), s.range(1, 28), s.range(0, 23), s.range(0, 59), s.range(0, 59))
  when 6 then Date.new(1900 + s.range(0, 200), s.range(1, 12), s.range(1, 28))
  when 7 then s.range(-1000, 1000).send(s.pick(%i[seconds minutes hours days weeks months years]))
  when 8 then Time.utc(2020, 1, 1).in_time_zone(s.pick(ZONES))
  when 9 then Array.new(s.range(0, 4)) { make_value(s, depth + 1) }
  when 10 then Hash[Array.new(s.range(0, 4)) { [s.pick(["a", :b, "c d", 1, "x[y]"]), make_value(s, depth + 1)] }]
  else ActiveSupport::HashWithIndifferentAccess.new("a" => make_value(s, depth + 1), b: 2)
  end
end

ZONES = ["UTC", "Eastern Time (US & Canada)", "Tokyo", "Kathmandu", "Chatham Is.", "Samoa", "America/St_Johns", "Australia/Lord_Howe",
         "Pacific/Apia", "Europe/Dublin", "Asia/Kolkata", "International Date Line West", "Etc/GMT+12"].freeze

def register(name, spec, out = nil, bails: ARG, &blk)
  Stitch.endpoint(name, spec, out, bails_on: bails, &blk)
end

Stitch.endpoint :value, { s: Src[64] }, New[Val] do |s|
  make_value(s)
end

# typed constructors (so stubs for one class get inputs of that class)
Stitch.endpoint :array_value, { s: Src[64] }, New[Arr] do |s|
  Array.new(s.range(0, 6)) { make_value(s, 1) }
end
Stitch.endpoint :hash_value, { s: Src[64] }, New[Hsh] do |s|
  Hash[Array.new(s.range(0, 5)) { [s.pick(["a", :b, "c d", 1, "x[y]", "user_name"]), make_value(s, 1)] }]
end
Stitch.endpoint :number_value, { s: Src[16] }, New[Num] do |s|
  s.pick([s.range(-10**12, 10**12), s.range(-1000, 1000) / s.pick([1.0, 3.0, 7.0]), BigDecimal("#{s.range(-10**6, 10**6)}.#{s.range(0, 999)}"), s.pick([0, -0.0, 1e300, 2**64])])
end
Stitch.endpoint :time_value, { y: Int[1, 9999], mo: Int[1, 12], d: Int[1, 28], h: Int[0, 23], zone: OneOf[*ZONES], kind: OneOf[:time, :date, :datetime, :twz] }, New[Tm] do |y, mo, d, h, zone, kind|
  case kind
  when :time then Time.utc(y, mo, d, h)
  when :date then Date.new(y, mo, d)
  when :datetime then DateTime.new(y, mo, d, h)
  else Time.find_zone!(zone).local(y, mo, d, h)
  end
end

# narrowing: a value produced by a generic stub, as a typed handle (bails when it has another class)
{ as_array: [Arr, ->(x) { x.is_a?(Array) }], as_hash: [Hsh, ->(x) { x.is_a?(Hash) }],
  as_number: [Num, ->(x) { x.is_a?(Numeric) }], as_duration: [Dur, ->(x) { x.is_a?(ActiveSupport::Duration) }],
  as_time: [Tm, ->(x) { x.is_a?(Time) || x.is_a?(Date) || x.is_a?(ActiveSupport::TimeWithZone) }] }.each do |name, (type, ok)|
  Stitch.endpoint name, { x: Borrow[Val] }, New[type] do |x|
    Stitch.require(ok.call(x), "other class")
    x
  end
end
# widening: a typed value as a generic one (for the stubs that accept any object)
{ array_as_value: Arr, hash_as_value: Hsh, number_as_value: Num, duration_as_value: Dur, time_as_value: Tm }.each do |name, type|
  Stitch.endpoint name, { x: Borrow[type] }, New[Val] do |x|
    x
  end
end
