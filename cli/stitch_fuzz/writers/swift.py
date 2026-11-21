"""`stitch write` for Swift: repro.swift, the main.swift of the harness package that calls each stub
body directly (Stitch's call / callBails / reproMain)."""
import json
import re
import shlex
from pathlib import Path

from ..standalone import read_plan
from . import comment_safe

FILENAME = "repro.swift"
SHIM = "the Stitch package"  # the harness package's own dependency (call / reproMain)
_VAR = re.compile(r"^\s*(?:public\s+|internal\s+)?let\s+(\w+)\s*=\s*(?:Stitch\.)?endpoint\(\s*\"([^\"]+)\"", re.M)


def install_shim(d: Path):
    pass


def swift_str(b: bytes):
    out = []
    for ch in b.decode("utf-8", "replace"):
        c = ord(ch)
        if ch in '"\\':
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif c < 32 or c == 127:
            out.append(f"\\u{{{c:x}}}")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def _bytes(b: bytes):
    return "[" + ", ".join(f"0x{c:02x}" for c in b) + "]"


def _literal(a, draws):
    k = a["kind"]
    if k in ("str", "bytes", "file"):
        b = bytes.fromhex(a.get("hex", ""))
        return {"str": f"Str({swift_str(b)})", "bytes": f"Bytes({_bytes(b)})", "file": f"FilePath({_bytes(b)})"}[k], None
    if k == "source":
        lines = "; ".join(f"{d.get('op')} -> {d.get('v', d.get('hex', ''))}" for d in draws)
        return f"Choices(Source(bytes: {_bytes(bytes.fromhex(a.get('hex', '')))}, cap: {a.get('size', 0)}))", \
            (f"draws: {comment_safe(lines)[:300]}" if lines else None)
    if k == "value":
        return f"Val(replay: {_bytes(bytes.fromhex(a.get('hex', '')))})", comment_safe(f"{a.get('name', '?')} = {a.get('v')}")[:300]
    v = a.get("v", "0")
    if k == "range":
        return f"Between({v})", None
    if k == "oneof":
        return f"Pick(index: {v})", None
    if k == "bool":
        return v, None
    if k == "float":
        d = a.get("d", "0")
        return {"nan": ".nan", "inf": ".infinity", "-inf": "-.infinity"}.get(d.lower(), d), None
    return v, None


def write(p, plan: Path, header):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    var_of = {}
    for f in sorted((p.root / "harness").rglob("*.swift")):
        for m in _VAR.finditer(f.read_text()):
            var_of[m.group(2)] = m.group(1)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    vars_, body = {}, []
    for n in order:
        c = calls[n]
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        args, summ, si = [], [], 0
        for a in c.get("args") or []:
            name = a.get("name", "")
            if "obj" in a:
                v = vars_.get(tuple(a["obj"]))
                args.append(f"{v}.{'take' if a.get('take') else 'borrow'}" if v else "fatalError(\"missing object\")")
                summ.append(f"{name}={v}")
            else:
                dr = draws.get(n, {}).get(si, []) if a["kind"] == "source" else []
                if a["kind"] == "source":
                    si += 1
                lit, comment = _literal(a, dr)
                if comment:
                    body.append(f"    // {comment}")
                args.append(lit)
                summ.append(name)
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f' (bailed in the original run: "{reason}")'}.get(status, "")
        body.append(f"    // #{n} {c['ep']}({', '.join(summ)}){note}")
        fn = var_of.get(c["ep"], c["ep"])
        expr = f"try {fn}.body({', '.join(args)})"
        if status == "bail":
            body.append(f"    try callBails({n}, {json.dumps(c['ep'])}) {{ _ = {expr} }}")
            continue
        types = c.get("outs", [])
        outs = (d or {}).get("outs", [])
        if types and len(types) == 1 and 0 in outs and status != "crash":
            v = f"{types[0][:1].lower() + types[0][1:]}{n}"
            vars_[(n, 0)] = v
            body.append(f"    let {v} = obj(try call({n}, {json.dumps(c['ep'])}) {{ {expr} }})")
        else:
            body.append(f"    _ = try call({n}, {json.dumps(c['ep'])}) {{ {expr} }}")
        if status == "crash":
            break
    if crash_node is not None:
        body.append(f"    // the original run crashed in call #{crash_node}; nothing after it ran")
    head = ["// " + h if h else "//" for h in header]
    text = "\n".join(head + ["import Stitch", "#if canImport(Glibc)", "import Glibc", "#endif", "",
                             "// The program. Each call(...) is one harness call with its arguments filled in;",
                             "// the stubs are the harness's `let NAME = endpoint(...)` values.",
                             "exit(reproMain {", *body, "})", ""])
    return text, {"calls": len(order), "crash_node": crash_node, "skipped": len(skips)}


def run_script(p, src_rel, timeout=60):
    pkg = "/project/.stitch/build/app/pkg"
    return (f"cp {pkg}/Sources/worker/main.swift /tmp/stitch-main.swift && cp {shlex.quote(src_rel)} {pkg}/Sources/worker/main.swift && "
            f"(cd /project && stitch-swift-try >/tmp/build.log 2>&1); rc=$?; cp /tmp/stitch-main.swift {pkg}/Sources/worker/main.swift; "
            f"[ $rc = 0 ] || {{ grep -E 'error' /tmp/build.log >&2; exit 201; }}; echo '[repro] ---- run ----' >&2; "
            f"export ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:symbolize=1:allocator_may_return_null=1:handle_sigtrap=1:handle_sigill=1 "
            f"SWIFT_BACKTRACE=enable=no; timeout {timeout} stitch-swift-run /tmp/stitch-try/release/worker")


def build_hint(src_rel):
    return "the main.swift of the harness package in .stitch/build/app/pkg (stitch try builds and runs it)"
