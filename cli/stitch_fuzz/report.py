"""`stitch report`: a reproducer small enough for a GitHub issue.

Three files a maintainer can read and run without STITCH++:

  Dockerfile    the STITCH++ toolchain image for the language (pinned by digest), the library at the
                pinned commit, built by the project's own Dockerfile (pasted in unchanged), then the
                reproducer, built and run with plain commands (clang++, python3, go build, cargo, ...)
  repro.<ext>   the standalone reproducer (`stitch write`, minimized to plain library code)
  run.sh        docker build + docker run

plus ISSUE.md (the issue text: the code, the output, the Dockerfile) and output.txt (what it printed
when it was verified).

Package versions are pinned to the date the project image was built (when the crash was found):
the Ubuntu snapshot archive for apt, PIP_UPLOADED_PRIOR_TO for pip, npm_config_before for npm. The
library itself is pinned by commit, the toolchain image by digest.
"""
import json
import re
import shlex
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from . import docker, reports
from .source import Source

# languages whose `stitch write` shim is a file that can ship next to the reproducer; elsewhere the
# shim is the harness package, so the reproducer must be plain library code first
SHIM_FILES = {"cpp": "stitch_standalone.h", "python": "stitch.py", "javascript": "stitch.js", "ruby": "stitch.rb"}
EXT = {"cpp": ".cpp", "objc": ".m", "python": ".py", "javascript": ".js", "ruby": ".rb", "go": ".go",
       "rust": ".rs", "swift": ".swift", "java": ".java"}
FENCE = {"cpp": "cpp", "objc": "objc", "python": "python", "javascript": "js", "ruby": "ruby", "go": "go",
         "rust": "rust", "swift": "swift", "java": "java"}
WHAT = {"cpp": "clang 18 with sanitizers", "objc": "clang 18 + GNUstep with sanitizers", "python": "Python 3.12",
        "javascript": "Node.js", "ruby": "Ruby", "go": "Go", "rust": "Rust nightly with sanitizers",
        "swift": "Swift with sanitizers", "java": "OpenJDK"}
REPO = "https://github.com/hgarrereyn/STITCHplusplus"


class ReportError(Exception):
    pass


# =============================================================================================
# per-language: how to build and run one plain file against the installed library
# =============================================================================================
def _san_flags(p):
    san = ",".join(p.sanitizers) or "none"
    f = ["-g", "-O1", "-fno-omit-frame-pointer"]
    if san != "none":
        f.append(f"-fsanitize={san}")
    if "undefined" in p.sanitizers:
        f += ["-fno-sanitize=vptr,function", "-fno-sanitize-recover=undefined"]
    return f


def _asan_env():
    from .standalone import asan_options
    return f"ASAN_OPTIONS={asan_options()}"


def _q(args):
    return " ".join(shlex.quote(str(a)) for a in args)


