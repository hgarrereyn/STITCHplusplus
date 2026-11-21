"""STITCH harness API for Python.

    import stitch
    from stitch import Borrow, Take, New, Str, Bytes, I32, Range, OneOf, Src

    Doc = stitch.object("Doc")
    TomlText = stitch.datatype("TomlText")

    @stitch.endpoint(bails_on=(tomlkit.exceptions.ParseError,))
    def parse(text: Str[4096, TomlText]) -> New[Doc]:
        return tomlkit.parse(text)

    @stitch.endpoint
    def set_key(doc: Borrow[Doc], key: Str[32], value: I32):
        doc[key] = value

An exception a stub does not declare in `bails_on` is a finding.
Reference: `stitch guide python`.
"""
from ._runtime import REG, RT, Bail, CheckFailed, Endpoint, _MISSING
from ._source import Source
from ._spec import (F32, F64, I8, I16, I32, I64, U8, U16, U32, U64, Bool, Borrow, Bytes, File, Flags, New, OneOf, Range,
                    Src, Str, Take)

__all__ = ["endpoint", "object", "time_limit", "datatype", "decoder", "init", "bail", "require", "check", "log", "consume", "set",
           "get", "get_obj", "has", "delete", "global_set", "global_get", "Source", "Borrow", "Take", "New", "Str",
           "Bytes", "File", "Src", "Range", "OneOf", "Flags", "Bool", "U8", "U16", "U32", "U64", "I8", "I16", "I32",
           "I64", "F32", "F64", "Bail"]


def endpoint(fn=None, *, name=None, bails_on=()):
    """Register a stub. Its annotations are the spec. `bails_on`: exceptions that are not bugs."""
    def wrap(f):
        REG.add_endpoint(Endpoint(f, name or f.__name__, bails_on))
        return f
    return wrap(fn) if fn is not None else wrap


def object(name, drop=None):
    """Declare an object type. `drop(value)` frees objects left over at the end of a program."""
    return REG.object(name, drop)


def datatype(name):
    """Declare a data type: harness/data/<name>/ holds seeds and *.dict tokens."""
    return REG.datatype(name if isinstance(name, str) else name.name)


def decoder(dt):
    """Register a decoder for a data type: fn(Source) -> str | bytes."""
    d = datatype(dt)

    def wrap(fn):
        d.decoder = fn
        return fn
    return wrap


def init(fn):
    """Run fn once per worker process, after the harness is loaded."""
    REG.inits.append(fn)
    return fn


def bail(reason="bail"):
    """Stop this call; not a bug. The program continues."""
    raise Bail(reason)


def require(cond, reason="requirement failed"):
    if not cond:
        raise Bail(reason)


def check(cond, what="check"):
    """An oracle: a false condition is reported as a bug ("check failed: what")."""
    if not cond:
        raise CheckFailed(what)


def log(msg, *args):
    """Printed by `stitch exec --trace`."""
    if RT.trace:
        import sys
        sys.stderr.write("[stitch]     log: " + (msg % args if args else str(msg)) + "\n")


def consume(obj):
    """The library took ownership of a borrowed input in this call."""
    RT.consume(obj)


def set(obj, key, value):
    """Attach state to an object: ints, strings, other objects (links die with them), anything."""
    RT.attr_set(obj, key, value)


def get(obj, key, default=_MISSING):
    """Read an attribute. Without a default, a missing key is reported by `stitch probe`."""
    return RT.attr_get(obj, key, default)


def get_obj(obj, key):
    """A linked object, or None if it is missing or was consumed."""
    return RT.attr_get(obj, key, None)


def has(obj, key):
    return RT.attr_has(obj, key)


def delete(obj, key):
    RT.attr_del(obj, key)


def global_set(key, value):
    RT.globals[key] = value


def global_get(key, default=_MISSING):
    if key in RT.globals:
        return RT.globals[key]
    if default is _MISSING:
        RT.note_miss(key)
        return None
    return default


class time_limit:
    """`with stitch.time_limit(0.5): ...` bails ("time limit") if the block runs longer.

    For library calls that are slow by design (iteration without an end): they become bails
    instead of hanging the worker until the engine's timeout kills it.
    """

    def __init__(self, seconds):
        self.seconds = seconds

    def __enter__(self):
        import signal

        def fire(signum, frame):
            raise Bail("time limit")
        self.prev = signal.signal(signal.SIGALRM, fire)
        signal.setitimer(signal.ITIMER_REAL, self.seconds)
        return self

    def __exit__(self, *exc):
        import signal
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, self.prev)
        return False
