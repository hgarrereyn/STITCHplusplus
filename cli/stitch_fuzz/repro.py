"""Self-contained dockerized reproducers.

Layout of a reproducer directory:
  README.md           what crashed and how to run it
  run.sh              builds the toolchain + library images and runs the crash
  stitch/             STITCH++ toolchain for the project's language (Dockerfile, runtime, build
                      scripts) - no fuzzing engine needed
  project/            the project's Dockerfile, stitch.toml and harness
  source.json         where the library source comes from (git URL + commit, or the bundled archive)
  source.tar.gz       (local sources only) snapshot of the library source
  crash.stp           the program that crashes
  trace.txt           every call with its arguments + the sanitizer report, as recorded
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from . import docker, reports
from .project import STITCH_HOME
from .source import Source

RUN_SH = r"""#!/bin/sh
# Reproduce: {title}
# Made with stitch-fuzz {version}.
# Requires docker (with buildx). Usage: ./run.sh            run the recorded harness program
#                                       ./run.sh --standalone  build + run standalone/ (the bug without the fuzzer)
set -e
cd "$(dirname "$0")"
ID={rid}
# on Linux, run as the calling user so files written into this folder stay yours
U=""; [ "$(uname)" = Linux ] && U="--user $(id -u):$(id -g) -e HOME=/tmp"
# the STITCH++ toolchain: the published image this reproducer was made with (pinned), else built
# from stitch/ (also when STITCH_REPRO_BUILD=1, e.g. after editing stitch/)
TOOLCHAIN={toolchain}
if [ -n "$TOOLCHAIN" ] && [ -z "$STITCH_REPRO_BUILD" ] && docker pull -q "$TOOLCHAIN" >/dev/null 2>&1; then
  docker tag "$TOOLCHAIN" stitch-repro-toolchain:$ID
else
  docker buildx build --load -q -f stitch/Dockerfile --target toolchain -t stitch-repro-toolchain:$ID stitch >/dev/null
fi
if [ ! -d source ]; then
{fetch}
fi
docker buildx build --load -q -f project/Dockerfile --build-context source=source \
  --build-arg STITCH_BASE=stitch-repro-toolchain:$ID -t stitch-repro:$ID project >/dev/null
if [ "$1" = "--standalone" ]; then
{standalone}
  exit $?
fi
docker run --rm $U -v "$PWD/project:/project" -v "$PWD:/repro" -w /project stitch-repro:$ID \
  sh -c 'stitch-build --target repro --out /tmp/stitch-build >/dev/null &&
    (timeout -s KILL {limit} /tmp/stitch-build/repro --trace /repro/crash.stp; rc=$?;
     if [ $rc = 137 ]; then echo "[stitch] TIMEOUT: still running after {limit} s" >&2; fi; exit $rc)'
