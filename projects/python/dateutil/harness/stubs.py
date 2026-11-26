"""python-dateutil: date parsing, relative deltas, recurrence rules, time zones.

Oracles: isoparse(dt.isoformat()) == dt, parse() reads back strftime() output, dt2 +
relativedelta(dt1, dt2) == dt1, rrule queries (after/before/between/xafter/[]/in/count) agree
with iterating the rule, str(rule) parses back to the same rule, a zone's UTC round trip keeps
the wall time, and tzfile/gettz agree with the standard library's zoneinfo.
"""
import datetime
import io
import itertools
import math
import warnings
import zoneinfo

import stitch
from dateutil import easter, parser, relativedelta, rrule, tz, utils
from stitch import Bool, Borrow, Bytes, F64, I64, New, OneOf, Range, Src, Str, Val

Dt = stitch.object("Dt")
Delta = stitch.object("Delta")
Rule = stitch.object("Rule")          # an rrule or an rruleset
Zone = stitch.object("Zone")
DateText = stitch.datatype("DateText")
IsoText = stitch.datatype("IsoText")      # ISO 8601 dates, times and offsets
IsoDate = stitch.datatype("IsoDate")
IsoTime = stitch.datatype("IsoTime")
IsoTz = stitch.datatype("IsoTz")
RRuleText = stitch.datatype("RRuleText")
RuleOpts = stitch.datatype("RuleOpts")
TzText = stitch.datatype("TzText")    # POSIX TZ strings
TZif = stitch.datatype("TZif")        # compiled zone files: harness/data/TZif/seeds
ICalText = stitch.datatype("ICalText")

FREQS = (rrule.YEARLY, rrule.MONTHLY, rrule.WEEKLY, rrule.DAILY, rrule.HOURLY, rrule.MINUTELY, rrule.SECONDLY)
UTC = datetime.timezone.utc


@stitch.decoder(DateText)
def date_text(s):
    parts = []
    while s.more():
        c = s.choice(5)
        if c == 0:
            parts.append(str(s.range(0, 9999)))
        elif c == 1:
            parts.append(s.token())
        elif c == 2:
            parts.append(f"{s.range(0, 30):02d}:{s.range(0, 70):02d}" + (f":{s.range(0, 70):02d}" if s.coin() else ""))
        elif c == 3:
            parts.append(s.pick(["Jan", "February", "Mar", "Sep", "Mon", "tuesday", "am", "PM", "UTC", "EST", "Z"]))
        else:
            parts.append(s.str(4))
    return s.pick([" ", "-", "/", "T", ", "]).join(parts)


def iso_date(s):
    y = f"{s.range(0, 9999):04d}"
    k = s.choice(5)
    if k == 0:
        return f"{y}-{s.range(0, 13):02d}-{s.range(0, 32):02d}"
    if k == 1:
        return f"{y}{s.range(1, 12):02d}{s.range(1, 31):02d}"
    if k == 2:
        return f"{y}-W{s.range(0, 54):02d}" + (f"-{s.range(0, 8)}" if s.coin() else "")
    if k == 3:
        return f"{y}-{s.range(0, 367):03d}"
    return f"{y}-{s.range(1, 12):02d}" if s.coin() else y


def iso_time(s):
    t = f"{s.range(0, 24):02d}"
    sep = ":" if s.coin(0.7) else ""
    if s.coin(0.8):
        t += f"{sep}{s.range(0, 60):02d}"
        if s.coin(0.7):
            t += f"{sep}{s.range(0, 60):02d}"
            if s.coin(0.4):
                t += s.pick([".", ","]) + str(s.range(0, 10**s.range(1, 9)))
    return t


def iso_tz(s):
    k = s.choice(4)
    if k == 0:
        return "Z"
    sign = s.pick(["+", "-"])
    if k == 1:
        return f"{sign}{s.range(0, 24):02d}"
    if k == 2:
        return f"{sign}{s.range(0, 24):02d}:{s.range(0, 60):02d}"
    return f"{sign}{s.range(0, 24):02d}{s.range(0, 60):02d}"


@stitch.decoder(IsoText)
def iso_text(s):
    k = s.choice(5)
    if k == 0:
        return iso_date(s)
    if k == 1:
        return iso_time(s) + (iso_tz(s) if s.coin(0.3) else "")
    if k == 2:
        return iso_tz(s)
    return iso_date(s) + s.pick(["T", " ", "t"]) + iso_time(s) + (iso_tz(s) if s.coin(0.5) else "")


@stitch.decoder(IsoDate)
def iso_date_text(s):
    return iso_date(s)


@stitch.decoder(IsoTime)
def iso_time_text(s):
    return iso_time(s)


@stitch.decoder(IsoTz)
def iso_tz_text(s):
    return iso_tz(s)


