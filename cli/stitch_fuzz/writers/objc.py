"""`stitch write` for Objective-C: repro.m, a program that includes the harness sources and calls
each stub function (stub_NAME) with its recorded arguments (Stitch.h STReproMain / STCall)."""
import importlib.util
import json
import re
import shlex
from importlib.machinery import SourceFileLoader
from pathlib import Path

from ..project import STITCH_HOME
from ..standalone import asan_options, read_plan
from . import comment_safe

FILENAME = "repro.m"
FILENAMES = ("repro.m", "repro.mm")  # repro.mm when the harness has Objective-C++ files
SHIM = "Stitch/Stitch.h"  # the runtime's reproducer helpers (STReproMain, STCall, STString, ...)

_BUILD = None


def _build_module():
    """languages/objc/bin/stitch-build: its parser of STITCH_ENDPOINT declarations."""
    global _BUILD
    if _BUILD is None:
        path = str(STITCH_HOME / "languages" / "objc" / "bin" / "stitch-build")
        spec = importlib.util.spec_from_loader("stitch_objc_build", SourceFileLoader("stitch_objc_build", path))
        _BUILD = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_BUILD)
    return _BUILD


def install_shim(d: Path):
    pass


def filename(p):
    """repro.mm when a harness file is Objective-C++ (it can only be included there)."""
    return "repro.mm" if any(f.suffix == ".mm" for f in harness_files(p.root)) else "repro.m"


def harness_files(root: Path):
    h = root / "harness"
    return sorted(f for pat in ("*.m", "*.mm", "*.c") for f in h.rglob(pat)
                  if f.is_file() and "data" not in f.relative_to(h).parts[:1])


def signatures(root: Path):
    """{stub name: {"ret": text, "outs": [(type, optional)], "args": [(type text, name, spec)]}}"""
    b = _build_module()
    sigs = {}
    for f in harness_files(root):
        try:
            for name, ret, outs, args, _ in b.endpoints(f.read_text(errors="replace")):
                sigs[name] = {"ret": ret, "outs": outs, "args": args}
        except b.SpecError:
            continue
    return sigs


def c_bytes(b: bytes) -> str:
    """A C string literal holding exactly these bytes."""
    out, prev_hex = [], False
    for c in b:
        ch = chr(c)
        if ch in '"\\':
            out.append("\\" + ch)
            prev_hex = False
        elif ch == "\n":
            out.append("\\n")
            prev_hex = False
        elif ch == "\t":
            out.append("\\t")
            prev_hex = False
        elif 32 <= c < 127 and not (prev_hex and ch in "0123456789abcdefABCDEF"):
            out.append(ch)
            prev_hex = False
        else:
            out.append(f"\\x{c:02x}")
            prev_hex = True
    return '"' + "".join(out) + '"'


def _printable(b: bytes):
    return all(32 <= c < 127 for c in b)


def _int_literal(v: str, ctype: str, signed: bool, size: int):
    try:
        n = int(v)
    except ValueError:
        return f"/* {comment_safe(v)} */ 0"
    if signed and size == 8 and n == -(1 << 63):
        lit = "(-9223372036854775807LL - 1)"
    else:
        lit = f"{n}{'LL' if signed else 'ULL'}"
    return f"({ctype}){lit}"


def _float_literal(a, ctype):
    d = str(a.get("d", "0")).lower()
    if d in ("nan", "-nan"):
        return f"({ctype})__builtin_nan(\"\")"
    if d in ("inf", "infinity"):
        return f"({ctype})__builtin_inf()"
    if d in ("-inf", "-infinity"):
        return f"({ctype})-__builtin_inf()"
    v = a.get("v", "0")  # hexadecimal float: exact
    try:
        float.fromhex(v)
        return f"({ctype}){v}"
    except (ValueError, TypeError):
        return f"({ctype}){d}"


def _literal(a, spec, draws):
    """(expression, comment) for one recorded parameter value."""
    k = a["kind"]
    raw = bytes.fromhex(a.get("hex", "")) if "hex" in a else b""
    mac = spec.get("mac") if spec else None
    if k in ("str", "bytes", "file"):
        if k == "bytes":
            return f"STData({c_bytes(raw)}, {len(raw)})", None
        if k == "file":
            return f"STTempFile({c_bytes(raw)}, {len(raw)})", None
        if mac == "CStr":
            if _printable(raw):
                return c_bytes(raw), None
            return f"STCString({c_bytes(raw)}, {len(raw)})", None
        if _printable(raw):
            return "@" + c_bytes(raw), None
        return f"STString({c_bytes(raw)}, {len(raw)})", None
    if k == "source":
        lines = "; ".join(f"{d.get('op')} -> {d.get('v', d.get('hex', ''))}" for d in draws)
        return (f"STSourceWith({c_bytes(raw)}, {len(raw)}, {a.get('size', 0)})",
                f"draws: {comment_safe(lines)[:300]}" if lines else None)
    if k == "value":
        tag = a.get("datatype") or (spec or {}).get("datatype") or "?"
        return (f"STValue(@{json.dumps(tag)}, {c_bytes(raw)}, {len(raw)}, {a.get('size', 0)})",
                comment_safe(f"{a.get('name', '?')} = {a.get('v')}")[:300])
    if k == "raw":
        typ = (spec or {}).get("type", "char")
        return (f"({{ {typ} stitch_v; memset(&stitch_v, 0, sizeof stitch_v); "
                f"memcpy(&stitch_v, {c_bytes(raw)}, {len(raw)} < sizeof stitch_v ? {len(raw)} : sizeof stitch_v); stitch_v; }})"), None
    v = str(a.get("v", "0"))
    if k == "bool":
        return ("YES" if v in ("true", "1") else "NO"), None
    if k == "float":
        return _float_literal(a, (spec or {}).get("c", "double" if a.get("size") == 8 else "float")), None
    if k == "oneof" and spec and spec.get("values"):
        # the constant's name as the harness wrote it
        vals = [str(x) for x in a.get("values", [])]
        if v in vals and len(spec["values"]) == len(vals):
            return spec["values"][vals.index(v)], f"= {v}"
        return v, None
    if k in ("range", "oneof", "flags"):
        return f"{v}LL" if re.fullmatch(r"-?\d+", v) else v, None
    if k == "int":
        ctype = (spec or {}).get("c", "long long")
        return _int_literal(v, ctype, bool(a.get("signed")), int(a.get("size", 8))), None
    return v, None


