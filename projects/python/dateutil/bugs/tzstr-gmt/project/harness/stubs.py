"""python-dateutil: date parsing, relative deltas, recurrence rules, time zones.

Oracles: isoparse(dt.isoformat()) == dt, and rrule.after() agrees with listing the occurrences.
"""
import datetime
import itertools

import stitch
from dateutil import parser, relativedelta, rrule, tz
from stitch import Bool, Borrow, New, OneOf, Range, Src, Str

Dt = stitch.object("Dt")
Delta = stitch.object("Delta")
Rule = stitch.object("Rule")
Zone = stitch.object("Zone")
DateText = stitch.datatype("DateText")
RRuleText = stitch.datatype("RRuleText")

FREQS = (rrule.YEARLY, rrule.MONTHLY, rrule.WEEKLY, rrule.DAILY, rrule.HOURLY, rrule.MINUTELY, rrule.SECONDLY)


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


@stitch.decoder(RRuleText)
def rrule_text(s):
    rules = [f"FREQ={s.pick(['YEARLY', 'MONTHLY', 'WEEKLY', 'DAILY', 'HOURLY', 'MINUTELY'])}"]
    while s.more():
        k = s.choice(8)
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
        else:
            rules.append(s.token())
    head = "DTSTART:19970902T090000\nRRULE:" if s.coin(0.7) else ""
    return head + ";".join(rules)


@stitch.endpoint(bails_on=(parser.ParserError, OverflowError))
def parse(text: Str[64, DateText], dayfirst: Bool, yearfirst: Bool, fuzzy: Bool) -> New[Dt]:
    return parser.parse(text, dayfirst=dayfirst, yearfirst=yearfirst, fuzzy=fuzzy)


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def isoparse(text: Str[40, DateText]) -> New[Dt]:
    return parser.isoparse(text)


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
        stitch.check(False, f"isoparse(isoformat()) raises ({e})")
        return
    stitch.check(again == dt and again.utcoffset() == dt.utcoffset(), "isoparse(isoformat()) == dt")


@stitch.endpoint(bails_on=(ValueError,))
def make_delta(years: Range[-100, 100], months: Range[-1000, 1000], days: Range[-100000, 100000],
               hours: Range[-10000, 10000], weekday: Range[0, 6], nth: Range[-6, 6], leapdays: Range[-3, 3]) -> New[Delta]:
    wd = relativedelta.weekday(weekday, nth or None)
    return relativedelta.relativedelta(years=years, months=months, days=days, hours=hours, weekday=wd, leapdays=leapdays)


@stitch.endpoint(bails_on=(ValueError, OverflowError, TypeError))  # TypeError: naive vs aware
def delta_between(a: Borrow[Dt], b: Borrow[Dt]) -> New[Delta]:
    return relativedelta.relativedelta(a, b)


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def add_delta(dt: Borrow[Dt], d: Borrow[Delta]) -> New[Dt]:
    return dt + d


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def delta_ops(a: Borrow[Delta], b: Borrow[Delta], factor: Range[-100, 100], op: OneOf["add", "sub", "mul", "neg", "normalized", "abs"]) -> New[Delta]:
    if op == "add":
        return a + b
    if op == "sub":
        return a - b
    if op == "mul":
        return a * factor
    if op == "neg":
        return -a
    if op == "abs":
        return abs(a)
    return a.normalized()


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
def rrulestr(text: Str[160, RRuleText], forceset: Bool) -> New[Rule]:
    return rrule.rrulestr(text, forceset=forceset)


@stitch.endpoint(bails_on=(TypeError,))
def rule_iterate(r: Borrow[Rule], n: Range[1, 40]):
    with stitch.time_limit(0.05):  # rules that never match iterate until year 9999
        out = list(itertools.islice(r, n))
    stitch.check(out == sorted(out), "occurrences are in order")


@stitch.endpoint(bails_on=(TypeError,))
def rule_after(r: Borrow[Rule], dt: Borrow[Dt], inc: Bool):
    with stitch.time_limit(0.05):  # rules that never match iterate until year 9999
        try:
            got = r.after(dt, inc=inc)
        except TypeError:
            stitch.bail("naive/aware mix")
        first = next((x for x in itertools.islice(r, 500) if (x >= dt if inc else x > dt)), None)
        stitch.require(first is not None or len(list(itertools.islice(r, 501))) <= 500, "beyond 500 occurrences")
    stitch.check(got == first, "after() agrees with iteration")


@stitch.endpoint(bails_on=(TypeError,))
def rule_between(r: Borrow[Rule], a: Borrow[Dt], b: Borrow[Dt], inc: Bool):
    with stitch.time_limit(0.05):
        r.between(a, b, inc=inc)


@stitch.endpoint
def gettz(name: OneOf["UTC", "America/New_York", "Europe/London", "Asia/Kolkata", "Australia/Lord_Howe", "EST5EDT", "local"]) -> New[Zone]:
    z = tz.tzlocal() if name == "local" else tz.gettz(name)
    stitch.require(z is not None, "unknown zone")
    return z


@stitch.endpoint(bails_on=(ValueError,))
def tzstr(text: Str[48]) -> New[Zone]:
    return tz.tzstr(text)


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def astimezone(dt: Borrow[Dt], z: Borrow[Zone]) -> New[Dt]:
    d = dt if dt.tzinfo else dt.replace(tzinfo=tz.UTC)
    return d.astimezone(z)


@stitch.endpoint(bails_on=(ValueError, OverflowError))
def resolve(dt: Borrow[Dt], z: Borrow[Zone], s: Src[4]):
    d = dt.replace(tzinfo=z)
    tz.resolve_imaginary(d)
    tz.datetime_ambiguous(d)
    tz.datetime_exists(d)
