"""`stitch write` for Java: Repro.java calls each stub (a static method) in order."""
import json
import shlex
from pathlib import Path

from ..standalone import read_plan
from . import comment_safe

FILENAME = "Repro.java"
SHIM = "stitch.Standalone"  # in /stitch/java/stitch.jar (the runtime jar)


def install_shim(d: Path):
    pass  # the runtime jar in the image provides stitch.Standalone


def _jstr(s):
    return json.dumps(s, ensure_ascii=True)


def _literal(a, draws):
    k = a["kind"]
    if k in ("str", "bytes", "file"):
        b = bytes.fromhex(a.get("hex", ""))
        if k == "str":
            return _jstr(b.decode("utf-8", "replace")), None
        lit = f'Standalone.hex("{b.hex()}")'
        if k == "file":
            lit = f"Standalone.file({lit})" + (".toFile()" if a.get("jtype") == "java.io.File" else "")
        return lit, None
    if k == "source":
        lines = "; ".join(f"{d['op']} -> {d.get('v', bytes.fromhex(d.get('hex', '')).decode('utf-8', 'replace'))}" for d in draws)
        return f'new stitch.Source(Standalone.hex("{a["hex"]}"))', (f"draws: {comment_safe(lines)[:300]}" if lines else None)
    if k == "value":
        return (f'Standalone.value({_jstr(a.get("datatype") or "")}, Standalone.hex("{a["hex"]}"))',
                comment_safe(f"{a.get('name', '?')} = {a.get('v')}")[:300])
    if a.get("java") is not None:
        return a["java"], None
    return "null", f"{a.get('name', '?')} was {a.get('v')}: not a literal, fill it in"


def write(p, plan: Path, header):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    vars_ = {}
    body = []
    for n in order:
        c = calls[n]
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        args, summ, si = [], [], 0
        for a in c.get("args") or []:
            name = a.get("name", "")
            if "obj" in a:
                v = vars_.get(tuple(a["obj"]), "null")
                args.append(v)
                summ.append(f"{name}={v}")
            else:
                dr = draws.get(n, {}).get(si, []) if a["kind"] == "source" else []
                if a["kind"] == "source":
                    si += 1
                lit, comment = _literal(a, dr)
                if comment:
                    body.append(f"            // {comment}")
                args.append(lit)
                summ.append(name)
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f' (bailed in the original run: "{reason}")'}.get(status, "")
        body.append(f"            // #{n} {c['ep']}({', '.join(summ)}){note}")
        cls = c.get("class", "Stubs").replace("$", ".")
        invoke = f"{cls}.{c.get('method', c['ep'])}({', '.join(args)})"
        if status == "bail":
            exp = "".join(f", {x.replace('$', '.')}.class" for x in c.get("bailsOn", []))
            body.append(f"            Standalone.execBails({n}, {_jstr(c['ep'])}, () -> {invoke}{exp});")
            continue
        types = c.get("outs", [])
        if types:
            v = f"{types[0][:1].lower() + types[0][1:]}{n}"
            vars_[(n, 0)] = v
            body.append(f"            var {v} = Standalone.call({n}, {_jstr(c['ep'])}, () -> {invoke});")
        else:
            body.append(f"            Standalone.exec({n}, {_jstr(c['ep'])}, () -> {invoke});")
        if status == "crash":
            break
    if crash_node is not None:
        body.append(f"            // the original run crashed in call #{crash_node}; nothing after it ran")
    head = ["// " + h if h else "//" for h in header]
    text = "\n".join(head + ["import stitch.Standalone;", "",
                             "public class Repro {",
                             "    public static void main(String[] args) {",
                             "        Standalone.run(() -> {", *body, "        });", "    }", "}", ""])
    return text, {"calls": len(order), "crash_node": crash_node, "skipped": len(skips)}


def _env(build=".stitch/build"):
    # the repro script exports the classpath and JVM options (`STITCH_JAVA_CP`, `STITCH_JVM`)
    return (f"eval \"$(grep '^export ' {build}/repro | sed 's#\\$HERE#/project/{build}#g')\"")


def run_script(p, src_rel, timeout=60):
    # javac wants the file named after its public class: compile a copy (min.java works too)
    q = shlex.quote(src_rel)
    return (f"{_env()} && mkdir -p /tmp/repro-src /tmp/repro-classes && "
            f"cls=$(sed -n 's/^[[:space:]]*public[[:space:]]\\+\\(final[[:space:]]\\+\\)\\?class[[:space:]]\\+\\([A-Za-z_][A-Za-z0-9_]*\\).*/\\2/p' {q} | head -1) && "
            f"cls=${{cls:-Repro}} && cp {q} /tmp/repro-src/$cls.java && "
            f"javac -nowarn -encoding UTF-8 -d /tmp/repro-classes -cp \"$STITCH_JAVA_CP\" /tmp/repro-src/$cls.java "
            f"2>/tmp/javac.log || {{ cat /tmp/javac.log >&2; exit 201; }}; "
            f"echo '[repro] ---- run ----' >&2; "
            f"timeout {timeout} java $STITCH_JVM -Dstitch.app=/project/.stitch/build/app -cp \"/tmp/repro-classes:$STITCH_JAVA_CP\" "
            f"$cls >&2")


def build_hint(src_rel):
    return f"javac -cp \"$STITCH_JAVA_CP\" {Path(src_rel).name} && java -cp \".:$STITCH_JAVA_CP\" {Path(src_rel).stem}"
