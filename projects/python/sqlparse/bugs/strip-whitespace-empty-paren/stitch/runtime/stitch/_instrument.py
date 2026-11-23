"""Coverage and comparison logging for the code under test.

Coverage: sys.monitoring (PEP 669) LINE and BRANCH events, enabled only on code objects of
target modules. A callback returns DISABLE, so a location reports once and then costs nothing.
Events are not re-armed for every program (restart_events() and the callbacks it brings back
cost several times the program itself on larger libraries): a program reports the locations
this worker has not seen yet, which is what the engine needs to recognize new coverage. They are
re-armed after a program that ended in a finding (the engine does not keep those, so their
locations must be reported again) and every RESTART_EVERY programs. STITCH_PY_COVERAGE=full
re-arms them before every program. Edge ids hash (file, qualname, first line, location), so
they are stable across worker restarts.

Comparisons: target modules are compiled from source through an import hook that rewrites
`a == b`, `a != b`, `a in b`, `a not in b` and `<`/`>` comparisons into `__stitch_cmp__(a, b, op)`
(the Atheris approach). While the engine traces a program, differing operands are recorded so
input-to-state mutations can copy the expected value into the input.

Target modules: STITCH_PY_INCLUDE (comma-separated module prefixes) plus `harness`; without it,
every module outside the standard library and the STITCH++ runtime.
"""
import ast
import builtins
import importlib.abc
import importlib.machinery
import operator
import os
import struct
import sys
import sysconfig
import zlib

MAP_SIZE = 1 << 16
_MASK = MAP_SIZE - 1
TOOL = 4  # sys.monitoring tool id (0-5; 1 is coverage.py's)

_hits = []
_cmps = []
_cmp_seen = set()
_cmplog = False
_node = 0
_base = {}
_active = False
_enabled = False
_include = [p.strip() for p in os.environ.get("STITCH_PY_INCLUDE", "").split(",") if p.strip()]
_exclude = [p.strip() for p in os.environ.get("STITCH_PY_EXCLUDE", "").split(",") if p.strip()]
_STDLIB = tuple({os.path.realpath(sysconfig.get_paths()[k]) for k in ("stdlib", "platstdlib")})
_RUNTIME = os.path.dirname(os.path.realpath(__file__))


# ------------------------------------------------------------------------------ per program
RESTART_EVERY = 1 if os.environ.get("STITCH_PY_COVERAGE") == "full" else 256
_since_restart = 0
_rearm = False


def rearm():
    """Report every location again from the next program on (after a finding)."""
    global _rearm
    _rearm = True


def begin_exec(cmplog=None):
    global _cmplog, _since_restart, _rearm
    _hits.clear()
    _cmps.clear()
    _cmp_seen.clear()
    if cmplog is not None:
        _cmplog = cmplog
    if _enabled:
        _since_restart += 1
        if _rearm or _since_restart >= RESTART_EVERY:
            sys.monitoring.restart_events()
            _since_restart = 0
            _rearm = False


def end_exec():
    global _cmplog
    _cmplog = False


def set_node(i):
    global _node
    _node = i


def edges_bytes():
    ids = set(_hits)
    out = bytearray(struct.pack("<I", len(ids)))
    for e in ids:
        out += struct.pack("<IB", e, 1)
    return bytes(out)


def cmps_bytes():
    return struct.pack("<I", len(_cmps)) + b"".join(_cmps)


# ------------------------------------------------------------------------------ coverage
def _register(code):
    if code in _base:
        return
    key = f"{code.co_filename}\0{getattr(code, 'co_qualname', code.co_name)}\0{code.co_firstlineno}".encode()
    _base[code] = zlib.crc32(key)
    if _enabled:
        sys.monitoring.set_local_events(TOOL, code, sys.monitoring.events.LINE | sys.monitoring.events.BRANCH)
    for c in code.co_consts:
        if isinstance(c, type(code)):
            _register(c)


def _on_line(code, line):
    b = _base.get(code)
    if b is not None:
        _hits.append((b ^ (line * 0x9E3779B1)) & _MASK)
    return sys.monitoring.DISABLE


def _on_branch(code, src, dst):
    b = _base.get(code)
    if b is not None:
        _hits.append((b ^ (src * 0x85EBCA6B) ^ (dst * 0xC2B2AE35) ^ 0x5BD1E995) & _MASK)
    return sys.monitoring.DISABLE


# ------------------------------------------------------------------------------ comparisons
def _contains(a, b):
    return a in b


def _not_contains(a, b):
    return a not in b


_OPS = (operator.eq, operator.ne, _contains, _not_contains, operator.lt, operator.le, operator.gt, operator.ge)
_AST_OPS = {ast.Eq: 0, ast.NotEq: 1, ast.In: 2, ast.NotIn: 3, ast.Lt: 4, ast.LtE: 5, ast.Gt: 6, ast.GtE: 7}


