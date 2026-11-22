"""`stitch write` for JavaScript: the harness code plus a main() that calls each stub in order."""
import json
import re
import shlex
from pathlib import Path

from ..project import STITCH_HOME
from ..standalone import read_plan
from . import comment_safe

FILENAME = "repro.js"
SHIM = "stitch.js"
_RT = STITCH_HOME / "languages" / "javascript"


def install_shim(d: Path):
    src = (_RT / "runtime" / "source.js").read_text().replace('module.exports = { Source };', '')
    api = (_RT / "standalone" / "stitch_api.js").read_text()
    (d / SHIM).write_text("// stitch.js: STITCH++ runtime stand-in for standalone reproducers (no fuzzer).\n" + src + "\n" + api)


def _literal(a, draws):
    k = a["kind"]
    if k in ("str", "bytes", "file"):
        b = bytes.fromhex(a.get("hex", ""))
        if k == "str":
            return json.dumps(b.decode("utf-8", "replace")), None
        buf = f'Buffer.from("{b.hex()}", "hex")'
        return (buf if k == "bytes" else f"stitch.file({buf})"), None
    if k == "source":
        lines = "; ".join(f"{d['op']} -> {d.get('v', bytes.fromhex(d.get('hex', '')).decode('utf-8', 'replace'))}" for d in draws)
        return f'new stitch.Source(Buffer.from("{a["hex"]}", "hex"))', (f"draws: {comment_safe(lines)[:300]}" if lines else None)
    if k == "value":
        return (f'stitch.value({json.dumps(a.get("datatype"))}, Buffer.from("{a["hex"]}", "hex"))',
                comment_safe(f"{a.get('name', '?')} = {a.get('v')}")[:300])
    js = a.get("js")
    if js is not None:
        return js, None
    return "undefined", f"{a.get('name', '?')} was {a.get('v')}: not a literal, fill it in"


# Each harness file becomes a module function, so files keep their own scope and `require` each
# other with relative paths, as they do in the fuzzer. Files load in sorted order, like the runtime.
_LOADER = """const harnessFiles = {};
const harnessCache = {};
function harnessLoad(name) {
  if (!harnessCache[name]) {
    const module = { exports: {} };
    harnessCache[name] = module;
    harnessFiles[name](module, module.exports, (spec) => harnessRequire(name, spec));
  }
  return harnessCache[name].exports;
}
function harnessRequire(from, spec) {
  if (spec === "stitch") return stitch;
  if (spec.startsWith("./") || spec.startsWith("../")) {
    const p = require("path").posix.join(require("path").posix.dirname(from), spec);
    for (const c of [p, p + ".js", p + ".cjs", p + ".mjs", p + "/index.js"]) if (harnessFiles[c]) return harnessLoad(c);
  }
  return require(spec);
}
// the default import of a module: an ES module's default export, or a CommonJS module's exports
function esmDefault(m) {
  return m && m[Symbol.toStringTag] === "Module" && "default" in m ? m.default : m;
}
"""


_IMPORT = re.compile(r"""^([ \t]*)import\s+(?:([\w$]+)\s*,?\s*)?(?:\*\s+as\s+([\w$]+)|\{([^}]*)\})?\s*(?:from\s+)?(["'][^"']+["'])\s*;?""",
                     re.M)
_EXPORT = re.compile(r"^([ \t]*)export\s+(default\s+)?(?!\{)(?:(async\s+function\*?|function\*?|class|const|let|var)\s+([\w$]+))?", re.M)


def _esm_to_cjs(text):
    """An ES module harness file (.mjs) as CommonJS, so it can be pasted like the others:
    import ... from "m" -> require("m"); export declarations -> exports. (require() loads ES
    module libraries.) Covers the import/export forms harness files use, not every one."""
    def imp(m):
        ind, default, star, names, mod = m.groups()
        out = []
        if default:
            out.append(f"const {default} = esmDefault(require({mod}));")
        if star:
            out.append(f"const {star} = require({mod});")
        if names is not None:
            fields = ", ".join(x.strip().replace(" as ", ": ") for x in names.split(",") if x.strip())
            out.append(f"const {{ {fields} }} = require({mod});")
        return ind + (" ".join(out) if out else f"require({mod});")
    exported = []

    def exp(m):
        ind, default, kind, name = m.groups()
        if default and not name:
            return ind + "module.exports.default ="
        if name:
            exported.append(name)
        return ind + (f"{kind} {name}" if kind else "")
    text = _IMPORT.sub(imp, text)
    text = re.sub(r"^[ \t]*export\s*\{([^}]*)\}\s*;?", lambda m: "Object.assign(module.exports, { " + ", ".join(
        (f"{b.strip()}: {a.strip()}" if " as " in x else x.strip()) for x in m.group(1).split(",") if x.strip()
        for a, _, b in [x.partition(" as ")]) + " });", text, flags=re.M)
    text = _EXPORT.sub(exp, text)
    if exported:
        text += "\nObject.assign(module.exports, { " + ", ".join(exported) + " });"
    return text


