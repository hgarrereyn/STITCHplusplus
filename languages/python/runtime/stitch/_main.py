"""Entry points: the engine's worker, and the reproducer CLI.

    python -m stitch [--trace] [--plan FILE] [--manifest] FILE.stp...     run programs (reproducer)
    python -m stitch --each [--timeout S] [--coverage DIR] [--files-from LIST] FILE.stp...
                                                                           run all, keep going (stitch cov)
    python -m stitch worker                                                engine worker (fds 3 and 4)

The harness is every .py file under <project>/harness (the `harness` package).
"""
import glob
import importlib
import json
import os
import resource
import signal
import struct
import sys

from . import _instrument
from ._runtime import REG, RT, Finding
from ._source import Source


def _limit_memory():
    mb = int(os.environ.get("STITCH_MEMORY_MB", "4096"))
    if mb > 0:
        try:
            resource.setrlimit(resource.RLIMIT_AS, (mb << 20, mb << 20))
        except (ValueError, OSError):
            pass


def load_harness(project, instrument=True):
    project = os.path.abspath(project)
    os.environ.setdefault("STITCH_PROJECT", project)
    if instrument:
        _instrument.install()
    sys.path.insert(0, project)
    hdir = os.path.join(project, "harness")
    files = sorted(glob.glob(os.path.join(hdir, "**", "*.py"), recursive=True))
    if not files:
        sys.exit(f"[stitch] error: no harness files ({hdir}/**/*.py)")
    for f in files:
        rel = os.path.relpath(f, project)[:-3].replace(os.sep, ".")
        if rel.endswith(".__init__"):
            rel = rel[:-9]
        importlib.import_module(rel)
    if not REG.endpoints:
        sys.exit("[stitch] error: the harness defines no stubs (@stitch.endpoint)")
    for fn in REG.inits:
        fn()


def run_program(data, trace=False, cmplog=False):
    _instrument._cmplog = cmplog
    try:
        return RT.run(data, trace=trace)
    except Finding:
        return "finding"


# ------------------------------------------------------------------------------ worker
def _read_exact(f, n):
    b = bytearray()
    while len(b) < n:
        c = f.read(n - len(b))
        if not c:
            raise EOFError
        b += c
    return bytes(b)


def worker(project):
    rx = os.fdopen(3, "rb", buffering=0)
    tx = os.fdopen(4, "wb", buffering=0)

    def send(tag, payload=b""):
        tx.write(tag + struct.pack("<I", len(payload)) + payload)

    _limit_memory()
    load_harness(project, instrument=os.environ.get("STITCH_FUZZING") == "1")
    send(b"H", struct.pack("<I", _instrument.MAP_SIZE) + REG.manifest().encode())
    dts = REG.dt_list()
    while True:
        try:
            h = _read_exact(rx, 5)
            payload = _read_exact(rx, struct.unpack("<I", h[1:])[0])
        except EOFError:
            return
        tag = h[:1]
        if tag == b"X":
            flags = struct.unpack_from("<I", payload)[0]
            r = run_program(payload[4:], trace=bool(flags & 1), cmplog=bool(flags & 2))
            send(b"C" if r == "finding" else b"O", RT.results_bytes())
            if r == "finding":
                _instrument.rearm()
        elif tag == b"D":
            dt, cap = struct.unpack_from("<II", payload)
            ch = payload[8:]
            d = dts[dt] if dt < len(dts) else None
            v = RT.run_decoder(d, Source(ch, cap=max(len(ch), cap)), cap) if d and d.decoder else ch[:cap]
            send(b"R", v)
        elif tag == b"G":
            dt, seed, cap = struct.unpack_from("<IQI", payload)
            d = dts[dt] if dt < len(dts) else None
            out = b""
            if d and d.decoder:
                src = Source(payload[16:16 + cap], cap=cap, gen=True, seed=seed, dict_tokens=REG.dicts.get(dt, []))
                RT.run_decoder(d, src)
                out = bytes(src._buf[:cap])
            send(b"R", out)
        elif tag == b"S":
            dt, = struct.unpack_from("<I", payload)
            d = dts[dt] if dt < len(dts) else None
            spans = []
            if d and d.decoder:
                RT.run_decoder(d, Source(payload[4:], cap=len(payload) - 4, spans=spans))
            send(b"R", struct.pack(f"<{len(spans)}I", *spans))
        elif tag == b"I":
            dt, = struct.unpack_from("<I", payload)
            d = dts[dt] if dt < len(dts) else None
            items = []
            if d and d.decoder:
                src = Source(payload[4:], cap=len(payload) - 4, items=items)
                RT.run_decoder(d, src)
                src._finish_items()
            send(b"R", struct.pack(f"<{len(items)}I", *[x & 0xFFFFFFFF for x in items]))
        elif tag == b"T":
            dt, = struct.unpack_from("<I", payload)
            toks = REG.dicts.setdefault(dt, [])
            if len(toks) < 4096 and payload[4:] not in toks:
                toks.append(payload[4:])
        elif tag == b"P":
            path = payload.decode()
            if RT.plan:
                RT.plan.close()
            RT.plan = open(path, "w") if path else None
        elif tag == b"Q":
            if RT.plan:
                RT.plan.close()
            return