"""

# `./run.sh --standalone`: C/C++ compiles a plain program against the library; other languages
# build the file next to the harness (it may use the harness's module or crate).
STANDALONE_CPP = ('  docker run --rm $U -v "$PWD/standalone:/standalone" -w /standalone stitch-repro:$ID \\\n'
                  '    sh -c "$(cat standalone/build.sh)"')
STANDALONE_LANG = ('  docker run --rm $U -v "$PWD/project:/project" -v "$PWD/standalone:/project/standalone" -w /project \\\n'
                   '    stitch-repro:$ID sh standalone/build.sh')

# report section title and code fence, per language
REPORT_NAME = {"cpp": "Sanitizer report"}
FENCE = {"cpp": "cpp", "python": "python", "javascript": "js", "java": "java", "go": "go", "rust": "rust", "ruby": "ruby",
         "swift": "swift", "objc": "objc"}


def make_repro(p, crash: Path, bucket, outdir: Path = None, run=True) -> Path:
    from . import __version__
    from .cli import build, run_repro, in_project

    info = build(p, harness_only=True, quiet=True)
    image = info["image"]
    minimized = crash.with_name(crash.stem + ".min.stp")
    if minimized.exists():
        crash = minimized
    rid = (bucket["id"] if bucket else crash.stem[:12]).replace(":", "-")
    if bucket and bucket.get("campaign") not in (None, "main"):  # ids are per campaign: probe:c3 is not main:c3
        rid = f"{bucket['campaign']}-{rid}"
    d = (outdir or (p.state_dir / "repros" / f"{p.name}-{rid}")).resolve()
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    # toolchain (no engine)
    from .docker import language_context
    shutil.copytree(language_context(p.language), d / "stitch",
                    ignore=shutil.ignore_patterns("standalone", "node_modules", "__pycache__", "target", "*.pyc"))
    # project
    pd = d / "project"
    pd.mkdir()
    shutil.copy2(p.dockerfile, pd / "Dockerfile")
    shutil.copy2(p.root / "stitch.toml", pd / "stitch.toml")
    shutil.copytree(p.root / "harness", pd / "harness")
    (pd / ".dockerignore").write_text(".stitch\nharness\n")
    (pd / ".gitignore").write_text(".stitch\n")  # build output and caches of run.sh
    (d / ".gitignore").write_text("source/\n")    # the library checkout run.sh makes
    for extra in (p.cfg["docker"].get("context_files") or []):
        src = p.root / extra
        if src.is_dir():
            shutil.copytree(src, pd / extra)
        elif src.exists():
            (pd / extra).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, pd / extra)
    shutil.copy2(crash, d / "crash.stp")
    # source
    src = Source(p)
    commit = info.get("commit")
    if src.git_url and commit and info.get("ident") != "local":
        sj = {"git": src.git_url, "commit": commit}
        fetch = (f"  git clone --quiet {src.git_url} source\n  git -C source checkout --quiet {commit}"
                 + ("\n  git -C source submodule update --init --recursive --quiet" if src.submodules else ""))
    else:
        src.snapshot_tar(d / "source.tar.gz")
        sj = {"archive": "source.tar.gz", "local_path": src.path, "commit_at_snapshot": commit}
        fetch = "  mkdir source && tar xzf source.tar.gz -C source"
    (d / "source.json").write_text(json.dumps(sj, indent=1))
    # recorded trace
    # a timeout finding is stopped after 10 s (it runs for much longer than any normal program)
    limit = 10 if bucket and bucket.get("kind") == "timeout" else 300
    output, code, to = run_repro(p, image, ".stitch/build/repro", in_project(p, crash), timeout=limit)
    if to:
        output += f"\n[stitch] TIMEOUT: still running after {limit} s\n"
    rep = reports.parse_report(output, code, to)
    (d / "trace.txt").write_text(output)
    title = rep["signature"] if rep["crashed"] else "program (did not crash when recorded)"
    sh = d / "run.sh"
    # image tags: unique per project and bucket (reproducers of several projects can verify at once)
    tag_id = re.sub(r"[^a-z0-9_.-]", "-", f"{p.name}-{rid}".lower())
    toolchain = docker.published_toolchain(p.language) or ""
    sh.write_text(RUN_SH.format(title=title, rid=tag_id, fetch=fetch, limit=limit, toolchain=f'"{toolchain}"', version=__version__,
                                standalone=STANDALONE_CPP if p.language == "cpp" else STANDALONE_LANG))
    sh.chmod(0o755)
    attach_standalone(p, d)
    (d / "README.md").write_text(render_readme(p.name, rid, rep, output, sj, d / "project" / "harness", p.language,
                                               toolchain=toolchain, version=__version__))
    if run:
        print(f"[stitch] verifying the reproducer from scratch ({d}/run.sh)...", file=sys.stderr, flush=True)
        r = subprocess.run([str(sh)], capture_output=True, text=True, errors="replace", timeout=3600)
        vrep = reports.parse_report(r.stderr, r.returncode)
        ok = vrep["crashed"] and vrep["signature_fn"] == rep["signature_fn"]
        shutil.rmtree(d / "source", ignore_errors=True)
        shutil.rmtree(d / "project" / ".stitch", ignore_errors=True)
        (d / "verified.txt").write_text(("verified: same crash\n" if ok else "NOT verified\n") + vrep["signature"] + "\n")
        if ok:
            rewrite_readme(p, d)  # adds the "Verified" row
        if not ok:
            print(f"[stitch] warning: reproducer did not reproduce the same crash: {vrep['signature'] or 'no crash'}", file=sys.stderr)
            print(r.stderr[-2000:], file=sys.stderr)
        else:
            print("[stitch] verified: same crash", file=sys.stderr)
    return d


HARNESS_EXT = {"cpp": ("*.cpp", "*.cc", "*.c", "*.hpp", "*.h"), "python": ("*.py",), "javascript": ("*.js", "*.mjs"),
               "java": ("*.java",), "go": ("*.go",), "rust": ("*.rs",), "ruby": ("*.rb",), "swift": ("*.swift",),
               "objc": ("*.m", "*.mm", "*.c", "*.h")}


def _brace_block(lines, i, start):
    """lines[start..] through the line where the brace block opened at or after line i closes."""
    depth, started = 0, False
    for j in range(i, len(lines)):
        depth += lines[j].count("{") - lines[j].count("}")
        started = started or "{" in lines[j]
        if started and depth <= 0:
            return "\n".join(lines[start:j + 1])
    return None


def _comments_above(lines, i, prefixes=("//",)):
    while i > 0 and lines[i - 1].strip().startswith(prefixes):
        i -= 1
    return i


def _stub_source(harness_dir: Path, name: str, language="cpp"):
    """Source of the stub `name` (with its leading comments / annotations), or None."""
    import re
    fn = name.split("::")[-1]
    files = sorted({f for pat in HARNESS_EXT.get(language, ("*",)) for f in harness_dir.rglob(pat)})
    for f in files:
        lines = f.read_text(errors="replace").split("\n")
        rel = str(f.relative_to(harness_dir.parent))
        if language == "python":
            for i, l in enumerate(lines):
                if re.match(r"\s*(async\s+)?def %s\s*\(" % re.escape(fn), l):
                    start = _comments_above(lines, i, ("@", "#"))
                    ind = len(l) - len(l.lstrip())
                    j = i + 1
                    while j < len(lines) and (not lines[j].strip() or len(lines[j]) - len(lines[j].lstrip()) > ind):
                        j += 1
                    return rel, "\n".join(lines[start:j]).rstrip()
        elif language == "javascript":
            for i, l in enumerate(lines):
                if re.search(r"""stitch\.endpoint\(\s*["'`]%s["'`]""" % re.escape(fn), l):
                    start = _comments_above(lines, i)
                    depth = 0
                    for j in range(i, len(lines)):
                        depth += lines[j].count("(") - lines[j].count(")")
                        if depth <= 0:
                            return rel, "\n".join(lines[start:j + 1])
        elif language == "ruby":
            # Stitch.endpoint :name, ... do |...| ... end   (`end` at the call's indentation)
            for i, l in enumerate(lines):
                if re.search(r"\bendpoint\(?\s*[:\"']%s\b" % re.escape(fn), l):
                    start = _comments_above(lines, i, ("#",))
                    ind = len(l) - len(l.lstrip())
                    if l.rstrip().endswith("}") or re.search(r"\bend\s*$", l):
                        return rel, "\n".join(lines[start:i + 1])
                    for j in range(i + 1, len(lines)):
                        s = lines[j].strip()
                        if s in ("end", "}") and len(lines[j]) - len(lines[j].lstrip()) == ind:
                            return rel, "\n".join(lines[start:j + 1])
        elif language == "swift":
            # let NAME = endpoint("name") { ... }   (a brace block)
            for i, l in enumerate(lines):
                if re.search(r"\bendpoint\(\s*\"%s\"" % re.escape(fn), l):
                    code = _brace_block(lines, i, _comments_above(lines, i))
                    if code:
                        return rel, code
        elif language == "objc":
            # STITCH_ENDPOINT(Ret, name, ...) { ... }
            for i, l in enumerate(lines):
                if re.search(r"STITCH_ENDPOINT\s*\(.*?,\s*%s\s*[,)]" % re.escape(fn), l) and not l.lstrip().startswith("#"):
                    code = _brace_block(lines, i, _comments_above(lines, i))
                    if code:
                        return rel, code
        elif language == "go":
            m = None
            for l in lines:
                m = m or re.search(r"""stitch\.Endpoint\(\s*"%s"\s*,\s*([A-Za-z_][A-Za-z0-9_]*)""" % re.escape(fn), l)
            target = m.group(1) if m else fn
            for g in files:
                gl = g.read_text(errors="replace").split("\n")
                for i, l in enumerate(gl):
                    if re.match(r"func %s\s*\(" % re.escape(target), l):
                        code = _brace_block(gl, i, _comments_above(gl, i))
                        if code:
                            return str(g.relative_to(harness_dir.parent)), code
            return None
        else:  # cpp, java, rust: a brace-delimited function
            pat = re.compile(r"\b%s\s*\(" % re.escape(fn))
            for i, l in enumerate(lines):
                if not pat.search(l) or "STITCH_ENDPOINT" in l:
                    continue
                if language == "java" and not re.search(r"\bstatic\b", l):
                    continue
                if language == "rust" and not re.search(r"\bfn\s+%s\b" % re.escape(fn), l):
                    continue
                head = "\n".join(lines[i:i + 3])
                if "{" not in head or (";" in head and head.index(";") < head.index("{")):
                    continue
                prefixes = ("//", "@", "*", "/*") if language == "java" else ("//",)
                code = _brace_block(lines, i, _comments_above(lines, i, prefixes))
                if code:
                    return rel, code
    return None


def _sanitizer_excerpt(output: str, limit=45):
    """The sanitizer report: error line, access, stacks (runtime frames dropped)."""
    lines = output.splitlines()
    start = next((i for i, l in enumerate(lines) if "ERROR: AddressSanitizer" in l or "runtime error:" in l
                  or "CHECK FAILED" in l or "UNCAUGHT EXCEPTION" in l or l.startswith("[stitch] EXCEPTION in ")
                  or l.startswith(("panic: ", "fatal error: "))), None)
    if start is None:
        return ""
    keep = []
    for l in lines[start:]:
        if l.startswith("SUMMARY:") or l.startswith("Shadow bytes"):
            break
        if any(x in l for x in ("stitch::run_program", "stitch_exec", "stitch_run_file", "main_repro.cpp", "libc.so", "_start", "__libc_start")):
            continue
        keep.append(l.replace("/project/", ""))
        if len(keep) >= limit:
            keep.append("...")
            break
    return "\n".join(keep).strip()


def render_readme(project, rid, rep, output, sj, harness_dir: Path, language="cpp", toolchain=None, version=None) -> str:
    calls = [l[len("[stitch] "):] for l in output.splitlines() if l.startswith("[stitch] #")]
    kind = rep["kind"] if rep["crashed"] else "no crash when recorded"
    where = reports.frame_func(rep["frames"][0]) if rep["frames"] else (rep.get("endpoint") or "?")
    src = f"[{sj['git']}]({sj['git']}) @ `{sj['commit'][:12]}`" if "git" in sj else "bundled snapshot (`source.tar.gz`)"
    r = [f"# {project}: {kind} in {where}", "",
         "| | |", "|---|---|",
         f"| {'Sanitizer' if language == 'cpp' else 'Kind'} | `{kind}` |",
         f"| Location | {', '.join(f'`{f}`' for f in rep['frames'][:3]) or '–'} |",
         f"| Library source | {src} |",
         f"| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |",
         f"| Crash id | `{rid}` |",
         f"| Made with | stitch-fuzz {version or '?'}"
         + (f"; toolchain image `{toolchain}`" if toolchain else "") + " |", ""]
    if harness_dir is not None and (harness_dir.parent.parent / "standalone").exists():
        r += [standalone_readme(harness_dir.parent.parent, language)]
    r += [f"## Call sequence ({len(calls)} calls)", "", "Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.", "", "```"]
    r += [c for c in calls[:60]] + ["```", ""]
    stubs, seen = [], set()
    for c in calls:
        name = c.split(" ", 1)[1].split("(")[0].split(":")[0] if " " in c else ""
        if name and name not in seen:
            seen.add(name)
            found = _stub_source(harness_dir, name, language)
            if found:
                stubs.append((name, *found))
    if stubs:
        r += ["## Stubs used", "", "What each call does (from `project/harness/`):", ""]
        for name, file, code in stubs:
            r += [f"`{name}` · {file}", "", "```" + FENCE.get(language, ""), code, "```", ""]
    exc = _sanitizer_excerpt(output)
    if exc:
        r += [f"## {REPORT_NAME.get(language, 'Report')}", "", "```", exc, "```", ""]
    r += ["## Files", "",
          "- `run.sh`: pulls the pinned toolchain image (or builds it from `stitch/`), fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`",
          "- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs",
          f"- `stitch/`: the STITCH++ {language} toolchain (Dockerfile, runtime, build scripts); no fuzzer needed",
          "- `crash.stp`: the crashing program; `trace.txt`: the full recorded output",
          "- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written", ""]
    return "\n".join(r) + "\n"


def rewrite_readme(p, d: Path):
    """Regenerate README.md of an existing reproducer from its recorded trace."""
    output = (d / "trace.txt").read_text(errors="replace")
    rep = reports.parse_report(output, 1)
    sj = json.loads((d / "source.json").read_text())
    rid = d.name
    sh = (d / "run.sh").read_text() if (d / "run.sh").exists() else ""
    toolchain = re.search(r'^TOOLCHAIN="([^"]*)"', sh, re.M)
    version = re.search(r"^# Made with stitch-fuzz (\S+)\.$", sh, re.M)
    text = render_readme(p.name, rid, rep, output, sj, d / "project" / "harness", p.language,
                         toolchain=toolchain and toolchain.group(1), version=version and version.group(1))
    if (d / "verified.txt").exists() and (d / "verified.txt").read_text().startswith("verified"):
        text = text.replace("| Crash id |", "| Verified | rebuilt from scratch with `run.sh`: same crash |\n| Crash id |", 1)
    (d / "README.md").write_text(text)


def _standalone_files(language, sdir: Path = None):
    """(generated file name, candidate names of the minimized file). A writer whose file name
    depends on the harness lists its names in FILENAMES: the one in `sdir` is the generated file."""
    if language == "cpp":
        return "repro.cpp", ("min.c", "min.cpp", "repro.cpp")
    from . import writers
    w = writers.get(language)
    gens = getattr(w, "FILENAMES", (w.FILENAME,))
    gen = next((g for g in gens if sdir and (sdir / g).exists()), w.FILENAME)
    return gen, tuple("min" + Path(g).suffix for g in gens) + tuple(gens)


def attach_standalone(p, d: Path, sdir: Path = None):
    """Copy a standalone reproducer (`stitch write`, ideally minimized to min.<ext>) into the
    reproducer directory `d` and describe it in the README. Returns the file used, or None."""
    cands = [sdir] if sdir else [p.state_dir / "standalone" / d.name, p.state_dir / "standalone" / d.name.replace(p.name + "-", "")]
    sdir = next((c for c in cands if c and (c / _standalone_files(p.language, c)[0]).exists()), None)
    if not sdir:
        return None
    gen, names = _standalone_files(p.language, sdir)
    main = next((sdir / n for n in names if (sdir / n).exists()), None)
    out = d / "standalone"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir()
    shutil.copy2(main, out / main.name)
    shutil.copy2(sdir / "expected.json", out / "expected.json")
    if main.name != gen:
        shutil.copy2(sdir / gen, out / ("generated" + Path(gen).suffix))
    if (sdir / "try.txt").exists():
        shutil.copy2(sdir / "try.txt", out / "try.txt")
    if p.language == "cpp":
        from .standalone import compile_cmd, asan_options
        if "stitch_standalone.h" in main.read_text():
            shutil.copy2(sdir / "stitch_standalone.h", out / "stitch_standalone.h")
        cmd = compile_cmd(p, main.name, "/tmp/repro").replace("-I. -I.stitch/include", "-I.")
        (out / "build.sh").write_text(f"set -e\n{cmd}\nASAN_OPTIONS={asan_options()} /tmp/repro\n")
    else:
        from . import writers
        w = writers.get(p.language)
        w.install_shim(out)
        # run in the project image with the project at /project (the harness is built first)
        (out / "build.sh").write_text("set -e\nstitch-build >/dev/null\n"
                                      + w.run_script(p, f"standalone/{main.name}", 60) + "\n")
    return out / main.name


def standalone_readme(d: Path, language="cpp") -> str:
    out = d / "standalone"
    if not out.exists():
        return ""
    gen, names = _standalone_files(language, out)
    main = next((out / n for n in names if (out / n).exists()), None)
    if not main:
        return ""
    if Path(gen).suffix != main.suffix:  # minimized: the generated file has the same extension
        gen = next((n for n in names if not n.startswith("min") and Path(n).suffix == main.suffix), gen)
    code = main.read_text()
    from .standalone import code_lines
    n = code_lines(code)
    if language == "cpp":
        fence = "c" if main.suffix == ".c" else "cpp"
        plain = "stitch_standalone.h" not in code
        how = ("Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with "
               "AddressSanitizer, runs it). Build line: `" + (out / "build.sh").read_text().splitlines()[1] + "`")
    else:
        from . import writers
        fence = FENCE.get(language, "")
        plain = writers.get(language).SHIM not in code
        how = ("Run it: `./run.sh --standalone` (builds the library at the pinned commit and runs this file in "
               "the project image).")
    if main.name == gen:
        what = f"`standalone/{main.name}` is the generated program (harness code pasted in, values filled in)."
    elif plain:
        what = f"`standalone/{main.name}` ({n} lines of code) needs only the library: no harness, no STITCH++ runtime."
    else:
        what = (f"`standalone/{main.name}` ({n} lines of code) calls the library directly; it uses the STITCH++ "
                "stand-in only to report the failed check the same way.")
    r = ["## Standalone reproducer", "", what, "", "```" + fence, code.rstrip(), "```", "", how, ""]
    g = out / ("generated" + Path(gen).suffix)
    if g.exists():
        r += [f"`standalone/{g.name}` ({code_lines(g.read_text())} lines of code) is what `stitch write` produced before "
              "minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).", ""]
    return "\n".join(r)
