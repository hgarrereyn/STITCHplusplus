# Durations, Time / Date / DateTime extensions, time zones and TimeWithZone.
DUR_UNITS = %i[seconds minutes hours days weeks fortnights months years].freeze

register :duration, { n: Int[-1000, 1000], u: OneOf[*DUR_UNITS] }, New[Dur] do |n, u|
  n.public_send(u)
end
# ISO 8601 durations built from parts (sign, fractions, week form, time part), with an optional edit
def iso_duration(s)
  return "P#{s.range(-9, 99)}W" if s.coin(0.1)
  t = +(s.coin(0.2) ? "-P" : "P")
  num = -> { s.coin(0.2) ? "#{s.range(0, 99)}#{s.pick(['.', ','])}#{s.range(0, 999)}" : s.range(0, 9999).to_s }
  %w[Y M D].each { |u| t << num.call << u if s.coin(0.5) }
  if s.coin(0.6)
    t << "T"
    %w[H M S].each { |u| t << num.call << u if s.coin(0.5) }
  end
  t
end
register :duration_parse, { s: Src[24], text: Str[24] }, New[Dur] do |s, text|
  iso = iso_duration(s)
  iso = s.coin(0.15) ? iso.insert(s.range(0, iso.size), text) : iso
  ActiveSupport::Duration.parse(iso)
end
register :duration_build, { secs: Int[-100_000_000, 100_000_000] }, New[Dur] do |secs|
  d = ActiveSupport::Duration.build(secs)
  Stitch.check(d.to_i == secs, "Duration.build(secs).to_i == secs")
  d
end
DUR_M = {
  iso8601: ->(d, s) { d.iso8601(precision: s.pick([nil, 0, 3])) },
  parts: ->(d, _) { d.parts },
  in_units: ->(d, _) { [d.in_seconds, d.in_minutes, d.in_hours, d.in_days, d.in_weeks, d.in_months, d.in_years] },
  inspect: ->(d, _) { d.inspect },
  ago: ->(d, _) { d.ago(Time.utc(2020, 2, 29, 12)) },
  since: ->(d, _) { d.since(Time.utc(2020, 2, 29, 12)) },
  from_date: ->(d, _) { d.from_now(Date.new(2021, 1, 31)) rescue d.since(Date.new(2021, 1, 31)) },
  plus: ->(d, s) { d + s.range(-100, 100).public_send(s.pick(DUR_UNITS)) },
  minus: ->(d, s) { d - s.range(-100, 100).public_send(s.pick(DUR_UNITS)) },
  times: ->(d, s) { d * s.pick([0, 1, -2, 1.5, Rational(1, 3)]) },
  divide: ->(d, s) { d / s.pick([1, 2, -3, 0.5, 1.day]) },
  modulo: ->(d, s) { d % s.pick([1, 60, 1.hour, 7.days]) },
  compare: ->(d, s) { d <=> s.pick([0, 60, 1.minute, 1.month]) },
  to_json: ->(d, _) { d.to_json },
  variable: ->(d, _) { d.variable? },
}
DUR_M.each do |m, fn|
  register :"duration_#{m}", { d: Borrow[Dur], s: Src[8] }, New[Val] | nil, bails: ARG + [ZeroDivisionError, FloatDomainError] do |d, s|
    Stitch.require(d.is_a?(ActiveSupport::Duration), "not a duration")
    r = fn.call(d, s)
    r.is_a?(ActiveSupport::Duration) || r.is_a?(Time) || r.is_a?(ActiveSupport::TimeWithZone) ? r : nil
  end
end

register :duration_iso8601_roundtrip, { d: Borrow[Dur] } do |d|
  Stitch.require(d.is_a?(ActiveSupport::Duration), "not a duration")
  text = d.iso8601
  back = ActiveSupport::Duration.parse(text)
  Stitch.log("#{d.inspect} -> #{text} -> #{back.inspect} #{back.parts}")
  Stitch.check(back.parts.reject { |_, v| v.zero? } == d.parts.reject { |_, v| v.zero? } || back.to_i == d.to_i, "Duration.parse(d.iso8601) == d")
end

register :duration_arith_oracle, { n: Int[-500, 500], u: OneOf[*DUR_UNITS], y: Int[1900, 2100], mo: Int[1, 12], day: Int[1, 31], zone: OneOf[*ZONES] } do |n, u, y, mo, day, zone|
  t = Time.find_zone!(zone).local(y, mo, [day, Time.days_in_month(mo, y)].min, 12)
  d = n.public_send(u)
  Stitch.check(((t + d) - d) == t || %i[months years].include?(u) || %i[days weeks fortnights].include?(u), "(t + d) - d == t for fixed-length durations")
  Stitch.check(t.advance(u => n) == t + d || %i[fortnights].include?(u), "advance(unit: n) == t + n.unit")
  Stitch.check(d.since(t) == t + d && d.ago(t) == t - d, "since / ago")
