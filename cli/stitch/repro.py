"""Self-contained dockerized reproducers.

Layout of a reproducer directory:
  README.md           what crashed and how to run it
  run.sh              builds the toolchain + library images and runs the crash
  stitch/             STITCH toolchain (Dockerfile, runtime, wrappers) - no fuzzing engine needed
  project/            the project's Dockerfile, stitch.toml and harness
  source.json         where the library source comes from (git URL + commit, or the bundled archive)
  source.tar.gz       (local sources only) snapshot of the library source
  crash.stp           the program that crashes
  trace.txt           every call with its arguments + the sanitizer report, as recorded
"""
import json
import shutil
import subprocess
from pathlib import Path

from . import docker, reports
from .project import STITCH_HOME
from .source import Source

RUN_SH = r"""#!/bin/sh
# Reproduce: {title}
# Requires docker (with buildx). Usage: ./run.sh            run the recorded harness program
#                                       ./run.sh --standalone  build + run standalone/ (plain C/C++ against the library)
set -e
cd "$(dirname "$0")"
ID={rid}
docker buildx build --load -q -f stitch/docker/base.Dockerfile --target toolchain -t stitch2-repro-toolchain:$ID stitch >/dev/null
if [ ! -d source ]; then
{fetch}
fi
docker buildx build --load -q -f project/Dockerfile --build-context source=source \
  --build-arg STITCH_BASE=stitch2-repro-toolchain:$ID -t stitch2-repro:$ID project >/dev/null
if [ "$1" = "--standalone" ]; then
  docker run --rm -v "$PWD/standalone:/standalone" -w /standalone stitch2-repro:$ID \
    sh -c "$(cat standalone/build.sh)"
  exit $?
fi
docker run --rm -v "$PWD/project:/project" -v "$PWD:/repro" -w /project stitch2-repro:$ID \
  sh -c 'stitch-build --target repro --out /tmp/stitch-build >/dev/null && /tmp/stitch-build/repro --trace /repro/crash.stp'
"""


def make_repro(p, crash: Path, bucket, outdir: Path = None, run=True) -> Path:
    from .cli import build, run_repro, in_project

    info = build(p, harness_only=True, quiet=True)
    image = info["image"]
    minimized = crash.with_name(crash.stem + ".min.stp")
    if minimized.exists():
        crash = minimized
    rid = (bucket["id"] if bucket else crash.stem[:12]).replace(":", "-")
    d = (outdir or (p.state_dir / "repros" / f"{p.name}-{rid}")).resolve()
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    # toolchain (no engine)
    (d / "stitch" / "docker").mkdir(parents=True)
    shutil.copy2(STITCH_HOME / "docker" / "base.Dockerfile", d / "stitch" / "docker" / "base.Dockerfile")
    shutil.copytree(STITCH_HOME / "docker" / "bin", d / "stitch" / "docker" / "bin")
    shutil.copytree(STITCH_HOME / "runtime", d / "stitch" / "runtime")
    # project
    pd = d / "project"
    pd.mkdir()
    shutil.copy2(p.dockerfile, pd / "Dockerfile")
    shutil.copy2(p.root / "stitch.toml", pd / "stitch.toml")
    shutil.copytree(p.root / "harness", pd / "harness")
    (pd / ".dockerignore").write_text(".stitch\nharness\n")
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
    output, code, to = run_repro(p, image, ".stitch/build/repro", in_project(p, crash))
    rep = reports.parse_report(output, code, to)
    (d / "trace.txt").write_text(output)
    title = rep["signature"] if rep["crashed"] else "program (did not crash when recorded)"
    sh = d / "run.sh"
    sh.write_text(RUN_SH.format(title=title, rid=rid.lower(), fetch=fetch))
    sh.chmod(0o755)
    attach_standalone(p, d)
    (d / "README.md").write_text(render_readme(p.name, rid, rep, output, sj, d / "project" / "harness"))
    if run:
        print(f"[stitch] verifying the reproducer from scratch ({d}/run.sh)...", flush=True)
        r = subprocess.run([str(sh)], capture_output=True, text=True, timeout=3600)
        vrep = reports.parse_report(r.stderr, r.returncode)
        ok = vrep["crashed"] and vrep["signature_fn"] == rep["signature_fn"]
        shutil.rmtree(d / "source", ignore_errors=True)
        (d / "verified.txt").write_text(("verified: same crash\n" if ok else "NOT verified\n") + vrep["signature"] + "\n")
        if ok:
            rewrite_readme(p, d)  # adds the "Verified" row
        if not ok:
            print(f"[stitch] warning: reproducer did not reproduce the same crash: {vrep['signature'] or 'no crash'}")
            print(r.stderr[-2000:])
        else:
            print("[stitch] verified: same crash")
    return d