def _harness_code(p):
    hdir = p.root / "harness"
    parts = [_LOADER]
    files = [f for f in sorted(hdir.rglob("*")) if f.suffix in (".js", ".cjs", ".mjs")
             and not {"data", "node_modules"} & set(f.relative_to(hdir).parts[:-1])]
    names = []
    for f in files:
        name = "harness/" + f.relative_to(hdir).as_posix()
        names.append(name)
        code = f.read_text().rstrip()
        if f.suffix == ".mjs":
            code = _esm_to_cjs(code)
        parts.append(f"// {'=' * 94}\n// {p.rel(f)}{' (an ES module, as CommonJS)' if f.suffix == '.mjs' else ''}\n// {'=' * 94}\n"
                     f"harnessFiles[{json.dumps(name)}] = (module, exports, require) => {{\n{code}\n}};\n")
    parts.append(f"for (const f of {json.dumps(names)}) harnessLoad(f);\n")
    return "\n".join(parts)


_IDENT = re.compile(r"^[A-Za-z_$][\w$]*$")


def _key(k):
    return k if _IDENT.match(k) else json.dumps(k)


def write(p, plan: Path, header):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    vars_ = {}
    body = ["async function main() {"]
    for n in order:
        c = calls[n]
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        fields, summ, si = [], [], 0
        for a in c.get("args") or []:
            name = a.get("name", "")
            if "obj" in a:
                v = vars_.get(tuple(a["obj"]), "undefined")
                fields.append((name, v))
                summ.append(f"{name}={v}")
            else:
                dr = draws.get(n, {}).get(si, []) if a["kind"] == "source" else []
                if a["kind"] == "source":
                    si += 1
                lit, comment = _literal(a, dr)
                if comment:
                    body.append(f"  // {comment}")
                fields.append((name, lit))
                summ.append(name)
        args = ("{ " + ", ".join(f"{_key(k)}: {v}" for k, v in fields) + " }"
                if c.get("named", True) else "[" + ", ".join(v for _, v in fields) + "]")
        if args == "{  }":
            args = "{}"
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f' (bailed in the original run: "{reason}")'}.get(status, "")
        body.append(f"  // #{n} {c['ep']}({', '.join(summ)}){note}")
        if status == "bail":
            body.append(f"  await stitch.callBails({n}, {json.dumps(c['ep'])}, {args});")
            continue
        types = c.get("outs", [])
        call = f"await stitch.call({n}, {json.dumps(c['ep'])}, {args})"
        if not types:
            body.append(f"  {call};")
        elif not c.get("multi"):
            v = f"{types[0][:1].lower() + types[0][1:]}{n}"
            vars_[(n, 0)] = v
            body.append(f"  const {v} = {call};")
        else:
            names = [f"{t[:1].lower() + t[1:]}{n}_{k}" for k, t in enumerate(types)]
            for k in (d or {}).get("outs", []):
                vars_[(n, k)] = names[k]
            body.append(f"  const [{', '.join(names)}] = {call};")
        if status == "crash":
            break
    if crash_node is not None:
        body.append(f"  // the original run crashed in call #{crash_node}; nothing after it ran")
    body.append("}")
    head = ["// " + h if h else "//" for h in header]
    text = "\n".join(head + ['"use strict";', 'const stitch = require("./stitch"); // stitch.js next to this file: the runtime without the fuzzer', "",
                             _harness_code(p), "",
                             "// " + "=" * 94,
                             "// The program. Each stitch.call(...) is one harness call with its arguments filled in.",
                             "// " + "=" * 94, *body, "", "stitch.run(main);", ""])
    return text, {"calls": len(order), "crash_node": crash_node, "skipped": len(skips)}


def run_script(p, src_rel, timeout=60):
    d = str(Path(src_rel).parent)
    return (f"cd {shlex.quote(d)} && echo '[repro] ---- run ----' >&2 && "
            f"timeout {timeout} node {shlex.quote(Path(src_rel).name)} >&2")


def build_hint(src_rel):
    return f"node {Path(src_rel).name}   (in the project image: stitch shell)"
