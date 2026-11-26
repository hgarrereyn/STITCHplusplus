"""tomlkit: parse, edit and print TOML while keeping its formatting.

Oracles: printing a parsed document gives back the input text, the text of an edited
document parses to the same data, Python data survives dumps/loads, item arithmetic agrees
with the plain Python value, and a printed item parses back to the same value.
"""
import copy
import datetime as dt
import io
import math
import os
import tempfile

import stitch
import tomlkit
from stitch import Bool, Borrow, F64, I32, I64, New, OneOf, Range, Src, Str, Take, Val, File
from tomlkit.exceptions import ConvertError, TOMLKitError
from tomlkit.items import AoT, Array, Date, DateTime, Float, InlineTable, Integer, String, Table, Time
from tomlkit.toml_file import TOMLFile

Doc = stitch.object("Doc")
Table_ = stitch.object("Table")
Arr = stitch.object("Arr")
Item = stitch.object("Item")      # a standalone value item, not yet placed in a document
Key = stitch.object("Key")
TomlText = stitch.datatype("TomlText")
TomlKey = stitch.datatype("TomlKey")
ValueText = stitch.datatype("ValueText")     # one TOML value: `1_000`, `"s"`, `[1, 2]`, `{a = 1}`
KeyValueText = stitch.datatype("KeyValueText")
ArrayText = stitch.datatype("ArrayText")
DateText = stitch.datatype("DateText")       # RFC 3339 local dates
TimeText = stitch.datatype("TimeText")       # RFC 3339 local times
DateTimeText = stitch.datatype("DateTimeText")
FloatText = stitch.datatype("FloatText")
PyData = stitch.datatype("PyData")           # a plain Python dict for dumps()/item()
PyValue = stitch.datatype("PyValue")         # a plain Python scalar or list for item()

KINDS = ("int", "str", "float", "bool", "date", "datetime", "array", "inline", "multiline", "literal")
ERRORS = (TOMLKitError, ValueError)          # comment(), integer(), date(), add_line() raise ValueError


# ---------------------------------------------------------------------------------------------
# decoders


def gen_key(s):
    return s.token() if s.coin(0.5) else s.pick(["a", "b.c", '"q k"', "'lit'", "x-y", "1"])


def gen_value(s, depth=0):
    k = s.choice(9 if depth < 2 else 7)
    if k == 0:
        return s.pick([str(s.i64()), "1_000", "0x1F", "0o17", "0b101", "+7", "-0"])
    if k == 1:
        return '"' + s.str(12).replace("\\", "\\\\").replace('"', '\\"') + '"'
    if k == 2:
        return s.pick(["1.5", "-0.0", "inf", "nan", "1e10", "+3.25", "6.02e+23", "1_0.0_1"])
    if k == 3:
        return s.pick(["true", "false"])
    if k == 4:
        return s.pick(["1979-05-27", "07:32:00", "1979-05-27T07:32:00Z", "1979-05-27 07:32:00.999-07:00",
                       "1979-05-27T00:32:00.999999", "07:32"])
    if k == 5:
        return "'''" + s.str(10) + "'''"
    if k == 6:
        return s.token()
    if k == 7:
        return "[" + ", ".join(gen_value(s, depth + 1) for _ in range(s.choice(4))) + ("," if s.coin(0.2) else "") + "]"
    return "{" + ", ".join(f"{gen_key(s)} = {gen_value(s, depth + 1)}" for _ in range(s.choice(3))) + "}"


@stitch.decoder(TomlText)
def toml_text(s):
    lines = []
    while s.more():
        c = s.choice(6)
        if c == 0:
            lines.append(f"[{gen_key(s)}]")
        elif c == 1:
            lines.append(f"[[{gen_key(s)}]]")
        elif c == 2:
            lines.append("# " + s.str(10))
        else:
            lines.append(f"{gen_key(s)} = {gen_value(s)}" + (" # c" if s.coin(0.2) else ""))
    return "\n".join(lines) + ("\n" if s.coin(0.8) else "")


@stitch.decoder(ValueText)
def value_text(s):
    return gen_value(s)


@stitch.decoder(KeyValueText)
def key_value_text(s):
    return f"{gen_key(s)} = {gen_value(s)}" + (" # " + s.str(6) if s.coin(0.2) else "")


@stitch.decoder(ArrayText)
def array_text(s):
    sep = s.pick([", ", ",", ",\n  ", " ,"])
    vals = []
    while s.more():
        vals.append(gen_value(s, 1) + (" # " + s.str(4) + "\n" if s.coin(0.1) else ""))
    return "[" + s.pick(["", "\n  ", " "]) + sep.join(vals) + ("," if s.coin(0.2) else "") + s.pick(["", "\n", " "]) + "]"


