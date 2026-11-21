"""Signature vocabulary: what a stub's annotations mean.

    def stub(doc: Borrow[Doc], key: Str[64], n: Range[0, 9]) -> New[Item]: ...

Inputs are objects (Borrow / Take). Parameters are fuzzed values; their byte encoding matches the
C++ runtime so the engine mutates them the same way.
"""
import struct


class ObjectType:
    """An object type: a name for values that stubs produce and consume."""

    def __init__(self, name, drop=None):
        self.name = name
        self.drop = drop

    def __repr__(self):
        return f"<stitch object {self.name}>"


class Datatype:
    """A data type: a name for a kind of fuzzed value (seeds, dictionary, decoder)."""

    def __init__(self, name):
        self.name = name
        self.decoder = None

    def __repr__(self):
        return f"<stitch datatype {self.name}>"


def _name_of(x):
    if x is None:
        return None
    if isinstance(x, (Datatype, ObjectType)):
        return x.name
    if isinstance(x, str):
        return x
    raise TypeError(f"expected a stitch.datatype(...) or a name, got {x!r}")


# ---------------------------------------------------------------------------------- objects
class _ObjSpec:
    mode = "borrow"

    def __init__(self, ty):
        if not isinstance(ty, ObjectType):
            raise TypeError(f"{type(self).__name__}[...] takes an object type from stitch.object(), got {ty!r}")
        self.type = ty

    def __repr__(self):
        return f"{type(self).__name__}[{self.type.name}]"


class _BorrowSpec(_ObjSpec):
    mode = "borrow"


class _TakeSpec(_ObjSpec):
    mode = "take"


class _NewSpec(_ObjSpec):
    def __init__(self, ty, optional=False):
        super().__init__(ty)
        self.optional = optional

    def __or__(self, other):
        if other is None:
            return _NewSpec(self.type, optional=True)
        return NotImplemented

    __ror__ = __or__


class Borrow:
    """Borrow[T]: uses an object; it stays available. `stitch.consume(x)` if the library took it."""

    def __class_getitem__(cls, ty):
        return _BorrowSpec(ty)


class Take:
    """Take[T]: consumes the object."""

    def __class_getitem__(cls, ty):
        return _TakeSpec(ty)


class New:
    """New[T]: the stub returns a new object. `New[T] | None`: optional output."""

    def __class_getitem__(cls, ty):
        return _NewSpec(ty)


# ---------------------------------------------------------------------------------- params
class Param:
    kind = ""
    ctype = ""
    size = 0
    signed = False
    lo = hi = 0
    values = ()
    datatype = None

    def decode(self, raw, form, rt):
        raise NotImplementedError

    def manifest(self, name):
        d = {"kind": self.kind, "type": self.ctype, "size": self.size, "signed": self.signed, "datatype": self.datatype,
             "name": name}
        if self.kind == "range":
            d["lo"], d["hi"] = self.lo, self.hi
        if self.values:
            d["values"] = list(self.values)
        return d


def _le_int(raw, size, signed):
    v = int.from_bytes(bytes(raw[:size]).ljust(size, b"\0"), "little")
    if signed and v >> (8 * size - 1):
        v -= 1 << (8 * size)
    return v


class IntParam(Param):
    kind = "int"

    def __init__(self, size, signed, ctype):
        self.size, self.signed, self.ctype = size, signed, ctype

    def decode(self, raw, form, rt):
        return _le_int(raw, self.size, self.signed)


class FloatParam(Param):
    kind = "float"

    def __init__(self, size):
        self.size = size
        self.ctype = "F32" if size == 4 else "F64"

    def decode(self, raw, form, rt):
        b = bytes(raw[:self.size]).ljust(self.size, b"\0")
        return struct.unpack("<f" if self.size == 4 else "<d", b)[0]


class BoolParam(Param):
    kind, ctype, size = "bool", "Bool", 1

    def decode(self, raw, form, rt):
        return bool(raw and raw[0] & 1)


class RangeParam(Param):
    kind, ctype, size, signed = "range", "Range", 8, True

    def __init__(self, lo, hi):
        if lo > hi:
            raise ValueError("Range[lo, hi] needs lo <= hi")
        self.lo, self.hi = int(lo), int(hi)

    def decode(self, raw, form, rt):
        v = _le_int(raw, 8, True)
        if self.lo <= v <= self.hi:
            return v
        span = (self.hi - self.lo + 1) & 0xFFFFFFFFFFFFFFFF
        off = (v - self.lo) & 0xFFFFFFFFFFFFFFFF
        return self.lo + (off % span if span else off)