def _var(typ: str, node: int, k: int, n_out: int):
    base = typ[:1].lower() + typ[1:]
    return f"{base}{node}" + (f"_{k}" if n_out > 1 else "")


def write(p, plan: Path, header):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    sigs = signatures(p.root)
    files = harness_files(p.root)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    vars_, body = {}, []
    for n in order:
        c = calls[n]
        sig = sigs.get(c["ep"], {"ret": "void", "outs": [], "args": []})
        specs = [sp for _, _, sp in sig["args"] if "obj" not in sp]
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        args, summ, si, pi = [], [], 0, 0
        names = [an for _, an, _ in sig["args"]]
        for ai, a in enumerate(c.get("args") or []):
            name = a.get("name") or (names[ai] if ai < len(names) else "")
            if "obj" in a:
                v = vars_.get(tuple(a["obj"]))
                args.append(v if v else "nil /* missing object */")
                summ.append(f"{name}={v}")
                continue
            spec = specs[pi] if pi < len(specs) else None
            pi += 1
            dr = draws.get(n, {}).get(si, []) if a["kind"] == "source" else []
            if a["kind"] == "source":
                si += 1
            lit, comment = _literal(a, spec, dr)
            if comment:
                body.append(f"        // {name}: {comment}")
            args.append(lit)
            summ.append(name)
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f' (bailed in the original run: "{comment_safe(reason)}")'}.get(status, "")
        body.append(f"        // #{n} {c['ep']}({', '.join(summ)}){note}")
        call = f"stub_{c['ep']}({', '.join(args)})"
        ep = json.dumps(c["ep"])
        if status == "bail":
            body.append(f"        STCallBails({n}, {ep}, ^{{ (void){call}; }});")
            continue
        types = c.get("outs", [])
        outs = (d or {}).get("outs", []) if status != "crash" else []
        if not types:
            body.append(f"        STCall({n}, {ep}, ^id {{ {call}; return nil; }});")
        elif len(types) == 1:
            if 0 in outs:
                v = _var(types[0], n, 0, 1)
                vars_[(n, 0)] = v
                body.append(f"        stitch_obj_{types[0]} {v} = STCall({n}, {ep}, ^id {{ return {call}; }});")
            else:
                body.append(f"        STCall({n}, {ep}, ^id {{ return {call}; }});")
        else:
            if outs:
                arr = f"outs{n}"
                body.append(f"        NSArray *{arr} = STCall({n}, {ep}, ^id {{ return {call}; }});")
                for k in outs:
                    v = _var(types[k], n, k, len(types))
                    vars_[(n, k)] = v
                    body.append(f"        stitch_obj_{types[k]} {v} = {arr}[{k}];")
            else:
                body.append(f"        STCall({n}, {ep}, ^id {{ return {call}; }});")
        if status == "crash":
            break
    if crash_node is not None:
        body.append(f"        // the original run crashed in call #{crash_node}; nothing after it ran")
    head = ["// " + h if h else "//" for h in header]
    includes = [f'#include "{f.relative_to(p.root)}"' for f in files]
    text = "\n".join(head + [
        "#import <Stitch/Stitch.h>", "#include <string.h>", "",
        "// The harness: its stubs are the functions stub_NAME (paths from the project directory).",
        *includes, "",
        "int main(void) {",
        "    // The program. Each STCall is one harness call with its arguments filled in.",
        "    return STReproMain(^{",
        *body,
        "    });",
        "}", ""])
    return text, {"calls": len(order), "crash_node": crash_node, "skipped": len(skips)}


def run_script(p, src_rel, timeout=60):
    return (f"stitch-build --standalone {shlex.quote(src_rel)} --out /tmp/stitch-standalone >/tmp/build.log 2>&1 "
            f"|| {{ grep -E 'error' /tmp/build.log >&2; exit 201; }}; cat /tmp/build.log >&2; echo '[repro] ---- run ----' >&2; "
            f"ASAN_OPTIONS={asan_options()} timeout {timeout} /tmp/stitch-standalone")


def build_hint(src_rel):
    return f"stitch-build --standalone {src_rel} --out /tmp/repro && /tmp/repro   (in `stitch shell`)"