# ------------------------------------------------------------------------------ reproducer
class _Timeout(KeyboardInterrupt):
    pass


def _alarm(signum, frame):
    raise _Timeout()


def repro(argv):
    trace = "--trace" in argv
    each = "--each" in argv
    files, plan, timeout, covdir, project = [], None, 10, None, os.getcwd()
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--plan", "--timeout", "--coverage", "--project", "--files-from"):
            v = argv[i + 1]
            if a == "--files-from":
                files += [l.strip() for l in open(v) if l.strip()]
            elif a == "--plan":
                plan = v
            elif a == "--timeout":
                timeout = int(v)
            elif a == "--coverage":
                covdir = v
            else:
                project = v
            i += 2
            continue
        if not a.startswith("-"):
            files.append(a)
        i += 1
    if "--manifest" in argv:
        load_harness(project, instrument=False)
        print(REG.manifest())
        return 0
    cov = None
    if covdir:
        import coverage
        inc = [p for p in os.environ.get("STITCH_PY_INCLUDE", "").split(",") if p]
        hdir = os.path.join(os.path.abspath(project), "harness")
        cov = coverage.Coverage(data_file=os.path.join(covdir, ".coverage"), source=(inc + [hdir]) if inc else None,
                                omit=[os.path.dirname(os.path.abspath(__file__)) + "/*"], branch=True)
        cov.start()
    _limit_memory()
    load_harness(project, instrument=False)
    if plan:
        RT.plan = open(plan, "w")
    if each:
        signal.signal(signal.SIGALRM, _alarm)
        stats = {"programs": 0, "ok": 0, "crashed": 0, "timeouts": 0, "invalid": 0}
        for f in files:
            stats["programs"] += 1
            signal.alarm(timeout)
            try:
                r = run_program(open(f, "rb").read())
                stats[{"ok": "ok", "finding": "crashed", "invalid": "invalid"}[r]] += 1
            except _Timeout:
                stats["timeouts"] += 1
            finally:
                signal.alarm(0)
        if cov:
            cov.stop()
            cov.save()
            cov.lcov_report(outfile=os.path.join(covdir, "lcov.info"))
            cov.html_report(directory=os.path.join(covdir, "html"))
            with open(os.path.join(covdir, "report.txt"), "w") as fh:
                cov.report(file=fh)
        print(json.dumps(stats))
        return 0
    for f in files:
        if trace:
            sys.stderr.write(f"[stitch] running {f}\n")
        try:
            data = open(f, "rb").read()
        except OSError as e:
            sys.stderr.write(f"[stitch] cannot open {f}: {e}\n")
            return 2
        r = run_program(data, trace=trace)
        if r == "finding":
            return 1
        if r == "invalid":
            return 2
    if RT.plan:
        RT.plan.close()
    return 0


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "worker":
        worker(argv[argv.index("--project") + 1] if "--project" in argv else os.getcwd())
        return 0
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    return repro(argv)
