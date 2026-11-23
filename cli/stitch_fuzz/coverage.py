"""`stitch cov`: run programs on a coverage build of the library and harness, write lcov + HTML.

Other languages measure coverage in their reproducer (`repro --each --coverage DIR`, see
languages/<lang>/); the C/C++ coverage build uses clang source-based coverage (-fprofile-instr-generate -fcoverage-mapping)
and no sanitizers. It is a separate project image (`stitch-<project>:<ident>-cov`) built from the
coverage base image, whose compiler wrappers switch flags when STITCH_COVERAGE=1, so project
Dockerfiles need no changes.

Programs run with `repro --each`: one forked child per program, so a crash or timeout only loses
that program. Children merge their counters into a profile pool in the container's /tmp.
"""
import json
import re
import shlex
import shutil
from pathlib import Path

from . import docker
from .source import Source

IGNORE = r"^(/usr/|/stitch/|/opt/)"
# languages whose coverage build is clang source-based coverage of the whole harness binary
CLANG_COV = ("cpp", "objc")
CHUNK = 200  # programs per repro process


def build_cov(p, rev=None, quiet=False):
    """Coverage image + reproducer binary. Returns (image, binary_rel, ident, commit)."""
    docker.check_docker()
    base = docker.ensure_cov_base(p.language)
    ctx, ident, commit = Source(p).materialize(rev)
    quiet = quiet or docker.image_exists(f"stitch-{p.name}:{ident}-cov")  # only a cache check
    image = docker.build_project_image(p, base, ctx, ident, quiet=quiet, variant="cov")
    outdir = f".stitch/cov/build-{ident}"
    r = docker.run(p, image, ["stitch-build", "--json", "--target", "repro", "--out", outdir], capture=True)
    if r.returncode != 0:
        raise docker.DockerError(f"the harness does not build with coverage:\n{docker.build_errors(r)}")
    return image, f"{outdir}/repro", ident, commit


def _script(binary, out_rel, timeout, jobs, html):
    b, o = shlex.quote(binary), shlex.quote("/project/" + out_rel)
    ign = shlex.quote(IGNORE)
    lines = [
        "set -e",
        "rm -rf /tmp/stitch-cov && mkdir -p /tmp/stitch-cov",
        f"export LLVM_PROFILE_FILE=/tmp/stitch-cov/cov-%4m.profraw",
        f"xargs -a {o}/programs.txt -n {CHUNK} -P {jobs} {b} --each --timeout {timeout} > {o}/runs.jsonl 2> {o}/runs.log || true",
        "unset LLVM_PROFILE_FILE",
        "ls /tmp/stitch-cov/*.profraw > /dev/null 2>&1 || { echo 'no coverage data written' >&2; exit 3; }",
        f"llvm-profdata merge -sparse /tmp/stitch-cov/*.profraw -o {o}/cov.profdata",
        f"llvm-cov export -format=lcov {b} -instr-profile={o}/cov.profdata -ignore-filename-regex={ign} > {o}/lcov.container.info",
        f"llvm-cov report {b} -instr-profile={o}/cov.profdata -ignore-filename-regex={ign} > {o}/report.txt",
    ]
    if html:
        lines.append(f"llvm-cov show -format=html -output-dir={o}/html -show-line-counts-or-regions "
                     f"-ignore-filename-regex={ign} {b} -instr-profile={o}/cov.profdata")
    # library sources live only in the image: copy them out so lcov.info works on the host
    lines += [
        f"grep '^SF:' {o}/lcov.container.info | cut -c4- | while read -r f; do "
        f"case \"$f\" in /project/*) ;; *) mkdir -p {o}/sources\"$(dirname \"$f\")\" && cp \"$f\" {o}/sources\"$f\" || true;; esac; done",
        f"grep '^FN:' {o}/lcov.container.info | cut -d, -f2- | sort -u > {o}/names.txt",
        f"c++filt < {o}/names.txt > {o}/names.demangled.txt",
    ]
    return "\n".join(lines)


def _lang_script(binary, out_rel, timeout, html):
    """Worker-runtime languages: the reproducer measures coverage itself (`--each --coverage DIR`)."""
    b, o = shlex.quote(binary), shlex.quote("/project/" + out_rel)
    return "\n".join([
        "set -e",
        f"{b} --each --timeout {timeout} --coverage {o} --files-from {o}/programs.txt > {o}/runs.jsonl 2> {o}/runs.log || true",
        f"test -f {o}/lcov.info || {{ echo 'no coverage data written' >&2; tail -20 {o}/runs.log >&2; exit 3; }}",
        # relative paths are relative to /project; harness snapshots map back to harness/
        f"sed -e 's#^SF:\\([^/]\\)#SF:/project/\\1#' -e 's#^SF:/project/.stitch/[^:]*/app/#SF:/project/#' {o}/lcov.info > {o}/lcov.container.info",
        f"rm -f {o}/lcov.info",
        "" if html else f"rm -rf {o}/html",
        f"grep '^SF:' {o}/lcov.container.info | cut -c4- | while read -r f; do "
        f"case \"$f\" in /project/*) ;; *) mkdir -p {o}/sources\"$(dirname \"$f\")\" && cp \"$f\" {o}/sources\"$f\" || true;; esac; done",
        f"grep '^FN:' {o}/lcov.container.info | cut -d, -f2- | sort -u > {o}/names.txt || true",
        f"cp {o}/names.txt {o}/names.demangled.txt",
    ])


