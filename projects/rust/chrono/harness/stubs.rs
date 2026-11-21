//! chrono 0.4.19: dates, times, datetimes with offsets, durations, formatting and parsing.
//!
//! Bugs: panics in APIs that report failure through Option / Result (the `_opt` and `checked_`
//! constructors and arithmetic, parsing, formatting, rounding), overflow and debug assertions, and
//! failed checks. Operations documented to panic on overflow (`+` on dates, `Duration::days`
//! beyond its range, `timestamp_nanos` out of range) are only called inside their documented range.
//! Oracles: parse(format(x)) == x for every specifier that determines the value; to_string /
//! FromStr, RFC 2822 / 3339 round trips; Datelike / Timelike getters agree with each other and with
//! the with_* setters; checked arithmetic agrees with the day / second counts.
use chrono::format::{Parsed, StrftimeItems};
use chrono::prelude::*;
use chrono::{Duration, DurationRound, FixedOffset, IsoWeek, NaiveDate, NaiveDateTime, NaiveTime, SubsecRound, Weekday};
use stitch::*;

object!(D, NaiveDate);
object!(T, NaiveTime);
object!(N, NaiveDateTime);
object!(Z, DateTime<FixedOffset>);
object!(U, Duration);
datatype!(DateText);

decoder!(DateText, fn date_text(s: &mut Source) -> String {
    let mut out = String::new();
    while s.more() {
        out += &match s.choice(6) {
            0 => format!("{:04}-{:02}-{:02}", s.range(-9999, 99999), s.range(0, 13), s.range(0, 32)),
            1 => format!("{:02}:{:02}:{:02}", s.range(0, 25), s.range(0, 61), s.range(0, 62)),
            2 => s.pick(&["T", " ", "Z", "+09:00", "-0530", "+24:00", ".123456789", ",5", "UTC", "GMT", "Mon, ", "Thu, 01 Jan 1970 ", " +0000", "PM", "am", "Sun", "December", "W53", "-W01-7"]).to_string(),
            3 => s.str(6),
            4 => s.token(),
            _ => s.range(-1_000_000_000, 1_000_000_000).to_string(),
        };
    }
    out
});

fn date(s: &mut Source) -> Option<NaiveDate> {
    match s.choice(4) {
        0 => NaiveDate::from_ymd_opt(s.range(-300_000, 300_000) as i32, s.range(1, 12) as u32, s.range(1, 31) as u32),
        1 => NaiveDate::from_ymd_opt(s.pick(&[-262144, 262143, 0, 1, 1970, 2000, 2020, 1600, -1]) as i32, s.range(1, 12) as u32, s.range(1, 31) as u32),
        2 => NaiveDate::from_num_days_from_ce_opt(s.range(-100_000_000, 100_000_000) as i32),
        _ => Some(s.pick(&[chrono::naive::MIN_DATE, chrono::naive::MAX_DATE, NaiveDate::from_ymd(2020, 2, 29), NaiveDate::from_ymd(1970, 1, 1)])),
    }
}
fn time(s: &mut Source) -> Option<NaiveTime> {
    NaiveTime::from_hms_nano_opt(s.range(0, 24) as u32, s.range(0, 59) as u32, s.range(0, 60) as u32, s.pick(&[0, 1, 500_000_000, 999_999_999, 1_000_000_000, 1_999_999_999, 123_456_789]) as u32)
}
fn dur(s: &mut Source) -> Duration {
    match s.choice(4) {
        0 => Duration::seconds(s.range(-1_000_000_000, 1_000_000_000)),
        1 => Duration::nanoseconds(s.range(i64::MIN, i64::MAX)),
        2 => Duration::milliseconds(s.range(-(i64::MAX / 1000), i64::MAX / 1000)),
        _ => s.pick(&[Duration::zero(), Duration::max_value(), Duration::min_value(), Duration::days(1), Duration::weeks(-1)]),
    }
}
const WEEK: [Weekday; 7] = [Weekday::Mon, Weekday::Tue, Weekday::Wed, Weekday::Thu, Weekday::Fri, Weekday::Sat, Weekday::Sun];
fn offset(s: &mut Source) -> FixedOffset {
    // (whole minutes: %z and RFC 3339 cannot represent offset seconds)
    FixedOffset::east_opt(s.pick(&[0, 3600, -3600, 19800, -12600, 86340, -86340, 45 * 60, 14 * 3600]) as i32).unwrap()
}