@stitch.decoder(RRuleText)
def rrule_text(s):
    rules = [f"FREQ={s.pick(['YEARLY', 'MONTHLY', 'WEEKLY', 'DAILY', 'HOURLY', 'MINUTELY'])}"]
    while s.more():
        k = s.choice(10)
        if k == 0:
            rules.append(f"COUNT={s.range(0, 50)}")
        elif k == 1:
            rules.append(f"INTERVAL={s.range(0, 10)}")
        elif k == 2:
            rules.append("BYDAY=" + ",".join(s.pick(["MO", "TU", "WE", "+1FR", "-1SU", "2SA", "-5TH"]) for _ in range(s.range(1, 3))))
        elif k == 3:
            rules.append(f"BYMONTHDAY={s.range(-40, 40)}")
        elif k == 4:
            rules.append(f"BYSETPOS={s.range(-400, 400)}")
        elif k == 5:
            rules.append(f"BYWEEKNO={s.range(-60, 60)}")
        elif k == 6:
            rules.append(f"BYYEARDAY={s.range(-400, 400)}")
        elif k == 7:
            rules.append(f"UNTIL={s.range(1990, 2030)}{s.range(1, 12):02d}{s.range(1, 28):02d}" + s.pick(["", "T000000", "T120000Z"]))
        elif k == 8:
            rules.append(f"{s.pick(['BYHOUR', 'BYMINUTE', 'BYSECOND', 'BYMONTH', 'BYEASTER'])}={s.range(-3, 60)}")
        else:
            rules.append(s.token())
    head = ""
    if s.coin(0.7):
        head = s.pick(["DTSTART:19970902T090000\n", "DTSTART;TZID=America/New_York:19970902T090000\n",
                       "DTSTART:19970902T090000Z\n"]) + "RRULE:"
    text = head + ";".join(rules)
    while s.coin(0.2):
        text += "\n" + s.pick(["RDATE:19970904T090000", "EXDATE:19970909T090000", "EXRULE:FREQ=WEEKLY;COUNT=3",
                               "RRULE:FREQ=DAILY;COUNT=2", "RDATE;VALUE=DATE:19971224", " ;INTERVAL=2"])
    return text


@stitch.decoder(RuleOpts)
def rule_opts(s):
    kw = {}
    while s.more():
        k = s.choice(12)
        if k == 0:
            kw["bymonth"] = [s.range(1, 12) for _ in range(s.range(1, 3))]
        elif k == 1:
            kw["bymonthday"] = [s.range(-31, 31) or 1 for _ in range(s.range(1, 3))]
        elif k == 2:
            kw["byyearday"] = [s.range(-366, 366) or 1 for _ in range(s.range(1, 3))]
        elif k == 3:
            kw["byweekno"] = [s.range(-53, 53) or 1 for _ in range(s.range(1, 3))]
        elif k == 4:
            kw["byweekday"] = [rrule.weekday(s.range(0, 6), s.pick([None, 1, -1, 2, -2, 5, -53]))
                               for _ in range(s.range(1, 3))]
        elif k == 5:
            kw["byeaster"] = [s.range(-10, 10) for _ in range(s.range(1, 2))]
        elif k == 6:
            kw["byhour"] = [s.range(0, 23) for _ in range(s.range(1, 3))]
        elif k == 7:
            kw["byminute"] = [s.range(0, 59) for _ in range(s.range(1, 3))]
        elif k == 8:
            kw["bysecond"] = [s.range(0, 59) for _ in range(s.range(1, 3))]
        elif k == 9:
            kw["bysetpos"] = [s.range(-366, 366) or 1 for _ in range(s.range(1, 2))]
        elif k == 10:
            kw["wkst"] = s.range(0, 6)
        else:
            kw["interval"] = s.range(1, 10)
    return kw


@stitch.decoder(TzText)
def tz_text(s):
    def name():
        return s.pick(["EST", "EDT", "UTC", "CET", "CEST", "<+0330>", "<-03>", "AEST", s.str(4)])

    def offset():
        return s.pick(["", "+", "-"]) + str(s.range(0, 25)) + (f":{s.range(0, 60):02d}" if s.coin(0.3) else "")

    def rule():
        k = s.choice(3)
        if k == 0:
            r = f"M{s.range(1, 12)}.{s.range(1, 5)}.{s.range(0, 6)}"
        elif k == 1:
            r = f"J{s.range(1, 365)}"
        else:
            r = str(s.range(0, 365))
        return r + (f"/{s.pick(['2', '02:00:00', '-1', '25', '167'])}" if s.coin(0.4) else "")

    text = name() + offset()
    if s.coin(0.7):
        text += name() + (offset() if s.coin(0.4) else "")
        if s.coin(0.8):
            text += f",{rule()},{rule()}"
    return text


# ---------------------------------------------------------------------------------------------
# parsing


def whole_minute_offset(dt):
    off = dt.utcoffset() if isinstance(dt, datetime.datetime) else None
    return off is None or (off.seconds % 60 == 0 and off.microseconds == 0)


@stitch.endpoint(bails_on=(parser.ParserError, OverflowError))
def parse(text: Str[64, DateText], dayfirst: Bool, yearfirst: Bool, fuzzy: Bool) -> New[Dt]:
    return parser.parse(text, dayfirst=dayfirst, yearfirst=yearfirst, fuzzy=fuzzy)