def recipe(p, fname: str) -> dict:
    """{'files': {name: dest}, 'lines': Dockerfile lines after the library is built, 'run': display}"""
    b, lang = p.cfg["build"], p.language
    if lang in ("cpp", "objc"):
        is_c = fname.endswith((".c", ".m"))
        cc = ["clang"] if is_c else ["clang++", f"-std={b.get('std', 'c++17')}"]
        objc = lang == "objc"
        arc = ["-fobjc-arc"] if objc and p.cfg.get("objc", {}).get("arc", True) else []
        parts = [_q(cc + _san_flags(p)), "$(cat /stitch/objc/objc-flags)" if objc else "",
                 _q(arc + list(b.get("cflags", []))), fname, _q(list(b.get("ldflags", []))),
                 "$(cat /stitch/objc/link-flags)" if objc else "",
                 " ".join(l for l in ([] if is_c else ["-lstdc++"]) + ["-lm", "-lpthread"] if l not in b.get("ldflags", [])),
                 "-o repro"]
        build = " ".join(x for x in parts if x)
        return {"files": {fname: f"/repro/{fname}"},
                # Objective-C: an uncaught exception aborts (GNUstep), so ASan prints where it was thrown
                "lines": ["WORKDIR /repro", f"RUN {build}", f"ENV {_asan_env()}" + (" CRASH_ON_ABORT=YES" if objc else ""),
                          'CMD ["./repro"]'],
                "run": "./repro"}
    if lang in ("python", "javascript", "ruby"):
        exe = {"python": "python3", "javascript": "node", "ruby": "ruby"}[lang]
        return {"files": {fname: f"/repro/{fname}"},
                "lines": ["WORKDIR /repro", f'CMD ["{exe}", "{fname}"]'], "run": f"{exe} {fname}"}
    if lang == "go":
        go = p.cfg.get("go", {})
        lib = go.get("module")
        if not lib:
            raise ReportError("stitch.toml has no [go] module (the library's import path)")
        m = re.search(r"[/.]v(\d+)$", lib)
        ver = f"v{m.group(1)}.0.0" if m and int(m.group(1)) >= 2 else "v0.0.0"
        race = " -race" if go.get("race") else ""
        return {"files": {fname: f"/repro/{fname}"},
                "lines": ["WORKDIR /repro",
                          f"RUN go mod init repro && go mod edit -require={lib}@{ver} -replace={lib}={go.get('replace', '/src')} \\",
                          f"  && go mod tidy && go build{race} -o repro .",
                          'CMD ["./repro"]'], "run": "go build && ./repro"}
    if lang == "rust":
        rs = p.cfg.get("rust", {})
        deps = rs.get("dependencies") or {}
        if not deps:
            raise ReportError("stitch.toml has no [rust] dependencies (how the harness depends on the library)")
        san = rs.get("sanitizers", list(p.sanitizers))
        cargo = ["[package]", 'name = "repro"', 'version = "0.1.0"', 'edition = "2021"', "", "[dependencies]"]
        cargo += [f"{k} = {_toml(v)}" for k, v in deps.items()]
        cargo += ["", "[profile.release]", "debug = true",
                  f"overflow-checks = {'true' if rs.get('overflow_checks', True) else 'false'}"]
        flags = "-Zsanitizer=address " if "address" in san else ""
        return {"files": {fname: "/repro/src/main.rs"},
                "lines": ["WORKDIR /repro", "COPY <<EOF Cargo.toml", *cargo, "EOF",
                          "# the library's own lockfile, so its dependencies resolve as in its repository",
                          "RUN if [ -f /src/Cargo.lock ]; then cp /src/Cargo.lock .; fi \\",
                          f'  && RUSTFLAGS="{flags}-Cforce-frame-pointers=yes" cargo build --release --target "$(rustc -vV | sed -n \'s/host: //p\')" \\',
                          "  && cp target/*/release/repro /usr/local/bin/repro",
                          f"ENV RUST_BACKTRACE=1 {_asan_env()}", 'CMD ["repro"]'], "run": "cargo run --release"}
    if lang == "swift":
        sw = p.cfg.get("swift", {})
        deps, prods = sw.get("dependencies") or [], sw.get("products") or []
        if not deps:
            raise ReportError("stitch.toml has no [swift] dependencies (how the harness depends on the library)")
        pkg = ["// swift-tools-version:5.9", "import PackageDescription", "let package = Package(",
               '    name: "repro",', f"    dependencies: [{', '.join(deps)}],",
               f'    targets: [.executableTarget(name: "repro", dependencies: [{", ".join(prods)}], path: ".", sources: ["main.swift"])]',
               ")"]
        san = " -Xswiftc -sanitize=address" if "address" in p.sanitizers else ""
        return {"files": {fname: "/repro/main.swift"},
                "lines": ["WORKDIR /repro", "COPY <<EOF Package.swift", *pkg, "EOF",
                          "# the library's own Package.resolved, so its dependencies resolve as in its repository",
                          "RUN if [ -f /src/Package.resolved ]; then cp /src/Package.resolved .; fi \\",
                          f"  && swift build -c release{san}",
                          # traps (bounds, overflow, nil) reach ASan, which prints the stack; names demangled
                          f"ENV {_asan_env()}:handle_sigtrap=1:handle_sigill=1 SWIFT_BACKTRACE=enable=no",
                          'CMD ["bash", "-c", "set -o pipefail; .build/release/repro 2>&1 | swift demangle --simplified"]'], "run": "swift build && .build/release/repro"}
    if lang == "java":
        jv = p.cfg.get("java", {})
        cp = ":".join(jv.get("classpath", ["/deps/*"]))
        cls = Path(fname).stem
        jvm = " ".join(jv.get("jvm_options", []))
        return {"files": {fname: f"/repro/{fname}"},
                "lines": ["WORKDIR /repro", f'RUN javac -g -cp "{cp}" {fname}',
                          " ".join(x for x in ["CMD java", jvm, f'-cp "/repro:{cp}"', cls] if x)], "run": f"javac {fname} && java {cls}"}
    raise ReportError(f"no report recipe for language '{lang}'")