end

TIME_M = %i[beginning_of_day end_of_day beginning_of_hour end_of_hour beginning_of_minute end_of_minute beginning_of_week end_of_week
            beginning_of_month end_of_month beginning_of_quarter end_of_quarter beginning_of_year end_of_year midnight middle_of_day
            yesterday tomorrow next_week prev_week next_month prev_month next_year prev_year next_quarter prev_quarter
            all_day all_week all_month all_quarter all_year on_weekend? on_weekday? today? past? future? seconds_since_midnight
            seconds_until_end_of_day sec_fraction to_fs to_date to_datetime to_time utc? formatted_offset as_json].freeze
TIME_M.each do |m|
  register :"time_#{m.to_s.delete('?')}", { t: Borrow[Tm] } do |t|
    Stitch.require(t.is_a?(Time) || t.is_a?(Date) || t.is_a?(ActiveSupport::TimeWithZone), "not a time")
    Stitch.require(t.respond_to?(m), "no such method on #{t.class}")
    r = t.public_send(m)
    if r.is_a?(Range) && r.begin.respond_to?(:<=)
      Stitch.check(r.begin <= r.end, "#{m}: begin <= end")
    end
    nil
  end
end

TIME_ARGS = {
  advance: ->(t, s) { t.advance(s.pick(%i[years months weeks days hours minutes seconds]) => s.range(-100, 100)) },
  change: ->(t, s) { t.change(s.pick(%i[year month day hour min sec usec]) => s.range(0, 30)) },
  ago: ->(t, s) { t.ago(s.range(-100_000, 100_000)) },
  since: ->(t, s) { t.since(s.range(-100_000, 100_000)) },
  to_fs_format: ->(t, s) { t.to_fs(s.pick(%i[db number long short long_ordinal rfc822 iso8601 nsec usec inspect time])) },
  beginning_of_week_day: ->(t, s) { t.beginning_of_week(s.pick(%i[monday sunday wednesday saturday])) },
  days_ago: ->(t, s) { t.days_ago(s.range(-1000, 1000)) },
  months_since: ->(t, s) { t.months_since(s.range(-1000, 1000)) },
  next_occurring: ->(t, s) { t.next_occurring(s.pick(%i[monday friday sunday])) },
  prev_occurring: ->(t, s) { t.prev_occurring(s.pick(%i[monday friday sunday])) },
  in_time_zone: ->(t, s) { t.in_time_zone(s.pick(ZONES)) },
  compare: ->(t, s) { t <=> t.since(s.range(-10, 10)) },
  round_digits: ->(t, s) { t.respond_to?(:round) ? t.round(s.range(0, 9)) : nil },
  minus_time: ->(t, s) { t - s.range(-100, 100).public_send(s.pick(DUR_UNITS)) },
  xmlschema: ->(t, s) { t.respond_to?(:xmlschema) ? t.xmlschema(s.range(0, 6)) : t.iso8601 },
}
TIME_ARGS.each do |m, fn|
  register :"time_#{m}", { t: Borrow[Tm], s: Src[8] }, New[Val] | nil do |t, s|
    Stitch.require(t.is_a?(Time) || t.is_a?(Date) || t.is_a?(ActiveSupport::TimeWithZone), "not a time")
    r = fn.call(t, s)
    r.is_a?(Time) || r.is_a?(ActiveSupport::TimeWithZone) || r.is_a?(Date) ? r : nil
  end
end

register :time_zone_find, { name: OneOf[*ZONES, "Mars", "UTC+5", "+05:30", "-12", "Asia/Kathmandu", ""] }, New[Val] | nil do |name|
  z = Time.find_zone(name)
  z && z.now.change(year: 2021)
end

register :time_zone_parse, { zone: OneOf[*ZONES], text: Str[48] }, New[Tm] | nil do |zone, text|
  Time.find_zone!(zone).parse(text)
end
register :time_zone_iso8601, { zone: OneOf[*ZONES], text: Str[48] }, New[Tm] | nil do |zone, text|
  Time.find_zone!(zone).iso8601(text)
end
register :time_zone_strptime, { zone: OneOf[*ZONES], text: Str[32], fmt: OneOf["%Y-%m-%d %H:%M:%S", "%d/%m/%Y", "%s", "%Y-%m-%dT%H:%M:%S%z", "%j %Y"] }, New[Val] | nil do |zone, text, fmt|
  Time.find_zone!(zone).strptime(text, fmt)
