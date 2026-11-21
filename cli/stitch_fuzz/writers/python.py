"""`stitch write` for Python: the harness code plus a main() that calls each stub in order."""
import json
import re
import shlex
from pathlib import Path

from ..project import STITCH_HOME
from ..standalone import read_plan
from . import comment_safe

FILENAME = "repro.py"
SHIM = "stitch.py"
_RT = STITCH_HOME / "languages" / "python"


def install_shim(d: Path):
    src = (_RT / "runtime" / "stitch" / "_source.py").read_text()
    api = (_RT / "standalone" / "stitch_api.py").read_text()
    (d / SHIM).write_text("# stitch.py: STITCH++ runtime stand-in for standalone reproducers (no fuzzer).\n" + src + "\n\n" + api)


def _literal(a, draws):
    """(Python expression, comment or None) for one recorded parameter."""
    k = a["kind"]
    if k in ("str", "bytes", "file"):
        b = bytes.fromhex(a.get("hex", ""))
        if k == "str":
            return repr(b.decode("utf-8", "replace")), None
        return (repr(b) if k == "bytes" else f"stitch.file({b!r})"), None
    if k == "source":
        lines = "; ".join(f"{d['op']} -> {d.get('v', bytes.fromhex(d.get('hex', '')).decode('utf-8', 'replace'))!s}"
                          for d in draws)
        return f"stitch.Source(bytes.fromhex({a['hex']!r}))", (f"draws: {comment_safe(lines)[:300]}" if lines else None)
    if k == "value":
        return f"stitch.value({a.get('datatype')!r}, bytes.fromhex({a['hex']!r}))", f"{a.get('name', '?')} = {a.get('py')}"[:300]
    py = a.get("py")
    if k == "float":
        return {"nan": 'float("nan")', "inf": 'float("inf")', "-inf": 'float("-inf")'}.get(py, py), None
    if k == "bool":
        return ("True" if a.get("v") == "true" else "False"), None
    if py and not py.startswith("<"):
        return py, None
    return "None", f"{a.get('name', '?')} was {py}: not a literal, fill it in"


def _harness_code(p):
    """All harness files, in load order, without imports of each other."""
    parts = []
    for f in sorted((p.root / "harness").rglob("*.py")):
        if "seeds" in f.relative_to(p.root / "harness").parts:
            continue
        text = f.read_text()
        text = re.sub(r"^(from\s+harness(\.[\w.]+)?\s+import[^\n]*|import\s+harness(\.[\w.]+)?[^\n]*|from\s+\.[\w.]*\s+import[^\n]*)$",
                      r"# \1  (pasted above)", text, flags=re.M)
        parts.append(f"# {'=' * 94}\n# {p.rel(f)}\n# {'=' * 94}\n{text.rstrip()}\n")
    return "\n\n".join(parts)


def write(p, plan: Path, header):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    vars_ = {}
    body = ["def main():"]
    for n in order:
        c = calls[n]
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        args, summ, si = [], [], 0
        for a in c.get("args") or []:
            name = a.get("name", "")
            if "obj" in a:
                v = vars_.get(tuple(a["obj"]), f"None  # missing o{a['obj'][0]}.{a['obj'][1]}")
                args.append(v)
                summ.append(f"{name}={v}")
            else:
                dr = draws.get(n, {}).get(si, []) if a["kind"] == "source" else []
                if a["kind"] == "source":
                    si += 1
                lit, comment = _literal(a, dr)
                if comment:
                    body.append(f"    # {comment}")
                args.append(lit)
                summ.append(name)
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f'  (bailed in the original run: "{reason}")'}.get(status, "")
        body.append(f"    # #{n} {c['ep']}({', '.join(summ)}){note}")
        expr = f"lambda: {c['ep']}({', '.join(args)})"
        outs = (d or {}).get("outs", [])
        if status == "bail":
            body.append(f"    stitch.call_bails({n}, {c['ep']!r}, {expr})")
            continue
        types = c.get("outs", [])
        if not types:
            body.append(f"    stitch.call({n}, {c['ep']!r}, {expr})")
        elif not c.get("multi"):
            v = f"{types[0].lower()}{n}"
            vars_[(n, 0)] = v
            body.append(f"    {v} = stitch.call({n}, {c['ep']!r}, {expr})")
        else:
            names = [f"{t.lower()}{n}_{k}" for k, t in enumerate(types)]
            for k in outs:
                vars_[(n, k)] = names[k]
            body.append(f"    {', '.join(names)} = stitch.call({n}, {c['ep']!r}, {expr})")
        if status == "crash":
            break
    if crash_node is not None:
        body.append(f"    # the original run crashed in call #{crash_node}; nothing after it ran")
    if len(body) == 1:
        body.append("    pass")
    head = ["# " + h if h else "#" for h in header]
    text = "\n".join(head + ["", "import stitch  # stitch.py next to this file: the runtime without the fuzzer", "",
                             _harness_code(p), "",
                             "# " + "=" * 94,
                             "# The program. Each stitch.call(...) is one harness call with its arguments filled in.",
                             "# " + "=" * 94, *body, "", "", 'if __name__ == "__main__":', "    stitch.run(main)", ""])
    return text, {"calls": len(order), "crash_node": crash_node, "skipped": len(skips)}


def run_script(p, src_rel, timeout=60):
    d = str(Path(src_rel).parent)
    return (f"cd {shlex.quote(d)} && echo '[repro] ---- run ----' >&2 && "
            f"timeout {timeout} python3 {shlex.quote(Path(src_rel).name)} >&2")


def build_hint(src_rel):
    return f"python3 {Path(src_rel).name}   (in the project image: stitch shell)"