def _toml(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    if isinstance(v, str):
        return json.dumps(v)
    if isinstance(v, list):
        return "[" + ", ".join(_toml(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{ " + ", ".join(f"{k} = {_toml(x)}" for k, x in v.items()) + " }"
    raise ReportError(f"cannot write {v!r} into Cargo.toml")


# =============================================================================================
# the Dockerfile
# =============================================================================================
def _created(image):
    r = docker._run(["docker", "image", "inspect", image, "--format", "{{.Created}}"], capture_output=True)
    try:
        s = re.sub(r"\.\d+", "", r.stdout.strip()).replace("Z", "+00:00")
        return datetime.fromisoformat(s).astimezone(timezone.utc)
    except ValueError:
        return None


def pin_time(image, toolchain=None) -> datetime:
    """When the project image was built: its apt / pip / npm installs resolved then. Never before
    the toolchain image was built (apt cannot downgrade the packages already in it)."""
    times = [t for t in (_created(image), toolchain and _created(toolchain)) if t]
    return max(times) if times else datetime.now(timezone.utc)


def pin_lines(t: datetime):
    snap, iso = t.strftime("%Y%m%dT%H%M%SZ"), t.strftime("%Y-%m-%dT%H:%M:%SZ")
    return [f"# package versions as of {iso} (when the library image was built): apt from the Ubuntu",
            "# snapshot archive, pip and npm packages published before then",
            f"RUN echo 'APT::Snapshot \"{snap}\";' > /etc/apt/apt.conf.d/50snapshot",
            f"ENV PIP_UPLOADED_PRIOR_TO={iso} npm_config_before={iso}"]


_FROM_BASE = re.compile(r"^\s*FROM\s+\$\{?STITCH_BASE\}?(\s+AS\s+\S+)?\s*$", re.I)
_ARG_BASE = re.compile(r"^\s*ARG\s+STITCH_BASE(=.*)?\s*$", re.I)
_ARG = re.compile(r"^(\s*ARG\s+)([A-Za-z_][A-Za-z0-9_]*)\s*$", re.I)


def project_body(p) -> list:
    """The project's Dockerfile, with FROM ${STITCH_BASE} -> the pinned base and the build args
    the CLI passes (stitch.toml [docker] build_args) as defaults."""
    args = {k: str(v) for k, v in (p.cfg["docker"].get("build_args") or {}).items()}
    out = []
    for line in p.dockerfile.read_text().splitlines():
        if _ARG_BASE.match(line) or line.startswith("# syntax="):
            continue
        m = _FROM_BASE.match(line)
        if m:
            out.append("FROM base" + (m.group(1) or ""))
            continue
        m = _ARG.match(line)
        if m and m.group(2) in args:
            line = f"{m.group(1)}{m.group(2)}={shlex.quote(args[m.group(2)])}"
        out.append(line)
    while out and not out[0].strip():
        out.pop(0)
    if not any(l.startswith("FROM base") for l in out):
        raise ReportError(f"{p.rel(p.dockerfile)} does not start FROM ${{STITCH_BASE}}")
    return out


def dockerfile(p, title, base_ref, source, t: datetime, rec, fname) -> str:
    v = _version()
    L = [f"# {title}",
         "#   docker build -t repro . && docker run --rm repro",
         "",
         f"# {WHAT.get(p.language, p.language)} and build tools: the STITCH++ {p.language} toolchain image (no fuzzer),",
         f"# {REPO}/blob/v{v}/languages/{p.language}/Dockerfile",
         f"FROM {base_ref} AS base"]
    L += pin_lines(t)
    L += ["", f"# the library source: {source['what']}"]
    if source.get("git"):
        sub = " \\\n  && git -C /src submodule update --init --recursive --quiet" if source.get("submodules") else ""
        L += ["FROM base AS fetch",
              f"RUN git clone --quiet {source['git']} /src && git -C /src checkout --quiet {source['commit']}{sub}",
              "FROM scratch AS source", "COPY --from=fetch /src /"]
    else:
        L += ["FROM scratch AS source", "ADD source.tar.gz /"]
    L += ["", "# the library, built by the fuzzing project's own Dockerfile (unchanged)"]
    L += project_body(p)
    L += ["", "# the reproducer"]
    for name, dest in rec["files"].items():
        L.append(f"COPY {name} {dest}")
    L += rec["lines"]
    if any(l.startswith("COPY <<") for l in rec["lines"]):  # heredocs: older Docker needs the frontend named
        L.insert(0, "# syntax=docker/dockerfile:1.7")
    return "\n".join(L) + "\n"


def _version():
    from . import __version__
    return __version__


# =============================================================================================
# the files
# =============================================================================================
RUN_SH = """#!/bin/sh
# {title}
# Builds the library and the reproducer, then runs it. Needs only Docker.
set -e
cd "$(dirname "$0")"
docker build{platform} -t {tag} .
docker run --rm{platform} {tag}
"""


def _source(p, image, outdir: Path):
    src = Source(p)
    st = p.load_state()
    commit = st.get("commit") if st.get("image") == image else None
    if src.git_url and commit and st.get("ident") != "local":
        url = src.git_url
        web = url[:-4] if url.endswith(".git") else url
        link = f"[{commit[:12]}]({web}/commit/{commit})" if "github.com" in web or "gitlab" in web else f"`{commit[:12]}`"
        return {"git": url, "commit": commit, "submodules": src.submodules, "what": f"{url} @ {commit}",
                "md": f"{web} at {link}"}
    src.snapshot_tar(outdir / "source.tar.gz")
    return {"archive": "source.tar.gz", "what": f"snapshot of {src.path} (source.tar.gz)" + (f", commit {commit}" if commit else ""),
            "md": f"a snapshot of a local checkout (`source.tar.gz`, attach it)" + (f", commit `{commit[:12]}`" if commit else "")}


def find_file(p, ref):
    """The reproducer file for a crash id (its minimized standalone file if there is one) or a path."""
    f = Path(ref)
    if f.exists() and f.is_file():
        return f.resolve()
    rid = ref.split(":")[-1].split("/")[-1]
    cands = [p.state_dir / "standalone" / ref.replace(":", "-"), p.state_dir / "standalone" / rid]
    for d in cands:
        if d.is_dir():
            files = [x for x in d.iterdir() if x.suffix in (EXT[p.language], ".c", ".mm") and x.name not in SHIM_FILES.values()]
            mins = sorted((x for x in files if x.stem.lower().startswith("min")), key=lambda x: x.stat().st_mtime)
            if mins:
                return mins[-1]
            if files:
                return sorted(files, key=lambda x: x.stat().st_mtime)[-1]
    raise ReportError(f"no reproducer file for '{ref}': run `stitch write {ref}` and shrink it to plain library "
                      "code first (`stitch guide reproducers`), or pass the file")


def make_report(p, ref, outdir: Path = None, run=True, timeout=600) -> dict:
    from .cli import build
    from .standalone import uses_shim, find_expected, verdict, crash_key
    f = find_file(p, ref)
    text = f.read_text()
    shim = uses_shim(p.language, text)
    if shim and p.language not in SHIM_FILES:
        raise ReportError(f"{p.rel(f)} still calls the harness through the STITCH++ runtime: shrink it to plain "
                          f"library code first (`stitch guide reproducers`), check it with `stitch try {p.rel(f)}`, "
                          "then run `stitch report` on it. `stitch repro` makes a full reproducer that includes the harness.")
    expected = find_expected(f) or {}
    rid = expected.get("crash_id") or f.parent.name
    info = build(p, image_only=True, quiet=True)
    image = info["image"]
    d = (outdir or (p.state_dir / "reports" / rid)).resolve()
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)

    # the reproducer file (Java: named after its public class)
    if p.language == "java":
        m = re.search(r"^\s*public\s+(?:final\s+)?class\s+([A-Za-z_]\w*)", text, re.M)
        fname = f"{m.group(1) if m else 'Repro'}.java"
    elif p.language in ("cpp", "objc"):
        fname = "repro" + (f.suffix if f.suffix in (".c", ".cpp", ".m", ".mm") else EXT[p.language])
    else:
        fname = "repro" + EXT[p.language]
    (d / fname).write_text(text)
    rec = recipe(p, fname)
    if shim:
        sf = SHIM_FILES[p.language]
        if (f.parent / sf).exists():
            shutil.copy2(f.parent / sf, d / sf)
        else:
            from . import writers
            writers.get(p.language).install_shim(d)
        rec["files"] = {sf: f"/repro/{sf}", **rec["files"]}
    for extra in (p.cfg["docker"].get("context_files") or []):  # files the project Dockerfile copies
        s = p.root / extra
        if s.is_dir():
            shutil.copytree(s, d / extra)
        elif s.exists():
            (d / extra).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(s, d / extra)

    # toolchain image: the published one, pinned by digest
    base_ref = docker.published_toolchain(p.language)
    published = bool(base_ref and "@sha256:" in base_ref)
    if not published:  # this version is not released: the Dockerfile builds only here
        local = docker.ensure_toolchain(p.language)
        base_ref = base_ref or local
        if base_ref != local:
            docker._run(["docker", "tag", local, base_ref], capture_output=True)
    source = _source(p, image, d)
    t = pin_time(image, base_ref)
    kind = expected.get("kind") or "crash"
    frame = expected.get("frame")
    title = f"{p.name}: {kind}" + (f" in {frame}" if frame else "")
    (d / "Dockerfile").write_text(dockerfile(p, title, base_ref, source, t, rec, fname))
    tag = re.sub(r"[^a-z0-9_.-]", "-", f"repro-{p.name}-{rid}".lower())
    plat = p.cfg["docker"].get("platform")
    (d / "run.sh").write_text(RUN_SH.format(title=title, tag=tag, platform=f" --platform {plat}" if plat else ""))
    (d / "run.sh").chmod(0o755)

    res = {"ok": True, "dir": str(d), "file": fname, "plain": not shim, "published_toolchain": published,
           "pinned_to": t.strftime("%Y-%m-%dT%H:%M:%SZ"), "source": source["what"]}
    output = ""
    if run:
        print(f"[stitch] building and running {d}/Dockerfile from scratch...", file=sys.stderr, flush=True)
        b = subprocess.run(["docker", "build", "-q", "-t", tag, str(d)] + (["--platform", plat] if plat else []),
                           capture_output=True, text=True, errors="replace")
        if b.returncode != 0:
            res.update(ok=False, check={"status": "build_failed"}, output=b.stderr[-6000:])
        else:
            try:
                r = subprocess.run(["docker", "run", "--rm", "--name", tag] + (["--platform", plat] if plat else []) + [tag],
                                   capture_output=True, text=True, errors="replace", timeout=timeout)
                output, code, to = (r.stderr or "") + (r.stdout or ""), r.returncode, False
            except subprocess.TimeoutExpired as e:
                docker.remove(tag)
                output, code, to = str(e.stderr or ""), -1, True
            rep = reports.parse_report(output, code, to)
            tr = {"status": "crash" if rep["crashed"] else "no_crash", "report": rep, "output": output}
            ver = verdict(expected, tr, plain=not shim) if expected else {"status": tr["status"]}
            res["check"] = {k: x for k, x in ver.items() if k != "output"}
            res["got"] = {**crash_key(rep), "signature": rep["signature"]}
            (d / "output.txt").write_text(output)
    (d / "ISSUE.md").write_text(issue(p, title, d, fname, rec, source, output, res, published))
    return res


# =============================================================================================
# ISSUE.md
# =============================================================================================
def _excerpt(output, limit=40):
    from .repro import _sanitizer_excerpt
    exc = _sanitizer_excerpt(output) if output else ""
    if exc:
        return exc
    lines = [l for l in output.splitlines() if l.strip()]
    return "\n".join(lines[-limit:])


def issue(p, title, d: Path, fname, rec, source, output, res, published) -> str:
    code = (d / fname).read_text().rstrip()
    extra = [n for n in rec["files"] if n != fname]
    files = ", ".join(f"`{n}`" for n in ["Dockerfile", fname] + extra + (["source.tar.gz"] if source.get("archive") else []))
    fence = "c" if fname.endswith(".c") else FENCE.get(p.language, "")
    r = [f"### {title}", "",
         f"Reproduces on {source['md']}.", "",
         f"`{fname}`:", "", "```" + fence, code, "```", ""]
    if output:
        r += ["Output:", "", "```", _excerpt(output), "```", ""]
    r += ["<details><summary>Build and run it with Docker</summary>", "",
          f"Put {files} in one directory, then:", "",
          "```sh", "docker build -t repro . && docker run --rm repro", "```", "",
          "The Dockerfile builds the library the same way it was built when the bug was found "
          f"(same {'commit' if source.get('git') else 'source'}, package versions as of {res['pinned_to'][:10]}), "
          f"then runs `{rec['run']}`.", "",
          "```dockerfile", (d / "Dockerfile").read_text().rstrip(), "```"]
    for n in extra:
        r += ["", f"`{n}`:", "", "```" + FENCE.get(p.language, ""), (d / n).read_text().rstrip(), "```"]
    r += ["", "</details>", "", f"*Found with [STITCH++]({REPO}) (API fuzzing).*", ""]
    if not published:
        r.insert(0, f"<!-- note: the toolchain image is not published for stitch-fuzz {_version()}: "
                    "this Dockerfile builds only on the machine that wrote it -->")
    return "\n".join(r)