def parse_lcov(text):
    """[{path, lines:(found,hit), functions:[(name,line,count)], branches:(found,hit)}]"""
    files, cur = [], None
    for line in text.splitlines():
        if line.startswith("SF:"):
            cur = {"path": line[3:], "lf": 0, "lh": 0, "brf": 0, "brh": 0, "fn": {}, "fnda": {}}
        elif cur is None:
            continue
        elif line.startswith("FN:"):
            # FN:<line>,<name> or (lcov 2) FN:<start>,<end>,<name>
            parts = line[3:].split(",", 2)
            name = parts[2] if len(parts) == 3 and parts[1].isdigit() else line[3:].partition(",")[2]
            cur["fn"][name] = int(parts[0])
        elif line.startswith("FNDA:"):
            cnt, _, name = line[5:].partition(",")
            cur["fnda"][name] = cur["fnda"].get(name, 0) + int(cnt)
        elif line.startswith(("LF:", "LH:", "BRF:", "BRH:")):
            k, _, v = line.partition(":")
            cur[k.lower()] = int(v)
        elif line == "end_of_record":
            files.append(cur)
            cur = None
    return files


_LOCAL = re.compile(r"^[^:]+\.(c|cc|cpp|cxx|h|hh|hpp|inc):")


def _pretty(name, dem):
    name = dem.get(name, name)
    return _LOCAL.sub("", name)  # static functions are recorded as "file.c:name"


def _pct(h, f):
    return round(100.0 * h / f, 1) if f else None


def summarize(p, out: Path, files, dem):
    def host_path(path):
        if path.startswith("/project/"):
            return path[len("/project/"):]
        return str((out / "sources" / path.lstrip("/")).relative_to(p.root))

    include = [str(x) for x in (p.cfg.get(p.language, {}) or {}).get("include") or []]

    def group(path):
        if path.startswith("/project/"):
            return "harness"
        # JavaScript: packages under node_modules are dependencies unless [javascript] include names them
        m = re.search(r"/node_modules/((?:@[^/]+/)?[^/]+)/", path)
        if p.language == "javascript" and include and m and m.group(1) not in include:
            return "dependencies"
        return "library"

    def functions(f):
        """(name, line, calls), one per function. llvm-cov lists each instantiation of a generic
        (and functions it never instantiated) separately: one function per definition line,
        covered if any instantiation ran."""
        fns = [(n, f["fn"].get(n, 0), f["fnda"].get(n, 0)) for n in f["fn"]]
        if p.language not in ("rust", "swift", "cpp", "objc"):
            return fns
        by_line = {}
        for n, ln, c in fns:
            if ln in by_line:
                n0, _, c0 = by_line[ln]
                by_line[ln] = (n0 if c0 or not c else n, ln, c0 + c)
            else:
                by_line[ln] = (n, ln, c)
        return list(by_line.values())

    groups = {"library": [], "harness": [], "dependencies": []}
    for f in files:
        fns = functions(f)
        rec = {
            "file": f["path"],
            "local": host_path(f["path"]),
            "lines": [f["lh"], f["lf"]],
            "functions": [sum(1 for _, _, c in fns if c), len(fns)],
            "branches": [f["brh"], f["brf"]],
            "uncovered_functions": sorted([{"name": _pretty(n, dem), "line": ln, "static": bool(_LOCAL.match(n))}
                                           for n, ln, c in fns if not c], key=lambda x: x["line"]),
        }
        groups[group(f["path"])].append(rec)

    def total(recs, key):
        return [sum(r[key][0] for r in recs), sum(r[key][1] for r in recs)]

    summary = {}
    for g, recs in groups.items():
        recs.sort(key=lambda r: r["file"])
        summary[g] = {k: total(recs, k) for k in ("lines", "functions", "branches")}
        summary[g]["files"] = recs
    return summary


def rewrite_lcov(p, out: Path, text):
    def sub(m):
        path = m.group(1)
        if path.startswith("/project/"):
            return "SF:" + str(p.root / path[len("/project/"):])
        return "SF:" + str(out / "sources" / path.lstrip("/"))
    return re.sub(r"^SF:(.*)$", sub, text, flags=re.M)


