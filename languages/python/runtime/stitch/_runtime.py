"""Registry, manifest and program execution (a port of languages/cpp/runtime/src/runtime.cpp)."""
import inspect
import json
import os
import struct
import sys
import tempfile
import types
import zlib

from . import _instrument
from ._source import Source

# set by the engine's `fuzz` command for the workers it starts (not for exec / triage)
_FUZZING = os.environ.get("STITCH_FUZZING") == "1"
from ._spec import (BufParam, Datatype, ObjectType, OneOfParam, Param, SrcParam, ValParam, _NewSpec, resolve)

ST_NOT_RUN, ST_OK, ST_BAIL, ST_SKIP_MISSING, ST_SKIP_INVALID = 0, 1, 2, 3, 4
MAX_NODES = 256
STRINGS_SIZE = 65536
RUNTIME_DIR = os.path.dirname(os.path.abspath(__file__))


class Bail(BaseException):
    """Raised by stitch.bail(): this call stops, the program continues. Not a bug."""

    def __init__(self, reason="bail"):
        super().__init__(reason)
        self.reason = reason


class CheckFailed(BaseException):
    def __init__(self, what):
        super().__init__(what)
        self.what = what


class Finding(Exception):
    """A bug was found in the current program (report already printed)."""


def fnv1a(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h or 1


# ============================================================================== registry
class Endpoint:
    def __init__(self, fn, name, bails_on):
        self.fn = fn
        self.name = name
        self.bails_on = tuple(bails_on or ())
        code = fn.__code__
        self.file = os.path.relpath(code.co_filename, os.environ.get("STITCH_PROJECT", os.getcwd()))
        self.line = code.co_firstlineno
        self.args = []      # (name, 'obj', spec) | (name, 'param', Param)
        self.outputs = []   # (ObjectType, optional)
        self.multi = False
        self._parse_signature()

    def _parse_signature(self):
        fn = self.fn
        anns = dict(getattr(fn, "__annotations__", {}))
        for k, v in list(anns.items()):
            if isinstance(v, str):  # `from __future__ import annotations`
                anns[k] = eval(v, fn.__globals__)
        sig = inspect.signature(fn)
        for pname, p in sig.parameters.items():
            if p.kind in (p.VAR_POSITIONAL, p.VAR_KEYWORD):
                raise TypeError(f"stub {self.name}: *args/**kwargs are not supported")
            if pname not in anns:
                raise TypeError(f"stub {self.name}: parameter '{pname}' needs a type annotation")
            kind, spec = resolve(anns[pname], f"stub {self.name}, parameter '{pname}'")
            self.args.append((pname, kind, spec))
        ret = anns.get("return")
        if ret is None or ret is type(None):
            return
        if isinstance(ret, _NewSpec):
            self.outputs = [(ret.type, ret.optional)]
        elif isinstance(ret, types.GenericAlias) and ret.__origin__ is tuple:
            self.multi = True
            for r in ret.__args__:
                if not isinstance(r, _NewSpec):
                    raise TypeError(f"stub {self.name}: tuple returns must contain New[T] (or New[T] | None)")
                self.outputs.append((r.type, r.optional))
        elif isinstance(ret, ObjectType):
            self.outputs = [(ret, False)]
        else:
            raise TypeError(f"stub {self.name}: unsupported return annotation {ret!r} (use New[T], tuple[New[A], New[B] | None] or None)")
        if len(self.outputs) > 16:
            raise TypeError(f"stub {self.name}: at most 16 outputs")

    @property
    def inputs(self):
        return [(n, s) for n, k, s in self.args if k == "obj"]

    @property
    def params(self):
        return [(n, s) for n, k, s in self.args if k == "param"]


class Registry:
    def __init__(self):
        self.objects = []
        self.datatypes = {}
        self.endpoints = []
        self.by_name = {}
        self.inits = []
        self.dicts = {}

    def object(self, name, drop=None):
        for o in self.objects:
            if o.name == name:
                if drop:
                    o.drop = drop
                return o
        o = ObjectType(name, drop)
        self.objects.append(o)
        return o

    def datatype(self, name):
        if name not in self.datatypes:
            self.datatypes[name] = Datatype(name)
        return self.datatypes[name]

    def add_endpoint(self, ep):
        if ep.name in self.by_name:
            raise ValueError(f"duplicate stub name '{ep.name}' ({self.by_name[ep.name].file} and {ep.file})")
        for _, spec in ep.inputs:
            self._adopt(spec.type)
        for ty, _ in ep.outputs:
            self._adopt(ty)
        for _, p in ep.params:
            if p.datatype:
                self.datatype(p.datatype)
        self.endpoints.append(ep)
        self.by_name[ep.name] = ep

    def _adopt(self, ty):
        if ty not in self.objects:
            same = [o for o in self.objects if o.name == ty.name]
            if same:
                raise ValueError(f"two different object types are named '{ty.name}'")
            self.objects.append(ty)

    def dt_list(self):
        return list(self.datatypes.values())

    def manifest(self):
        out = {"version": 2,
               "objects": [{"name": o.name, "drop": o.drop is not None} for o in self.objects],
               "datatypes": [{"name": d.name, "decoder": d.decoder is not None} for d in self.dt_list()],
               "endpoints": []}
        for e in self.endpoints:
            out["endpoints"].append({
                "name": e.name, "file": e.file,
                "inputs": [{"type": s.type.name, "mode": s.mode, "name": n} for n, s in e.inputs],
                "params": [p.manifest(n) for n, p in e.params],
                "outputs": [{"type": t.name, "optional": o} for t, o in e.outputs],
            })
        return json.dumps(out)


REG = Registry()


# ============================================================================== execution
class Slot:
    __slots__ = ("type", "value", "alive", "gen", "node", "out", "attrs")

    def __init__(self, ty, value, gen, node, out):
        self.type, self.value, self.alive, self.gen, self.node, self.out = ty, value, True, gen, node, out
        self.attrs = {}


class _Ref:
    """Attribute value that links to another object (dies with it)."""
    __slots__ = ("slot", "gen")

    def __init__(self, slot, gen):
        self.slot, self.gen = slot, gen


_MISSING = object()


def _preview(b, n=160):
    s = "".join(chr(c) if 32 <= c < 127 and c not in (34, 92) else "\\" + chr(c) if c in (34, 92) else "\\n" if c == 10
                else f"\\x{c:02x}" for c in b[:n])
    return f'"{s}"' + ("..." if len(b) > n else "")


class Runtime:
    def __init__(self, reg=REG):
        self.reg = reg
        self.trace = False
        self.plan = None
        self.in_exec = False
        self.cur_node = -1
        self.pool = []
        self.obj_of = {}
        self.globals = {}
        self.pending = []
        self.cur_in_slots = []
        self.cur_consumed = 0
        self.last_miss = 0
        self.gen_counter = 1
        self.tmp_files = []
        self.exc_bail = os.environ.get("STITCH_EXCEPTIONS") == "bail"
        self.plan_src_next = 0
        self.reset_results(0)

    # ------------------------------------------------------------------ results
    def reset_results(self, n):
        self.n_nodes = n
        self.current = -1
        self.crash_pc = 0
        self.status = [0] * n
        self.n_out = [0] * n
        self.out_mask = [0] * n
        self.reason = [0] * n
        self.miss = [0] * n
        self.consumed = [0] * n
        self.strings = bytearray()
        self.strings_seen = set()

    def intern(self, s):
        b = s.encode("utf-8", "replace")
        h = fnv1a(b)
        if h in self.strings_seen:
            return h
        b = b[:512]
        if len(self.strings) + 6 + len(b) <= STRINGS_SIZE:
            self.strings += struct.pack("<IH", h, len(b)) + b
            self.strings_seen.add(h)
        return h

    def results_bytes(self):
        o = bytearray(struct.pack("<IiQ", self.n_nodes, self.current, self.crash_pc & 0xFFFFFFFFFFFFFFFF))
        for i in range(self.n_nodes):
            o += struct.pack("<BBHIII", self.status[i], self.n_out[i], self.out_mask[i], self.reason[i], self.miss[i],
                             self.consumed[i])
        o += struct.pack("<I", len(self.strings)) + self.strings
        o += _instrument.edges_bytes()
        o += _instrument.cmps_bytes()
        return bytes(o)

    # ------------------------------------------------------------------ objects + attributes
    def live(self, i):
        if 0 <= i < len(self.pool) and self.pool[i].alive:
            return self.pool[i]
        return None

    def slot_of(self, obj):
        for i in range(len(self.pool) - 1, -1, -1):
            s = self.pool[i]
            if s.alive and s.value is obj:
                return i
        return -1

    def note_miss(self, key):
        if self.in_exec:
            self.last_miss = self.intern(key)

    def _wrap(self, v):
        if isinstance(v, (int, float, str, bytes, bool)) or v is None:
            return v
        i = self.slot_of(v)
        return _Ref(i, self.pool[i].gen) if i >= 0 else v

    def _unwrap(self, v):
        if isinstance(v, _Ref):
            s = self.live(v.slot)
            return s.value if s and s.gen == v.gen else None
        return v

    def attr_set(self, obj, key, value):
        i = self.slot_of(obj)
        if i >= 0:
            self.pool[i].attrs[key] = self._wrap(value)
        else:
            self.pending.append((obj, key, self._wrap(value)))

    def attr_get(self, obj, key, default=_MISSING):
        i = self.slot_of(obj)
        if i >= 0 and key in self.pool[i].attrs:
            v = self._unwrap(self.pool[i].attrs[key])
            if v is not None or not isinstance(self.pool[i].attrs[key], _Ref):
                return v
        if i < 0:
            for o, k, v in reversed(self.pending):
                if o is obj and k == key:
                    return self._unwrap(v)
        if default is _MISSING:
            self.note_miss(key)
            return None
        return default

    def attr_has(self, obj, key):
        i = self.slot_of(obj)
        return i >= 0 and key in self.pool[i].attrs

    def attr_del(self, obj, key):
        i = self.slot_of(obj)
        if i >= 0:
            self.pool[i].attrs.pop(key, None)

    def consume(self, obj):
        i = self.slot_of(obj)
        for k, s in enumerate(self.cur_in_slots):
            if s == i:
                self.cur_consumed |= 1 << k

    # ------------------------------------------------------------------ params
    def temp_file(self, data):
        d = os.environ.get("STITCH_TMPDIR") or tempfile.gettempdir()
        fd, path = tempfile.mkstemp(prefix=f"stitch-{os.getpid()}-", dir=d)
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        self.tmp_files.append(path)
        return path

    def new_source(self, buf, cap):
        s = Source(buf, cap=cap)
        if self.plan and self.in_exec and self.cur_node >= 0:
            sid = self.plan_src_next
            self.plan_src_next += 1
            node = self.cur_node
            s._log = lambda op, v, is_bytes: self.plan_draw(node, sid, op, v, is_bytes)
        return s

    def decode_param(self, dtname, raw, form, mx):
        dt = self.reg.datatypes.get(dtname) if dtname else None
        if form == 1 and dt is not None and dt.decoder is not None:
            return self.run_decoder(dt, Source(raw, cap=max(len(raw), mx)), mx)
        return bytes(raw[:mx])

    def decode_value(self, dtname, raw, mx):
        dt = self.reg.datatypes.get(dtname)
        if dt is None or dt.decoder is None:
            raise TypeError(f"Val[{dtname}]: the data type has no decoder")
        return dt.decoder(Source(raw, cap=mx))

    def run_decoder(self, dt, src, mx=None):
        try:
            v = dt.decoder(src)
        except Bail:
            v = b""
        except Exception:
            v = b""
        if isinstance(v, str):
            v = v.encode("utf-8", "replace")
        elif not isinstance(v, (bytes, bytearray)):
            v = repr(v).encode("utf-8", "replace")  # a typed decoder: its value as text
        return bytes(v[:mx]) if mx is not None else bytes(v)

    def describe(self, p, raw, form):
        if isinstance(p, BufParam):
            v = p.raw_value(raw, form, self)
            return f"{p.ctype}[{len(v)}{',decoded' if form == 1 else ''}]{_preview(v)}"
        if isinstance(p, SrcParam):
            return f"Src[{len(raw)}]{bytes(raw[:48]).hex()}{'...' if len(raw) > 48 else ''}"
        if isinstance(p, ValParam):
            return f"Val<{p.datatype}>[{len(raw)} choices] {repr(p.decode(raw, form, self))[:300]}"
        if isinstance(p, OneOfParam):
            return repr(p.decode(raw, form, self))
        v = p.decode(raw, form, self)
        return ("true" if v else "false") if isinstance(v, bool) else repr(v)

    # ------------------------------------------------------------------ plan mode
    def plan_line(self, d):
        if self.plan:
            self.plan.write(json.dumps(d) + "\n")
            self.plan.flush()

    def plan_draw(self, node, src, op, v, is_bytes):
        d = {"t": "draw", "node": node, "src": src, "op": op}
        if is_bytes:
            d["hex"] = bytes(v).hex()
        else:
            d["v"] = repr(v) if not isinstance(v, bool) else ("true" if v else "false")
            d["py"] = repr(v)
        self.plan_line(d)

    def plan_param(self, p, raw, form):
        d = {"kind": p.kind, "size": p.size}
        if isinstance(p, BufParam):
            d["decoded"] = form == 1
            d["hex"] = p.raw_value(raw, form, self).hex()
        elif isinstance(p, (SrcParam, ValParam)):
            d["hex"] = bytes(raw[:p.size]).hex()
        else:
            v = p.decode(raw, form, self)
            d["signed"] = p.signed
            d["v"] = ("true" if v else "false") if isinstance(v, bool) else repr(v)
            if isinstance(p, OneOfParam):
                d["index"] = p.index(raw)
            if p.kind == "float":
                d["d"] = repr(v)
        d["py"] = repr(p.decode(raw, form, self)) if not isinstance(p, (SrcParam,)) and p.kind != "file" else None
        if p.datatype:
            d["datatype"] = p.datatype
        return d

    # ------------------------------------------------------------------ program
    @staticmethod
    def parse(data):
        """Program bytes -> [(ep_name, [(src, out)], [(form, bytes)])] or None."""
        try:
            if data[:4] != b"STCH":
                return None
            off = 4
            nn, = struct.unpack_from("<I", data, off)
            off += 4
            if nn > 100000:
                return None
            names = []
            for _ in range(nn):
                l, = struct.unpack_from("<H", data, off)
                off += 2
                names.append(data[off:off + l].decode("utf-8", "replace"))
                off += l
            n, = struct.unpack_from("<I", data, off)
            off += 4
            if n > MAX_NODES:
                return None
            nodes = []
            for _ in range(n):
                ni, nin = struct.unpack_from("<IB", data, off)
                off += 5
                ins = []
                for _ in range(nin):
                    ins.append(struct.unpack_from("<IB", data, off))
                    off += 5
                np_, = struct.unpack_from("<B", data, off)
                off += 1
                ps = []
                for _ in range(np_):
                    form, l = struct.unpack_from("<BI", data, off)
                    off += 5
                    if l > (16 << 20) or off + l > len(data):
                        return None
                    ps.append((form, data[off:off + l]))
                    off += l
                nodes.append((names[ni] if ni < len(names) else "?", ins, ps))
            return nodes
        except (struct.error, IndexError):
            return None

    def report(self, ep, exc, what=None):
        """Print a finding report (format shared by all runtimes, parsed by triage).

        While fuzzing (not tracing) only the bucket key is needed: nobody reads the text, and
        triage re-runs every saved crash with a full report."""
        quiet = _FUZZING and not self.trace
        w = (lambda s: None) if quiet else sys.stderr.write
        if what is not None:
            self.crash_pc = (1 << 63) | fnv1a(what.encode())
            w(f"\n[stitch] CHECK FAILED: {what} (node {self.cur_node})\n")
            if quiet:
                return
            frames = _frames(exc.__traceback__)
        else:
            kind = _exc_name(exc)
            msg = str(exc).replace("\n", " ")[:500]
            frames = _frames(exc.__traceback__)
            top = next((f"{fn}:{ln}" for _, fn, ln in frames if not fn.startswith(os.environ.get("STITCH_PROJECT", "/project") + "/harness")),
                       frames[0][1] if frames else "")
            # bucket key for the engine: endpoint + exception type + innermost library line
            self.crash_pc = (1 << 63) | (self.reg.endpoints.index(self.reg.by_name[ep]) << 32) \
                | fnv1a(f"{kind}|{top}".encode())
            if quiet:
                return
            sys.stderr.write(f"\n[stitch] EXCEPTION in {ep}: {kind}: {msg}\n")
        for k, (func, fn, ln) in enumerate(frames[:24]):
            sys.stderr.write(f"    #{k} 0x0 in {func} {fn}:{ln}\n")
        sys.stderr.write("\n")
        sys.stderr.flush()

    def run(self, data, trace=False):
        """Run one program. Returns 'ok', 'finding' or 'invalid'."""
        nodes = self.parse(data)
        if nodes is None:
            self.reset_results(0)
            if trace:
                sys.stderr.write("[stitch] invalid program\n")
            return "invalid"
        self.reset_results(len(nodes))
        self.pool, self.obj_of, self.globals = [], {}, {}
        self.trace = trace
        self.in_exec = True
        _instrument.begin_exec()
        try:
            return self._run_nodes(nodes)
        finally:
            self.in_exec = False
            self.cur_node = -1
            _instrument.end_exec()
            for f in self.tmp_files:
                try:
                    os.unlink(f)
                except OSError:
                    pass
            self.tmp_files = []

    def _run_nodes(self, nodes):
        reg = self.reg
        trace = self.trace
        err = sys.stderr
        for i, (name, ins, ps) in enumerate(nodes):
            self.current = i
            self.cur_node = i
            _instrument.set_node(i)
            e = reg.by_name.get(name)
            if e is None:
                self.status[i] = ST_SKIP_INVALID
                self.plan_line({"t": "skip", "node": i, "why": "unknown endpoint"})
                if trace:
                    err.write(f"[stitch] #{i} {name}: unknown endpoint, skipped\n")
                continue
            self.n_out[i] = len(e.outputs)
            inputs, params = e.inputs, e.params
            if len(ins) != len(inputs) or len(ps) != len(params):
                self.status[i] = ST_SKIP_INVALID
                self.plan_line({"t": "skip", "node": i, "why": "signature mismatch"})
                if trace:
                    err.write(f"[stitch] #{i} {name}: signature mismatch, skipped\n")
                continue
            in_slot = []
            missing = False
            for k, (src, out) in enumerate(ins):
                si = self.obj_of.get((src, out), -1)
                s = self.live(si)
                if s is None or s.type is not inputs[k][1].type or si in in_slot:
                    missing = True
                    break
                in_slot.append(si)
            if missing:
                self.status[i] = ST_SKIP_MISSING
                self.plan_line({"t": "skip", "node": i, "why": "input not available"})
                if trace:
                    err.write(f"[stitch] #{i} {name}: input not available, skipped\n")
                continue
            if trace:
                parts, oi, pi = [], 0, 0
                for an, kind, spec in e.args:
                    if kind == "obj":
                        src, out = ins[oi]
                        parts.append(f"{an}={'take ' if spec.mode == 'take' else ''}o{src}.{out}")
                        oi += 1
                    else:
                        form, raw = ps[pi]
                        parts.append(f"{an}={self.describe(spec, raw, form)}")
                        pi += 1
                err.write(f"[stitch] #{i} {name}({', '.join(parts)})\n")
            if self.plan:
                self.plan_call(i, e, ins, ps)
                self.plan_src_next = 0
            self.cur_in_slots = in_slot
            self.cur_consumed = 0
            self.last_miss = 0
            self.pending = []
            bail_reason = None
            result = None
            try:
                args, oi, pi = [], 0, 0
                for an, kind, spec in e.args:
                    if kind == "obj":
                        args.append(self.pool[in_slot[oi]].value)
                        oi += 1
                    else:
                        form, raw = ps[pi]
                        args.append(spec.decode(raw, form, self))
                        pi += 1
                result = e.fn(*args)
                self.status[i] = ST_OK
            except Bail as b:
                self.status[i] = ST_BAIL
                bail_reason = b.reason
            except CheckFailed as c:
                self.report(name, c, what=c.what)
                raise Finding()
            except KeyboardInterrupt:
                raise
            except BaseException as ex:  # noqa: B902 - everything else is a finding unless declared
                if isinstance(ex, e.bails_on):
                    self.status[i] = ST_BAIL
                    bail_reason = f"raised {_exc_name(ex)}"
                elif self.exc_bail and isinstance(ex, Exception):
                    self.status[i] = ST_BAIL
                    bail_reason = "exception"
                else:
                    self.report(name, ex)
                    raise Finding()
            # taken / consumed inputs leave the pool
            for k, si in enumerate(in_slot):
                take = inputs[k][1].mode == "take"
                if take or self.cur_consumed & (1 << k):
                    self.pool[si].alive = False
                    self.pool[si].value = None
                    if not take:
                        self.consumed[i] |= 1 << k
            if self.status[i] == ST_OK and e.outputs:
                vals = result if e.multi else (result,)
                if not isinstance(vals, tuple) or len(vals) != len(e.outputs):
                    self.status[i] = ST_BAIL
                    bail_reason = f"returned {type(result).__name__}, expected {len(e.outputs)} outputs"
                else:
                    for k, v in enumerate(vals):
                        if v is None:
                            continue
                        ty = e.outputs[k][0]
                        s = Slot(ty, v, self.gen_counter, i, k)
                        self.gen_counter += 1
                        for o, key, val in self.pending:
                            if o is v:
                                s.attrs[key] = val
                        self.pool.append(s)
                        self.obj_of[(i, k)] = len(self.pool) - 1
                        self.out_mask[i] |= 1 << k
                    if any(not opt and not self.out_mask[i] & (1 << k) for k, (_, opt) in enumerate(e.outputs)):
                        self.status[i] = ST_BAIL
                        bail_reason = "returned None"
            if self.status[i] == ST_BAIL:
                self.reason[i] = self.intern(bail_reason or "bail")
                self.miss[i] = self.last_miss
            if self.plan:
                d = {"t": "done", "node": i, "status": "ok" if self.status[i] == ST_OK else "bail",
                     "outs": [k for k in range(len(e.outputs)) if self.out_mask[i] & (1 << k)],
                     "consumed": [k for k in range(len(in_slot)) if self.consumed[i] & (1 << k)]}
                if self.status[i] == ST_BAIL:
                    d["reason"] = bail_reason or "bail"
                self.plan_line(d)
            if trace:
                if self.status[i] == ST_OK:
                    outs = "".join(f" o{i}.{k}" for k in range(len(e.outputs)) if self.out_mask[i] & (1 << k))
                    err.write(f"[stitch]     -> ok{' produced' + outs if outs else ''}\n")
                else:
                    err.write(f"[stitch]     -> bail \"{bail_reason or 'bail'}\"\n")
        # drop leftovers, newest first (values before the contexts they depend on)
        self.current = len(nodes)
        self.cur_node = len(nodes)
        _instrument.set_node(len(nodes))
        live = [s for s in reversed(self.pool) if s.alive and s.type.drop is not None]
        if self.plan:
            self.plan_line({"t": "drops", "objs": [[s.node, s.out] for s in live]})
        for s in live:
            try:
                s.type.drop(s.value)
            except BaseException:  # noqa: B902
                pass
        self.pool = []
        self.current = -1
        self.plan_line({"t": "end"})
        return "ok"

    def plan_call(self, i, e, ins, ps):
        args, oi, pi = [], 0, 0
        for an, kind, spec in e.args:
            if kind == "obj":
                src, out = ins[oi]
                args.append({"obj": [src, out], "type": spec.type.name, "take": spec.mode == "take", "name": an})
                oi += 1
            else:
                form, raw = ps[pi]
                d = self.plan_param(spec, raw, form)
                d["name"] = an
                args.append(d)
                pi += 1
        self.plan_line({"t": "call", "node": i, "ep": e.name, "args": args, "outs": [t.name for t, _ in e.outputs],
                        "multi": e.multi})


def _exc_name(ex):
    t = type(ex)
    mod = t.__module__
    return t.__qualname__ if mod in ("builtins", "__main__") else f"{mod}.{t.__qualname__}"


def _frames(tb):
    """Innermost-first (func, file, line), without STITCH++ runtime frames."""
    out = []
    while tb is not None:
        co = tb.tb_frame.f_code
        fn = co.co_filename
        if not fn.startswith(RUNTIME_DIR):
            out.append((getattr(co, "co_qualname", co.co_name), fn, tb.tb_lineno))
        tb = tb.tb_next
    return out[::-1]


RT = Runtime()