def _stub_source(harness_dir: Path, name: str):
    """Source of the endpoint function `name` (with its leading comments), or None."""
    import re
    fn = name.split("::")[-1]
    pat = re.compile(r"\b%s\s*\(" % re.escape(fn))
    for f in sorted(harness_dir.rglob("*.cpp")):
        lines = f.read_text(errors="replace").split("\n")
        for i, l in enumerate(lines):
            if not pat.search(l) or "STITCH_ENDPOINT" in l:
                continue
            head = "\n".join(lines[i:i + 3])
            if "{" not in head or (";" in head and head.index(";") < head.index("{")):
                continue
            start = i
            while start > 0 and lines[start - 1].strip().startswith("//"):
                start -= 1
            depth, started = 0, False
            for j in range(i, len(lines)):
                depth += lines[j].count("{") - lines[j].count("}")
                started = started or "{" in lines[j]
                if started and depth <= 0:
                    return str(f.relative_to(harness_dir.parent)), "\n".join(lines[start:j + 1])
    return None


def _sanitizer_excerpt(output: str, limit=45):
    """The sanitizer report: error line, access, stacks (runtime frames dropped)."""
    lines = output.splitlines()
    start = next((i for i, l in enumerate(lines) if "ERROR: AddressSanitizer" in l or "runtime error:" in l
                  or "CHECK FAILED" in l or "UNCAUGHT EXCEPTION" in l), None)
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


def render_readme(project, rid, rep, output, sj, harness_dir: Path) -> str:
    calls = [l[len("[stitch] "):] for l in output.splitlines() if l.startswith("[stitch] #")]
    kind = rep["kind"] if rep["crashed"] else "no crash when recorded"
    where = rep["frames"][0].split(" ")[0] if rep["frames"] else (rep.get("endpoint") or "?")
    src = f"[{sj['git']}]({sj['git']}) @ `{sj['commit'][:12]}`" if "git" in sj else "bundled snapshot (`source.tar.gz`)"
    r = [f"# {project}: {kind} in {where}", "",
         "| | |", "|---|---|",
         f"| Sanitizer | `{kind}` |",
         f"| Location | {', '.join(f'`{f}`' for f in rep['frames'][:3]) or '–'} |",
         f"| Library source | {src} |",
         f"| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |",
         f"| Crash id | `{rid}` |", ""]
    if harness_dir is not None and (harness_dir.parent.parent / "standalone").exists():
        r += [standalone_readme(harness_dir.parent.parent)]
    r += [f"## Call sequence ({len(calls)} calls)", "", "Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.", "", "```"]
    r += [c for c in calls[:60]] + ["```", ""]
    stubs, seen = [], set()
    for c in calls:
        name = c.split(" ", 1)[1].split("(")[0].split(":")[0] if " " in c else ""
        if name and name not in seen:
            seen.add(name)
            found = _stub_source(harness_dir, name)
            if found:
                stubs.append((name, *found))
    if stubs:
        r += ["## Stubs used", "", "What each call does (from `project/harness/`):", ""]
        for name, file, code in stubs:
            r += [f"`{name}` · {file}", "", "```cpp", code, "```", ""]
    exc = _sanitizer_excerpt(output)
    if exc:
        r += ["## Sanitizer report", "", "```", exc, "```", ""]
    r += ["## Files", "",
          "- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`",
          "- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs",
          "- `stitch/`: the STITCH toolchain (base image Dockerfile, runtime, compiler wrappers); no fuzzer needed",
          "- `crash.stp`: the crashing program; `trace.txt`: the full recorded output",
          "- `standalone/`: the same bug as a plain C/C++ program (`./run.sh --standalone`), if one was written", ""]
    return "\n".join(r) + "\n"