def gen_date_text(s, k):
    def num(width, lo, hi):
        if s.coin(0.97):
            return str(s.range(lo, hi)).zfill(width)
        return s.pick([str(hi + 1), "0" * width, s.str(3)])          # out of range or junk

    def date():
        return f"{num(4, 0, 9999)}-{num(2, 1, 12)}-{num(2, 1, 28 if s.coin(0.9) else 31)}"

    def time():
        t = f"{num(2, 0, 23)}:{num(2, 0, 59)}"
        if s.coin(0.8):
            t += f":{num(2, 0, 59)}"
            if s.coin(0.3):
                t += "." + str(s.range(0, 10**9))
        return t

    if k == 0:
        return date()
    if k == 1:
        return time()
    off = s.pick(["", "Z", "z", f"+{num(2, 0, 23)}:{num(2, 0, 59)}", f"-{num(2, 0, 23)}:{num(2, 0, 59)}"])
    return date() + s.pick(["T", "t", " "]) + time() + off


@stitch.decoder(DateText)
def date_text(s):
    return gen_date_text(s, 0 if s.coin(0.9) else s.choice(3))


@stitch.decoder(TimeText)
def time_text(s):
    return gen_date_text(s, 1 if s.coin(0.9) else s.choice(3))


@stitch.decoder(DateTimeText)
def datetime_text(s):
    return gen_date_text(s, 2 if s.coin(0.9) else s.choice(3))


@stitch.decoder(FloatText)
def float_text(s):
    return s.pick([repr(s.f64(-1e300, 1e300)), "inf", "-inf", "nan", "-0.0", "1e5", "1_000.5", " 2.5 ",
                   "0x10", "1e400", str(s.i64()), s.str(8)])


def gen_scalar(s):
    k = s.choice(9)
    if k == 0:
        return s.i64()
    if k == 1:
        return s.str(12)
    if k == 2:
        return s.pick([s.f64(-1e9, 1e9), math.inf, -math.inf, math.nan, -0.0, 1e300, 5e-324])
    if k == 3:
        return s.coin()
    if k == 4:
        return dt.date(s.range(1, 9999), s.range(1, 12), s.range(1, 28))
    if k == 5:
        return dt.time(s.range(0, 23), s.range(0, 59), s.range(0, 59), s.pick([0, s.range(0, 999999)]))
    tz = s.pick([None, dt.timezone.utc, dt.timezone(dt.timedelta(minutes=s.range(-1439, 1439))),
                 dt.timezone(dt.timedelta(seconds=s.range(-86399, 86399)))])
    return dt.datetime(s.range(1, 9999), s.range(1, 12), s.range(1, 28), s.range(0, 23), s.range(0, 59),
                       s.range(0, 59), s.pick([0, s.range(0, 999999)]), tzinfo=tz, fold=s.range(0, 1))


def gen_py(s, depth):
    k = s.choice(5 if depth < 3 else 2)
    if k <= 1:
        return gen_scalar(s)
    if k == 2:
        return [gen_py(s, depth + 1) for _ in range(s.choice(4))]
    if k == 3:
        return [gen_dict(s, depth + 1) for _ in range(1 + s.choice(3))]
    return gen_dict(s, depth + 1)


def gen_dict(s, depth):
    d = {}
    for _ in range(s.choice(5)):
        d[s.token() if s.coin(0.5) else s.str(8)] = gen_py(s, depth)
    return d


@stitch.decoder(PyData)
def py_data(s):
    d = {}
    while s.more():
        d[s.token() if s.coin(0.5) else s.str(8)] = gen_py(s, 0)
    return d


@stitch.decoder(PyValue)
def py_value(s):
    if s.coin(0.7):
        return gen_scalar(s)
    return [gen_scalar(s) if s.coin(0.8) else [gen_scalar(s) for _ in range(s.choice(3))]
            for _ in range(s.choice(4))]


# ---------------------------------------------------------------------------------------------
# helpers


def make_value(kind, n, x, text):
    if kind == "int":
        return n
    if kind == "str":
        return text
    if kind == "float":
        return x
    if kind == "bool":
        return n % 2 == 0
    if kind == "date":
        return tomlkit.date("1979-05-27")
    if kind == "datetime":
        return tomlkit.datetime("1979-05-27T07:32:00Z")
    if kind == "array":
        a = tomlkit.array()
        a.extend([n, n + 1])
        return a
    if kind == "inline":
        t = tomlkit.inline_table()
        t.update({"k": n, "s": text})
        return t
    if kind == "multiline":
        return tomlkit.string(text, multiline=True)
    return tomlkit.string(text, literal=True)


def existing_key(container, s):
    keys = list(container.keys())
    stitch.require(keys, "no keys")
    return s.pick(keys)


