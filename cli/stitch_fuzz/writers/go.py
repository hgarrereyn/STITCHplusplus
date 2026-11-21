"""`stitch write` for Go: repro.go calls each stub in order with its recorded arguments."""
import json
import shlex
from pathlib import Path

from ..standalone import read_plan
from . import comment_safe

FILENAME = "repro.go"
SHIM = "stitch"  # the stitch module in the image (stitch.Run / stitch.Call)


def install_shim(d: Path):
    pass


def _gostr(b: bytes) -> str:
    # a Go string literal for arbitrary bytes
    out = '"'
    for c in b:
        ch = chr(c)
        if ch in '"\\':
            out += "\\" + ch
        elif 32 <= c < 127:
            out += ch
        elif ch == "\n":
            out += "\\n"
        else:
            out += f"\\x{c:02x}"
    return out + '"'


def _literal(a, draws):
    k = a["kind"]
    if k in ("str", "bytes", "file"):
        b = bytes.fromhex(a.get("hex", ""))
        if k == "str":
            return _gostr(b), None
        return (f"[]byte({_gostr(b)})" if k == "bytes" else f"stitch.TempFile([]byte({_gostr(b)}))"), None
    if k == "source":
        lines = "; ".join(f"{d['op']} -> {d.get('v', bytes.fromhex(d.get('hex', '')).decode('utf-8', 'replace'))}" for d in draws)
        return f"stitch.NewSource([]byte({_gostr(bytes.fromhex(a['hex']))}))", (f"draws: {comment_safe(lines)[:300]}" if lines else None)
    if k == "value":
        return (f"stitch.Value({json.dumps(a.get('datatype') or '')}, []byte({_gostr(bytes.fromhex(a['hex']))}))",
                comment_safe(f"{a.get('name', '?')} = {a.get('v')}")[:300])
    if a.get("go") is not None:
        return a["go"], None
    return "nil", f"{a.get('name', '?')} was {a.get('v')}: not a literal, fill it in"


def write(p, plan: Path, header):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    vars_ = {}
    body = []
    uses_math = False
    for n in order:
        c = calls[n]
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        args, summ, si = [], [], 0
        for a in c.get("args") or []:
            name = a.get("name", "")
            if "obj" in a:
                v = vars_.get(tuple(a["obj"]), "nil")
                args.append(v)
                summ.append(f"{name}={v}")
            else:
                dr = draws.get(n, {}).get(si, []) if a["kind"] == "source" else []
                if a["kind"] == "source":
                    si += 1
                lit, comment = _literal(a, dr)
                uses_math = uses_math or "math." in lit
                if comment:
                    body.append(f"\t\t// {comment}")
                args.append(lit)
                summ.append(name)
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f' (bailed in the original run: "{reason}")'}.get(status, "")
        body.append(f"\t\t// #{n} {c['ep']}({', '.join(summ)}){note}")
        call_args = "".join(", " + x for x in args)
        if status == "bail":
            body.append(f"\t\tstitch.CallBails({n}, {json.dumps(c['ep'])}{call_args})")
            continue
        outs = (d or {}).get("outs", [])
        types = c.get("outs", [])
        if not types:
            body.append(f"\t\tstitch.Call({n}, {json.dumps(c['ep'])}{call_args})")
        else:
            body.append(f"\t\tr{n} := stitch.Call({n}, {json.dumps(c['ep'])}{call_args})")
            for k, t in enumerate(types):
                if status == "crash" or k not in outs:
                    continue
                v = f"{t[:1].lower() + t[1:]}{n}" + (f"_{k}" if len(types) > 1 else "")
                vars_[(n, k)] = v
                body.append(f"\t\t{v} := r{n}[{k}]")
            body.append(f"\t\t_ = r{n}")
        if status == "crash":
            break
    if crash_node is not None:
        body.append(f"\t\t// the original run crashed in call #{crash_node}; nothing after it ran")
    used = {v for v in vars_.values()}
    head = ["// " + h if h else "//" for h in header]
    imports = ['\t"stitch"', '\t_ "stitchharness" // the harness: registers the stubs'] + (['\t"math"'] if uses_math else [])
    text = "\n".join(head + ["package main", "", "import (", *imports, ")", "", "func main() {", "\tstitch.Run(func() {",
                             *body, *[f"\t\t_ = {v}" for v in sorted(used)], "\t})", "}", ""])
    return text, {"calls": len(order), "crash_node": crash_node, "skipped": len(skips)}


def run_script(p, src_rel, timeout=60):
    mod = "/project/.stitch/build/app/mod"
    return (f"d={mod}/cmd/stitchtry && rm -rf $d && mkdir -p $d && cp {shlex.quote(src_rel)} $d/main.go && cd {mod} && "
            f"GOCACHE=/project/.stitch/gocache go build -o /tmp/stitch-repro ./cmd/stitchtry 2>/tmp/build.log "
            f"|| {{ cat /tmp/build.log >&2; rm -rf $d; exit 201; }}; rm -rf $d; echo '[repro] ---- run ----' >&2; "
            f"timeout {timeout} /tmp/stitch-repro >&2")


def build_hint(src_rel):
    return "go run . (from a directory of the harness module: stitch shell; see the README)"
