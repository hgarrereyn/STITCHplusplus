"""tomlkit: parse, edit and print TOML while keeping its formatting.

Oracles: printing a parsed document gives back the input text, and the text of an edited
document parses to the same data.
"""
import math

import stitch
import tomlkit
from stitch import Bool, Borrow, F64, I64, New, OneOf, Src, Str
from tomlkit.exceptions import TOMLKitError

Doc = stitch.object("Doc")
Table = stitch.object("Table")
Arr = stitch.object("Arr")
TomlText = stitch.datatype("TomlText")
TomlKey = stitch.datatype("TomlKey")

KINDS = ("int", "str", "float", "bool", "date", "datetime", "array", "inline", "multiline", "literal")


@stitch.decoder(TomlText)
def toml_text(s):
    def key():
        return s.token() if s.coin(0.5) else s.pick(["a", "b.c", '"q k"', "'lit'", "x-y", "1"])

    def value(depth=0):
        k = s.choice(9 if depth < 2 else 7)
        if k == 0:
            return str(s.i64())
        if k == 1:
            return '"' + s.str(12).replace("\\", "\\\\").replace('"', '\\"') + '"'
        if k == 2:
            return s.pick(["1.5", "-0.0", "inf", "nan", "1e10", "+3.25"])
        if k == 3:
            return s.pick(["true", "false"])
        if k == 4:
            return s.pick(["1979-05-27", "07:32:00", "1979-05-27T07:32:00Z", "1979-05-27 07:32:00.999-07:00"])
        if k == 5:
            return "'''" + s.str(10) + "'''"
        if k == 6:
            return s.token()
        if k == 7:
            return "[" + ", ".join(value(depth + 1) for _ in range(s.choice(4))) + ("," if s.coin(0.2) else "") + "]"
        return "{" + ", ".join(f"{key()} = {value(depth + 1)}" for _ in range(s.choice(3))) + "}"

    lines = []
    while s.more():
        c = s.choice(6)
        if c == 0:
            lines.append(f"[{key()}]")
        elif c == 1:
            lines.append(f"[[{key()}]]")
        elif c == 2:
            lines.append("# " + s.str(10))
        else:
            lines.append(f"{key()} = {value()}" + (" # c" if s.coin(0.2) else ""))
    return "\n".join(lines) + ("\n" if s.coin(0.8) else "")


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


@stitch.endpoint(bails_on=(TOMLKitError,))
def parse(text: Str[2048, TomlText]) -> New[Doc]:
    doc = tomlkit.parse(text)
    stitch.check(doc.as_string() == text, "parse/print preserves the text")
    return doc


@stitch.endpoint
def document() -> New[Doc]:
    return tomlkit.document()


@stitch.endpoint(bails_on=(TOMLKitError,))
def set_value(doc: Borrow[Doc], key: Str[16, TomlKey], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    doc[key] = make_value(kind, n, x, text)


@stitch.endpoint(bails_on=(TOMLKitError,))
def add_table(doc: Borrow[Doc], key: Str[16, TomlKey], super_table: Bool) -> New[Table]:
    doc.add(key, tomlkit.table(is_super_table=super_table))
    return doc[key]


def existing_key(container, s):
    keys = list(container.keys())
    stitch.require(keys, "no keys")
    return s.pick(keys)


@stitch.endpoint(bails_on=(TOMLKitError,))
def get_table(doc: Borrow[Doc], s: Src[8]) -> New[Table]:
    t = doc[existing_key(doc, s)]
    stitch.require(isinstance(t, dict), "not a table")
    return t


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_set(t: Borrow[Table], key: Str[16, TomlKey], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    t[key] = make_value(kind, n, x, text)


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_add_table(t: Borrow[Table], key: Str[16, TomlKey]) -> New[Table]:
    t[key] = tomlkit.table()
    return t[key]


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_remove(t: Borrow[Table], s: Src[8]):
    del t[existing_key(t, s)]


@stitch.endpoint(bails_on=(TOMLKitError,))
def table_comment(t: Borrow[Table], text: Str[16]):
    stitch.require(hasattr(t, "comment"), "no comment() on this table type")
    t.comment(text)


@stitch.endpoint(bails_on=(TOMLKitError,))
def remove(doc: Borrow[Doc], s: Src[8]):
    doc.remove(existing_key(doc, s))


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


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_append(a: Borrow[Arr], kind: OneOf[KINDS], n: I64, x: F64, text: Str[24]):
    v = make_value(kind, n, x, text)
    if isinstance(a, tomlkit.items.AoT):
        t = tomlkit.table()
        t["v"] = v
        a.append(t)
    else:
        a.append(v)


@stitch.endpoint(bails_on=(TOMLKitError,))
def array_delete(a: Borrow[Arr], i: I64):
    stitch.require(len(a) > 0, "empty")
    del a[i % len(a)]


@stitch.endpoint(bails_on=(TOMLKitError,))
def comment_and_newline(doc: Borrow[Doc], text: Str[16]):
    doc.add(tomlkit.comment(text))
    doc.add(tomlkit.nl())


def same(a, b):
    """Data equality where nan == nan."""
    if isinstance(a, float) and isinstance(b, float):
        return a == b or (math.isnan(a) and math.isnan(b))
    if isinstance(a, dict) and isinstance(b, dict):
        return list(a) == list(b) and all(same(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    return type(a) is type(b) and a == b


@stitch.endpoint
def roundtrip(doc: Borrow[Doc]):
    text = doc.as_string()
    stitch.log("printed: %r", text)
    try:
        again = tomlkit.parse(text)
    except TOMLKitError as e:
        stitch.check(False, f"printed document parses ({type(e).__name__})")
        return
    stitch.check(same(again.unwrap(), doc.unwrap()), "printed document has the same data")