class OneOfParam(Param):
    """One of the listed values (any Python constants). The engine sees indices."""
    kind, ctype, size = "oneof", "OneOf", 8

    def __init__(self, choices):
        if not choices:
            raise ValueError("OneOf[...] needs at least one value")
        self.choices = list(choices)
        self.values = tuple(range(len(self.choices)))

    def index(self, raw):
        v = _le_int(raw, 8, True)
        return v if 0 <= v < len(self.choices) else (v & 0xFFFFFFFFFFFFFFFF) % len(self.choices)

    def decode(self, raw, form, rt):
        return self.choices[self.index(raw)]


class FlagsParam(Param):
    kind, ctype, size = "flags", "Flags", 8

    def __init__(self, flags):
        self.values = tuple(int(f) for f in flags)
        self.mask = 0
        for f in self.values:
            self.mask |= f

    def decode(self, raw, form, rt):
        return _le_int(raw, 8, True) & self.mask


class BufParam(Param):
    """Bytes / Str / File: a buffer of at most `size` bytes, optionally of a data type."""

    def __init__(self, kind, size, datatype=None):
        self.kind = kind
        self.ctype = {"bytes": "Bytes", "str": "Str", "file": "File"}[kind]
        self.size = int(size)
        self.datatype = _name_of(datatype)

    def raw_value(self, raw, form, rt):
        return rt.decode_param(self.datatype, raw, form, self.size)

    def decode(self, raw, form, rt):
        b = self.raw_value(raw, form, rt)
        if self.kind == "bytes":
            return b
        if self.kind == "str":
            return b.decode("utf-8", "replace")
        return rt.temp_file(b)


class SrcParam(Param):
    kind, ctype = "source", "Source"

    def __init__(self, size):
        self.size = int(size)

    def decode(self, raw, form, rt):
        return rt.new_source(bytes(raw[:self.size]), self.size)


def _sized(cls_kind, args, default):
    if not isinstance(args, tuple):
        args = (args,)
    size = default
    dt = None
    for a in args:
        if isinstance(a, int):
            size = a
        else:
            dt = a
    return BufParam(cls_kind, size, dt)


class Bytes:
    """Bytes[max, Tag]: a bytes value of at most max bytes."""

    def __class_getitem__(cls, args):
        return _sized("bytes", args, 256)


class Str:
    """Str[max, Tag]: a str (UTF-8 decoded, invalid bytes replaced)."""

    def __class_getitem__(cls, args):
        return _sized("str", args, 256)


class File:
    """File[max, Tag]: path of a temporary file holding the data (valid during the call)."""

    def __class_getitem__(cls, args):
        return _sized("file", args, 4096)


class Range:
    """Range[lo, hi]: an int in [lo, hi]."""

    def __class_getitem__(cls, args):
        lo, hi = args
        return RangeParam(lo, hi)


class OneOf:
    """OneOf[a, b, c]: one of the listed values (ints, strings, enums, None ...)."""

    def __class_getitem__(cls, args):
        return OneOfParam(args if isinstance(args, tuple) else (args,))


class Flags:
    """Flags[A, B, C]: bitwise OR of a subset of the flags."""

    def __class_getitem__(cls, args):
        return FlagsParam(args if isinstance(args, tuple) else (args,))


class Src:
    """Src[max]: a stitch.Source for choices made inside the stub."""

    def __class_getitem__(cls, size):
        return SrcParam(size)


U8 = IntParam(1, False, "U8")
U16 = IntParam(2, False, "U16")
U32 = IntParam(4, False, "U32")
U64 = IntParam(8, False, "U64")
I8 = IntParam(1, True, "I8")
I16 = IntParam(2, True, "I16")
I32 = IntParam(4, True, "I32")
I64 = IntParam(8, True, "I64")
F32 = FloatParam(4)
F64 = FloatParam(8)
Bool = BoolParam()


def resolve(ann, where):
    """Annotation -> ('obj', spec) | ('param', Param)."""
    if isinstance(ann, (_BorrowSpec, _TakeSpec)):
        return "obj", ann
    if isinstance(ann, _NewSpec):
        raise TypeError(f"{where}: New[...] is for return values")
    if isinstance(ann, ObjectType):
        return "obj", _BorrowSpec(ann)
    if isinstance(ann, Param):
        return "param", ann
    plain = {int: I64, float: F64, bool: Bool, str: BufParam("str", 256), bytes: BufParam("bytes", 256)}
    if ann in plain:
        return "param", plain[ann]
    raise TypeError(f"{where}: unsupported annotation {ann!r} (use Borrow[T], Take[T], Str[n], Bytes[n], I32, Range[a, b], ...)")