TZINFOS = {"EST": -18000, "XYZ": tz.gettz("Asia/Kolkata"), "BRST": tz.tzoffset("BRST", -10800), "Z9": 0}


@stitch.endpoint(bails_on=(parser.ParserError, OverflowError))
def parse_options(text: Str[64, DateText], default: Borrow[Dt], dayfirst: Bool, yearfirst: Bool, ignoretz: Bool,
                  tzinfos: OneOf[["dict", "callable", None]]) -> New[Dt]:
    info = parser.parserinfo(dayfirst=dayfirst, yearfirst=yearfirst)
    tzi = {"dict": TZINFOS, "callable": lambda name, off: TZINFOS.get(name), None: None}[tzinfos]
    out = parser.parser(info).parse(text, default=default, ignoretz=ignoretz, tzinfos=tzi)
    if ignoretz:
        stitch.check(out.tzinfo is None or out.tzinfo is default.tzinfo, "ignoretz drops the time zone")
    return out


@stitch.endpoint(bails_on=(parser.ParserError, OverflowError))
def parse_fuzzy_tokens(text: Str[64, DateText]) -> New[Dt]:
    out, tokens = parser.parse(text, fuzzy_with_tokens=True)
    stitch.check(isinstance(tokens, tuple), "skipped tokens come as a tuple")
    return out


FORMATS = ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%dT%H:%M:%S%z", "%d %B %Y %H:%M:%S", "%a, %d %b %Y %H:%M:%S %z",
           "%Y%m%dT%H%M%S", "%m/%d/%Y %I:%M:%S %p", "%B %d, %Y %H:%M", "%Y-%m-%d")


@stitch.endpoint(bails_on=(OverflowError,))
def parse_roundtrip(dt: Borrow[Dt], fmt: OneOf[FORMATS]):
    """parse() reads back what strftime() wrote."""
    stitch.require(dt.year >= 1000, "years below 1000 print without padding")
    stitch.require(whole_minute_offset(dt), "offset with seconds")
    aware = "%z" in fmt and dt.utcoffset() is not None
    text = dt.strftime(fmt)
    try:
        got = parser.parse(text)
    except parser.ParserError as e:
        stitch.check(False, f"parse() reads strftime({fmt!r}) output ({e})")
        return
    want = dt.replace(tzinfo=None)
    if "%f" not in fmt:
        want = want.replace(microsecond=0)
    if "%S" not in fmt:
        want = want.replace(second=0)
    if "%H" not in fmt and "%I" not in fmt:
        want = want.replace(hour=0, minute=0)
    stitch.check(got.replace(tzinfo=None) == want, f"parse(strftime({fmt!r})) keeps the fields")
    if aware:
        stitch.check(got.utcoffset() == dt.utcoffset(), "parse(strftime(%z)) keeps the offset")


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def isoparse(text: Str[40, IsoText]) -> New[Dt]:
    return parser.isoparse(text)


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def parse_isodate(text: Str[16, IsoDate]):
    p = parser.isoparser()
    d = p.parse_isodate(text)
    stitch.check(p.parse_isodate(d.isoformat()) == d, "parse_isodate(isoformat()) round trip")


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def parse_isotime(text: Str[24, IsoTime], sep: OneOf[[None, "T", " "]]):
    p = parser.isoparser(sep=sep)
    t = p.parse_isotime(text)
    stitch.check(p.parse_isotime(t.isoformat()) == t, "parse_isotime(isoformat()) round trip")


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def parse_tzstr(text: Str[8, IsoTz], zero_as_utc: Bool):
    z = parser.isoparser().parse_tzstr(text, zero_as_utc=zero_as_utc)
    stitch.check(z.utcoffset(None) is not None, "parse_tzstr() gives a fixed offset")


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def make_datetime(year: Range[1, 9999], month: Range[1, 12], day: Range[1, 31], hour: Range[0, 23],
                  minute: Range[0, 59], micro: Range[0, 999999]) -> New[Dt]:
    return datetime.datetime(year, month, day, hour, minute, 0, micro)


@stitch.endpoint
def iso_roundtrip(dt: Borrow[Dt]):
    text = dt.isoformat()
    try:
        again = parser.isoparse(text)
    except ValueError as e:
        stitch.check(whole_minute_offset(dt) is False, f"isoparse(isoformat()) raises ({e})")
        return
    stitch.check(again == dt and again.utcoffset() == dt.utcoffset(), "isoparse(isoformat()) == dt")
    p = parser.isoparser()
    stitch.check(p.parse_isodate(dt.date().isoformat()) == dt.date(), "parse_isodate(date.isoformat())")
    stitch.check(p.parse_isotime(dt.time().isoformat()) == dt.time(), "parse_isotime(time.isoformat())")