def run(p, programs, out: Path, label, rev=None, timeout=10, jobs=4, html=True, quiet=False):
    """Run `programs` (paths relative to the project root) on the coverage build. Returns the summary."""
    if p.language in CLANG_COV:
        image, binary, ident, commit = build_cov(p, rev, quiet=quiet)
    elif rev:
        from .cli import build_repro_at
        image, binary, commit = build_repro_at(p, rev)
    else:
        from .cli import build
        info = build(p, harness_only=True, quiet=quiet)
        if not info["result"].get("ok"):
            raise docker.DockerError("harness build failed (stitch build)")
        image, binary, commit = info["image"], ".stitch/build/repro", info.get("commit")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    (out / "programs.txt").write_text("\n".join(programs) + "\n")
    out_rel = str(out.relative_to(p.root))
    script = _script(binary, out_rel, timeout, jobs, html) if p.language in CLANG_COV else _lang_script(binary, out_rel, timeout, html)
    r = docker.run(p, image, ["sh", "-c", script], capture=True)
    if r.returncode != 0:
        raise docker.DockerError(f"coverage run failed:\n{(r.stderr or '')[-3000:]}")

    runs = {"programs": 0, "ok": 0, "crashed": 0, "timeouts": 0, "invalid": 0}
    for line in (out / "runs.jsonl").read_text().splitlines():
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(rec, dict):  # (other lines are program output that reached the file)
            for k, v in rec.items():
                if isinstance(v, int):
                    runs[k] = runs.get(k, 0) + v

    raw = (out / "lcov.container.info").read_text()
    (out / "lcov.info").write_text(rewrite_lcov(p, out, raw))
    (out / "lcov.container.info").unlink()
    names = (out / "names.txt").read_text().splitlines()
    dems = (out / "names.demangled.txt").read_text().splitlines()
    dem = dict(zip(names, dems)) if len(names) == len(dems) else {}
    for f in ("names.txt", "names.demangled.txt"):
        (out / f).unlink()

    summary = {"ok": True, "label": label, "commit": commit, "rev": rev, "image": image, "runs": runs,
               **summarize(p, out, parse_lcov(raw), dem),
               "outputs": {"lcov": p.rel(out / "lcov.info"), "report": p.rel(out / "report.txt"),
                           "summary": p.rel(out / "summary.json"), "profdata": p.rel(out / "cov.profdata"),
                           "binary": binary, **({"html": p.rel(out / "html" / "index.html")} if html else {})}}
    (out / "summary.json").write_text(json.dumps(summary, indent=1))
    return summary


def render(s, limit=20, show_harness=False):
    def row(name, h_f):
        h, f = h_f
        return f"{name:<11}{h:>7} / {f:<7} {(_pct(h, f) or 0):5.1f}%" if f else f"{name:<11}{'-':>7}"

    r = s["runs"]
    lines = [f"coverage of {r['programs']} programs ({r['ok']} ok, {r['crashed']} crashed, {r['timeouts']} timed out"
             + (f", {r['invalid']} invalid" if r.get("invalid") else "") + f"), library at {(s['commit'] or 'local')[:12]}",
             ""]
    for g in ("library", "harness") if show_harness else ("library",):
        t = s[g]
        lines += [f"{g}:", "  " + row("lines", t["lines"]), "  " + row("functions", t["functions"]),
                  "  " + row("branches", t["branches"]), ""]
    dep = s.get("dependencies") or {}
    if dep.get("files"):
        lines += [f"dependencies ({len(dep['files'])} files under node_modules, not counted above): "
                  f"lines {(_pct(*dep['lines']) or 0):.1f}%", ""]
    lib = s["library"]["files"]
    if lib:
        w = min(48, max(len(f["local"].split("/sources/", 1)[-1]) for f in lib))
        lines.append(f"  {'file':<{w}}  {'lines':>7}  {'functions':>9}")
        for f in sorted((f for f in lib if f["lines"][1]), key=lambda f: -f["lines"][1]):
            name = f["local"].split("/sources/", 1)[-1][-w:]
            lp, fp = _pct(*f["lines"]), _pct(*f["functions"])
            lines.append(f"  {name:<{w}}  {(lp or 0):6.1f}%  {(fp or 0):8.1f}%")
        lines.append("")
    unc = [(f["local"].split("/sources/", 1)[-1], u) for f in sorted(lib, key=lambda f: -f["lines"][1])
           for u in f["uncovered_functions"]]
    if unc:
        ext = [x for x in unc if not x[1]["static"]]
        lines.append(f"never called: {len(unc)} library functions, {len(ext)} of them not static"
                     + (f" (first {limit}, not static first)" if len(unc) > limit else ""))
        for path, u in (ext + [x for x in unc if x[1]["static"]])[:limit]:
            lines.append(f"  {u['name']}{'  (static)' if u['static'] else ''}  {path}:{u['line']}")
        lines.append("")
    o = s["outputs"]
    lines.append(f"lcov: {o['lcov']}" + (f"\nhtml: {o['html']}" if "html" in o else "") + f"\nsummary: {o['summary']}")
    return "\n".join(lines)