endpoint! {
    fn date_ymd(y: Range<-300000, 300000>, m: Range<0, 13>, d: Range<0, 32>) -> Option<New<D>> {
        let r = NaiveDate::from_ymd_opt(*y as i32, *m as u32, *d as u32);
        if let Some(x) = r {
            check(x.year() == *y as i32 && x.month() == *m as u32 && x.day() == *d as u32, "from_ymd_opt keeps the fields");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_value(s: Src<16>) -> Option<New<D>> {
        date(&mut s).map(New::new)
    }
}

endpoint! {
    fn date_yo(y: Range<-300000, 300000>, o: Range<0, 367>) -> Option<New<D>> {
        let r = NaiveDate::from_yo_opt(*y as i32, *o as u32);
        if let Some(x) = r { check(x.ordinal() == *o as u32 && x.year() == *y as i32, "from_yo_opt keeps the fields"); }
        r.map(New::new)
    }
}

endpoint! {
    fn date_isoywd(y: Range<-300000, 300000>, w: Range<0, 54>, wd: Range<0, 6>) -> Option<New<D>> {
        let wday = WEEK[*wd as usize];
        let r = NaiveDate::from_isoywd_opt(*y as i32, *w as u32, wday);
        if let Some(x) = r {
            let iw = x.iso_week();
            check(iw.year() == *y as i32 && iw.week() == *w as u32 && x.weekday() == wday, "from_isoywd_opt round trips through iso_week");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_weekday_of_month(y: Range<-3000, 3000>, m: Range<0, 13>, wd: Range<0, 6>, n: Range<0, 6>) -> Option<New<D>> {
        let wday = WEEK[*wd as usize];
        let r = NaiveDate::from_weekday_of_month_opt(*y as i32, *m as u32, wday, *n as u8);
        if let Some(x) = r { check(x.weekday() == wday && x.month() == *m as u32 && (x.day() - 1) / 7 + 1 == *n as u32, "the nth weekday of the month"); }
        r.map(New::new)
    }
}

endpoint! {
    fn date_days_ce(n: i32) -> Option<New<D>> {
        let r = NaiveDate::from_num_days_from_ce_opt(n);
        if let Some(x) = r { check(x.num_days_from_ce() == n, "num_days_from_ce round trip"); }
        r.map(New::new)
    }
}

endpoint! {
    fn time_value(s: Src<16>) -> Option<New<T>> {
        time(&mut s).map(New::new)
    }
}

endpoint! {
    fn time_hms(h: Range<0, 25>, m: Range<0, 61>, sec: Range<0, 61>, nano: Range<0, 2000000000>) -> Option<New<T>> {
        let r = NaiveTime::from_hms_nano_opt(*h as u32, *m as u32, *sec as u32, *nano as u32);
        if let Some(t) = r { check(t.hour() == *h as u32 && t.minute() == *m as u32 && t.second() == *sec as u32 && t.nanosecond() == *nano as u32, "from_hms_nano_opt keeps the fields"); }
        r.map(New::new)
    }
}

endpoint! {
    fn time_from_secs(secs: Range<0, 90000>, nano: Range<0, 2000000000>) -> Option<New<T>> {
        let r = NaiveTime::from_num_seconds_from_midnight_opt(*secs as u32, *nano as u32);
        if let Some(t) = r { check(t.num_seconds_from_midnight() == *secs as u32, "num_seconds_from_midnight round trip"); }
        r.map(New::new)
    }
}

endpoint! {
    fn datetime(d: Borrow<D>, t: Borrow<T>) -> New<N> {
        New::new(NaiveDateTime::new(*d, *t))
    }
}

endpoint! {
    fn datetime_timestamp(secs: i64, nano: Range<0, 2000000000>) -> Option<New<N>> {
        let r = NaiveDateTime::from_timestamp_opt(secs, *nano as u32);
        if let Some(x) = r { check(x.timestamp() == secs && x.timestamp_subsec_nanos() == *nano as u32, "from_timestamp_opt round trip"); }
        r.map(New::new)
    }
}

endpoint! {
    fn with_offset(n: Borrow<N>, s: Src<8>) -> Option<New<Z>> {
        let off = offset(&mut s);
        off.from_local_datetime(&*n).single().map(New::new)
    }
}

endpoint! {
    fn utc_timestamp(secs: i64, nano: Range<0, 2000000000>) -> Option<New<Z>> {
        Utc.timestamp_opt(secs, *nano as u32).single().map(|x| New::new(x.with_timezone(&FixedOffset::east(0))))
    }
}

endpoint! {
    fn utc_timestamp_millis(ms: i64) -> Option<New<Z>> {
        let r = Utc.timestamp_millis_opt(ms).single();
        if let Some(x) = r { check(x.timestamp_millis() == ms, "timestamp_millis round trip"); }
        r.map(|x| New::new(x.with_timezone(&FixedOffset::east(0))))
    }
}

endpoint! {
    fn duration_value(s: Src<16>) -> New<U> {
        New::new(dur(&mut s))
    }
}

endpoint! {
    fn duration_weeks(n: i64) -> New<U> {
        require(n.checked_abs().map_or(false, |a| a <= i64::MAX / 604800000), "outside the documented range");
        let d = Duration::weeks(n);
        check(d.num_weeks() == n, "num_weeks() round trips");
        New::new(d)
    }
}

endpoint! {
    fn duration_days(n: i64) -> New<U> {
        require(n.checked_abs().map_or(false, |a| a <= i64::MAX / 86400000), "outside the documented range");
        let d = Duration::days(n);
        check(d.num_days() == n, "num_days() round trips");
        New::new(d)
    }
}

endpoint! {
    fn duration_hours(n: i64) -> New<U> {
        require(n.checked_abs().map_or(false, |a| a <= i64::MAX / 3600000), "outside the documented range");
        let d = Duration::hours(n);
        check(d.num_hours() == n, "num_hours() round trips");
        New::new(d)
    }
}

endpoint! {
    fn duration_minutes(n: i64) -> New<U> {
        require(n.checked_abs().map_or(false, |a| a <= i64::MAX / 60000), "outside the documented range");
        let d = Duration::minutes(n);
        check(d.num_minutes() == n, "num_minutes() round trips");
        New::new(d)
    }
}

endpoint! {
    fn duration_seconds(n: i64) -> New<U> {
        require(n.checked_abs().map_or(false, |a| a <= i64::MAX / 1000), "outside the documented range");
        let d = Duration::seconds(n);
        check(d.num_seconds() == n, "num_seconds() round trips");
        New::new(d)
    }
}

endpoint! {
    fn duration_milliseconds(n: i64) -> New<U> {
        require(n.checked_abs().map_or(false, |a| a <= i64::MAX), "outside the documented range");
        let d = Duration::milliseconds(n);
        check(d.num_milliseconds() == n, "num_milliseconds() round trips");
        New::new(d)
    }
}

endpoint! {
    fn duration_microseconds(n: i64) -> New<U> {
        require(n.checked_abs().map_or(false, |a| a <= i64::MAX), "outside the documented range");
        let d = Duration::microseconds(n);
        check(d.num_microseconds() == Some(n), "num_microseconds() round trips");
        New::new(d)
    }
}

endpoint! {
    fn duration_nanoseconds(n: i64) -> New<U> {
        require(n.checked_abs().map_or(false, |a| a <= i64::MAX), "outside the documented range");
        let d = Duration::nanoseconds(n);
        check(d.num_nanoseconds() == Some(n), "num_nanoseconds() round trips");
        New::new(d)
    }
}

endpoint! {
    fn date_datelike(x: Borrow<D>) {
        let (y, m, d, o) = (x.year(), x.month(), x.day(), x.ordinal());
        check(x.month0() + 1 == m && x.day0() + 1 == d && x.ordinal0() + 1 == o, "0-based getters");
        let leap = NaiveDate::from_ymd_opt(y, 2, 29).is_some();
        let cum = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334][(m - 1) as usize] + d + if leap && m > 2 { 1 } else { 0 };
        check(cum == o, "ordinal from month and day");
        let w = x.weekday();
        check(w.num_days_from_monday() == (x.num_days_from_ce() - 1).rem_euclid(7) as u32, "weekday from num_days_from_ce");
        let iw = x.iso_week();
        check(iw.week() >= 1 && iw.week() <= 53 && (iw.year() - y).abs() <= 1, "iso_week in range");
        let (ce, yy) = x.year_ce();
        check(if ce { yy as i32 == y } else { 1 - yy as i32 == y }, "year_ce");
    }
}

endpoint! {
    fn date_with_year(x: Borrow<D>, v: Range<-300000, 300000>) -> Option<New<D>> {
        let r = x.with_year(*v as i32);
        if let Some(y) = &r {
            check(y.year() == *v as i32, "with_year sets the field");
            check(y.month() == x.month() && y.day() == x.day(), "with_year keeps month and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_with_month(x: Borrow<D>, v: Range<0, 13>) -> Option<New<D>> {
        let r = x.with_month(*v as u32);
        if let Some(y) = &r {
            check(y.month() == *v as u32, "with_month sets the field");
            check(y.year() == x.year() && y.day() == x.day(), "with_month keeps year and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_with_month0(x: Borrow<D>, v: Range<0, 12>) -> Option<New<D>> {
        let r = x.with_month0(*v as u32);
        if let Some(y) = &r {
            check(y.month0() == *v as u32, "with_month0 sets the field");
            check(y.year() == x.year() && y.day() == x.day(), "with_month keeps year and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_with_day(x: Borrow<D>, v: Range<0, 32>) -> Option<New<D>> {
        let r = x.with_day(*v as u32);
        if let Some(y) = &r {
            check(y.day() == *v as u32, "with_day sets the field");
            check(y.year() == x.year() && y.month() == x.month(), "with_day keeps year and month");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_with_day0(x: Borrow<D>, v: Range<0, 31>) -> Option<New<D>> {
        let r = x.with_day0(*v as u32);
        if let Some(y) = &r {
            check(y.day0() == *v as u32, "with_day0 sets the field");
            check(y.year() == x.year() && y.month() == x.month(), "with_day keeps year and month");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_with_ordinal(x: Borrow<D>, v: Range<0, 367>) -> Option<New<D>> {
        let r = x.with_ordinal(*v as u32);
        if let Some(y) = &r {
            check(y.ordinal() == *v as u32, "with_ordinal sets the field");
            
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_with_ordinal0(x: Borrow<D>, v: Range<0, 366>) -> Option<New<D>> {
        let r = x.with_ordinal0(*v as u32);
        if let Some(y) = &r {
            check(y.ordinal0() == *v as u32, "with_ordinal0 sets the field");
            
        }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_datelike(x: Borrow<N>) {
        let (y, m, d, o) = (x.year(), x.month(), x.day(), x.ordinal());
        check(x.month0() + 1 == m && x.day0() + 1 == d && x.ordinal0() + 1 == o, "0-based getters");
        let leap = NaiveDate::from_ymd_opt(y, 2, 29).is_some();
        let cum = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334][(m - 1) as usize] + d + if leap && m > 2 { 1 } else { 0 };
        check(cum == o, "ordinal from month and day");
        let w = x.weekday();
        check(w.num_days_from_monday() == (x.num_days_from_ce() - 1).rem_euclid(7) as u32, "weekday from num_days_from_ce");
        let iw = x.iso_week();
        check(iw.week() >= 1 && iw.week() <= 53 && (iw.year() - y).abs() <= 1, "iso_week in range");
        let (ce, yy) = x.year_ce();
        check(if ce { yy as i32 == y } else { 1 - yy as i32 == y }, "year_ce");
    }
}

endpoint! {
    fn ndt_with_year(x: Borrow<N>, v: Range<-300000, 300000>) -> Option<New<N>> {
        let r = x.with_year(*v as i32);
        if let Some(y) = &r {
            check(y.year() == *v as i32, "with_year sets the field");
            check(y.month() == x.month() && y.day() == x.day(), "with_year keeps month and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_month(x: Borrow<N>, v: Range<0, 13>) -> Option<New<N>> {
        let r = x.with_month(*v as u32);
        if let Some(y) = &r {
            check(y.month() == *v as u32, "with_month sets the field");
            check(y.year() == x.year() && y.day() == x.day(), "with_month keeps year and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_month0(x: Borrow<N>, v: Range<0, 12>) -> Option<New<N>> {
        let r = x.with_month0(*v as u32);
        if let Some(y) = &r {
            check(y.month0() == *v as u32, "with_month0 sets the field");
            check(y.year() == x.year() && y.day() == x.day(), "with_month keeps year and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_day(x: Borrow<N>, v: Range<0, 32>) -> Option<New<N>> {
        let r = x.with_day(*v as u32);
        if let Some(y) = &r {
            check(y.day() == *v as u32, "with_day sets the field");
            check(y.year() == x.year() && y.month() == x.month(), "with_day keeps year and month");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_day0(x: Borrow<N>, v: Range<0, 31>) -> Option<New<N>> {
        let r = x.with_day0(*v as u32);
        if let Some(y) = &r {
            check(y.day0() == *v as u32, "with_day0 sets the field");
            check(y.year() == x.year() && y.month() == x.month(), "with_day keeps year and month");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_ordinal(x: Borrow<N>, v: Range<0, 367>) -> Option<New<N>> {
        let r = x.with_ordinal(*v as u32);
        if let Some(y) = &r {
            check(y.ordinal() == *v as u32, "with_ordinal sets the field");
            
        }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_ordinal0(x: Borrow<N>, v: Range<0, 366>) -> Option<New<N>> {
        let r = x.with_ordinal0(*v as u32);
        if let Some(y) = &r {
            check(y.ordinal0() == *v as u32, "with_ordinal0 sets the field");
            
        }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_datelike(x: Borrow<Z>) {
        let (y, m, d, o) = (x.year(), x.month(), x.day(), x.ordinal());
        check(x.month0() + 1 == m && x.day0() + 1 == d && x.ordinal0() + 1 == o, "0-based getters");
        let leap = NaiveDate::from_ymd_opt(y, 2, 29).is_some();
        let cum = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334][(m - 1) as usize] + d + if leap && m > 2 { 1 } else { 0 };
        check(cum == o, "ordinal from month and day");
        let w = x.weekday();
        check(w.num_days_from_monday() == (x.num_days_from_ce() - 1).rem_euclid(7) as u32, "weekday from num_days_from_ce");
        let iw = x.iso_week();
        check(iw.week() >= 1 && iw.week() <= 53 && (iw.year() - y).abs() <= 1, "iso_week in range");
        let (ce, yy) = x.year_ce();
        check(if ce { yy as i32 == y } else { 1 - yy as i32 == y }, "year_ce");
    }
}

endpoint! {
    fn dt_with_year(x: Borrow<Z>, v: Range<-300000, 300000>) -> Option<New<Z>> {
        let r = x.with_year(*v as i32);
        if let Some(y) = &r {
            check(y.year() == *v as i32, "with_year sets the field");
            check(y.month() == x.month() && y.day() == x.day(), "with_year keeps month and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_month(x: Borrow<Z>, v: Range<0, 13>) -> Option<New<Z>> {
        let r = x.with_month(*v as u32);
        if let Some(y) = &r {
            check(y.month() == *v as u32, "with_month sets the field");
            check(y.year() == x.year() && y.day() == x.day(), "with_month keeps year and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_month0(x: Borrow<Z>, v: Range<0, 12>) -> Option<New<Z>> {
        let r = x.with_month0(*v as u32);
        if let Some(y) = &r {
            check(y.month0() == *v as u32, "with_month0 sets the field");
            check(y.year() == x.year() && y.day() == x.day(), "with_month keeps year and day");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_day(x: Borrow<Z>, v: Range<0, 32>) -> Option<New<Z>> {
        let r = x.with_day(*v as u32);
        if let Some(y) = &r {
            check(y.day() == *v as u32, "with_day sets the field");
            check(y.year() == x.year() && y.month() == x.month(), "with_day keeps year and month");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_day0(x: Borrow<Z>, v: Range<0, 31>) -> Option<New<Z>> {
        let r = x.with_day0(*v as u32);
        if let Some(y) = &r {
            check(y.day0() == *v as u32, "with_day0 sets the field");
            check(y.year() == x.year() && y.month() == x.month(), "with_day keeps year and month");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_ordinal(x: Borrow<Z>, v: Range<0, 367>) -> Option<New<Z>> {
        let r = x.with_ordinal(*v as u32);
        if let Some(y) = &r {
            check(y.ordinal() == *v as u32, "with_ordinal sets the field");
            
        }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_ordinal0(x: Borrow<Z>, v: Range<0, 366>) -> Option<New<Z>> {
        let r = x.with_ordinal0(*v as u32);
        if let Some(y) = &r {
            check(y.ordinal0() == *v as u32, "with_ordinal0 sets the field");
            
        }
        r.map(New::new)
    }
}

endpoint! {
    fn time_timelike(x: Borrow<T>) {
        let (h, m, s) = (x.hour(), x.minute(), x.second());
        check(h < 24 && m < 60 && s < 60, "fields in range");
        check(x.num_seconds_from_midnight() == h * 3600 + m * 60 + s, "num_seconds_from_midnight");
        let (pm, h12) = x.hour12();
        check(h12 >= 1 && h12 <= 12 && pm == (h >= 12) && h12 % 12 == h % 12, "hour12");
    }
}

endpoint! {
    fn time_with_hour(x: Borrow<T>, v: Range<0, 25>) -> Option<New<T>> {
        let r = x.with_hour(*v as u32);
        if let Some(y) = &r { check(y.hour() == *v as u32, "with_hour sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn time_with_minute(x: Borrow<T>, v: Range<0, 61>) -> Option<New<T>> {
        let r = x.with_minute(*v as u32);
        if let Some(y) = &r { check(y.minute() == *v as u32, "with_minute sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn time_with_second(x: Borrow<T>, v: Range<0, 61>) -> Option<New<T>> {
        let r = x.with_second(*v as u32);
        if let Some(y) = &r { check(y.second() == *v as u32, "with_second sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn time_with_nanosecond(x: Borrow<T>, v: Range<0, 2000000000>) -> Option<New<T>> {
        let r = x.with_nanosecond(*v as u32);
        if let Some(y) = &r { check(y.nanosecond() == *v as u32, "with_nanosecond sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_timelike(x: Borrow<N>) {
        let (h, m, s) = (x.hour(), x.minute(), x.second());
        check(h < 24 && m < 60 && s < 60, "fields in range");
        check(x.num_seconds_from_midnight() == h * 3600 + m * 60 + s, "num_seconds_from_midnight");
        let (pm, h12) = x.hour12();
        check(h12 >= 1 && h12 <= 12 && pm == (h >= 12) && h12 % 12 == h % 12, "hour12");
    }
}

endpoint! {
    fn ndt_with_hour(x: Borrow<N>, v: Range<0, 25>) -> Option<New<N>> {
        let r = x.with_hour(*v as u32);
        if let Some(y) = &r { check(y.hour() == *v as u32, "with_hour sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_minute(x: Borrow<N>, v: Range<0, 61>) -> Option<New<N>> {
        let r = x.with_minute(*v as u32);
        if let Some(y) = &r { check(y.minute() == *v as u32, "with_minute sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_second(x: Borrow<N>, v: Range<0, 61>) -> Option<New<N>> {
        let r = x.with_second(*v as u32);
        if let Some(y) = &r { check(y.second() == *v as u32, "with_second sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_with_nanosecond(x: Borrow<N>, v: Range<0, 2000000000>) -> Option<New<N>> {
        let r = x.with_nanosecond(*v as u32);
        if let Some(y) = &r { check(y.nanosecond() == *v as u32, "with_nanosecond sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_timelike(x: Borrow<Z>) {
        let (h, m, s) = (x.hour(), x.minute(), x.second());
        check(h < 24 && m < 60 && s < 60, "fields in range");
        check(x.num_seconds_from_midnight() == h * 3600 + m * 60 + s, "num_seconds_from_midnight");
        let (pm, h12) = x.hour12();
        check(h12 >= 1 && h12 <= 12 && pm == (h >= 12) && h12 % 12 == h % 12, "hour12");
    }
}

endpoint! {
    fn dt_with_hour(x: Borrow<Z>, v: Range<0, 25>) -> Option<New<Z>> {
        let r = x.with_hour(*v as u32);
        if let Some(y) = &r { check(y.hour() == *v as u32, "with_hour sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_minute(x: Borrow<Z>, v: Range<0, 61>) -> Option<New<Z>> {
        let r = x.with_minute(*v as u32);
        if let Some(y) = &r { check(y.minute() == *v as u32, "with_minute sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_second(x: Borrow<Z>, v: Range<0, 61>) -> Option<New<Z>> {
        let r = x.with_second(*v as u32);
        if let Some(y) = &r { check(y.second() == *v as u32, "with_second sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn dt_with_nanosecond(x: Borrow<Z>, v: Range<0, 2000000000>) -> Option<New<Z>> {
        let r = x.with_nanosecond(*v as u32);
        if let Some(y) = &r { check(y.nanosecond() == *v as u32, "with_nanosecond sets the field"); }
        r.map(New::new)
    }
}

endpoint! {
    fn date_checked_add(x: Borrow<D>, d: Borrow<U>) -> Option<New<D>> {
        let r = x.checked_add_signed(*d);
        if let Some(y) = r {
            check(y.signed_duration_since(*x).num_days() == d.num_days(), "the result is num_days away");
            check(y.checked_sub_signed(*d) == Some(*x), "subtracting gives back the date");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn date_checked_sub(x: Borrow<D>, d: Borrow<U>) -> Option<New<D>> {
        x.checked_sub_signed(*d).map(New::new)
    }
}

endpoint! {
    fn date_succ_pred(x: Borrow<D>) {
        if let Some(s) = x.succ_opt() { check(s.pred_opt() == Some(*x), "pred(succ(d)) == d"); check(s.num_days_from_ce() == x.num_days_from_ce() + 1, "succ is the next day"); }
        if let Some(p) = x.pred_opt() { check(p.succ_opt() == Some(*x), "succ(pred(d)) == d"); }
    }
}

endpoint! {
    fn date_iter(x: Borrow<D>, n: Range<0, 10>) {
        let days: Vec<NaiveDate> = x.iter_days().take(*n as usize).collect();
        for w in days.windows(2) { check(w[1].pred_opt() == Some(w[0]), "iter_days yields consecutive days"); }
        let weeks: Vec<NaiveDate> = x.iter_weeks().take(*n as usize).collect();
        for w in weeks.windows(2) { check(w[1].signed_duration_since(w[0]) == Duration::weeks(1), "iter_weeks yields consecutive weeks"); }
    }
}

endpoint! {
    fn ndt_checked_add(x: Borrow<N>, d: Borrow<U>) -> Option<New<N>> {
        let r = x.checked_add_signed(*d);
        if let (Some(y), true) = (r, x.nanosecond() < 1_000_000_000) {
            check(y.signed_duration_since(*x) == *d, "signed_duration_since(x + d, x) == d");
            check(y.checked_sub_signed(*d) == Some(*x), "(x + d) - d == x");
        }
        r.map(New::new)
    }
}

endpoint! {
    fn ndt_checked_sub(x: Borrow<N>, d: Borrow<U>) -> Option<New<N>> {
        x.checked_sub_signed(*d).map(New::new)
    }
}

endpoint! {
    fn dt_checked_add(x: Borrow<Z>, d: Borrow<U>) -> Option<New<Z>> {
        let r = x.checked_add_signed(*d);
        if let (Some(y), true) = (r, x.nanosecond() < 1_000_000_000) { check(y.signed_duration_since(*x) == *d, "signed_duration_since"); }
        r.map(New::new)
    }
}

endpoint! {
    fn time_overflowing_add(x: Borrow<T>, d: Borrow<U>) -> New<T> {
        let (t, secs) = x.overflowing_add_signed(*d);
        check(t.nanosecond() < 2_000_000_000, "nanosecond in range");
        let (t2, _) = t.overflowing_sub_signed(*d);
        if x.nanosecond() < 1_000_000_000 && d.num_seconds().abs() < 1 << 40 { check(t2 == *x, "(t + d) - d == t (mod a day)"); }
        let _ = secs;
        New::new(t)
    }
}

endpoint! {
    fn time_signed_duration(a: Borrow<T>, b: Borrow<T>) {
        let d = a.signed_duration_since(*b);
        check(d.num_seconds().abs() <= 86401, "two times are within a day of each other");
        let (t, _) = b.overflowing_add_signed(d);
        if a.nanosecond() < 1_000_000_000 && b.nanosecond() < 1_000_000_000 { check(t == *a, "b + (a - b) == a"); }
    }
}

endpoint! {
    fn dt_compare(a: Borrow<Z>, b: Borrow<Z>) {
        check((*a < *b) == ((a.timestamp(), a.timestamp_subsec_nanos()) < (b.timestamp(), b.timestamp_subsec_nanos())), "ordering follows the instant");
    }
}

endpoint! {
    fn duration_checked_add(a: Borrow<U>, b: Borrow<U>) -> Option<New<U>> {
        let r = a.checked_add(&*b);
        let exact = a.num_nanoseconds().and_then(|x| b.num_nanoseconds().and_then(|y| x.checked_add(y)));
        if let (Some(r), Some(e)) = (&r, exact) { check(r.num_nanoseconds() == Some(e), "checked_add is exact"); }
        r.map(New::new)
    }
}

endpoint! {
    fn duration_checked_sub(a: Borrow<U>, b: Borrow<U>) -> Option<New<U>> {
        let r = a.checked_sub(&*b);
        let exact = a.num_nanoseconds().and_then(|x| b.num_nanoseconds().and_then(|y| x.checked_sub(y)));
        if let (Some(r), Some(e)) = (&r, exact) { check(r.num_nanoseconds() == Some(e), "checked_sub is exact"); }
        r.map(New::new)
    }
}

endpoint! {
    fn duration_getters(a: Borrow<U>) {
        check(a.num_weeks() == a.num_days() / 7, "num_weeks");
        check(a.num_days() == a.num_hours() / 24, "num_days");
        check(a.num_hours() == a.num_minutes() / 60, "num_hours");
        check(a.num_minutes() == a.num_seconds() / 60, "num_minutes");
        check(a.num_seconds() == a.num_milliseconds() / 1000, "num_seconds");
        if let Some(us) = a.num_microseconds() { check(us / 1000 == a.num_milliseconds(), "num_microseconds"); }
        if let Some(ns) = a.num_nanoseconds() { check(ns / 1000 == a.num_microseconds().unwrap(), "num_nanoseconds"); }
    }
}

endpoint! {
    fn duration_display(a: Borrow<U>) {
        let s = a.to_string();
        check(s.starts_with('P') || s.starts_with("-P"), "ISO 8601 duration format");
    }
}

endpoint! {
    fn duration_std(a: Borrow<U>) {
        match a.to_std() {
            Ok(s) => { check(Duration::from_std(s).ok() == Some(*a), "from_std(to_std(d)) == d"); }
            Err(_) => check(*a < Duration::zero(), "to_std fails only for negative durations"),
        }
    }
}

endpoint! {
    fn duration_neg_abs(a: Borrow<U>) {
        require(*a != Duration::min_value(), "min_value has no negation");
        let n = -*a;
        check(n + *a == Duration::zero(), "d + (-d) == 0");
        check((n.num_milliseconds() == 0) == (a.num_milliseconds() == 0), "negation keeps zero");
    }
}

endpoint! {
    fn duration_mul_div(a: Borrow<U>, k: Range<-1000, 1000>) {
        require(*k != 0, "zero");
        let d = *a / *k as i32;
        check((d.num_milliseconds() - a.num_milliseconds() / *k).abs() <= 1, "d / k");
        if let Some(ns) = a.num_nanoseconds() {
            if let Some(p) = ns.checked_mul(*k) {
                require(p.checked_abs().map_or(false, |x| x < i64::MAX / 2), "too large");
                let m = *a * *k as i32;
                check(m.num_nanoseconds() == Some(p), "d * k is exact");
            }
        }
    }
}

endpoint! {
    fn ndt_timestamps(x: Borrow<N>) {
        let t = x.timestamp();
        check(NaiveDateTime::from_timestamp_opt(t, x.timestamp_subsec_nanos()) == Some(*x), "from_timestamp_opt(timestamp()) == x");
        check(x.timestamp_millis() == t * 1000 + x.timestamp_subsec_millis() as i64, "timestamp_millis");
        check(x.timestamp_subsec_micros() == x.timestamp_subsec_nanos() / 1000, "subsec micros");
        if t.abs() < 9_000_000_000 { check(x.timestamp_nanos() == t * 1_000_000_000 + x.timestamp_subsec_nanos() as i64, "timestamp_nanos"); }
    }
}

endpoint! {
    fn dt_offsets(x: Borrow<Z>, s: Src<8>) {
        let off = offset(&mut s);
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let y = x.with_timezone(&off);
        check(y == *x, "with_timezone keeps the instant");
        check(y.timestamp() == x.timestamp(), "timestamp is offset-independent");
        check(y.naive_utc() == x.naive_utc(), "naive_utc is offset-independent");
        check(y.naive_local() == y.naive_utc() + Duration::seconds(off.local_minus_utc() as i64) || y.naive_utc().checked_add_signed(Duration::seconds(off.local_minus_utc() as i64)).is_none(), "naive_local == naive_utc + offset");
        check(y.offset().fix() == off, "the offset is kept");
    }
}

endpoint! {
    fn ndt_round_subsecs(x: Borrow<N>, digits: Range<0, 12>) -> New<N> {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let r = x.round_subsecs(*digits as u16);
        let unit = 10u32.pow(9 - (*digits as u32).min(9));
        check(r.nanosecond() % unit == 0 || r.nanosecond() >= 1_000_000_000, "round_subsecs leaves no digits beyond the precision");
        check((r.signed_duration_since(*x)).num_nanoseconds().map_or(true, |d| d.abs() <= unit as i64 / 2 + 1), "rounding moves at most half a unit");
        New::new(r)
    }
}

endpoint! {
    fn ndt_trunc_subsecs(x: Borrow<N>, digits: Range<0, 12>) -> New<N> {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let r = x.trunc_subsecs(*digits as u16);
        let unit = 10u32.pow(9 - (*digits as u32).min(9));
        check(r.nanosecond() % unit == 0 || r.nanosecond() >= 1_000_000_000, "trunc_subsecs leaves no digits beyond the precision");
        check(r <= *x, "truncation does not move later");
        New::new(r)
    }
}

endpoint! {
    fn dt_duration_round(x: Borrow<Z>, s: Src<8>) -> Option<New<Z>> {
        let span = s.pick(&[Duration::seconds(1), Duration::minutes(15), Duration::hours(1), Duration::days(1), Duration::milliseconds(7), Duration::nanoseconds(3), Duration::weeks(1)]);
        match x.duration_round(span) {
            Ok(r) => {
                let d = r.signed_duration_since(*x);
                check(d.num_nanoseconds().map_or(true, |n| n.abs() * 2 <= span.num_nanoseconds().unwrap()), "rounding moves at most half the span");
                if r.timestamp().abs() < 9_000_000_000 { check((r.timestamp_nanos() % span.num_nanoseconds().unwrap()) == 0, "the result is a multiple of the span"); }
                Some(New::new(r))
            }
            Err(_) => None,
        }
    }
}

endpoint! {
    fn dt_duration_trunc(x: Borrow<Z>, s: Src<8>) -> Option<New<Z>> {
        let span = s.pick(&[Duration::seconds(1), Duration::minutes(15), Duration::hours(1), Duration::days(1), Duration::milliseconds(7), Duration::nanoseconds(3), Duration::weeks(1)]);
        match x.duration_trunc(span) {
            Ok(r) => {
                let d = r.signed_duration_since(*x);
                check(d <= Duration::zero() && -d < span, "truncation moves back less than the span");
                if r.timestamp().abs() < 9_000_000_000 { check((r.timestamp_nanos() % span.num_nanoseconds().unwrap()) == 0, "the result is a multiple of the span"); }
                Some(New::new(r))
            }
            Err(_) => None,
        }
    }
}

endpoint! {
    fn format_parse_0(x: Borrow<N>) {
        // %Y-%m-%d
        let fmt = "%Y-%m-%d";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_1(x: Borrow<N>) {
        // %Y-%m-%d %H:%M:%S
        let fmt = "%Y-%m-%d %H:%M:%S";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_2(x: Borrow<N>) {
        // %Y-%m-%dT%H:%M:%S%.f
        let fmt = "%Y-%m-%dT%H:%M:%S%.f";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_3(x: Borrow<N>) {
        // %Y-%m-%dT%H:%M:%S%.3f
        let fmt = "%Y-%m-%dT%H:%M:%S%.3f";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_4(x: Borrow<N>) {
        // %Y-%m-%dT%H:%M:%S%.9f
        let fmt = "%Y-%m-%dT%H:%M:%S%.9f";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_5(x: Borrow<N>) {
        // %G-W%V-%u
        let fmt = "%G-W%V-%u";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_6(x: Borrow<N>) {
        // %Y-%j
        let fmt = "%Y-%j";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_7(x: Borrow<N>) {
        // %d/%m/%Y %I:%M:%S %p
        let fmt = "%d/%m/%Y %I:%M:%S %p";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_8(x: Borrow<N>) {
        // %A, %d %B %Y
        let fmt = "%A, %d %B %Y";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_9(x: Borrow<N>) {
        // %a %b %e %T %Y
        let fmt = "%a %b %e %T %Y";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_10(x: Borrow<N>) {
        // %F %T
        let fmt = "%F %T";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_11(x: Borrow<N>) {
        // %D %r
        let fmt = "%D %r";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_12(x: Borrow<N>) {
        // %Y%m%d%H%M%S
        let fmt = "%Y%m%d%H%M%S";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_13(x: Borrow<N>) {
        // %C%y-%m-%d
        let fmt = "%C%y-%m-%d";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_14(x: Borrow<N>) {
        // %s
        let fmt = "%s";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_15(x: Borrow<N>) {
        // %Y-%m-%d %H:%M:%S.%f
        let fmt = "%Y-%m-%d %H:%M:%S.%f";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_16(x: Borrow<N>) {
        // %+
        let fmt = "%+";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_17(x: Borrow<N>) {
        // %c
        let fmt = "%c";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_18(x: Borrow<N>) {
        // %x %X
        let fmt = "%x %X";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_19(x: Borrow<N>) {
        // %h %d %Y %R
        let fmt = "%h %d %Y %R";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_20(x: Borrow<N>) {
        // %e-%b-%Y
        let fmt = "%e-%b-%Y";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_21(x: Borrow<N>) {
        // %-d/%-m/%Y %-H:%M
        let fmt = "%-d/%-m/%Y %-H:%M";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_22(x: Borrow<N>) {
        // %_m/%_d/%Y
        let fmt = "%_m/%_d/%Y";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_23(x: Borrow<N>) {
        // %Y-%U-%w
        let fmt = "%Y-%U-%w";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_24(x: Borrow<N>) {
        // %Y-%W-%u
        let fmt = "%Y-%W-%u";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if let Ok(d) = back_date { check(d == x.date() || fmt.contains("%U") || fmt.contains("%W"), "parsed date == the date"); }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_25(x: Borrow<N>) {
        // %Y.%m.%d %H:%M:%S%.6f
        let fmt = "%Y.%m.%d %H:%M:%S%.6f";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn format_parse_26(x: Borrow<N>) {
        // %v %T
        let fmt = "%v %T";
        let s = x.format(fmt).to_string();
        log(&format!("{} -> {}", *x, s));
        let back = NaiveDateTime::parse_from_str(&s, fmt);
        let back_date = NaiveDate::parse_from_str(&s, fmt);
        let exact_seconds = x.nanosecond() == 0 || fmt.contains("%.f") || fmt.contains("%.9f") || fmt.contains("%+");
        let in_range = x.year() >= 0 && x.year() <= 9999;
        if in_range && x.nanosecond() < 1_000_000_000 {
            if exact_seconds && !fmt.contains("%.3f") && !fmt.contains("%.6f") { match back { Ok(b) => check(b == *x || fmt.contains("%U") || fmt.contains("%W"), "parse_from_str(format(x)) == x"), Err(e) => check(fmt.contains("%U") || fmt.contains("%W") || fmt.contains("%c") || fmt.contains("%x"), &format!("format output parses back: {}", e)) } }
        }
        let _ = (back, back_date);
    }
}

endpoint! {
    fn parse_date_from_str(text: Str<64, DateText>, fmt: Str<32>) {
        let r = NaiveDate::parse_from_str(&text, &fmt);
        if let Ok(v) = r { let _ = v.format(&fmt).to_string(); }
    }
}

endpoint! {
    fn parse_date_fromstr(text: Str<64, DateText>) {
        let r: Result<NaiveDate, _> = text.parse();
        if let Ok(v) = r {
            let s = v.to_string();
            let again: Result<NaiveDate, _> = s.parse();
            check(again.as_ref().ok() == Some(&v), &format!("FromStr(to_string()) round trips: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_from_str(text: Str<64, DateText>, fmt: Str<32>) {
        let r = NaiveTime::parse_from_str(&text, &fmt);
        if let Ok(v) = r { let _ = v.format(&fmt).to_string(); }
    }
}

endpoint! {
    fn parse_time_fromstr(text: Str<64, DateText>) {
        let r: Result<NaiveTime, _> = text.parse();
        if let Ok(v) = r {
            let s = v.to_string();
            let again: Result<NaiveTime, _> = s.parse();
            check(again.as_ref().ok() == Some(&v), &format!("FromStr(to_string()) round trips: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_from_str(text: Str<64, DateText>, fmt: Str<32>) {
        let r = NaiveDateTime::parse_from_str(&text, &fmt);
        if let Ok(v) = r { let _ = v.format(&fmt).to_string(); }
    }
}

endpoint! {
    fn parse_ndt_fromstr(text: Str<64, DateText>) {
        let r: Result<NaiveDateTime, _> = text.parse();
        if let Ok(v) = r {
            let s = v.to_string();
            let again: Result<NaiveDateTime, _> = s.parse();
            check(again.as_ref().ok() == Some(&v), &format!("FromStr(to_string()) round trips: {}", s));
        }
    }
}

endpoint! {
    fn parse_dt_from_str(text: Str<64, DateText>, fmt: Str<32>) {
        let r = DateTime::parse_from_str(&text, &fmt);
        if let Ok(v) = r { let _ = v.format(&fmt).to_string(); }
    }
}

endpoint! {
    fn parse_dt_fromstr(text: Str<64, DateText>) {
        let r: Result<DateTime<FixedOffset>, _> = text.parse();
        if let Ok(v) = r {
            let s = v.to_string();
            let again: Result<DateTime<FixedOffset>, _> = s.parse();
            check(again.as_ref().ok() == Some(&v), &format!("FromStr(to_string()) round trips: {}", s));
        }
    }
}

endpoint! {
    fn parse_rfc3339(text: Str<64, DateText>) {
        if let Ok(d) = DateTime::parse_from_rfc3339(&text) {
            let s = d.to_rfc3339();
            check(DateTime::parse_from_rfc3339(&s).ok() == Some(d), &format!("rfc3339 round trip: {}", s));
            for sf in &[SecondsFormat::Secs, SecondsFormat::Millis, SecondsFormat::Micros, SecondsFormat::Nanos, SecondsFormat::AutoSi] {
                let s2 = d.to_rfc3339_opts(*sf, true);
                check(DateTime::parse_from_rfc3339(&s2).is_ok(), "to_rfc3339_opts output parses");
            }
        }
    }
}

endpoint! {
    fn parse_rfc2822(text: Str<64, DateText>) {
        if let Ok(d) = DateTime::parse_from_rfc2822(&text) {
            let s = d.to_rfc2822();
            check(DateTime::parse_from_rfc2822(&s).ok() == Some(d.with_nanosecond(0).unwrap_or(d)), &format!("rfc2822 round trip: {}", s));
        }
    }
}

endpoint! {
    fn dt_rfc_roundtrips(x: Borrow<Z>) {
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside the RFC year range / leap second");
        let s = x.to_rfc3339();
        check(DateTime::parse_from_rfc3339(&s).ok() == Some(*x), &format!("to_rfc3339 parses back: {}", s));
        let s2 = x.to_rfc2822();
        check(DateTime::parse_from_rfc2822(&s2).map(|d| d.timestamp()).ok() == Some(x.timestamp()), &format!("to_rfc2822 parses back: {}", s2));
    }
}

endpoint! {
    fn strftime_items(fmt: Str<32>, x: Borrow<N>) {
        let items: Vec<_> = StrftimeItems::new(&fmt).collect();
        let has_error = items.iter().any(|i| matches!(i, chrono::format::Item::Error));
        if !has_error { let _ = x.format(&fmt).to_string(); }
    }
}

endpoint! {
    fn parsed_fields(s: Src<32>) {
        let mut p = Parsed::new();
        let _ = p.set_year(s.range(-10000, 10000));
        let _ = p.set_month(s.range(0, 13));
        let _ = p.set_day(s.range(0, 32));
        if s.coin(0.5) { let _ = p.set_ordinal(s.range(0, 367)); }
        if s.coin(0.3) { let _ = p.set_isoyear(s.range(-10000, 10000)); let _ = p.set_isoweek(s.range(0, 54)); }
        let _ = p.set_hour(s.range(0, 25));
        let _ = p.set_minute(s.range(0, 61));
        let _ = p.set_second(s.range(0, 61));
        if s.coin(0.3) { let _ = p.set_timestamp(s.range(-1 << 40, 1 << 40)); }
        if s.coin(0.3) { let _ = p.set_offset(s.range(-90000, 90000)); }
        let _ = p.to_naive_date();
        let _ = p.to_naive_time();
        let _ = p.to_naive_datetime_with_offset(0);
        let _ = p.to_fixed_offset();
        let _ = p.to_datetime();
    }
}

endpoint! {
    fn weekday_ops(n: Range<0, 13>, text: Str<12>) {
        if let Some(&w) = WEEK.get(*n as usize) {
            check(w.succ().pred() == w && w.pred().succ() == w, "succ / pred");
            check(w.number_from_monday() == w.num_days_from_monday() + 1, "number_from_monday");
            check(w.number_from_sunday() == w.num_days_from_sunday() + 1, "number_from_sunday");
            let s = w.to_string();
            check(s.parse::<Weekday>().ok() == Some(w), "Weekday FromStr(to_string())");
        }
        let _ = text.parse::<Weekday>();
        let _ = text.parse::<chrono::Month>();
    }
}

endpoint! {
    fn iso_week_ops(x: Borrow<D>) {
        let w: IsoWeek = x.iso_week();
        if let Some(back) = NaiveDate::from_isoywd_opt(w.year(), w.week(), x.weekday()) { check(back == *x, "from_isoywd_opt(iso_week(d)) == d"); }
        let _ = format!("{:?}", w);
    }
}

endpoint! {
    fn offset_ops(secs: Range<-100000, 100000>) {
        let e = FixedOffset::east_opt(*secs as i32);
        let w = FixedOffset::west_opt(-*secs as i32);
        check(e == w, "east(s) == west(-s)");
        if let Some(o) = e {
            check(o.local_minus_utc() == *secs as i32 && o.utc_minus_local() == -*secs as i32, "local_minus_utc");
            let s = o.to_string();
            check(s.len() >= 6, "offset formats as +HH:MM");
        }
    }
}