end
register :time_zone_rfc3339, { zone: OneOf[*ZONES], s: Src[24], text: Str[16] }, New[Tm] | nil do |zone, s, text|
  r = format("%04d-%02d-%02dT%02d:%02d:%02d", s.range(0, 9999), s.range(0, 13), s.range(0, 32), s.range(0, 24), s.range(0, 60), s.range(0, 60))
  r << "." << s.range(0, 999_999_999).to_s if s.coin(0.3)
  r << s.pick(["Z", "z", "+00:00", "-05:30", "+14:00", "+5", ""])
  r = r.insert(s.range(0, r.size), text) if s.coin(0.15)
  Time.find_zone!(zone).rfc3339(r)
end

register :zone_local_oracle, { zone: OneOf[*ZONES], y: Int[1900, 2100], mo: Int[1, 12], d: Int[1, 28], h: Int[0, 23], mi: Int[0, 59] } do |zone, y, mo, d, h, mi|
  z = Time.find_zone!(zone)
  t = z.local(y, mo, d, h, mi)
  Stitch.check(z.at(t.to_i) == t, "zone.at(t.to_i) == t")
  Stitch.check(z.parse(t.iso8601) == t, "zone.parse(t.iso8601) == t")
  Stitch.check(z.iso8601(t.iso8601) == t, "zone.iso8601(t.iso8601) == t")
  Stitch.check(Time.at(t.to_f).utc == t.utc, "to_f round trip")
  Stitch.check(t.utc.in_time_zone(zone) == t, "utc / in_time_zone round trip")
  Stitch.check(t.to_date == Date.new(t.year, t.month, t.day), "to_date uses local fields")
  Stitch.check((t + 1.day).day != t.day || Time.days_in_month(mo, y) == 1, "adding a day changes the day")
  Stitch.check(t.beginning_of_day <= t && t <= t.end_of_day, "a time lies within its day")
  j = ActiveSupport::JSON.decode(t.to_json)
  Stitch.check(z.parse(j) == t.change(usec: 0), "JSON encodes TimeWithZone so it parses back")
end

DATE_M = %i[beginning_of_week end_of_month next_month prev_month yesterday tomorrow next_week to_fs to_time midnight beginning_of_day end_of_day
            all_week all_month next_weekday prev_weekday on_weekday? acts_like_date? xmlschema].freeze
DATE_M.each do |m|
  register :"date_#{m.to_s.delete('?')}", { y: Int[1, 9999], mo: Int[1, 12], d: Int[1, 31] }, New[Val] | nil do |y, mo, d|
    Stitch.require(Date.valid_date?(y, mo, d), "invalid date")
    r = Date.new(y, mo, d).public_send(m)
    r.is_a?(Date) || r.is_a?(Time) ? r : nil
  end
end

register :date_and_time_calculations_oracle, { y: Int[1600, 2400], mo: Int[1, 12], d: Int[1, 31], n: Int[-30, 30] } do |y, mo, d, n|
  Stitch.require(Date.valid_date?(y, mo, d), "invalid date")
  dt = Date.new(y, mo, d)
  Stitch.check(dt.months_since(n).months_ago(n) <= dt, "months_since then months_ago does not move later")
  Stitch.check(dt.beginning_of_month.day == 1 && dt.end_of_month.next_day.day == 1, "month boundaries")
  Stitch.check(dt.beginning_of_week.wday == 1 && dt.end_of_week.wday == 0, "Monday-based weeks")
  Stitch.check(dt.next_weekday.on_weekday? && dt.prev_weekday.on_weekday?, "next/prev weekday are weekdays")
  Stitch.check(dt.all_month.cover?(dt) && dt.all_year.cover?(dt) && dt.all_quarter.cover?(dt), "all_* ranges cover the date")
  Stitch.check((dt.advance(days: n) - dt).to_i == n, "advance(days: n) moves n days")
end

register :datetime_ops, { y: Int[1, 9999], mo: Int[1, 12], d: Int[1, 28], off: Int[-14, 14], s: Src[8] }, New[Val] | nil do |y, mo, d, off, s|
  dt = DateTime.new(y, mo, d, 12, 0, 0, Rational(off, 24))
  r = dt.public_send(s.pick(%i[utc beginning_of_day end_of_hour to_fs seconds_since_midnight in_time_zone getlocal advance localtime]))
  r.is_a?(DateTime) || r.is_a?(Time) ? r : nil
end
