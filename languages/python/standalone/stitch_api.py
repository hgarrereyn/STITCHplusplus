# ---------------------------------------------------------------------------------------------
# Standalone stand-in for the STITCH++ runtime (written next to repro.py by `stitch write`).
# The harness code above runs unchanged: decorators only record bails_on, stitch.bail/check and
# attributes work without the fuzzer, and Source replays the recorded choices.
#   stitch.call(n, name, fn)        a call that succeeded in the original run; a bail = DIVERGED
#   stitch.call_bails(n, name, fn)  a call that bailed in the original run
# STITCH_REPRO_TRACE=1 prints one line per call.
# ---------------------------------------------------------------------------------------------
import os as _os
import sys as _sys
import tempfile as _tempfile


class Bail(BaseException):
    def __init__(self, reason="bail"):
        super().__init__(reason)
        self.reason = reason


class _CheckFailed(BaseException):
    def __init__(self, what):
        super().__init__(what)
        self.what = what


class _Any:
    """Annotation stand-in: Str[64, T], New[Doc] | None, ... evaluate to this."""

    def __class_getitem__(cls, item):
        return cls

    def __or__(self, other):
        return self

    __ror__ = __or__


Borrow = Take = New = Str = Bytes = File = Src = Range = OneOf = Flags = _Any
U8 = U16 = U32 = U64 = I8 = I16 = I32 = I64 = F32 = F64 = Bool = _Any
_bails_on = {}
_attrs = {}
_globals = {}
_cur = [-1, "?"]
_TRACE = bool(_os.environ.get("STITCH_REPRO_TRACE"))


class _Named:
    def __init__(self, name):
        self.name = name


def object(name, drop=None):
    return _Named(name)


def datatype(name):
    return _Named(name)


def decoder(dt):
    return lambda fn: fn


def init(fn):
    fn()
    return fn


def endpoint(fn=None, *, name=None, bails_on=()):
    def wrap(f):
        _bails_on[f.__name__] = tuple(bails_on)
        return f
    return wrap(fn) if fn is not None else wrap


def bail(reason="bail"):
    raise Bail(reason)


def require(cond, reason="requirement failed"):
    if not cond:
        raise Bail(reason)


def check(cond, what="check"):
    if not cond:
        raise _CheckFailed(what)


def log(msg, *args):
    if _TRACE:
        _sys.stderr.write("[repro]     log: " + (msg % args if args else str(msg)) + "\n")


def consume(obj):
    pass


def set(obj, key, value):
    _attrs.setdefault(id(obj), {})[key] = value


def get(obj, key, default=None):
    return _attrs.get(id(obj), {}).get(key, default)


get_obj = get


def has(obj, key):
    return key in _attrs.get(id(obj), {})


def delete(obj, key):
    _attrs.get(id(obj), {}).pop(key, None)


def global_set(key, value):
    _globals[key] = value


def global_get(key, default=None):
    return _globals.get(key, default)


def file(data):
    """Temporary file holding `data` (a File[...] parameter)."""
    fd, path = _tempfile.mkstemp(prefix="stitch-repro-")
    with _os.fdopen(fd, "wb") as f:
        f.write(data)
    return path


def _diverged(why):
    _sys.stderr.write(f"\n[repro] DIVERGED at call #{_cur[0]} {_cur[1]}: {why}\n")
    _sys.exit(99)


def call(n, name, fn):
    _cur[0], _cur[1] = n, name
    try:
        r = fn()
    except Bail as b:
        _diverged(f'bailed: "{b.reason}" (it succeeded in the original run)')
    if _TRACE:
        _sys.stderr.write(f"[repro] #{n} {name} -> ok\n")
    return r


def call_bails(n, name, fn):
    _cur[0], _cur[1] = n, name
    try:
        fn()
    except Bail:
        pass
    except BaseException as e:  # noqa: B902
        if not isinstance(e, _bails_on.get(name, ())):
            raise
    if _TRACE:
        _sys.stderr.write(f"[repro] #{n} {name} -> bail\n")


def run(main):
    """Run the program; report a crash in the same format as the fuzzer."""
    try:
        main()
    except _CheckFailed as c:
        _sys.stderr.write(f"\n[stitch] CHECK FAILED: {c.what} (node {_cur[0]})\n")
        _sys.exit(1)
    except SystemExit:
        raise
    except BaseException as e:  # noqa: B902
        t = type(e)
        kind = t.__qualname__ if t.__module__ in ("builtins", "__main__") else f"{t.__module__}.{t.__qualname__}"
        msg = str(e).replace("\n", " ")[:500]
        _sys.stderr.write(f"\n[stitch] EXCEPTION in {_cur[1]}: {kind}: {msg}\n")
        frames, tb = [], e.__traceback__
        while tb is not None:
            co = tb.tb_frame.f_code
            if co.co_filename != __file__:
                frames.append((getattr(co, "co_qualname", co.co_name), _os.path.abspath(co.co_filename), tb.tb_lineno))
            tb = tb.tb_next
        for k, (fn, path, ln) in enumerate(frames[::-1][:24]):
            _sys.stderr.write(f"    #{k} 0x0 in {fn} {path}:{ln}\n")
        _sys.exit(1)
