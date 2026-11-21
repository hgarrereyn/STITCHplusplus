"""`stitch write` for Ruby: the harness code plus a main that calls each stub in order."""
import json
import re
import shlex
from pathlib import Path

from ..project import STITCH_HOME
from ..standalone import read_plan
from . import comment_safe

FILENAME = "repro.rb"
SHIM = "stitch.rb"
_RT = STITCH_HOME / "languages" / "ruby"


def install_shim(d: Path):
    src = (_RT / "runtime" / "lib" / "stitch" / "source.rb").read_text()
    api = (_RT / "standalone" / "stitch_api.rb").read_text()
    (d / SHIM).write_text("# stitch.rb: STITCH++ runtime stand-in for standalone reproducers (no fuzzer).\n"
                          + src.replace("# frozen_string_literal: true\n", "") + "\n" + api)


def ruby_str(b: bytes, binary=False):
    """A Ruby string literal for b (printable ASCII stays readable)."""
    out = []
    for c in b:
        ch = chr(c)
        if ch in '"\\#':
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif 32 <= c < 127:
            out.append(ch)
        else:
            out.append(f"\\x{c:02x}")
    lit = '"' + "".join(out) + '"'
    return lit + ".b" if binary else lit


def _literal(a, draws):
    """(Ruby expression, comment or None) for one recorded parameter."""
    k = a["kind"]
    if k in ("str", "bytes", "file"):
        b = bytes.fromhex(a.get("hex", ""))
        if k == "str":
            return ruby_str(b.decode("utf-8", "replace").encode()), None
        return (ruby_str(b, True) if k == "bytes" else f"Stitch.file({ruby_str(b, True)})"), None
    if k == "source":
        lines = "; ".join(f"{d['op']} -> {d.get('v', bytes.fromhex(d.get('hex', '')).decode('utf-8', 'replace'))!s}"
                          for d in draws)
        return f"Stitch::Source.new([{a['hex']!r}].pack(\"H*\"))", (f"draws: {comment_safe(lines)[:300]}" if lines else None)
    if k == "value":
        return (f"Stitch.value({json.dumps(a.get('datatype') or '')}, [{a['hex']!r}].pack(\"H*\"))",
                comment_safe(f"{a.get('name', '?')} = {a.get('v')}")[:300])
    rb = a.get("rb")
    if k == "float":
        return {"NaN": "Float::NAN", "Infinity": "Float::INFINITY", "-Infinity": "-Float::INFINITY"}.get(rb, rb), None
    if rb and not rb.startswith("#<"):
        return rb, None
    return "nil", f"{a.get('name', '?')} was {rb}: not a literal, fill it in"


def _harness_code(p):
    """All harness files, in load order, without requires of each other."""
    parts = []
    for f in sorted((p.root / "harness").rglob("*.rb")):
        if "seeds" in f.relative_to(p.root / "harness").parts:
            continue
        text = f.read_text()
        text = re.sub(r"^(\s*require_relative\s[^\n]*)$", r"# \1  (pasted above)", text, flags=re.M)
        parts.append(f"# {'=' * 94}\n# {p.rel(f)}\n# {'=' * 94}\n{text.rstrip()}\n")
    return "\n\n".join(parts)


def write(p, plan: Path, header):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    vars_ = {}
    body = ["def main"]
    for n in order:
        c = calls[n]
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        args, summ, si = [], [], 0
        for a in c.get("args") or []:
            name = a.get("name", "")
            if "obj" in a:
                v = vars_.get(tuple(a["obj"]), f"nil # missing o{a['obj'][0]}.{a['obj'][1]}")
                args.append(v)
                summ.append(f"{name}={v}")
            else:
                dr = draws.get(n, {}).get(si, []) if a["kind"] == "source" else []
                if a["kind"] == "source":
                    si += 1
                lit, comment = _literal(a, dr)
                if comment:
                    body.append(f"  # {comment}")
                args.append(lit)
                summ.append(name)
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f'  (bailed in the original run: "{reason}")'}.get(status, "")
        body.append(f"  # #{n} {c['ep']}({', '.join(summ)}){note}")
        call = f"Stitch.stub(:{c['ep']}).call({', '.join(args)})"
        if status == "bail":
            body.append(f"  Stitch.call_bails({n}, :{c['ep']}) {{ {call} }}")
            continue
        types = c.get("outs", [])
        if not types:
            body.append(f"  Stitch.call({n}, :{c['ep']}) {{ {call} }}")
        elif not c.get("multi"):
            v = f"{types[0].lower()}{n}"
            vars_[(n, 0)] = v
            body.append(f"  {v} = Stitch.call({n}, :{c['ep']}) {{ {call} }}")
        else:
            names = [f"{t.lower()}{n}_{k}" for k, t in enumerate(types)]
            for k in (d or {}).get("outs", []):
                vars_[(n, k)] = names[k]
            body.append(f"  {', '.join(names)} = Stitch.call({n}, :{c['ep']}) {{ {call} }}")
        if status == "crash":
            break
    if crash_node is not None:
        body.append(f"  # the original run crashed in call ##{crash_node}; nothing after it ran")
    body.append("end")
    head = ["# " + h if h else "#" for h in header]
    text = "\n".join(head + ["", "$LOAD_PATH.unshift(__dir__) # stitch.rb next to this file: the runtime without the fuzzer",
                             'require "stitch"', "", _harness_code(p), "",
                             "# " + "=" * 94,
                             "# The program. Each Stitch.call(...) is one harness call with its arguments filled in.",
                             "# " + "=" * 94, *body, "", "Stitch.run { main }", ""])
    return text, {"calls": len(order), "crash_node": crash_node, "skipped": len(skips)}


def run_script(p, src_rel, timeout=60):
    d = str(Path(src_rel).parent)
    return (f"cd {shlex.quote(d)} && echo '[repro] ---- run ----' >&2 && "
            f"timeout {timeout} ruby {shlex.quote(Path(src_rel).name)} >&2")


def build_hint(src_rel):
    return f"ruby {Path(src_rel).name}   (in the project image: stitch shell)"