def rewrite_readme(p, d: Path):
    """Regenerate README.md of an existing reproducer from its recorded trace."""
    output = (d / "trace.txt").read_text(errors="replace")
    rep = reports.parse_report(output, 1)
    sj = json.loads((d / "source.json").read_text())
    rid = d.name
    text = render_readme(p.name, rid, rep, output, sj, d / "project" / "harness")
    if (d / "verified.txt").exists() and (d / "verified.txt").read_text().startswith("verified"):
        text = text.replace("| Crash id |", "| Verified | rebuilt from scratch with `run.sh`: same crash |\n| Crash id |", 1)
    (d / "README.md").write_text(text)


def attach_standalone(p, d: Path, sdir: Path = None):
    """Copy a standalone reproducer (`stitch write`, ideally minimized to min.c/min.cpp) into the
    reproducer directory `d` and describe it in the README. Returns the file used, or None."""
    from .standalone import compile_cmd
    cands = [sdir] if sdir else [p.state_dir / "standalone" / d.name, p.state_dir / "standalone" / d.name.replace(p.name + "-", "")]
    sdir = next((c for c in cands if c and (c / "repro.cpp").exists()), None)
    if not sdir:
        return None
    main = next((sdir / n for n in ("min.c", "min.cpp", "repro.cpp") if (sdir / n).exists()), None)
    out = d / "standalone"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir()
    for f in [main, sdir / "expected.json"] + ([sdir / "stitch_standalone.h"] if "stitch_standalone.h" in main.read_text() else []):
        shutil.copy2(f, out / f.name)
    if main.name != "repro.cpp":
        shutil.copy2(sdir / "repro.cpp", out / "generated.cpp")
    cmd = compile_cmd(p, main.name, "/tmp/repro").replace("-I. -I.stitch/include", "-I.")
    from .standalone import asan_options
    (out / "build.sh").write_text(f"set -e\n{cmd}\nASAN_OPTIONS={asan_options()} /tmp/repro\n")
    if (sdir / "try.txt").exists():
        shutil.copy2(sdir / "try.txt", out / "try.txt")
    return out / main.name


def standalone_readme(d: Path) -> str:
    out = d / "standalone"
    if not out.exists():
        return ""
    main = next((out / n for n in ("min.c", "min.cpp", "repro.cpp") if (out / n).exists()), None)
    if not main:
        return ""
    code = main.read_text()
    lang = "c" if main.suffix == ".c" else "cpp"
    from .standalone import code_lines
    n = code_lines(code)
    r = ["## Standalone reproducer", "",
         f"`standalone/{main.name}` ({n} lines of code) needs only the library: no harness, no STITCH runtime."
         if "stitch_standalone.h" not in code else
         f"`standalone/{main.name}` is the generated program (harness code pasted in, values filled in).", "",
         "```" + lang, code.rstrip(), "```", "",
         "Run it: `./run.sh --standalone` (builds the library at the pinned commit, compiles this file with "
         "AddressSanitizer, runs it). Build line: `" + (out / "build.sh").read_text().splitlines()[1] + "`", ""]
    if (out / "generated.cpp").exists():
        g = (out / "generated.cpp").read_text()
        r += [f"`standalone/generated.cpp` ({code_lines(g)} lines of code) is what `stitch write` produced before minimization; "
              "`stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).", ""]
    return "\n".join(r)