def same(a, b, ordered=True):
    """Data equality where nan == nan; `ordered` also compares the key order of dicts."""
    if isinstance(a, float) and isinstance(b, float):
        return a == b or (math.isnan(a) and math.isnan(b))
    if isinstance(a, dict) and isinstance(b, dict):
        if ordered and list(a) != list(b):
            return False
        return set(a) == set(b) and all(same(a[k], b[k], ordered) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same(x, y, ordered) for x, y in zip(a, b))
    return type(a) is type(b) and a == b


def plain(v):
    """Plain data of an item, or of a dict/list holding items (OutOfOrderTableProxy.copy())."""
    if hasattr(v, "unwrap"):
        return v.unwrap()
    if isinstance(v, dict):
        return {k: plain(x) for k, x in v.items()}
    if isinstance(v, list):
        return [plain(x) for x in v]
    return v


def prune_empty_super(item, data):
    """A table forced to be a super table (`table(True)`) has no header of its own, so once it is
    empty it prints nothing. Drop those from the expected data."""
    for k in list(data):
        try:
            child = item[k]
        except Exception:
            continue
        if isinstance(child, (dict, Table)) and isinstance(data[k], dict):
            prune_empty_super(child, data[k])
            if getattr(child, "_is_super_table", None) is True and data[k] == {}:
                del data[k]
        elif isinstance(child, list) and isinstance(data[k], list):
            for c, d in zip(child, data[k]):
                if isinstance(c, (dict, Table)) and isinstance(d, dict):
                    prune_empty_super(c, d)
    return data


def reparse_value(it):
    """Parse an item's printed text again as a standalone value."""
    text = it.as_string()
    try:
        return tomlkit.value(text)
    except TOMLKitError as e:
        stitch.check(False, f"printed item parses as a value ({type(e).__name__})")


# ---------------------------------------------------------------------------------------------
# documents


@stitch.endpoint(bails_on=(TOMLKitError,))
def parse(text: Str[2048, TomlText]) -> New[Doc]:
    doc = tomlkit.parse(text)
    stitch.check(doc.as_string() == text, "parse/print preserves the text")
    return doc


@stitch.endpoint(bails_on=(TOMLKitError, UnicodeDecodeError))
def loads_bytes(data: Str[2048, TomlText], crlf: Bool) -> New[Doc]:
    text = data.replace("\n", "\r\n") if crlf else data
    doc = tomlkit.loads(text.encode("utf-8"))
    stitch.check(doc.as_string() == text, "loads(bytes)/print preserves the text")
    return doc


@stitch.endpoint(bails_on=(TOMLKitError, UnicodeDecodeError))
def load_file(path: File[2048, TomlText]) -> New[Doc]:
    with open(path, "rb") as f:
        data = f.read()
        f.seek(0)
        doc = tomlkit.load(f)
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return doc
    if b"\r" not in data:                # TOMLFile.read normalizes CRLF line endings
        again = TOMLFile(path).read()
        stitch.check(again.as_string() == doc.as_string(), "TOMLFile.read and load agree")
    return doc


@stitch.endpoint
def document() -> New[Doc]:
    return tomlkit.document()


@stitch.endpoint(bails_on=(ConvertError,))
def from_data(data: Val[PyData, 1024]) -> New[Doc]:
    """dumps() a plain dict, then loads() it back: the data must survive."""
    text = tomlkit.dumps(data)
    stitch.log("dumped: %r", text)
    try:
        doc = tomlkit.loads(text)
    except TOMLKitError as e:
        stitch.check(False, f"dumps() output parses ({type(e).__name__})")
        return None
    stitch.check(same(doc.unwrap(), data, ordered=False), "loads(dumps(data)) == data")
    return doc