def _as_bytes(v):
    if isinstance(v, str):
        return v.encode("utf-8", "replace")
    if isinstance(v, (bytes, bytearray)):
        return bytes(v)
    return None


def _rec_int(a, b):
    a &= 0xFFFFFFFFFFFFFFFF
    b &= 0xFFFFFFFFFFFFFFFF
    k = (_node, 0, a, b)
    if k in _cmp_seen or len(_cmps) >= 16384:
        return
    _cmp_seen.add(k)
    _cmps.append(struct.pack("<HBBBBBB", _node, 0, 8, 8, 8, 0, 0) + a.to_bytes(8, "little").ljust(32, b"\0")
                 + b.to_bytes(8, "little").ljust(32, b"\0"))


def _rec_bytes(a, b):
    a, b = a[:32], b[:32]
    if not a or not b:
        return
    k = (_node, 1, a, b)
    if k in _cmp_seen or len(_cmps) >= 16384:
        return
    _cmp_seen.add(k)
    _cmps.append(struct.pack("<HBBBBBB", _node, 1, 0, len(a), len(b), 0, 0) + a.ljust(32, b"\0") + b.ljust(32, b"\0"))


def _record(a, b, op):
    ta, tb = type(a), type(b)
    if op >= 2 and op <= 3:  # membership
        ba = _as_bytes(a)
        if ba is not None and _as_bytes(b) is not None:
            _rec_bytes(ba, _as_bytes(b))
        elif ta in (str, bytes, int) and tb in (set, frozenset, dict, list, tuple) and len(b) <= 256:
            for x in list(b)[:64]:
                if type(x) is ta:
                    if ta is int:
                        _rec_int(a, x)
                    else:
                        _rec_bytes(_as_bytes(a), _as_bytes(x))
        return
    if ta is int and tb is int:
        if a != b:
            _rec_int(a, b)
        return
    ba, bb = _as_bytes(a), _as_bytes(b)
    if ba is not None and bb is not None and ba != bb:
        _rec_bytes(ba, bb)


def __stitch_cmp__(a, b, op):
    r = _OPS[op](a, b)
    if _cmplog:
        try:
            _record(a, b, op)
        except Exception:
            pass
    return r


class _CmpRewriter(ast.NodeTransformer):
    def visit_Compare(self, node):
        self.generic_visit(node)
        if len(node.ops) != 1:
            return node
        code = _AST_OPS.get(type(node.ops[0]))
        if code is None:
            return node
        call = ast.Call(func=ast.Name("__stitch_cmp__", ast.Load()), args=[node.left, node.comparators[0], ast.Constant(code)],
                        keywords=[])
        return ast.copy_location(call, node)


# ------------------------------------------------------------------------------ import hook
def _wanted(fullname, origin):
    if fullname == "harness" or fullname.startswith("harness."):
        return True
    if any(fullname == p or fullname.startswith(p + ".") for p in _exclude):
        return False
    if _include:
        return any(fullname == p or fullname.startswith(p + ".") for p in _include)
    if not origin:
        return False
    o = os.path.realpath(origin)
    if o.startswith(_RUNTIME):
        return False
    return not (o.startswith(_STDLIB) and "site-packages" not in o and "dist-packages" not in o)


class _Loader(importlib.machinery.SourceFileLoader):
    def get_code(self, fullname):
        path = self.get_filename(fullname)
        return self.source_to_code(self.get_data(path), path)

    def source_to_code(self, data, path, *, _optimize=-1):
        tree = ast.parse(data, filename=path)
        tree = ast.fix_missing_locations(_CmpRewriter().visit(tree))
        code = compile(tree, path, "exec", dont_inherit=True, optimize=_optimize)
        _register(code)
        return code


class _Finder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path, target=None):
        for f in sys.meta_path:
            if f is self or not hasattr(f, "find_spec"):
                continue
            spec = f.find_spec(fullname, path, target)
            if spec is None:
                continue
            ld = spec.loader
            if isinstance(ld, importlib.machinery.SourceFileLoader) and _wanted(fullname, spec.origin):
                spec.loader = _Loader(ld.name, ld.path)
            return spec
        return None


def install(coverage=True):
    """Instrument modules imported from now on. Call before importing the harness."""
    global _active, _enabled
    if _active:
        return
    _active = True
    builtins.__stitch_cmp__ = __stitch_cmp__
    sys.meta_path.insert(0, _Finder())
    if coverage and hasattr(sys, "monitoring"):
        m = sys.monitoring
        try:
            m.use_tool_id(TOOL, "stitch")
        except ValueError:
            return
        m.register_callback(TOOL, m.events.LINE, _on_line)
        m.register_callback(TOOL, m.events.BRANCH, _on_branch)
        _enabled = True