@stitch.endpoint
def isoformat_variants(dt: Borrow[Dt], sep: OneOf[["T", " "]], strict_sep: Bool,
                       timespec: OneOf[["auto", "hours", "minutes", "seconds", "milliseconds", "microseconds"]]):
    stitch.require(whole_minute_offset(dt), "offset with seconds")
    text = dt.isoformat(sep=sep, timespec=timespec)
    try:
        again = parser.isoparser(sep=sep if strict_sep else None).isoparse(text)
    except ValueError as e:
        stitch.check(False, f"isoparse reads isoformat(timespec={timespec}) ({e})")
        return
    want = dt
    if timespec in ("hours", "minutes", "seconds", "milliseconds"):
        want = want.replace(microsecond=want.microsecond // 1000 * 1000 if timespec == "milliseconds" else 0)
    if timespec in ("hours", "minutes"):
        want = want.replace(second=0)
    if timespec == "hours":
        want = want.replace(minute=0)
    stitch.check(again.replace(tzinfo=None) == want.replace(tzinfo=None) and again.utcoffset() == want.utcoffset(),
                 "isoparse(isoformat(timespec)) == truncated dt")


# ---------------------------------------------------------------------------------------------
# relativedelta


@stitch.endpoint(bails_on=(ValueError,))
def make_delta(years: Range[-100, 100], months: Range[-1000, 1000], days: Range[-100000, 100000],
               hours: Range[-10000, 10000], weekday: Range[0, 6], nth: Range[-6, 6], leapdays: Range[-3, 3]) -> New[Delta]:
    wd = relativedelta.weekday(weekday, nth or None)
    return relativedelta.relativedelta(years=years, months=months, days=days, hours=hours, weekday=wd, leapdays=leapdays)


@stitch.endpoint(bails_on=(ValueError,))
def make_delta_absolute(year: Range[0, 9999], month: Range[0, 12], day: Range[0, 31], hour: Range[-1, 23],
                        minute: Range[-1, 59], second: Range[-1, 59], microsecond: Range[-1, 999999],
                        yearday: Range[0, 400], nlyear: Bool, weekday: Range[-1, 6]) -> New[Delta]:
    kw = {}
    for name, v in (("year", year), ("month", month), ("day", day)):
        if v:
            kw[name] = v
    for name, v in (("hour", hour), ("minute", minute), ("second", second), ("microsecond", microsecond)):
        if v >= 0:
            kw[name] = v
    if yearday:
        kw["nlyearday" if nlyear else "yearday"] = yearday
    if weekday >= 0:
        kw["weekday"] = relativedelta.weekdays[weekday]
    return relativedelta.relativedelta(**kw)


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def make_delta_float(weeks: F64, days: F64, hours: F64, minutes: F64, seconds: F64, microseconds: F64) -> New[Delta]:
    stitch.require(all(map(math.isfinite, (weeks, days, hours, minutes, seconds, microseconds))), "not finite")
    return relativedelta.relativedelta(weeks=weeks, days=days, hours=hours, minutes=minutes, seconds=seconds,
                                       microseconds=microseconds)


@stitch.endpoint(bails_on=(ValueError, OverflowError, TypeError))  # TypeError: naive vs aware
def delta_between(a: Borrow[Dt], b: Borrow[Dt]) -> New[Delta]:
    d = relativedelta.relativedelta(a, b)
    if a.tzinfo is b.tzinfo:
        try:
            back = b + d
        except (OverflowError, ValueError):
            return d
        stitch.check(back == a, "dt2 + relativedelta(dt1, dt2) == dt1")
    return d


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def add_delta(dt: Borrow[Dt], d: Borrow[Delta]) -> New[Dt]:
    return dt + d


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def sub_delta(dt: Borrow[Dt], d: Borrow[Delta], as_date: Bool) -> New[Dt]:
    if as_date:
        out = dt.date() - d
        if not isinstance(out, datetime.datetime):
            out = datetime.datetime.combine(out, datetime.time())
        return out
    out = dt - d
    stitch.check(out == dt + (-d), "dt - d == dt + (-d)")
    return out


@stitch.endpoint(bails_on=(ValueError, OverflowError, ZeroDivisionError))
def delta_ops(a: Borrow[Delta], b: Borrow[Delta], factor: Range[-100, 100], x: F64,
              op: OneOf["add", "sub", "mul", "rmul", "fmul", "div", "neg", "normalized", "abs"]) -> New[Delta]:
    if op == "add":
        out = a + b
        stitch.check(out == b + a or a.weekday != b.weekday, "delta addition commutes")
        return out
    if op == "sub":
        return a - b
    if op == "mul":
        return a * factor
    if op == "rmul":
        out = factor * a
        stitch.check(out == a * factor, "n * d == d * n")
        return out
    if op == "fmul":
        return a * x
    if op == "div":
        return a / factor
    if op == "neg":
        out = -a
        stitch.check(-out == a, "--d == d")
        return out
    if op == "abs":
        return abs(a)
    out = a.normalized()
    stitch.check(out.normalized() == out, "normalized() is idempotent")
    return out


@stitch.endpoint(bails_on=(OverflowError,))   # weeks of a delta with a huge float day count
def delta_inspect(d: Borrow[Delta], weeks: Range[-1000, 1000]):
    repr(d)
    bool(d)
    stitch.check((d == relativedelta.relativedelta()) == (not d), "bool(d) is False only for an empty delta")
    old = d.days
    d.weeks = weeks
    if isinstance(old, int) and 0 <= old < 2**50 and weeks >= 0:   # weeks truncates toward zero (via float)
        stitch.check(d.weeks == weeks, "the weeks setter sets weeks")


# ---------------------------------------------------------------------------------------------
# rrule and rruleset


def bounded(r, n, limit=0.1):
    """The first n occurrences, or a bail when the rule takes too long to produce them."""
    with stitch.time_limit(limit):  # rules that never match iterate until year 9999
        return list(itertools.islice(r, n))


def comparable(r, dt):
    """A datetime that can be compared with the rule's occurrences (same awareness)."""
    first = bounded(r, 1)
    stitch.require(first, "rule has no occurrences")
    stitch.require((first[0].tzinfo is None) == (dt.tzinfo is None), "naive/aware mix")
    return first


@stitch.endpoint(bails_on=(ValueError, TypeError))
def make_rule(freq: OneOf[FREQS], start: Borrow[Dt], count: Range[0, 60], interval: Range[1, 20],
              bymonthday: Range[-31, 31], byweekday: Range[-1, 6], bysetpos: Range[-366, 366]) -> New[Rule]:
    kw = {}
    if bymonthday:
        kw["bymonthday"] = bymonthday
    if byweekday >= 0:
        kw["byweekday"] = byweekday
    if bysetpos:
        kw["bysetpos"] = bysetpos
    return rrule.rrule(freq, dtstart=start, count=count or 20, interval=interval, cache=bool(count % 2), **kw)


@stitch.endpoint(bails_on=(ValueError, TypeError))
def make_rule_full(freq: OneOf[FREQS], start: Borrow[Dt], until: Borrow[Dt], use_until: Bool, count: Range[0, 60],
                   opts: Val[RuleOpts, 128], cache: Bool) -> New[Rule]:
    kw = dict(opts)
    if use_until:
        kw["until"] = until
    elif count:
        kw["count"] = count
    return rrule.rrule(freq, dtstart=start, cache=cache, **kw)


@stitch.endpoint(bails_on=(ValueError, TypeError))
def rrulestr(text: Str[160, RRuleText], forceset: Bool) -> New[Rule]:
    return rrule.rrulestr(text, forceset=forceset)


@stitch.endpoint(bails_on=(ValueError, TypeError))
def rrulestr_options(text: Str[256, RRuleText], dtstart: Borrow[Dt], use_dtstart: Bool, unfold: Bool,
                     compatible: Bool, ignoretz: Bool, cache: Bool,
                     tzids: OneOf[[None, "dict", "callable"]]) -> New[Rule]:
    tzi = {None: None, "dict": {"America/New_York": tz.gettz("Europe/Paris")},
           "callable": lambda name: tz.gettz("Asia/Tokyo")}[tzids]
    return rrule.rrulestr(text, dtstart=dtstart if use_dtstart else None, unfold=unfold, compatible=compatible,
                          ignoretz=ignoretz, cache=cache, tzids=tzi)


@stitch.endpoint(bails_on=(ValueError, TypeError))
def rule_replace(r: Borrow[Rule], start: Borrow[Dt], field: OneOf[["dtstart", "interval", "count", "freq",
                                                                    "bymonthday", "byhour"]], n: Range[1, 31]) -> New[Rule]:
    stitch.require(isinstance(r, rrule.rrule), "not an rrule")
    value = {"dtstart": start, "interval": n, "count": n, "freq": FREQS[n % len(FREQS)],
             "bymonthday": n, "byhour": n % 24}[field]
    kw = {field: value}
    if field == "count":
        kw["until"] = None
    return r.replace(**kw)


@stitch.endpoint(bails_on=(TypeError,))
def rule_iterate(r: Borrow[Rule], n: Range[1, 40]):
    out = bounded(r, n)
    stitch.check(out == sorted(out), "occurrences are in order")
    if isinstance(r, rrule.rruleset):
        stitch.check(len(set(out)) == len(out), "a rule set yields each occurrence once")
        ex = stitch.get(r, "exdates", [])
        stitch.check(not any(x in ex for x in out), "excluded dates do not occur")


@stitch.endpoint(bails_on=(TypeError,))
def rule_after(r: Borrow[Rule], dt: Borrow[Dt], inc: Bool):
    with stitch.time_limit(0.1):  # rules that never match iterate until year 9999
        try:
            got = r.after(dt, inc=inc)
        except TypeError:
            stitch.bail("naive/aware mix")
        first = next((x for x in itertools.islice(r, 500) if (x >= dt if inc else x > dt)), None)
        stitch.require(first is not None or len(list(itertools.islice(r, 501))) <= 500, "beyond 500 occurrences")
    stitch.check(got == first, "after() agrees with iteration")


@stitch.endpoint(bails_on=(TypeError,))
def rule_before(r: Borrow[Rule], dt: Borrow[Dt], inc: Bool):
    comparable(r, dt)
    with stitch.time_limit(0.1):
        got = r.before(dt, inc=inc)
        head = list(itertools.islice(r, 501))
    stitch.require(len(head) <= 500 or head[-1] > dt, "beyond 500 occurrences")
    want = None
    for x in head:
        if x > dt or (x == dt and not inc):
            break
        want = x
    stitch.check(got == want, "before() agrees with iteration")


@stitch.endpoint(bails_on=(TypeError,))
def rule_between(r: Borrow[Rule], a: Borrow[Dt], b: Borrow[Dt], inc: Bool):
    comparable(r, a)
    stitch.require((a.tzinfo is None) == (b.tzinfo is None), "naive/aware mix")
    with stitch.time_limit(0.1):
        got = r.between(a, b, inc=inc)
        head = list(itertools.islice(r, 501))
    stitch.require(len(head) <= 500 or head[-1] >= b, "beyond 500 occurrences")
    want = [x for x in head if (a <= x <= b if inc else a < x < b)]
    stitch.check(got == want, "between() agrees with iteration")


@stitch.endpoint(bails_on=(TypeError,))
def rule_xafter(r: Borrow[Rule], dt: Borrow[Dt], count: Range[0, 10], inc: Bool):
    comparable(r, dt)
    with stitch.time_limit(0.1):
        got = list(itertools.islice(r.xafter(dt, count=count or None, inc=inc), 20))
        want = [x for x in itertools.islice(r, 500) if (x >= dt if inc else x > dt)][:count or 20]
    stitch.require(len(want) == (count or 20) or len(bounded(r, 501)) <= 500, "beyond 500 occurrences")
    stitch.check(got == want, "xafter() agrees with iteration")


@stitch.endpoint(bails_on=(TypeError,))
def rule_index(r: Borrow[Rule], i: Range[-5, 30], j: Range[-5, 30], step: OneOf[[None, 1, 2, 3]]):
    if i >= 0 and step is None:
        head = bounded(r, i + 1)
        with stitch.time_limit(0.1):
            try:
                got = r[i]
            except IndexError:
                stitch.check(len(head) <= i, "r[i] exists when iteration reaches it")
                return
        stitch.check(got == head[i], "r[i] agrees with iteration")
        stitch.check(got in r, "an occurrence is in the rule")
    else:
        stitch.require(i >= 0 and j >= 0 or len(bounded(r, 501)) <= 500, "negative index on a long rule")
        with stitch.time_limit(0.1):
            got = r[i:j:step]
            stitch.check(got == list(r)[i:j:step], "r[i:j:k] agrees with list(r)[i:j:k]")


@stitch.endpoint(bails_on=(TypeError,))
def rule_contains(r: Borrow[Rule], dt: Borrow[Dt]):
    comparable(r, dt)
    head = bounded(r, 500)
    with stitch.time_limit(0.1):
        got = dt in r
    if dt in head:
        stitch.check(got, "an occurrence is in the rule")
    elif len(head) < 500 or (head and dt < head[-1]):
        stitch.check(not got, "a date that iteration does not reach is not in the rule")


@stitch.endpoint(bails_on=(TypeError,))
def rule_count(r: Borrow[Rule]):
    head = bounded(r, 501)
    stitch.require(len(head) <= 500, "beyond 500 occurrences")
    with stitch.time_limit(0.1):
        stitch.check(r.count() == len(head), "count() agrees with iteration")


@stitch.endpoint(bails_on=(TypeError,))
def rule_str_roundtrip(r: Borrow[Rule]):
    stitch.require(isinstance(r, rrule.rrule), "not an rrule")
    stitch.require(r._dtstart.tzinfo is None, "str() drops the time zone")
    text = str(r)
    stitch.log("str: %r", text)
    try:
        again = rrule.rrulestr(text)
    except ValueError as e:
        stitch.check(False, f"rrulestr(str(rule)) parses ({e})")
        return
    stitch.check(bounded(again, 30) == bounded(r, 30), "rrulestr(str(rule)) has the same occurrences")


@stitch.endpoint
def new_ruleset(cache: Bool) -> New[Rule]:
    return rrule.rruleset(cache=cache)


@stitch.endpoint(bails_on=(TypeError,))
def set_add(st: Borrow[Rule], r: Borrow[Rule], dt: Borrow[Dt], what: OneOf[["rrule", "rdate", "exrule", "exdate"]]):
    stitch.require(isinstance(st, rrule.rruleset), "not a rule set")
    if what == "rrule" or what == "exrule":
        stitch.require(isinstance(r, rrule.rrule), "only plain rules go in a set")
        getattr(st, what)(r)
    elif what == "rdate":
        st.rdate(dt)
    else:
        st.exdate(dt)
        stitch.set(st, "exdates", stitch.get(st, "exdates", []) + [dt])


# ---------------------------------------------------------------------------------------------
# time zones


IANA = []
NAMES = ("UTC", "America/New_York", "Europe/London", "Asia/Kolkata", "Australia/Lord_Howe", "EST5EDT", "local",
         "EST5EDT,M3.2.0,M11.1.0", "UTC+3", ":America/Sao_Paulo", "Nowhere/Bogus", "", "GMT-14", "../etc/passwd")


@stitch.init
def setup():
    IANA.extend(sorted(tz.available_iana_timezones()))


@stitch.endpoint(bails_on=(ValueError,))
def gettz(name: OneOf[NAMES], nocache: Bool) -> New[Zone]:
    if name == "local":
        z = tz.tzlocal()
    else:
        with warnings.catch_warnings():   # names that leave TZPATH are deprecated (DeprecatedTzKeyWarning)
            warnings.simplefilter("ignore", FutureWarning)
            z = tz.gettz.nocache(name) if nocache else tz.gettz(name)
    stitch.require(z is not None, "unknown zone")
    return z


@stitch.endpoint
def gettz_iana(s: Src[4], clear: Bool) -> New[Zone]:
    stitch.require(IANA, "no zone data")
    name = s.pick(IANA)
    if clear:
        tz.gettz.cache_clear()
    z = tz.gettz(name)
    stitch.check(z is not None, "every available zone loads")
    stitch.check(tz.gettz(name) is z, "gettz() caches zones")
    stitch.set(z, "ref", zoneinfo.ZoneInfo(name))
    return z


@stitch.endpoint
def fixed_zone(name: OneOf[["UTC", "X", None, ""]], seconds: Range[-86399, 86399], utc: Bool) -> New[Zone]:
    if utc:
        z = tz.tzutc()
        stitch.check(z is tz.UTC, "tzutc() is the UTC singleton")
        return z
    z = tz.tzoffset(name, seconds)
    stitch.check(z.utcoffset(None) == datetime.timedelta(seconds=seconds), "tzoffset has its offset")
    stitch.check(z is tz.tzoffset(name, seconds), "tzoffset instances are cached")
    return z


@stitch.endpoint(bails_on=(ValueError,))
def make_tzrange(stdoff: Range[-50400, 50400], dst: Bool, dstoff: Range[-50400, 50400], use_dstoff: Bool,
                 start_month: Range[1, 12], start_nth: OneOf[[1, 2, -1]], end_month: Range[1, 12],
                 rules: Bool) -> New[Zone]:
    kw = {}
    if dst:
        kw["dstabbr"] = "DST"
        if use_dstoff:
            # dst() is the difference of the two offsets, and datetime rejects a dst() of a day or more
            stitch.require(abs(dstoff - stdoff) < 86400, "offsets a day apart")
            kw["dstoffset"] = dstoff
        if rules:
            kw["start"] = relativedelta.relativedelta(hours=+2, month=start_month, day=1,
                                                      weekday=relativedelta.SU(start_nth))
            kw["end"] = relativedelta.relativedelta(hours=+1, month=end_month, day=31, weekday=relativedelta.SU(-1))
    z = tz.tzrange("STD", stdoff, **kw)
    if dst:
        z.transitions(2000)
    return z


@stitch.endpoint(bails_on=(ValueError,))
def tzstr(text: Str[48, TzText], posix_offset: Bool) -> New[Zone]:
    z = tz.tzstr(text, posix_offset=posix_offset)
    z.transitions(2024)
    return z


@stitch.endpoint(bails_on=(ValueError,))
def tzfile_bytes(data: Bytes[4096, TZif]) -> New[Zone]:
    z = tz.tzfile(io.BytesIO(data), filename="fuzz")
    try:
        ref = zoneinfo.ZoneInfo.from_file(io.BytesIO(data))
    except Exception:
        ref = None
    if ref is not None:
        stitch.set(z, "ref", ref)
    stitch.set(z, "fuzzed", True)
    return z


@stitch.endpoint(bails_on=(ValueError,))
def tzical(text: Str[2048, ICalText], s: Src[4]) -> New[Zone]:
    ical = tz.tzical(io.StringIO(text))
    keys = ical.keys()
    stitch.require(keys, "no zones")
    z = ical.get(s.pick(keys)) if len(keys) > 1 or s.coin() else ical.get()
    stitch.check(z is not None, "a listed zone is there")
    return z


@stitch.endpoint(bails_on=(OverflowError,))
def zone_inspect(z: Borrow[Zone], dt: Borrow[Dt], fold: Range[0, 1]):
    """A wall time that exists survives the round trip through UTC."""
    d = tz.enfold(dt.replace(tzinfo=z), fold=fold)
    stitch.check(d.fold == fold, "enfold() sets fold")
    off = d.utcoffset()
    d.dst()
    d.tzname()
    amb = tz.datetime_ambiguous(d)
    stitch.check(amb == z.is_ambiguous(d.replace(tzinfo=None)) if hasattr(z, "is_ambiguous") else True,
                 "datetime_ambiguous() agrees with is_ambiguous()")
    stitch.require(off is not None, "no offset")
    if tz.datetime_exists(d):
        try:
            back = d.astimezone(tz.UTC).astimezone(z)
        except (OverflowError, ValueError):
            stitch.bail("out of range")
        stitch.check(back.replace(tzinfo=None) == d.replace(tzinfo=None), "UTC round trip keeps the wall time")
        stitch.check(back.utcoffset() == off, "UTC round trip keeps the offset")
    try:
        r = tz.resolve_imaginary(d)
    except (OverflowError, ValueError):
        stitch.bail("out of range")
    if not stitch.get(z, "fuzzed", False):   # (a crafted file can have back-to-back gaps)
        stitch.check(tz.datetime_exists(r), "resolve_imaginary() gives a time that exists")
    if tz.datetime_exists(d):
        stitch.check(r == d, "resolve_imaginary() keeps a time that exists")


@stitch.endpoint(bails_on=(OverflowError,))
def zone_vs_zoneinfo(z: Borrow[Zone], dt: Borrow[Dt], fold: Range[0, 1]):
    """tzfile is a backport of zoneinfo.ZoneInfo: offsets and names agree."""
    ref = stitch.get(z, "ref", None)
    stitch.require(ref is not None, "no reference zone")
    naive = dt.replace(tzinfo=None, fold=fold)
    a = naive.replace(tzinfo=z)
    b = naive.replace(tzinfo=ref)
    stitch.check(a.utcoffset() == b.utcoffset(), "utcoffset() agrees with zoneinfo")
    stitch.check(a.tzname() == b.tzname(), "tzname() agrees with zoneinfo")
    u = naive.replace(fold=0, tzinfo=UTC)
    try:
        ua, ub = u.astimezone(z), u.astimezone(ref)
    except (OverflowError, ValueError):
        stitch.bail("out of range")
    stitch.check(ua.replace(tzinfo=None) == ub.replace(tzinfo=None) and ua.fold == ub.fold,
                 "fromutc() agrees with zoneinfo")


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def astimezone(dt: Borrow[Dt], z: Borrow[Zone]) -> New[Dt]:
    d = dt if dt.tzinfo else dt.replace(tzinfo=tz.UTC)
    return d.astimezone(z)


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def localize(dt: Borrow[Dt], z: Borrow[Zone], fold: Range[0, 1]) -> New[Dt]:
    return tz.enfold(dt.replace(tzinfo=z), fold=fold)


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def resolve(dt: Borrow[Dt], z: Borrow[Zone]):
    d = dt.replace(tzinfo=z)
    tz.resolve_imaginary(d)
    tz.datetime_ambiguous(d)
    tz.datetime_exists(d)
    tz.datetime_exists(dt, tz=z)
    tz.datetime_ambiguous(dt.replace(tzinfo=None), tz=z)


# ---------------------------------------------------------------------------------------------
# easter, utils and the legacy zoneinfo module


@stitch.endpoint(bails_on=(ValueError,))
def easter_date(year: Range[1, 9999], method: OneOf[[easter.EASTER_JULIAN, easter.EASTER_ORTHODOX, easter.EASTER_WESTERN]]):
    d = easter.easter(year, method)
    if method == easter.EASTER_WESTERN and 1583 <= year <= 4099:
        stitch.check(d.weekday() == 6 and d.month in (3, 4), "western Easter is a Sunday in March or April")
    if method == easter.EASTER_ORTHODOX and 1583 <= year <= 4099:
        stitch.check(d.weekday() == 6, "orthodox Easter is a Sunday")


@stitch.endpoint(bails_on=(OverflowError,))
def util_calls(a: Borrow[Dt], b: Borrow[Dt], z: Borrow[Zone], seconds: Range[0, 1000000]):
    d = utils.default_tzinfo(a, z)
    stitch.check(d.tzinfo is (a.tzinfo or z), "default_tzinfo() only fills in a missing zone")
    t = utils.today(z)
    stitch.check(t.hour == 0 and t.minute == 0 and t.tzinfo is z, "today() is midnight in the zone")
    stitch.require((a.tzinfo is None) == (b.tzinfo is None), "naive/aware mix")
    delta = datetime.timedelta(seconds=seconds)
    w = utils.within_delta(a, b, delta)
    stitch.check(w == utils.within_delta(b, a, delta), "within_delta() is symmetric")
    stitch.check(w == (abs(a - b) <= delta), "within_delta() agrees with subtraction")


@stitch.endpoint
def legacy_zoneinfo(name: OneOf[["UTC", "America/New_York", "Europe/London", "Nowhere/Bogus"]]):
    with warnings.catch_warnings():   # the module is deprecated
        warnings.simplefilter("ignore", DeprecationWarning)
        from dateutil import zoneinfo as dzi
    inst = dzi.get_zonefile_instance()
    z = inst.get(name)
    z2 = dzi.gettz(name)
    stitch.check((z is None) == (z2 is None), "ZoneInfoFile.get() and zoneinfo.gettz() agree")
    dzi.gettz_db_metadata()