@stitch.endpoint(bails_on=(TOMLKitError,))
def set_value(doc: Borrow[Doc], key: Str[16, TomlKey], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    doc[key] = make_value(kind, n, x, text)


@stitch.endpoint(bails_on=(TOMLKitError,))
def append_value(doc: Borrow[Doc], key: Str[16, TomlKey], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    doc.append(key, make_value(kind, n, x, text))


@stitch.endpoint(bails_on=(TOMLKitError,))
def put_item(doc: Borrow[Doc], key: Str[16, TomlKey], it: Take[Item]):
    doc[key] = it


@stitch.endpoint(bails_on=(TOMLKitError,))
def append_key(doc: Borrow[Doc], k: Borrow[Key], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    doc.append(k, make_value(kind, n, x, text))


@stitch.endpoint(bails_on=(TOMLKitError,))
def setdefault(doc: Borrow[Doc], key: Str[16, TomlKey], n: I64):
    doc.setdefault(key, n)
    stitch.check(key in doc, "setdefault leaves the key in the document")


@stitch.endpoint(bails_on=(TOMLKitError,))
def update(doc: Borrow[Doc], data: Val[PyData, 512]):
    doc.update(data)


@stitch.endpoint(bails_on=(TOMLKitError,))
def add_table(doc: Borrow[Doc], key: Str[16, TomlKey], super_table: Bool) -> New[Table_]:
    doc.add(key, tomlkit.table(is_super_table=super_table))
    t = doc[key]
    stitch.require(isinstance(t, dict), "key held an array of tables")    # add() appended to it
    return t


@stitch.endpoint(bails_on=(TOMLKitError,))
def get_table(doc: Borrow[Doc], s: Src[8]) -> New[Table_]:
    t = doc[existing_key(doc, s)]
    stitch.require(isinstance(t, dict), "not a table")
    return t


@stitch.endpoint(bails_on=(TOMLKitError,))
def get_array(doc: Borrow[Doc], s: Src[8]) -> New[Arr]:
    a = doc[existing_key(doc, s)]
    stitch.require(isinstance(a, list), "not an array")
    return a


@stitch.endpoint(bails_on=(TOMLKitError,))
def remove(doc: Borrow[Doc], s: Src[8]):
    doc.remove(existing_key(doc, s))


@stitch.endpoint(bails_on=(TOMLKitError,))
def pop(doc: Borrow[Doc], s: Src[8]):
    k = existing_key(doc, s)
    doc.pop(k)
    stitch.check(k not in doc, "a popped key is gone")


@stitch.endpoint(bails_on=(TOMLKitError,))
def delete(doc: Borrow[Doc], s: Src[8]):
    del doc[existing_key(doc, s)]


@stitch.endpoint(bails_on=(TOMLKitError,))
def add_array_of_tables(doc: Borrow[Doc], key: Str[16, TomlKey], n: I64) -> New[Arr]:
    aot = tomlkit.aot()
    t = tomlkit.table()
    t["n"] = n
    aot.append(t)
    doc[key] = aot
    return doc[key]


@stitch.endpoint(bails_on=(TOMLKitError,))
def new_array(doc: Borrow[Doc], key: Str[16, TomlKey], multiline: Bool) -> New[Arr]:
    a = tomlkit.array()
    a.multiline(multiline)
    doc[key] = a
    return doc[key]


@stitch.endpoint(bails_on=(TOMLKitError, ValueError))
def array_from_text(doc: Borrow[Doc], key: Str[16, TomlKey], raw: Str[256, ArrayText]) -> New[Arr]:
    a = tomlkit.array(raw)
    stitch.check(a.as_string() == raw, "array(raw) keeps the text")
    doc[key] = a
    return doc[key]


@stitch.endpoint(bails_on=ERRORS)
def comment_and_newline(doc: Borrow[Doc], text: Str[16]):
    doc.add(tomlkit.comment(text))
    doc.add(tomlkit.nl())


@stitch.endpoint(bails_on=(TOMLKitError,))
def add_whitespace(doc: Borrow[Doc], ws: OneOf[[" ", "\n", "\n\n", "\t\n", "  \n  "]]):
    doc.add(tomlkit.ws(ws))


@stitch.endpoint(bails_on=(TOMLKitError,))
def copy_doc(doc: Borrow[Doc], deep: Bool) -> New[Doc]:
    c = copy.deepcopy(doc) if deep else doc.copy()
    stitch.check(c.as_string() == doc.as_string(), "a copy prints the same")
    stitch.check(same(c.unwrap(), doc.unwrap()), "a copy has the same data")
    # a shallow copy shares its tables with the original, so only a deep copy is a new document
    return c if deep else copy.deepcopy(doc)


@stitch.endpoint(bails_on=(TOMLKitError,))
def inspect(doc: Borrow[Doc], s: Src[8]):
    keys = list(doc.keys())
    stitch.check(len(keys) == len(doc), "len() matches keys()")
    for k in keys:
        stitch.check(k in doc, "every key is in the document")
    list(doc.items())
    doc.value
    doc.body
    doc.last_item()
    if keys:
        k = s.pick(keys)
        doc.item(k)
        v = doc[k]
        stitch.check(same(v.unwrap() if hasattr(v, "unwrap") else v, doc.unwrap()[k]),
                     "doc[k] and unwrap()[k] agree")


@stitch.endpoint(bails_on=(TOMLKitError,))
def dumps_sorted(doc: Borrow[Doc]):
    text = tomlkit.dumps(doc, sort_keys=True)
    stitch.log("sorted: %r", text)
    try:
        again = tomlkit.parse(text)
    except TOMLKitError as e:
        stitch.check(False, f"sorted output parses ({type(e).__name__})")
        return
    stitch.check(same(again.unwrap(), prune_empty_super(doc, doc.unwrap()), ordered=False),
                 "sorted output has the same data")


@stitch.endpoint(bails_on=(TOMLKitError,))
def dump_stream(doc: Borrow[Doc], sort_keys: Bool):
    out = io.StringIO()
    tomlkit.dump(doc, out, sort_keys=sort_keys)
    stitch.check(out.getvalue() == tomlkit.dumps(doc, sort_keys=sort_keys), "dump() writes what dumps() returns")


@stitch.endpoint(bails_on=(TOMLKitError,))
def file_roundtrip(doc: Borrow[Doc], crlf: Bool):
    text = doc.as_string()
    stitch.require("\r" not in text, "document has carriage returns")
    fd, path = tempfile.mkstemp(suffix=".toml")
    os.close(fd)
    try:
        f = TOMLFile(path)
        if crlf:
            with open(path, "w", newline="") as fh:
                fh.write("a = 1\r\n")
            f.read()                     # remember CRLF line endings
        f.write(doc)
        with open(path, "rb") as fh:
            raw = fh.read()
        stitch.check(b"\r\n" not in raw if not crlf else raw.count(b"\n") == raw.count(b"\r\n"),
                     "write() uses the file's line endings")
        again = TOMLFile(path).read()
        stitch.check(again.as_string() == text, "TOMLFile write/read keeps the text")
    finally:
        os.unlink(path)


# ---------------------------------------------------------------------------------------------
# tables


@stitch.endpoint(bails_on=ERRORS)   # an inline table rejects tables (ValueError)
def table_set(t: Borrow[Table_], key: Str[16, TomlKey], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    t[key] = make_value(kind, n, x, text)


@stitch.endpoint(bails_on=ERRORS)   # an inline table rejects tables (ValueError)
def table_append(t: Borrow[Table_], key: Str[16, TomlKey], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    stitch.require(hasattr(t, "append"), "no append() on this table type")
    t.append(key, make_value(kind, n, x, text))


@stitch.endpoint(bails_on=ERRORS)   # an inline table rejects tables (ValueError)
def table_append_key(t: Borrow[Table_], k: Borrow[Key], n: I64):
    stitch.require(hasattr(t, "append"), "no append() on this table type")
    t.append(k, n)


@stitch.endpoint(bails_on=ERRORS)   # an inline table rejects tables (ValueError)
def table_put_item(t: Borrow[Table_], key: Str[16, TomlKey], it: Take[Item]):
    t[key] = it


@stitch.endpoint(bails_on=ERRORS)   # an inline table rejects tables (ValueError)
def table_add_table(t: Borrow[Table_], key: Str[16, TomlKey]) -> New[Table_]:
    t[key] = tomlkit.table()
    return t[key]


@stitch.endpoint(bails_on=ERRORS)   # an inline table rejects tables (ValueError)
def table_add_inline(t: Borrow[Table_], key: Str[16, TomlKey], data: Val[PyData, 256]) -> New[Table_]:
    it = tomlkit.inline_table()
    it.update(data)
    t[key] = it
    return t[key]


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_remove(t: Borrow[Table_], s: Src[8]):
    del t[existing_key(t, s)]


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_pop(t: Borrow[Table_], s: Src[8]):
    k = existing_key(t, s)
    if hasattr(t, "remove") and s.coin():
        t.remove(k)
    else:
        t.pop(k)
    stitch.check(k not in t, "a removed key is gone")


@stitch.endpoint(bails_on=ERRORS)   # an inline table rejects tables (ValueError)
def table_setdefault(t: Borrow[Table_], key: Str[16, TomlKey], n: I64, s: Src[8]):
    keys = list(t.keys())
    if keys and s.coin():
        t.setdefault(s.pick(keys))           # an existing key needs no default
    else:
        t.setdefault(key, n)
        stitch.check(key in t, "setdefault leaves the key in the table")


@stitch.endpoint(bails_on=ERRORS)   # an inline table rejects tables (ValueError)
def table_update(t: Borrow[Table_], data: Val[PyData, 256]):
    t.update(data)


@stitch.endpoint(bails_on=ERRORS)
def table_comment(t: Borrow[Table_], text: Str[16]):
    stitch.require(hasattr(t, "comment"), "no comment() on this table type")
    t.comment(text)


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_add_comment(t: Borrow[Table_], text: Str[16], nl: Bool):
    stitch.require(isinstance(t, Table), "comments only go in a standard table")
    try:
        c = tomlkit.comment(text)
    except ValueError:
        stitch.bail("comment text has control characters")
    t.add(c)
    if nl:
        t.add(tomlkit.nl())


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_indent(t: Borrow[Table_], n: Range[0, 8]):
    stitch.require(hasattr(t, "indent"), "no indent() on this table type")
    t.indent(n)


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_copy(t: Borrow[Table_]):
    stitch.require(hasattr(t, "copy"), "no copy() on this table type")
    c = t.copy()
    stitch.check(same(plain(c), plain(t)), "a table copy has the same data")


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_inspect(t: Borrow[Table_], s: Src[8]):
    keys = list(t.keys())
    stitch.check(len(keys) == len(t), "len() matches keys()")
    data = t.unwrap()
    stitch.check(set(data) == set(keys), "unwrap() keeps the keys")
    if isinstance(t, (Table, InlineTable)):
        t.value
        t.as_string()
        t.is_table(), t.is_inline_table(), t.is_aot()
        if isinstance(t, Table):
            t.is_super_table()
            t.is_aot_element()
            t.invalidate_display_name()
    if keys:
        k = s.pick(keys)
        if hasattr(t, "item"):
            t.item(k)
        stitch.check(k in t, "every key is in the table")


# ---------------------------------------------------------------------------------------------
# arrays and arrays of tables


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_append(a: Borrow[Arr], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    v = make_value(kind, n, x, text)
    if isinstance(a, AoT):
        t = tomlkit.table()
        t["v"] = v
        a.append(t)
    else:
        a.append(v)


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_put_item(a: Borrow[Arr], it: Take[Item], i: I64, insert: Bool):
    stitch.require(isinstance(a, Array), "not a value array")
    if insert:
        a.insert(i % (len(a) + 1), it)
    else:
        a.append(it)


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_insert(a: Borrow[Arr], i: I64, kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    v = make_value(kind, n, x, text)
    pos = i % (len(a) + 1)
    if isinstance(a, AoT):
        t = tomlkit.table()
        t["v"] = v
        a.insert(pos, t)
    else:
        a.insert(pos, v)


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_set(a: Borrow[Arr], i: I64, kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    stitch.require(len(a) > 0, "empty")
    v = make_value(kind, n, x, text)
    if isinstance(a, AoT):
        t = tomlkit.table()
        t["v"] = v
        v = t
    a[i % len(a)] = v


@stitch.endpoint(bails_on=(TOMLKitError, ValueError))
def array_add_line(a: Borrow[Arr], vals: Val[PyValue, 128], indent: OneOf[["", " ", "    ", "\t"]],
                   comment: Str[12], add_comma: Bool, newline: Bool):
    stitch.require(isinstance(a, Array), "not a value array")
    items = vals if isinstance(vals, list) else [vals]
    if not add_comma:   # then the caller writes the separators (documented)
        items = [x for v in items for x in (v, tomlkit.ws(", "))][:-1]
    a.add_line(*items, indent=indent, comment=comment or None, add_comma=add_comma, newline=newline)


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_delete(a: Borrow[Arr], i: I64):
    stitch.require(len(a) > 0, "empty")
    del a[i % len(a)]


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_delete_slice(a: Borrow[Arr], start: Range[-4, 4], stop: Range[-4, 4], step: OneOf[[None, 1, 2, -1]]):
    del a[start:stop:step]


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_pop(a: Borrow[Arr], i: I64):
    stitch.require(len(a) > 0, "empty")
    n = len(a)
    a.pop(i % n)
    stitch.check(len(a) == n - 1, "pop() removes one element")


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_clear(a: Borrow[Arr]):
    a.clear()
    stitch.check(len(a) == 0, "clear() empties the array")


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_sort(a: Borrow[Arr], reverse: Bool):
    stitch.require(isinstance(a, Array), "not a value array")
    before = a.unwrap()
    kinds = {type(v) for v in before}
    # Bool items are not ints, and nested arrays compare their elements
    stitch.require(len(kinds) <= 1 or kinds <= {int, float}, "mixed types do not compare")
    stitch.require(not kinds & {list, dict}, "nested values")
    try:
        expect = sorted(before, reverse=reverse)
    except TypeError:
        stitch.bail("elements do not compare")
    stitch.require(not any(isinstance(v, float) and math.isnan(v) for v in before), "nan")
    a.sort(reverse=reverse)
    stitch.check(same(a.unwrap(), expect), "sort() orders the values like sorted()")


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_multiline(a: Borrow[Arr], multiline: Bool):
    stitch.require(isinstance(a, Array), "not a value array")
    a.multiline(multiline)


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_inspect(a: Borrow[Arr], s: Src[8]):
    data = a.unwrap()
    stitch.check(len(data) == len(a), "unwrap() keeps the length")
    a.as_string()
    a.value
    if isinstance(a, AoT):
        stitch.check(len(a.body) == len(a), "body and list agree")
    if len(a):
        i = s.range(0, len(a) - 1)
        el = a.item(i) if isinstance(a, Array) else a[i]
        stitch.check(hasattr(el, "unwrap"), "an array holds items")
        stitch.check(same(el.unwrap(), data[i]), "element i and unwrap()[i] agree")
        if isinstance(a, Array) and not isinstance(data[i], float):
            stitch.check(a.index(a[i]) <= i, "index() finds the element")


@stitch.endpoint(bails_on=(TOMLKitError,))
def aot_get_table(a: Borrow[Arr], s: Src[8]) -> New[Table_]:
    stitch.require(isinstance(a, AoT) and len(a), "not a non-empty array of tables")
    return a[s.range(0, len(a) - 1)]


# ---------------------------------------------------------------------------------------------
# standalone items and keys


@stitch.endpoint(bails_on=(TOMLKitError, ValueError))
def parse_value(raw: Str[256, ValueText]) -> New[Item]:
    it = tomlkit.value(raw)
    stitch.check(it.as_string() == raw, "value(raw) keeps the text")
    return it


@stitch.endpoint(bails_on=(TOMLKitError, ValueError))
def new_integer(raw: Str[32, ValueText]) -> New[Item]:
    it = tomlkit.integer(raw)
    stitch.check(it.as_string() == raw, "integer(raw) keeps the text")
    stitch.check(it == int(raw.replace("_", ""), 0), "integer(raw) has the literal's value")
    return it


@stitch.endpoint(bails_on=(TOMLKitError, ValueError, OverflowError))
def new_number(n: I64, x: F64, as_float: Bool) -> New[Item]:
    return tomlkit.float_(x) if as_float else tomlkit.integer(n)


@stitch.endpoint(bails_on=(TOMLKitError, ValueError))
def new_float(raw: Str[32, FloatText]) -> New[Item]:
    return tomlkit.float_(raw)


@stitch.endpoint
def new_boolean(raw: OneOf[["true", "false", True, False]]) -> New[Item]:
    it = tomlkit.boolean(raw)
    stitch.check(it.unwrap() is (raw in ("true", True)), "boolean() value")
    return it


@stitch.endpoint(bails_on=(TOMLKitError, ValueError))
def new_date(raw: Str[40, DateText]) -> New[Item]:
    return tomlkit.date(raw)


@stitch.endpoint(bails_on=(TOMLKitError, ValueError))
def new_time(raw: Str[40, TimeText]) -> New[Item]:
    return tomlkit.time(raw)


@stitch.endpoint(bails_on=(TOMLKitError, ValueError))
def new_datetime(raw: Str[40, DateTimeText]) -> New[Item]:
    return tomlkit.datetime(raw)


@stitch.endpoint(bails_on=(TOMLKitError,))
def new_string(text: Str[48], literal: Bool, multiline: Bool) -> New[Item]:
    it = tomlkit.string(text, literal=literal, multiline=multiline)
    again = reparse_value(it)
    stitch.check(str(again) == text, "a string() item parses back to its text")
    return it


@stitch.endpoint(bails_on=(ConvertError,))
def py_item(v: Val[PyValue, 256]) -> New[Item]:
    it = tomlkit.item(v)
    again = reparse_value(it)
    stitch.check(same(again.unwrap(), it.unwrap()), "item(v) parses back to the same value")
    stitch.check(same(it.unwrap(), v), "item(v).unwrap() == v")
    return it


@stitch.endpoint(bails_on=(TOMLKitError, ValueError, ZeroDivisionError, OverflowError))
def item_binop(it: Borrow[Item], op: OneOf[["+", "-", "*", "//", "%", "/", "r+", "r-"]], n: I32, x: F64,
               text: Str[12], use_float: Bool) -> New[Item]:
    """Arithmetic on a value item gives an item with the same value as on the plain value."""
    base = it.unwrap()
    if isinstance(it, Integer):
        arg = x if use_float else n
    elif isinstance(it, Float):
        arg = x if use_float else n
    elif isinstance(it, String):
        stitch.require(op in ("+", "r+"), "strings only add")
        arg = text
    elif isinstance(it, (DateTime, Date)):
        stitch.require(op in ("+", "-", "r+"), "dates only add or subtract")
        arg = dt.timedelta(days=n % 100000, seconds=int(x) % 86400 if math.isfinite(x) else 0)
    else:
        stitch.bail("no arithmetic on this item")
    fns = {"+": lambda a, b: a + b, "-": lambda a, b: a - b, "*": lambda a, b: a * b,
           "//": lambda a, b: a // b, "%": lambda a, b: a % b, "/": lambda a, b: a / b,
           "r+": lambda a, b: b + a, "r-": lambda a, b: b - a}
    expect = fns[op](base, arg)
    res = fns[op](it, arg)
    stitch.require(hasattr(res, "as_string"), "result is not an item")
    stitch.check(same(res.unwrap(), expect), "item arithmetic agrees with the plain value")
    again = reparse_value(res)
    stitch.check(same(again.unwrap(), res.unwrap()), "an arithmetic result prints as its value")
    return res


@stitch.endpoint(bails_on=(TOMLKitError, ValueError, OverflowError))
def item_replace(it: Borrow[Item], field: OneOf[["year", "month", "day", "hour", "minute", "second",
                                                 "microsecond", "tzinfo", "fold"]], n: I32) -> New[Item]:
    stitch.require(isinstance(it, (DateTime, Date, Time)), "not a date or time")
    if field == "tzinfo":
        v = None if n % 3 == 0 else dt.timezone(dt.timedelta(minutes=n % 1440))
    elif field == "fold":
        v = n % 2
    else:
        v = n
    try:
        expect = it.unwrap().replace(**{field: v})
    except TypeError:
        stitch.bail("field not on this type")
    res = it.replace(**{field: v})
    if isinstance(res, Time) and res.tzinfo is not None:
        expect = expect.replace(tzinfo=None)           # TOML local times have no offset
        stitch.check(same(res.unwrap().replace(tzinfo=None), expect), "replace() agrees with the plain value")
    else:
        stitch.check(same(res.unwrap(), expect), "replace() agrees with the plain value")
    again = reparse_value(res)
    a, b = again.unwrap(), res.unwrap()
    if isinstance(b, dt.time):
        b = b.replace(tzinfo=None, fold=0)
    stitch.check(same(a, b) or (isinstance(a, dt.datetime) and a == b), "a replaced date prints as its value")
    return res


@stitch.endpoint(bails_on=(TOMLKitError, ValueError, OverflowError))
def item_astimezone(it: Borrow[Item], minutes: Range[-1439, 1439], local: Bool) -> New[Item]:
    stitch.require(isinstance(it, DateTime), "not a datetime")
    tz = None if local else dt.timezone(dt.timedelta(minutes=minutes))
    res = it.astimezone(tz)
    stitch.check(res == it.unwrap().astimezone(tz), "astimezone() keeps the instant")
    again = reparse_value(res)
    stitch.check(again.unwrap() == res.unwrap(), "a converted datetime prints as its instant")
    return res


@stitch.endpoint(bails_on=(TOMLKitError,))
def item_comment(it: Borrow[Item], text: Str[16]):
    try:
        it.comment(text)
    except ValueError:
        stitch.bail("comment text has control characters")


@stitch.endpoint(bails_on=(TOMLKitError,))
def item_indent(it: Borrow[Item], n: Range[0, 8]):
    it.indent(n)


@stitch.endpoint(bails_on=(TOMLKitError,))
def item_inspect(it: Borrow[Item]):
    it.trivia
    it.discriminant
    it.is_boolean(), it.is_table(), it.is_inline_table(), it.is_aot()
    it.value
    again = reparse_value(it)
    stitch.check(same(again.unwrap(), it.unwrap()) or isinstance(it, Time),
                 "a value item prints as its value")


@stitch.endpoint(bails_on=(TOMLKitError,))
def new_key(a: Str[12, TomlKey], b: Str[12, TomlKey], dotted: Bool) -> New[Key]:
    k = tomlkit.key([a, b]) if dotted else tomlkit.key(a)
    k2, _ = tomlkit.key_value(k.as_string() + " = 1")
    stitch.check(k2 == k, "a key's text parses back to the same key")
    return k


@stitch.endpoint(bails_on=(TOMLKitError,))
def parse_key_value(src: Str[256, KeyValueText]) -> tuple[New[Key], New[Item]]:
    k, v = tomlkit.key_value(src)
    k.is_dotted(), k.is_multi(), list(k)
    return k, v


@stitch.endpoint
def key_concat(a: Borrow[Key], b: Borrow[Key]) -> New[Key]:
    k = a.concat(b)
    stitch.check(list(k) == list(a) + list(b), "concat() joins the parts")
    return k


class Point:
    def __init__(self, x, y):
        self.x, self.y = x, y


def encode_point(obj, _parent=None, _sort_keys=False):
    if isinstance(obj, Point):
        t = tomlkit.inline_table()
        t.update({"x": obj.x, "y": obj.y})
        return t
    raise ConvertError("not a Point")


@stitch.endpoint(bails_on=(TOMLKitError,))
def custom_encoder(doc: Borrow[Doc], key: Str[16, TomlKey], x: I64, y: F64):
    tomlkit.register_encoder(encode_point)
    try:
        doc[key] = Point(x, y)
    finally:
        tomlkit.unregister_encoder(encode_point)
    try:
        tomlkit.item(Point(x, y))
    except ConvertError:
        pass
    else:
        stitch.check(False, "an unregistered encoder is not used")


# ---------------------------------------------------------------------------------------------
# the main oracle


@stitch.endpoint
def roundtrip(doc: Borrow[Doc]):
    text = doc.as_string()
    stitch.log("printed: %r", text)
    try:
        again = tomlkit.parse(text)
    except TOMLKitError as e:
        stitch.check(False, f"printed document parses ({type(e).__name__})")
        return
    # unordered: tomlkit moves plain values ahead of tables so they do not land in a table's body
    stitch.check(same(again.unwrap(), prune_empty_super(doc, doc.unwrap()), ordered=False),
                 "printed document has the same data")
