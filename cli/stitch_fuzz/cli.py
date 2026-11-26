"""stitch: build, fuzz, triage and reproduce STITCH++ harnesses (everything runs in Docker)."""
import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

from . import docker, lints, reports
from .project import STITCH_HOME, Project, ProjectError, find_project
from .source import Source, SourceError

JSON = False


def out(obj, text=None):
    """Print `obj` as JSON in --json mode, else the human text."""
    if JSON:
        reports.dump(obj)
    elif text is not None:
        print(text)


def die(msg, code=1):
    if JSON:
        reports.dump({"ok": False, "error": msg})
    else:
        print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


# =============================================================================================
# build
# =============================================================================================
_BUILD_DEPTH = 0


def build(project, **kw):
    """Build base + project image + harness. Returns dict(image, ident, commit, result). One stitch
    process at a time builds a project: two (`exec` while `crashes` runs) would write
    .stitch/build at the same time and one of them would see a half-written harness."""
    global _BUILD_DEPTH
    if _BUILD_DEPTH:
        return _build(project, **kw)
    import fcntl
    project.state_dir.mkdir(parents=True, exist_ok=True)
    with open(project.state_dir / "build.lock", "w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            if not kw.get("quiet"):
                print("[stitch] waiting for another stitch command to finish building this project", file=sys.stderr, flush=True)
            fcntl.flock(lock, fcntl.LOCK_EX)
        _BUILD_DEPTH += 1
        try:
            return _build(project, **kw)
        finally:
            _BUILD_DEPTH -= 1


def _build(project, rev=None, image_only=False, harness_only=False, no_cache=False, out_dir=None, target="all",
           quiet=False, rebuild_base=False):
    docker.check_docker()
    st = project.load_state()
    base_tag = docker.base_tag(project.language, project.sanitizers)
    if (harness_only and st.get("image") and docker.image_exists(st["image"]) and rev is None
            and st.get("base") == base_tag and not rebuild_base):
        image, ident, commit = st["image"], st.get("ident"), st.get("commit")
    else:
        base = docker.ensure_base(project.language, project.sanitizers, rebuild=rebuild_base)
        src = Source(project)
        ctx, ident, commit = src.materialize(rev)
        image = docker.build_project_image(project, base, ctx, ident, no_cache=no_cache, quiet=quiet)
        if rev is None:
            project.save_state(image=image, ident=ident, commit=commit, base=base, built_at=time.time())
    info = {"image": image, "ident": ident, "commit": commit}
    if image_only:
        return info
    cmd = ["stitch-build", "--json", "--target", target]
    if out_dir:
        cmd += ["--out", out_dir]
    r = docker.run(project, image, cmd, capture=True)
    echoed = ""
    if r.stderr and not quiet:
        sys.stderr.write(r.stderr)
        echoed = r.stderr
    try:
        res = json.loads(r.stdout.strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        res = {"ok": False, "errors": [{"file": "stitch-build", "output": (r.stdout + r.stderr)[-3000:]}]}
    if not res.get("ok", True) and not any((e.get("output") or "").strip() for e in res.get("errors", [])):
        # a failure without a message (the build script died, or reported nothing): show its output
        res.setdefault("errors", []).append({"file": "stitch-build", "output": ((r.stdout or "") + (r.stderr or ""))[-3000:]
                                             or f"stitch-build exited with {r.returncode} and printed nothing"})
    info["result"] = res
    info["echoed"] = echoed  # build output already shown (callers need not print it again)
    return info


def cmd_build(a):
    p = find_project(a.project)
    info = build(p, rev=a.rev, image_only=a.image_only, harness_only=a.harness_only, no_cache=a.no_cache,
                 rebuild_base=a.rebuild_base)
    res = info.get("result", {"ok": True})
    if JSON:
        reports.dump({**info, "ok": res.get("ok", True)})
    elif res.get("ok"):
        what = f"{info['image']}" + (f" (commit {info['commit'][:12]})" if info.get("commit") else "")
        if a.image_only:
            print(f"image ready: {what}")
        else:
            print(f"ok: {res.get('endpoints')} endpoints, {res.get('objects')} object types, {res.get('datatypes')} datatypes  [{what}]")
    else:
        for e in res.get("errors", []):
            if (e.get("output") or "").strip() and e["output"].strip() in info.get("echoed", ""):
                continue  # the compiler output above
            sys.stderr.write(f"--- {e.get('file', 'build')} ---\n{e.get('output', '').rstrip()}\n")
        sys.stderr.write("build failed\n")
    sys.exit(0 if res.get("ok", True) else 1)


# =============================================================================================
# check
# =============================================================================================
def load_manifest(p):
    f = p.build_dir / "manifest.json"
    if not f.exists():
        die("no manifest: run `stitch build` first")
    return json.loads(f.read_text())


def cmd_check(a):
    p = find_project(a.project)
    info = build(p, harness_only=not a.full, quiet=JSON)
    res = info["result"]
    if not res.get("ok"):
        if JSON:
            reports.dump({"ok": False, "stage": "build", "errors": res.get("errors")})
        else:
            print("build failed (errors above)")
        sys.exit(1)
    m = load_manifest(p)
    issues = lints.manifest_lints(m) + lints.attr_lints(p) + lints.datatype_lints(p, m)
    folders = {}
    for e in m["endpoints"]:
        f = p.rel(e["file"].replace("/project/", str(p.root) + "/")) if e["file"].startswith("/project/") else e["file"]
        folders.setdefault(str(Path(f).parent), []).append(e["name"])
    errors = [i for i in issues if i["level"] == "error"]
    if JSON:
        reports.dump({"ok": not errors, "endpoints": len(m["endpoints"]), "objects": [o["name"] for o in m["objects"]],
                      "datatypes": m["datatypes"], "folders": {k: len(v) for k, v in folders.items()}, "issues": issues,
                      "manifest": str(p.build_dir / "manifest.json")})
    else:
        print(f"{len(m['endpoints'])} endpoints, {len(m['objects'])} object types, {len(m['datatypes'])} datatypes")
        for k, v in sorted(folders.items()):
            print(f"  {k}/: {len(v)}")
        for lvl in ("error", "warning", "info"):
            for i in issues:
                if i["level"] == lvl:
                    where = f"  [{', '.join(i['where'])}]" if i.get("where") else ""
                    print(f"{lvl}: {i['message']}{where}")
        if not issues:
            print("no issues")
    sys.exit(1 if errors else 0)


# =============================================================================================
# campaigns
# =============================================================================================
def programs(d: Path, pattern=""):
    """Program files (.stp) in d (or d/<pattern>), without the fuzzer's .<name>.stp metadata files."""
    return [f for f in d.glob(f"{pattern}/*.stp" if pattern else "*.stp") if not f.name.startswith(".")]


def container_name(p, name):
    return f"stitch-{p.name}-{name}"


def start_campaign(p, name, secs=None, jobs=1, only=None, fresh=False, detach=True, extra=()):
    info = build(p, harness_only=True, quiet=True)
    if not info["result"].get("ok"):
        for e in info["result"].get("errors", []):
            sys.stderr.write(e["output"])
        die("harness build failed")
    cdir = p.campaign_dir(name)
    cname = container_name(p, name)
    stc = docker.container_state(cname)
    if stc and stc.get("Running"):
        die(f"campaign '{name}' is already running (stitch stop {name})")
    if stc:
        docker.remove(cname)
    if fresh and cdir.exists():
        shutil.rmtree(cdir)
    elif cdir.exists() and name != "probe":
        kept = len({f.name for f in programs(cdir, "w*/corpus")})  # workers share programs: count each once
        crashes = len({f.name for f in programs(cdir, "w*/crashes")})
        if kept or crashes:
            print(f"[stitch] resuming campaign '{name}': {kept} saved corpus programs (rerun first; the corpus keeps the"
                  f" ones that still add coverage) and {crashes} crash programs. Time and execs start from 0"
                  " (`--fresh` starts over)", file=sys.stderr, flush=True)
    old = cdir / "bin"
    if old.is_dir() and not (all(_same_file(p.build_dir / t, old / t) for t in ("fuzzer", "repro", "manifest.json"))
                             and _tree_print(p.build_dir / "app") == _tree_print(old / "app")):
        # resuming with a changed harness: saved crashes are rerun with it at the next triage, so
        # buckets the current stubs no longer produce go away
        for c in cdir.glob("w*/triage-cache.json"):
            c.unlink()
        if not fresh and any(programs(cdir, "w*/crashes")):
            print("[stitch] the harness changed: saved crashes are rerun with it by `stitch crashes`",
                  file=sys.stderr, flush=True)
    (cdir / "bin").mkdir(parents=True, exist_ok=True)
    for t in ("fuzzer", "repro", "manifest.json"):
        shutil.copy2(p.build_dir / t, cdir / "bin" / t)
    # interpreted / JVM harnesses: the build snapshot the scripts run from
    if (p.build_dir / "app").is_dir():
        shutil.rmtree(cdir / "bin" / "app", ignore_errors=True)
        shutil.copytree(p.build_dir / "app", cdir / "bin" / "app", symlinks=True)
    fz = p.cfg["fuzz"]
    rel = f".stitch/campaigns/{name}"
    args = ["--data", "harness/data", "--quiet", "--timeout-ms", str(fz["timeout_ms"]), "--max-nodes", str(fz["max_nodes"]),
            "--persistent", str(p.persistent)]
    if secs:
        args += ["--secs", str(secs)]
    if only:
        args += ["--only", only]
    args += list(extra)
    seed = random.randint(1, 1 << 40)
    lines = ["#!/bin/sh", "cd /project", f"export STITCH_EXCEPTIONS={fz.get('exceptions', 'finding')}"]
    for i in range(jobs):
        sync = " ".join(f"--sync {rel}/w{j}/corpus" for j in range(jobs) if j != i)
        lines.append(f"{rel}/bin/fuzzer fuzz --out {rel}/w{i} --seed {seed + i} {' '.join(args)} {sync} > {rel}/w{i}.log 2>&1 &")
    lines += ["wait", ""]
    (cdir / "run.sh").write_text("\n".join(lines))
    meta = {"name": name, "image": info["image"], "commit": info.get("commit"), "ident": info.get("ident"), "jobs": jobs,
            "secs": secs, "only": only, "timeout_ms": fz["timeout_ms"], "started": time.time(), "container": cname}
    (cdir / "campaign.json").write_text(json.dumps(meta, indent=1))
    env = {"STITCH_EXCEPTIONS": fz.get("exceptions", "finding")}
    if detach:
        docker.run(p, info["image"], ["sh", f"{rel}/run.sh"], name=cname, detach=True, capture=True, check=True, env=env)
    else:
        docker.run(p, info["image"], ["sh", f"{rel}/run.sh"], name=cname, env=env)
    return meta


def worker_dirs(cdir: Path):
    """Worker directories of the current run (older runs may have left more)."""
    ws = sorted((d for d in cdir.glob("w*") if d.is_dir() and d.name[1:].isdigit()), key=lambda d: int(d.name[1:]))
    meta = cdir / "campaign.json"
    if meta.exists():
        jobs = json.loads(meta.read_text()).get("jobs") or len(ws)
        ws = [d for d in ws if int(d.name[1:]) < jobs]
    return ws


def merged_stats(cdir: Path):
    ws = worker_dirs(cdir)
    stats = [json.loads((d / "stats.json").read_text()) for d in ws if (d / "stats.json").exists()]
    if not stats:
        return None
    tot = {"workers": len(stats), "elapsed_secs": max(s["elapsed_secs"] for s in stats),
           "execs": sum(s["execs"] for s in stats), "execs_per_sec": sum(s["execs_per_sec"] for s in stats),
           "edges_covered": max(s["edges_covered"] for s in stats), "edges_total": stats[0]["edges_total"],
           "corpus": sum(s["corpus"] for s in stats), "crashes_saved": sum(s["crashes_and_timeouts_saved"] for s in stats),
           "crashing_execs": sum(s["crashing_execs"] for s in stats)}
    tot["exec_bail_rate"] = round(sum(s["exec_bail_rate"] * s["execs"] for s in stats) / max(tot["execs"], 1), 3)
    calls = sum(e["invoked"] for s in stats for e in s["endpoints"])
    tot["call_bail_rate"] = round(sum(e["bail"] for s in stats for e in s["endpoints"]) / max(calls, 1), 3)
    eps = {}
    for s in stats:
        for e in s["endpoints"]:
            t = eps.setdefault(e["name"], {**e, "invoked": 0, "ok": 0, "bail": 0, "crashes": 0, "timeouts": 0,
                                           "skipped_missing_input": 0, "_reasons": {}, "_miss": {}, "_prev": {}})
            for k in ("invoked", "ok", "bail", "crashes", "timeouts", "skipped_missing_input"):
                t[k] += e[k]
            for r in e["bail_reasons"]:
                t["_reasons"][r["reason"]] = t["_reasons"].get(r["reason"], 0) + r["count"]
            for r in e["missing_attr_reads"]:
                t["_miss"][r["key"]] = t["_miss"].get(r["key"], 0) + r["count"]
            for r in e["by_previous_call"]:
                k = (r["input"], r["after"])
                n, ok = t["_prev"].get(k, (0, 0))
                t["_prev"][k] = (n + r["calls"], ok + r["calls"] * r["ok_rate"])
    for t in eps.values():
        t["bail_rate"] = round(t["bail"] / t["invoked"], 3) if t["invoked"] else 0
        t["bail_reasons"] = [{"reason": k, "count": v} for k, v in sorted(t.pop("_reasons").items(), key=lambda x: -x[1])][:5]
        t["missing_attr_reads"] = [{"key": k, "count": v} for k, v in sorted(t.pop("_miss").items(), key=lambda x: -x[1])][:5]
        t["by_previous_call"] = [{"input": k[0], "after": k[1], "calls": n, "ok_rate": round(ok / n, 3)} for k, (n, ok) in sorted(t.pop("_prev").items(), key=lambda x: -x[1][0])][:12]
    tot["endpoints"] = list(eps.values())
    ops = {}
    for st in stats:
        for m in st.get("mutations", []):
            a, w = ops.get(m["op"], (0, 0))
            ops[m["op"]] = (a + m["applied"], w + m["new_coverage"])
    tot["mutations"] = [{"op": k, "applied": a, "new_coverage": w} for k, (a, w) in ops.items()]
    tot["learned_tokens"] = sorted({t for st in stats for t in st.get("learned_tokens", [])})[:100]
    return tot


def health_report(s, limit=15):
    """Agent-oriented summary of what blocks the fuzzer."""
    eps = s["endpoints"]
    lines = []
    never = [e for e in eps if e["invoked"] >= 20 and e["ok"] == 0]  # engine down-weights after 200
    high = [e for e in eps if e["invoked"] >= 20 and e["ok"] > 0 and e["bail_rate"] >= 0.8]
    unreached = [e for e in eps if e["invoked"] == 0]
    skipped = [e for e in eps if e["skipped_missing_input"] > max(20, e["invoked"])]

    def reasons(e):
        rs = ", ".join(f'"{r["reason"]}" x{r["count"]}' for r in e["bail_reasons"][:3])
        ms = ", ".join(f"'{m['key']}'" for m in e["missing_attr_reads"][:3])
        bad = e.get("crashes", 0) + e.get("timeouts", 0)
        # a stub that crashes on every call is usually a harness bug (a typo, a wrong argument)
        cr = (f"crashed {e['crashes']}x" if e.get("crashes") else "") + \
             (f"{', ' if e.get('crashes') else ''}timed out {e['timeouts']}x" if e.get("timeouts") else "")
        out = rs + (f"; read missing attr {ms}" if ms else "")
        return (out + "; " if out else "") + cr + " (see `stitch crashes`)" if bad else out

    if never:
        lines.append(f"never succeeds ({len(never)}, 20+ calls and none ok so far): the fuzzer picks these less and less")
        for e in sorted(never, key=lambda e: -e["invoked"])[:limit]:
            lines.append(f"  {e['name']}  [{e['invoked']} calls] {reasons(e)}")
    if high:
        lines.append(f"mostly bails (>=80%, {len(high)}):")
        for e in sorted(high, key=lambda e: -e["bail"])[:limit]:
            lines.append(f"  {e['name']}  {reports.pct(e['bail_rate'])} of {e['invoked']}: {reasons(e)}")
    dep = []
    for e in eps:
        prev = [x for x in e["by_previous_call"] if x["calls"] >= 20]
        if len(prev) >= 2:
            lo, hi = min(prev, key=lambda x: x["ok_rate"]), max(prev, key=lambda x: x["ok_rate"])
            if hi["ok_rate"] - lo["ok_rate"] >= 0.5:
                dep.append((e, lo, hi))
    if dep:
        lines.append("depends on where an input came from (the stub that made or last used that object; the fuzzer"
                     " learns these pairs. Act only when the low one should work: a different kind of object behind"
                     " one type gets its own type):")
        for e, lo, hi in dep[:limit]:
            lines.append(f"  {e['name']}({lo['input']}): ok {reports.pct(lo['ok_rate'])} when {lo['input']} last came from"
                         f" {lo['after']}, {reports.pct(hi['ok_rate'])} when from {hi['after']}")
    if skipped:
        lines.append(f"often skipped because an input object was missing ({len(skipped)}): its producer bailed or returned no object")
        for e in skipped[:limit]:
            lines.append(f"  {e['name']}  skipped {e['skipped_missing_input']}x, ran {e['invoked']}x")
    if unreached:
        lines.append(f"never called ({len(unreached)}): " + ", ".join(e["name"] for e in unreached[:40]) + (" ..." if len(unreached) > 40 else ""))
    return lines


def _edges(s):
    # worker languages (all but C/C++, Rust, Swift) number edges as they are first seen: the total
    # is the size of the coverage map, not the number of edges in the library
    t = s.get("edges_total") or 0
    return f"{s['edges_covered']}/{t} edges" if 0 < t < 65536 else f"{s['edges_covered']} edges"


def summary_line(s):
    return (f"{s['elapsed_secs']:.0f}s, {reports.human(s['execs'])} execs ({reports.human(s['execs_per_sec'])}/s), "
            f"{_edges(s)}, corpus {s['corpus']}, calls bailing {reports.pct(s.get('call_bail_rate', 0))} "
            f"(programs with a bail {reports.pct(s['exec_bail_rate'])}), "
            f"{s['crashes_saved']} crash/timeout programs saved")


def cmd_fuzz(a):
    p = find_project(a.project)
    jobs = a.jobs or p.cfg["fuzz"]["jobs"]
    workers = f"{jobs} worker{'s' if jobs > 1 else ''}"
    if a.foreground:
        if not JSON:
            print(f"[stitch] fuzzing '{a.name}' in the foreground ({workers}"
                  + (f", {a.secs}s" if a.secs else ", until Ctrl-C") + ")", file=sys.stderr, flush=True)
        meta = start_campaign(p, a.name, secs=a.secs, jobs=jobs, only=a.only, fresh=a.fresh, detach=False)
        s = merged_stats(p.campaign_dir(a.name))
        if JSON:
            reports.dump({**meta, "stats": s})
        elif s:
            print(f"campaign '{a.name}' finished: " + summary_line(s))
            print(f"  stitch crashes {a.name}    triage crashes\n  stitch status {a.name}     what blocks the fuzzer")
        else:
            print(f"campaign '{a.name}' produced no statistics (see .stitch/campaigns/{a.name}/w0.log)")
        return
    meta = start_campaign(p, a.name, secs=a.secs, jobs=jobs, only=a.only, fresh=a.fresh, detach=True)
    out(meta, f"campaign '{a.name}' started in the background ({workers}, container {meta['container']}).\n"
              f"  stitch status {a.name}     progress + what blocks the fuzzer\n"
              f"  stitch crashes {a.name}    triage crashes\n"
              f"  stitch stop {a.name}")


def cmd_probe(a):
    p = find_project(a.project)
    name = "probe"
    if not JSON:
        print(f"[stitch] probing for {a.secs}s of fuzzing" + (f" with {a.jobs} workers" if a.jobs > 1 else "")
              + " (plus the harness build and worker start-up)...", flush=True)
    start_campaign(p, name, secs=a.secs, jobs=a.jobs, only=a.only, fresh=True, detach=False)
    s = merged_stats(p.campaign_dir(name))
    if not s:
        die("probe produced no statistics (see .stitch/campaigns/probe/w0.log)")
    if JSON:
        reports.dump({k: v for k, v in s.items()} | {"report": health_report(s, 1000)})
        return
    print("probe: " + summary_line(s))
    for l in health_report(s):
        print(l)
    if s["crashes_saved"]:
        print(f"crashes found: run `stitch crashes probe`")


def resolve_campaign(p, name):
    if name:
        if not (p.campaign_dir(name) / "campaign.json").exists():
            die(f"no campaign '{name}' (campaigns: {', '.join(p.campaigns()) or 'none yet: stitch fuzz'})")
        return name
    cs = [c for c in p.campaigns() if c != "probe"] or p.campaigns()
    if not cs:
        die("no campaigns yet (stitch fuzz)")
    return max(cs, key=lambda c: (p.campaign_dir(c) / "campaign.json").stat().st_mtime if (p.campaign_dir(c) / "campaign.json").exists() else 0)


def cmd_status(a):
    p = find_project(a.project)
    name = resolve_campaign(p, a.name)
    cdir = p.campaign_dir(name)
    meta = json.loads((cdir / "campaign.json").read_text())
    stc = docker.container_state(meta["container"])
    running = bool(stc and stc.get("Running"))
    if running and a.wait:
        if not JSON:
            print(f"[stitch] waiting for campaign '{name}' to stop" + (f" (--secs {meta['secs']})" if meta.get("secs") else
                  " (it has no --secs limit: `stitch stop` ends it)"), file=sys.stderr, flush=True)
        while running:
            time.sleep(5)
            stc = docker.container_state(meta["container"])
            running = bool(stc and stc.get("Running"))
    s = merged_stats(cdir)
    if JSON:
        reports.dump({"campaign": name, "running": running, "meta": meta, "stats": s, "report": health_report(s, 1000) if s else []})
        return
    print(f"campaign '{name}': {'running' if running else 'stopped'}" + (f", commit {meta['commit'][:12]}" if meta.get("commit") else ""))
    if not s:
        print("  no statistics yet")
        return
    print("  " + summary_line(s))
    for l in health_report(s, a.limit):
        print(l)
    if a.mutations:
        print("mutation operators (applied -> new corpus entries):")
        for m in sorted(s["mutations"], key=lambda m: -m["new_coverage"]):
            print(f"  {m['op']:22s} {reports.human(m['applied']):>7s} -> {m['new_coverage']}")
        if s["learned_tokens"]:
            print("tokens learned from comparisons: " + ", ".join(repr(t) for t in s["learned_tokens"][:30]))


def cmd_stop(a):
    p = find_project(a.project)
    name = resolve_campaign(p, a.name)
    meta = json.loads((p.campaign_dir(name) / "campaign.json").read_text())
    docker.stop(meta["container"])
    out({"stopped": name}, f"stopped '{name}'")


# =============================================================================================
# crashes
# =============================================================================================
def hang_ms(p, name):
    """The campaign's per-execution timeout: slower programs are triaged as timeouts."""
    try:
        meta = json.loads((p.campaign_dir(name) / "campaign.json").read_text())
    except (OSError, ValueError, TypeError):
        meta = {}
    return int(meta.get("timeout_ms") or p.cfg["fuzz"]["timeout_ms"])


def _same_file(a: Path, b: Path):
    try:
        return a.read_bytes() == b.read_bytes()
    except OSError:
        return False


def _tree_print(d: Path):
    """(path, content hash) of every file under d. Contents, not mtimes: a rebuild rewrites the
    files even when nothing changed."""
    import hashlib
    if not d.is_dir():
        return None
    return sorted((str(f.relative_to(d)), hashlib.sha1(f.read_bytes()).hexdigest()) for f in d.rglob("*") if f.is_file())


def refresh_campaign_bin(p, name, meta):
    """Crashes are rerun with the campaign's copy of the harness. When the harness changed since
    the campaign ran (and it is stopped), use the current build instead, so buckets that the
    current stubs no longer produce go away and ids refer to what `exec` runs now."""
    cdir = p.campaign_dir(name)
    stc = docker.container_state(meta["container"])
    if stc and stc.get("Running"):
        return meta
    try:
        info = build(p, harness_only=True, quiet=True)
    except SystemExit:
        return meta
    if not info["result"].get("ok"):
        print("[stitch] the harness does not build: triaging with the campaign's copy", file=sys.stderr)
        return meta
    cur, old = p.build_dir, cdir / "bin"
    if all(_same_file(cur / t, old / t) for t in ("fuzzer", "repro", "manifest.json")) \
            and _tree_print(cur / "app") == _tree_print(old / "app"):
        return meta
    for t in ("fuzzer", "repro", "manifest.json"):
        shutil.copy2(cur / t, old / t)
    if (cur / "app").is_dir():
        shutil.rmtree(old / "app", ignore_errors=True)
        shutil.copytree(cur / "app", old / "app", symlinks=True)
    print(f"[stitch] the harness changed since campaign '{name}' ran: its crashes are rerun with the current build",
          file=sys.stderr, flush=True)
    meta.update(image=info["image"], ident=info.get("ident"), commit=info.get("commit"))
    for c in cdir.glob("w*/triage-cache.json"):  # results of the old harness (the engine binary may be unchanged)
        c.unlink()
    (cdir / "campaign.json").write_text(json.dumps(meta, indent=1))
    return meta


def triage(p, name):
    cdir = p.campaign_dir(name)
    meta = refresh_campaign_bin(p, name, json.loads((cdir / "campaign.json").read_text()))
    image = meta["image"] if docker.image_exists(meta["image"]) else p.load_state().get("image")
    buckets = {}
    dirs = [w for w in worker_dirs(cdir) if (w / "crashes").exists() and any(programs(w / "crashes"))]
    # all workers' crash directories in one container, at the same time
    jobs = int(os.environ.get("STITCH_TRIAGE_JOBS") or 0) or max(1, min(8, (os.cpu_count() or 4) // max(1, len(dirs))))
    tmp = p.state_dir / "tmp"
    tmp.mkdir(exist_ok=True)
    lines = ["#!/bin/sh"]
    for w in dirs:
        rel = p.rel(w)
        lines.append(f"STITCH_TRIAGE_JOBS={jobs} .stitch/campaigns/{name}/bin/fuzzer triage --crashes {rel}/crashes "
                     f"--hang-ms {hang_ms(p, name)} > .stitch/tmp/triage-{name}-{w.name}.json 2> .stitch/tmp/triage-{name}-{w.name}.log &")
    lines.append("wait")
    if dirs:
        def cached(w):
            try:
                return set(json.loads((w / "triage-cache.json").read_text()))
            except (OSError, ValueError):
                return set()
        n = sum(len({f.name for f in programs(w / "crashes")} - cached(w)) for w in dirs)
        if not JSON and n:
            print(f"[stitch] rerunning {n} new crash program{'s' if n != 1 else ''} of '{name}' to group them (timeouts take"
                  " their full timeout; slower while a campaign runs)", file=sys.stderr, flush=True)
        (tmp / f"triage-{name}.sh").write_text("\n".join(lines) + "\n")
        docker.run(p, image, ["sh", f".stitch/tmp/triage-{name}.sh"], capture=True)
    for w in dirs:
        rel = p.rel(w)
        try:
            bs = json.loads((tmp / f"triage-{name}-{w.name}.json").read_text())
        except (OSError, json.JSONDecodeError):
            log = tmp / f"triage-{name}-{w.name}.log"
            die(f"triage of {rel} failed: {log.read_text(errors='replace')[-2000:] if log.exists() else 'no output'}")
        for b in bs:
            # <x>.min.stp files are `stitch minimize` outputs next to the crash they reduce: they are
            # not crashes of their own (resolve_crash runs them in place of <x>.stp)
            files = b.get("files") or []
            mins = [f for f in files if f.endswith(".min.stp") and f[:-len(".min.stp")] + ".stp" in files]
            if mins and len(mins) < len(files):
                b["count"] -= len(mins)
            if b["example"].endswith(".min.stp") and (w / "crashes" / (b["example"][:-len(".min.stp")] + ".stp")).exists():
                b["example"] = b["example"][:-len(".min.stp")] + ".stp"
            b["example"] = f"{rel}/crashes/{b['example']}"
            t = buckets.get(b["signature"])
            if not t:
                buckets[b["signature"]] = b
            else:
                t["count"] += b["count"]
                if b["example_nodes"] < t["example_nodes"]:
                    t["example"], t["example_nodes"] = b["example"], b["example_nodes"]
                    t["reproduced"] = b.get("reproduced")
    bl = sorted(buckets.values(), key=lambda b: -b["count"])
    # ids stay the same across calls (and while a campaign runs): signature -> id, new ones appended
    ids_f = cdir / "triage-ids.json"
    try:
        ids = json.loads(ids_f.read_text())
    except (OSError, ValueError):
        ids = {}
        try:  # older campaigns: keep the ids they already showed
            ids = {b["signature"]: b["id"] for b in json.loads((cdir / "triage.json").read_text())}
        except (OSError, ValueError, KeyError, TypeError):
            pass
    nxt = max([int(v[1:]) for v in ids.values() if re.fullmatch(r"c\d+", v)] + [0]) + 1
    for b in bl:
        if b["signature"] not in ids:
            ids[b["signature"]] = f"c{nxt}"
            nxt += 1
        b["id"] = ids[b["signature"]]
        b.pop("files", None)
    bl.sort(key=lambda b: int(b["id"][1:]))
    ids_f.write_text(json.dumps(ids, indent=1))
    tj = cdir / "triage.json"
    try:
        before = [b["id"] for b in json.loads(tj.read_text())]
    except (OSError, ValueError, KeyError, TypeError):
        before = []
    gone = [i for i in before if i not in {b["id"] for b in bl}]
    if gone and not JSON:
        print(f"[stitch] {', '.join(gone)} no longer crash{'es' if len(gone) == 1 else ''} with the current harness "
              "(fixed stubs, or a crash that does not reproduce)", file=sys.stderr, flush=True)
    tj.write_text(json.dumps(bl, indent=1))
    return bl


def cmd_crashes(a):
    p = find_project(a.project)
    name = resolve_campaign(p, a.name)
    bl = triage(p, name)
    gone = not_reproduced(p, name)
    note = (f"{gone} saved crash program{'s do' if gone != 1 else ' does'} not crash when rerun (flaky, or fixed"
            " by a harness change): not listed") if gone else ""
    if JSON:
        reports.dump({"campaign": name, "buckets": bl, "not_reproduced": gone})
        return
    if not bl:
        print(f"no reproducible crashes in '{name}'" + (f"; {note}" if note else ""))
        return
    print(f"{len(bl)} unique crash{'es' if len(bl) != 1 else ''} in '{name}':")
    for b in bl:
        rp = b.get("reproduced")
        flaky = f"  [flaky: crashed {rp[0]} of {rp[1]} runs]" if rp and rp[0] < rp[1] else ""
        print(f"  {b['id']:4s} x{b['count']:<5d} {b['kind']}  in {b.get('endpoint') or '?'}{flaky}")
        for f in b["frames"][:3]:
            print(f"         {f}")
        if not b["frames"] and b.get("summary") and "no error report" in b["kind"]:
            print(f"         last output: {b['summary'][:200]}")
        print(f"         example ({b['example_nodes']} calls): {b['example']}")
    if note:
        print(note)
    print("next: stitch exec <id> --trace | stitch minimize <id> | stitch repro <id> | stitch bisect <id>")


def not_reproduced(p, name):
    """Saved crash programs whose last triage run did not crash."""
    n = 0
    for w in worker_dirs(p.campaign_dir(name)):
        try:
            cache = json.loads((w / "triage-cache.json").read_text())
        except (OSError, ValueError):
            continue
        n += sum(1 for k, r in cache.items() if k != "__binary__" and isinstance(r, dict) and not r.get("crashed")
                 and not k.endswith(".min.stp") and (w / "crashes" / k).exists())
    return n


CRASH_REF = "a .stp file, or a crash id: c3 (latest campaign), probe:c3 or probe/c3"


def resolve_crash(p, ref, campaign=None, minimized=True):
    """A crash reference: path to a .stp, 'c3' (latest campaign, or --campaign), 'camp:c3' or
    'camp/c3'. A bucket's minimized program (<example>.min.stp from `stitch minimize`) is used
    when it exists."""
    if ref.endswith(".stp") or os.path.exists(ref):
        f = Path(ref).resolve()
        if not f.exists():
            die(f"{ref} not found")
        return f, None
    m = re.fullmatch(r"(?:(.+)[:/])?(c\d+)", ref)
    if not m:
        die(f"'{ref}' is not {CRASH_REF}")
    camp = m.group(1) or campaign or resolve_campaign(p, None)
    cid = m.group(2)
    if not (p.campaign_dir(camp) / "campaign.json").exists():
        die(f"no campaign '{camp}' (campaigns: {', '.join(p.campaigns()) or 'none'})")
    tj = p.campaign_dir(camp) / "triage.json"
    if not tj.exists():
        triage(p, camp)
    for b in json.loads(tj.read_text()):
        if b["id"] == cid:
            f = (p.root / b["example"]).resolve()
            mn = f.with_name(f.stem + ".min.stp")
            use_min = minimized and mn.exists() and not f.stem.endswith(".min")
            if use_min:
                f = mn
            if not JSON and (use_min or not m.group(1)):
                # a bare id is ambiguous when there are several campaigns (each numbers its crashes from c1)
                other = [c for c in p.campaigns() if c != camp] if not m.group(1) and not campaign else []
                note = f" (latest campaign; also: {', '.join(other)})" if other else ""
                print(f"[stitch] {camp}:{cid}{note}" + (f": running the minimized program {p.rel(mn)}" if use_min else ""),
                      file=sys.stderr)
            return f, {**b, "campaign": camp}
    die(f"unknown crash '{cid}' in campaign '{camp}' (see `stitch crashes {camp}`)")


def in_project(p, f: Path) -> str:
    try:
        return str(f.relative_to(p.root))
    except ValueError:
        tmp = p.state_dir / "tmp"
        tmp.mkdir(exist_ok=True)
        dst = tmp / f.name
        shutil.copy2(f, dst)
        return str(dst.relative_to(p.root))


def run_repro(p, image, binary_rel, prog_rel, timeout=60, extra=""):
    """Run one program with the reproducer binary. Returns (stderr, exit code, timed out). The
    timeout is enforced inside the container (killing `docker run` would leave it running)."""
    cmd = (f"s=$(date +%s%N); timeout -s KILL {int(timeout)} {binary_rel} --trace {extra} {prog_rel}; rc=$?; "
           f"echo \"[stitch] elapsed $(( ($(date +%s%N) - s) / 1000000 )) ms\" >&2; exit $rc")
    try:
        r = docker.run(p, image, ["sh", "-c", cmd], capture=True, timeout=timeout + 120)
    except subprocess.TimeoutExpired as e:
        run_repro.elapsed_ms = None
        return (e.stderr or b"").decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or ""), -1, True
    m = re.search(r"^\[stitch\] elapsed (\d+) ms\n?", r.stderr or "", re.M)
    elapsed = int(m.group(1)) if m else None
    timed_out = r.returncode == 137 and elapsed is not None and elapsed >= timeout * 1000 - 50
    out = (r.stderr or "")[:m.start()] + (r.stderr or "")[m.end():] if m else r.stderr
    run_repro.elapsed_ms = elapsed
    return out, r.returncode, timed_out


def build_repro_at(p, rev):
    """Library image at `rev` + a reproducer binary from the current harness. Returns (image, binary_rel)."""
    src = Source(p)
    ctx, ident, commit = src.materialize(rev)
    base = docker.ensure_base(p.language, p.sanitizers)
    image = docker.build_project_image(p, base, ctx, ident, quiet=True)
    outdir = f".stitch/rev/{ident}"
    r = docker.run(p, image, ["stitch-build", "--json", "--target", "repro", "--out", outdir], capture=True)
    if r.returncode != 0:
        die(f"the harness does not build against {rev} ({ident}):\n{docker.build_errors(r)}")
    return image, f"{outdir}/repro", commit


def format_plan(plan: Path):
    """Calls of a program with their arguments in full (from the repro binary's --plan output)."""
    def val(x):
        if "obj" in x:
            return f"o{x['obj'][0]}.{x['obj'][1]}"
        if "hex" in x:
            b = bytes.fromhex(x["hex"])
            if x.get("kind") in ("str", "file", "raw", "source") or x.get("decoded"):
                try:
                    return json.dumps(b.decode())
                except UnicodeDecodeError:
                    pass
            return f"bytes[{len(b)}] {x['hex']}"
        for k in ("js", "v", "value"):
            if k in x:
                return str(x[k])
        return json.dumps({k: v for k, v in x.items() if k not in ("kind", "size", "signed", "name")})
    lines = []
    for l in plan.read_text(errors="replace").splitlines():
        try:
            e = json.loads(l)
        except ValueError:
            continue
        if e.get("t") == "call":
            args = ", ".join((f"{x['name']}=" if x.get("name") else "") + val(x) for x in e.get("args", []))
            lines.append(f"#{e['node']} {e['ep']}({args})")
        elif e.get("t") == "done" and e.get("status") != "ok":
            lines.append(f"   -> {e.get('status')}")
    return "\n".join(lines)


def cmd_exec(a):
    p = find_project(a.project)
    f, b = resolve_crash(p, a.program, campaign=a.campaign)
    limit_ms = hang_ms(p, b["campaign"]) if b else int(p.cfg["fuzz"]["timeout_ms"])
    if a.timeout is None:  # long enough for a slow start (JVM, Swift), short enough for a hang
        a.timeout = max(10, 5 * limit_ms // 1000)
    if a.rev:
        image, binary, commit = build_repro_at(p, a.rev)
        if not JSON:
            print(f"[stitch] running against {a.rev} ({commit[:12] if commit else 'local'})", file=sys.stderr)
    else:
        info = build(p, harness_only=True, quiet=True)
        if not info["result"].get("ok"):
            die("harness build failed")
        image, binary = info["image"], ".stitch/build/repro"
    plan = None
    if a.args:
        (p.state_dir / "tmp").mkdir(exist_ok=True)
        plan = p.state_dir / "tmp" / "exec-plan.jsonl"
        plan.unlink(missing_ok=True)
    output, code, to = run_repro(p, image, binary, in_project(p, f), timeout=a.timeout,
                                 extra=f"--plan {p.rel(plan)}" if plan else "")
    elapsed = getattr(run_repro, "elapsed_ms", None)
    rep = reports.parse_report(output, code, to)
    # (startup included: a JVM or a Swift binary takes a while to start, so only well past the limit)
    slow = (not rep["crashed"] and elapsed is not None and elapsed > 2 * limit_ms)
    if to and not JSON:
        print(f"[stitch] killed after {a.timeout}s (--timeout); the campaign's timeout is {limit_ms} ms", file=sys.stderr)
    if JSON:
        reports.dump({**rep, "elapsed_ms": elapsed, "slow": slow, "output": output[-20000:]}
                     | ({"calls": format_plan(plan)} if plan and plan.exists() else {}))
    elif plan:
        print(format_plan(plan) if plan.exists() else "(no plan written)")
        rep_lines = [l for l in _drop_abort_report(output.splitlines(), rep) if not l.startswith("[stitch] ") or "CHECK FAILED" in l or "UNCAUGHT" in l]
        print("\n".join(rep_lines[-200:]))
        print(_exec_result(rep, elapsed, slow, limit_ms, b))
    else:
        lines = _drop_abort_report(output.splitlines(), rep)
        if not a.trace:
            lines = [l for l in lines if not l.startswith("[stitch] ") or "CHECK FAILED" in l or "UNCAUGHT" in l]
        print("\n".join(lines[-400:]))
        print(_exec_result(rep, elapsed, slow, limit_ms, b))


def _drop_abort_report(lines, rep):
    """A failed check (or an uncaught exception) aborts the process, and AddressSanitizer reports
    the abort with a stack and registers: noise after the real report."""
    if not rep["kind"].startswith(("check failed", "uncaught")):
        return lines
    for i, l in enumerate(lines):
        if "AddressSanitizer:DEADLYSIGNAL" in l or "ERROR: AddressSanitizer: ABRT" in l:
            return lines[:i] + ["(AddressSanitizer's report of the abort that follows is left out)"]
    return lines


def _exec_result(rep, elapsed, slow, limit_ms, bucket=None):
    took = f" ({elapsed} ms)" if elapsed is not None else ""
    differs = ""
    if bucket and rep["crashed"] and rep["kind"] != bucket.get("kind"):
        differs = (f"\n(bucket {bucket['campaign']}:{bucket['id']} is '{bucket['kind']}': this run ended differently. A timeout"
                   " that depends on load, or a flaky crash; `stitch crashes` shows how often it reproduces)")
    if rep["crashed"]:
        return f"\nresult: CRASH {rep['signature']}{took}{differs}"
    if bucket and not slow:
        rp = bucket.get("reproduced")
        if rp and rp[0] < rp[1]:
            return f"\nresult: no crash{took} (bucket {bucket['campaign']}:{bucket['id']} is flaky: crashed {rp[0]} of {rp[1]} runs)"
    if slow:
        return (f"\nresult: no crash, but it took {elapsed} ms, far over the campaign's timeout ({limit_ms} ms): the"
                " fuzzer counts that as a timeout (unless the machine is just busy)")
    return f"\nresult: no crash{took}"


def cmd_minimize(a):
    p = find_project(a.project)
    f, b = resolve_crash(p, a.crash, campaign=a.campaign, minimized=False)
    name = b["campaign"] if b else None
    if name:
        # the same harness `stitch exec` runs: the current build, once the campaign has stopped
        meta = refresh_campaign_bin(p, name, json.loads((p.campaign_dir(name) / "campaign.json").read_text()))
        image = meta["image"] if docker.image_exists(meta["image"]) else p.load_state().get("image")
    else:
        image = build(p, harness_only=True, quiet=True)["image"]
    binrel = f".stitch/campaigns/{name}/bin/fuzzer" if name else ".stitch/build/fuzzer"
    if not JSON:
        print(f"[stitch] minimizing {p.rel(f)} (up to {a.secs}s; --secs to change)", file=sys.stderr, flush=True)
    # minimizing a minimized program again reduces it in place (not <x>.min.min.stp)
    dst = Path(a.output) if a.output else (f if f.stem.endswith(".min") else f.with_name(f.stem + ".min.stp"))
    existed = dst.exists()
    r = docker.run(p, image, [binrel, "minimize", in_project(p, f), "-o", p.rel(dst.resolve()) if str(dst.resolve()).startswith(str(p.root)) else in_project(p, dst), "--secs", str(a.secs), "--hang-ms", str(hang_ms(p, name))], capture=True)
    try:
        res = json.loads(r.stdout.strip().splitlines()[-1])
    except Exception:
        die(f"minimize failed: {r.stderr[-2000:]}")
    if not res["crashes"]:
        # nothing to minimize: do not leave a .min.stp that crash ids would then run
        if dst != f and not existed:
            dst.unlink(missing_ok=True)
        rp = (b or {}).get("reproduced")
        if rp and rp[0] < rp[1]:
            die(f"{p.rel(f)} did not crash in 3 runs; it is flaky (crashed {rp[0]} of {rp[1]} runs at triage): try again")
        die(f"{p.rel(f)} did not crash in 3 runs with the current harness (`stitch exec {a.crash}`): nothing to minimize"
            + (" (a timeout that does not reproduce may just be slow under load)" if b and b.get("kind") == "timeout" else ""))
    notes = []
    if res.get("flaky"):
        notes.append("the program crashes only on some runs (flaky): each candidate got two tries")
    if res.get("out_of_time"):
        notes.append(f"stopped by the time budget (--secs {a.secs}): running again may reduce it further")
    out(res, f"{res['calls_before']} -> {res['calls_after']} calls, still crashes: {res['crashes']} ({res['signature']})\n  {res['output']}"
        + "".join(f"\n  ({n})" for n in notes))


def cmd_sample(a):
    p = find_project(a.project)
    info = build(p, harness_only=True, quiet=True)
    r = docker.run(p, info["image"], [".stitch/build/fuzzer", "sample", a.datatype, "-n", str(a.n), "--data", "harness/data", "--seed", str(a.seed)], capture=True)
    if r.returncode != 0:
        die(r.stderr.strip()[-1000:])
    vals = json.loads(r.stdout)
    if JSON:
        reports.dump(vals)
    else:
        for v in vals:
            print(json.dumps(v["value"])[:2000])


def cmd_shell(a):
    p = find_project(a.project)
    st = p.load_state()
    if not st.get("image") or not docker.image_exists(st["image"]):
        build(p, image_only=True)
        st = p.load_state()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    os.execvp("docker", docker.run_args(p, st["image"], tty=True) + (cmd or ["bash"]))


SKILL = STITCH_HOME / "skill" / "stitch"
# The agent guide: skill/stitch/reference/<topic>.md (the docs site serves the same files).
GUIDES = {"": "reference/overview.md",
          **{t: f"reference/{t}.md" for t in ("stubs", "probe", "triage", "severity", "reproducers", "coverage", "stitch-toml", "cli",
                                              "cpp", "python", "javascript", "java", "go", "rust", "ruby", "swift", "objc")}}


def cmd_guide(a):
    from .project import normalize_language
    topic = "cpp" if a.spec else (a.topic or "")
    if topic == "api":
        topic = "cpp"
    if topic in ("config", "toml", "stitch.toml"):
        topic = "stitch-toml"
    if topic and topic not in GUIDES:
        topic = normalize_language(topic)   # "js" -> "javascript", "c++" -> "cpp"
    if topic not in GUIDES:
        die(f"unknown topic '{topic}' (one of: {', '.join(t for t in GUIDES if t)})")
    print((SKILL / GUIDES[topic]).read_text())


def install_skill(dest: Path) -> Path:
    """Copy the STITCH++ agent skill to dest/stitch (replacing an older copy)."""
    target = dest / "stitch"
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(SKILL, target)
    return target


def skill_dir(user: bool, path):
    if path:
        return Path(path).expanduser().resolve()
    return (Path.home() if user else Path.cwd()) / ".claude" / "skills"


def cmd_skill(a):
    t = install_skill(skill_dir(a.user, a.dir))
    out({"ok": True, "skill": str(t)}, f"installed the stitch skill in {t}")


def cmd_setup(a):
    from . import __version__
    docker.check_docker()
    if docker._run(["docker", "buildx", "version"], capture_output=True).returncode != 0:
        die("docker buildx is missing (install the buildx plugin)")
    if not JSON:
        print(f"[stitch] stitch-fuzz {__version__}, docker ok")
    from .project import normalize_language
    langs = [normalize_language(l) for x in (a.lang or ["cpp"]) for l in x.split(",")]
    if "all" in langs:
        langs = list(docker.LANGUAGES)
    images = {lang: docker.ensure_base(lang, rebuild=a.rebuild) for lang in langs}
    tag = ", ".join(images.values())
    res = {"ok": True, "version": __version__, "base_image": tag, "images": images}
    if a.skill or a.skill_dir:
        res["skill"] = str(install_skill(skill_dir(a.user, a.skill_dir)))
    out(res, f"ready: base image {tag}" + (f"\nskill installed in {res['skill']}" if "skill" in res else "")
        + "\nnext: stitch init DIR --git URL --rev REV   (guide: stitch guide)")


def cmd_changelog(a):
    from . import __version__, changelog
    since = a.since
    if since is None and not a.all:  # in a project: what changed since its pinned release
        try:
            since = find_project(a.project, check_version=False).stitch_version
        except ProjectError:
            pass
    es = changelog.between(since, a.until or (None if a.all else __version__))
    if JSON:
        reports.dump({"ok": True, "since": since, "installed": __version__,
                      "entries": [{"version": v, "text": t} for v, t in es]})
        return
    if not es:
        print(f"no changelog entries after {since}" if since else "no changelog entries")
        return
    print(changelog.render(es))


def cmd_upgrade(a):
    from . import __version__, changelog
    from .project import version_key
    p = find_project(a.project, check_version=False)
    old = p.stitch_version
    if old == __version__:
        out({"ok": True, "from": old, "to": __version__, "changed": False}, f"already pinned to {__version__}")
        return
    if old and version_key(old) > version_key(__version__):
        die(f"the project is pinned to {old}, newer than the installed stitch-fuzz {__version__}: "
            f"install it (pip install stitch-fuzz=={old}) instead of downgrading the project")
    es = changelog.between(old, __version__)
    changelog.set_pin(p.root / "stitch.toml", __version__)
    if JSON:
        reports.dump({"ok": True, "from": old, "to": __version__, "changed": True,
                      "entries": [{"version": v, "text": t} for v, t in es]})
        return
    if es:
        print(changelog.render(es) + "\n")
    print(f"stitch.toml: stitch_version {old or '(none)'} -> {__version__}")
    print("next: make the harness changes listed under Upgrading above (if any), then "
          "`stitch build`, `stitch check` and `stitch probe` with this release")


def cmd_init(a):
    from .templates import init_project
    root = Path(a.dir).resolve()
    msg = init_project(root, name=a.name, git=a.git, rev=a.rev, path=a.path, language=a.lang)
    out({"ok": True, "project": str(root)}, msg)


def cmd_repro(a):
    from .repro import make_repro
    p = find_project(a.project)
    f, b = resolve_crash(p, a.crash, campaign=a.campaign)
    d = make_repro(p, f, b, Path(a.output) if a.output else None, run=not a.no_run)
    out({"ok": True, "dir": str(d)}, f"reproducer: {d}\n  run it anywhere with docker: {d}/run.sh")


def cmd_report(a):
    from .report import make_report, ReportError
    p = find_project(a.project)
    try:
        res = make_report(p, a.crash, Path(a.output).resolve() if a.output else None, run=not a.no_run)
    except ReportError as e:
        die(str(e))
    if JSON:
        reports.dump(res)
        return
    d = Path(res["dir"])
    st = (res.get("check") or {}).get("status")
    if st == "build_failed":
        sys.stderr.write(res.get("output", "") + "\n")
        die(f"the reproducer's Dockerfile does not build: {d}/Dockerfile")
    print(f"report: {p.rel(d)}  (Dockerfile, {res['file']}, run.sh; ISSUE.md is the issue text)")
    if st:
        msg = {"same": "verified: same crash", "different": "WARNING: it crashes differently",
               "no_crash": "WARNING: it does not crash", "diverged": "WARNING: it diverged",
               "crash": "it crashes (no expected.json next to the file to compare with)"}.get(st, st)
        got = (res.get("got") or {}).get("signature")
        print(f"  {msg}" + (f": {got}" if got and st != "same" else ""))
    print(f"  library pinned to {res['source']}; packages as of {res['pinned_to']}")
    if not res["plain"]:
        print("  note: it still uses the STITCH++ stand-in (shim file included); shrink it to plain library code for a cleaner report")
    if not res["published_toolchain"]:
        print("  note: no published toolchain image for this stitch-fuzz version: the Dockerfile builds only on this machine")
    if st and st not in ("same", "crash"):
        sys.exit(1)


def cmd_bisect(a):
    from .bisect import bisect
    p = find_project(a.project)
    f, b = resolve_crash(p, a.crash, campaign=a.campaign)
    res = bisect(p, f, good=a.good, bad=a.bad, strict=a.strict, max_auto=a.max_auto, verbose=not JSON,
                 fixed=(a.fixed_at or "HEAD") if a.fixed else None)
    if JSON:
        reports.dump(res)
    elif not res.get("ok"):
        die(res.get("error", "bisect failed"))
    elif not res.get("found"):
        print(f"[bisect] {res.get('message', 'no single commit found')}")
        skipped = [t for t in res.get("tested", []) if t["status"] == "skip"]
        if skipped:
            print(f"[bisect] {len(skipped)} revisions were skipped (library or harness did not build there); "
                  "pass --good REV with a buildable older revision")


def cov_programs(p, inputs, crashes=False):
    """Programs for `stitch cov`: campaign names (their corpus) or .stp files / directories."""
    if not inputs:
        inputs = [resolve_campaign(p, None)]
    progs, labels = {}, []
    for x in inputs:
        if not os.path.exists(x) and p.campaign_dir(x).is_dir():
            cdir = p.campaign_dir(x)
            dirs = [w / "corpus" for w in worker_dirs(cdir)] + ([w / "crashes" for w in worker_dirs(cdir)] if crashes else [])
            files = [f for d in dirs if d.is_dir() for f in programs(d)]
            labels.append(x)
        elif os.path.isdir(x):
            files = [f for f in Path(x).resolve().rglob("*.stp") if not f.name.startswith(".")]
            labels.append(Path(x).resolve().name)
        elif os.path.exists(x):
            files = [Path(x).resolve()]
            labels.append(Path(x).stem)
        else:
            die(f"'{x}' is neither a campaign nor a file or directory")
        for f in files:
            if f.name.startswith("."):
                continue  # LibAFL metadata next to corpus entries
            progs.setdefault(f.name if f.parent.name in ("corpus", "crashes") else str(f), f)
    if not progs:
        die("no programs found" + ("" if crashes else " (the corpus is empty; is the campaign running?)"))
    return [in_project(p, f) for f in progs.values()], "+".join(dict.fromkeys(labels))


def cmd_cov(a):
    from . import coverage
    p = find_project(a.project)
    progs, label = cov_programs(p, a.inputs, crashes=a.crashes)
    odir = Path(a.output).resolve() if a.output else p.state_dir / "cov" / re.sub(r"[^\w.+@-]", "_", label + (f"@{a.rev}" if a.rev else ""))
    if p.root not in odir.parents:
        die("--output must be inside the project")
    if not JSON:
        print(f"[stitch] coverage build + {len(progs)} programs ({label})", file=sys.stderr, flush=True)
    try:
        s = coverage.run(p, progs, odir, label, rev=a.rev, timeout=a.timeout, jobs=a.jobs or os.cpu_count() or 4,
                         html=not a.no_html, quiet=JSON)
    except docker.DockerError as e:
        die(str(e))
    out(s, coverage.render(s, limit=a.limit, show_harness=a.harness))


def cmd_write(a):
    from . import standalone
    p = find_project(a.project)
    f, b = resolve_crash(p, a.crash, campaign=a.campaign)
    res = standalone.write(p, f, b, Path(a.output) if a.output else None, run_check=not a.no_check)
    if JSON:
        reports.dump({k: v for k, v in res.items() if k != "check"} | ({"check": {k: v for k, v in res["check"].items() if k != "output"}} if "check" in res else {}))
        return
    if res.get("warning"):
        print(f"warning: {res['warning']}")
    d = Path(res["dir"])
    gen = Path(res.get("file") or res["cpp"])
    mini = gen.with_name("min" + (gen.suffix if p.language != "cpp" else ".cpp"))
    print(f"wrote {p.rel(gen)} ({res['lines']} lines, {res['calls']} calls"
          + (f", crash in call #{res['crash_node']}" if res["crash_node"] is not None else "") + ")")
    print(f"  expected: {res['expected']['kind'] or 'no crash'}" + (f" in {res['expected']['frame']}" if res['expected']['frame'] else ""))
    if "check" in res:
        _print_verdict(res["check"], res["expected"], p.rel(gen))
    print(f"next: cp {p.rel(gen)} {p.rel(mini)}, shrink it, and run `stitch try {p.rel(mini)}` after each edit")


def _print_verdict(v, expected, name, show=False):
    from .standalone import VERDICT_TEXT
    head = VERDICT_TEXT.get(v["status"], v["status"])
    exp = (expected["kind"] + (f" in {expected['frame']}" if expected.get("frame") else "")) if expected.get("kind") else "no crash"
    if v["status"] == "same":
        print(f"{head}: {name} -> {exp}")
        if v.get("matched") == "own check":
            print("  (the file failed in its own code, with no library frame: it is taken as your check of the same"
                  " property; --show prints its output)")
    elif v["status"] == "different":
        got = v["got"]["kind"] + (f" in {v['got']['frame']}" if v["got"].get("frame") else "")
        print(f"{head}: {name}\n  expected: {exp}\n  got:      {got}")
    elif v["status"] == "compile_error":
        print(f"{head}: {name}")
        for e in v.get("errors") or v["output"].splitlines()[-15:]:
            print("  " + e.replace("/project/", ""))
    elif v["status"] == "diverged":
        print(f"{head}: {name}\n  {v['message']}\n  (a stitch::require failed where the original run passed; the pasted code no longer follows the original path)")
    else:
        print(f"{head}: {name} ran to completion (expected {exp})")
    if show or v["status"] in ("different", "no_crash"):
        lines = [l for l in v["output"].splitlines() if l.strip()]
        print("  --- output (tail) ---")
        for l in lines[-25:]:
            print("  " + l.replace("/project/", ""))


def cmd_try(a):
    from . import standalone
    p = find_project(a.project)
    f = Path(a.file).resolve()
    if not f.exists():
        die(f"{a.file} not found")
    orig = f
    try:
        rel = f.relative_to(p.root.resolve())
    except ValueError:
        # it runs in the project's container, which only sees the project: copy it in, with the
        # runtime stand-in that may be next to it (expected.json is still looked up next to the original)
        d = p.state_dir / "tmp" / "try"
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        for x in [f] + [f.parent / n for n in ("stitch.js", "stitch.py", "stitch.rb", "stitch_standalone.h")]:
            if x.is_file():
                shutil.copy2(x, d / x.name)
        f = d / f.name
        rel = f.relative_to(p.root.resolve())
        if not JSON:
            print(f"[stitch] {a.file} is outside the project: running a copy, {p.rel(f)}", file=sys.stderr)
    if a.crash:
        _, b = resolve_crash(p, a.crash, campaign=a.campaign)
        if b is None:
            die("to compare with a crash, name it by id (c3, probe:c3)")
        top = next((reports.frame_func(x) for x in b.get("frames", []) if not x.endswith("(stub)") and not x.startswith("main")), "")
        expected = {"kind": b["kind"], "frame": top, "signature": b["signature"], "crash_id": f"{b['campaign']}:{b['id']}"}
    elif a.expect:
        e = Path(a.expect)
        e = e / "expected.json" if e.is_dir() else e
        expected = json.loads(e.read_text())
    else:
        expected = standalone.find_expected(orig)
    if a.rev:
        image = build(p, rev=a.rev, image_only=True, quiet=True)["image"]
    else:
        image = build(p, harness_only=True, image_only=True, quiet=True)["image"]
    if expected is None:
        # nothing to compare with: run the file and say what happened
        t = standalone.try_file(p, image, str(rel), timeout=a.timeout)
        rep = t.get("report") or {}
        if JSON:
            reports.dump({"status": t["status"], "report": rep, "errors": t.get("errors"), "output": t["output"][-8000:]})
        else:
            if t["status"] == "compile_error":
                print(f"COMPILE ERROR: {p.rel(f)}")
                for e in t.get("errors") or t["output"].splitlines()[-15:]:
                    print("  " + e.replace("/project/", ""))
            elif t["status"] in ("crash", "diverged"):
                print(f"CRASH: {p.rel(f)} -> {rep.get('kind')}" + (f" in {rep['frames'][0]}" if rep.get("frames") else ""))
                print(f"  signature: {rep.get('signature')}")
            else:
                print(f"NO CRASH: {p.rel(f)} ran to completion")
            print("  (no expected.json next to the file: name a crash to compare with, e.g. `stitch try FILE c3`)")
            if a.show or t["status"] != "no_crash":
                lines = [l for l in t["output"].splitlines() if l.strip()]
                print("  --- output (tail) ---")
                for l in lines[-25:]:
                    print("  " + l.replace("/project/", ""))
        sys.exit(2 if t["status"] == "compile_error" else 0)
    t = standalone.try_file(p, image, str(rel), timeout=a.timeout, leaks="leak" in expected["kind"])
    text = f.read_text(errors="replace")
    shimmed = standalone.uses_shim(p.language, text)
    v = standalone.verdict(expected, t, plain=not shimmed)
    v["lines"] = standalone.code_lines(text)
    v["uses_shim"] = shimmed
    if JSON:
        reports.dump({**v, "expected": expected, "output": v["output"][-8000:]})
    else:
        _print_verdict(v, expected, p.rel(f), show=a.show)
        if v["status"] == "same":
            print(f"  {v['lines']} code lines" + (f", still uses {standalone.shim_name(p.language)}" if v["uses_shim"] else ", plain library code"))
    sys.exit({"same": 0, "compile_error": 2}.get(v["status"], 1))


# =============================================================================================
def main(argv=None):
    global JSON
    ap = argparse.ArgumentParser(prog="stitch", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="Start with `stitch guide` (how to write and debug harnesses).")
    ap.add_argument("-C", "--project", default=None, help="project directory (default: search upwards for stitch.toml)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    from . import __version__
    ap.add_argument("--version", action="version", version=f"stitch-fuzz {__version__}")
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("-C", "--project", default=argparse.SUPPRESS, help=argparse.SUPPRESS)
    common.add_argument("--json", action="store_true", default=argparse.SUPPRESS, help="machine-readable output")
    sp = ap.add_subparsers(dest="cmd", required=True)
    _add = sp.add_parser
    sp.add_parser = lambda *x, **k: _add(*x, parents=[common], **k)

    s = sp.add_parser("setup", help="check Docker and get the base image (run once per version)")
    s.add_argument("--skill", action="store_true", help="also install the agent skill in ./.claude/skills")
    s.add_argument("--skill-dir", help="install the agent skill into this skills directory")
    s.add_argument("--user", action="store_true", help="with --skill: install into ~/.claude/skills")
    s.add_argument("--rebuild", action="store_true", help="build the base image locally even if one exists")
    s.add_argument("--lang", action="append", help="language image(s) to get: cpp (default), python, javascript, java, go, rust, ruby, swift, objc, all")
    s.set_defaults(fn=cmd_setup)

    s = sp.add_parser("changelog", help="what changed in each STITCH++ release (in a project: since its pinned release)")
    s.add_argument("--since", help="only releases after this one")
    s.add_argument("--until", help="only releases up to this one (default: the installed one)")
    s.add_argument("--all", action="store_true", help="every release, also outside a project")
    s.set_defaults(fn=cmd_changelog)

    s = sp.add_parser("upgrade", help="pin the project to the installed STITCH++ release (stitch.toml stitch_version)")
    s.set_defaults(fn=cmd_upgrade)

    s = sp.add_parser("init", help="create a new project (stitch.toml, Dockerfile, harness/)")
    s.add_argument("dir", nargs="?", default=".")
    s.add_argument("--name")
    g = s.add_mutually_exclusive_group(required=True)
    g.add_argument("--git", help="library git URL")
    g.add_argument("--path", help="local library checkout")
    s.add_argument("--rev", help="default revision (git)")
    s.add_argument("--lang", default="cpp", help="harness language: cpp (C/C++), python, javascript, java, go, rust, ruby, swift, objc (Objective-C)")
    s.set_defaults(fn=cmd_init)

    s = sp.add_parser("build", help="build the library image and the harness")
    s.add_argument("--rev", help="build the library at this revision (does not change the default)")
    s.add_argument("--image-only", action="store_true")
    s.add_argument("--harness-only", action="store_true", help="reuse the current image")
    s.add_argument("--no-cache", action="store_true")
    s.add_argument("--rebuild-base", action="store_true")
    s.set_defaults(fn=cmd_build)

    s = sp.add_parser("check", help="build + static checks (unreachable endpoints, attribute keys, datatypes)")
    s.add_argument("--full", action="store_true", help="also rebuild the project image")
    s.set_defaults(fn=cmd_check)

    s = sp.add_parser("probe", help="short fuzzing run + report of what blocks progress (campaign 'probe', restarted each time)")
    s.add_argument("only", nargs="?", help="endpoint names/globs to focus on, comma separated (e.g. 'tree/*,parse')")
    s.add_argument("--secs", type=int, default=60)
    s.add_argument("--jobs", type=int, default=1, help="parallel workers (default 1)")
    s.set_defaults(fn=cmd_probe)

    s = sp.add_parser("fuzz", help="start a campaign in the background")
    s.add_argument("--name", default="main", help="campaign name (default: main). Running it again resumes its corpus and "
                   "crashes; each campaign numbers its crashes from c1 (refer to others as NAME:c3)")
    s.add_argument("--secs", type=int, help="stop after this many seconds (default: run until `stitch stop`)")
    s.add_argument("--jobs", type=int, help="parallel workers (default: [fuzz] jobs in stitch.toml)")
    s.add_argument("--only", help="only these stubs and the stubs that produce their inputs: names/globs, comma separated")
    s.add_argument("--fresh", action="store_true", help="start the campaign over: discard its corpus and crashes")
    s.add_argument("--foreground", action="store_true", help="run in this terminal and print the final stats")
    s.set_defaults(fn=cmd_fuzz)

    s = sp.add_parser("status", help="campaign progress and health report")
    s.add_argument("name", nargs="?")
    s.add_argument("--limit", type=int, default=15)
    s.add_argument("--mutations", action="store_true", help="also show which mutation operators find new coverage")
    s.add_argument("--wait", action="store_true", help="wait until the campaign stops (after --secs), then print the status")
    s.set_defaults(fn=cmd_status)

    s = sp.add_parser("stop", help="stop a campaign")
    s.add_argument("name", nargs="?")
    s.set_defaults(fn=cmd_stop)

    s = sp.add_parser("crashes", help="triage crashes into unique buckets")
    s.add_argument("name", nargs="?")
    s.set_defaults(fn=cmd_crashes)

    s = sp.add_parser("exec", help="run a program (.stp or crash id) with the current harness")
    s.add_argument("program", help=CRASH_REF)
    s.add_argument("--campaign", help="campaign for a crash id like c3 (default: the latest; or write CAMPAIGN:c3)")
    s.add_argument("--args", action="store_true", help="print every call with its arguments in full (--trace shortens long values)")
    s.add_argument("--trace", action="store_true", help="print every call with its arguments")
    s.add_argument("--timeout", type=int, default=None, help="kill it after this many seconds (default: 5x the campaign's timeout_ms, at least 10)")
    s.add_argument("--rev", help="run against the library at this revision (e.g. HEAD: is it fixed upstream?)")
    s.set_defaults(fn=cmd_exec)

    s = sp.add_parser("minimize", help="reduce a crashing program (writes <program>.min.stp, which crash ids then use)")
    s.add_argument("crash", help=CRASH_REF)
    s.add_argument("--campaign", help="campaign for a crash id like c3 (default: the latest; or write CAMPAIGN:c3)")
    s.add_argument("-o", "--output")
    s.add_argument("--secs", type=int, default=120, help="time budget (default 120); it stops earlier when nothing more can be removed")
    s.set_defaults(fn=cmd_minimize)

    s = sp.add_parser("repro", help="write a self-contained dockerized reproducer")
    s.add_argument("crash", help=CRASH_REF)
    s.add_argument("--campaign", help="campaign for a crash id like c3 (default: the latest; or write CAMPAIGN:c3)")
    s.add_argument("-o", "--output")
    s.add_argument("--no-run", action="store_true", help="do not verify the reproducer")
    s.set_defaults(fn=cmd_repro)

    s = sp.add_parser("report", help="a reproducer for a GitHub issue: Dockerfile + reproducer file + run.sh (+ ISSUE.md)")
    s.add_argument("crash", help="a standalone reproducer file (from `stitch write`, shrunk to plain library code), or a crash id: "
                   "its newest min.* file in .stitch/standalone/<id>/")
    s.add_argument("-o", "--output", help="output directory (default .stitch/reports/<id>)")
    s.add_argument("--no-run", action="store_true", help="do not build and run it to verify")
    s.set_defaults(fn=cmd_report)

    s = sp.add_parser("cov", help="run a corpus on a coverage build; write lcov.info, HTML and a summary")
    s.add_argument("inputs", nargs="*", help="campaign names (their corpus) or .stp files/directories (default: latest campaign)")
    s.add_argument("--crashes", action="store_true", help="also run the campaigns' crashing programs")
    s.add_argument("--rev", help="measure against the library at this revision")
    s.add_argument("-o", "--output", help="output directory inside the project (default .stitch/cov/<campaign>)")
    s.add_argument("--timeout", type=int, default=10, help="seconds per program")
    s.add_argument("--jobs", type=int, help="parallel processes (default: all cores)")
    s.add_argument("--no-html", action="store_true", help="skip the HTML report")
    s.add_argument("--harness", action="store_true", help="also show coverage of the harness code")
    s.add_argument("--limit", type=int, default=20, help="never-called functions to list")
    s.set_defaults(fn=cmd_cov)

    s = sp.add_parser("write", help="turn a crash into a standalone reproducer (harness code pasted in, values filled in)")
    s.add_argument("crash", help=CRASH_REF)
    s.add_argument("--campaign", help="campaign for a crash id like c3 (default: the latest; or write CAMPAIGN:c3)")
    s.add_argument("-o", "--output", help="output directory inside the project (default .stitch/standalone/<id>)")
    s.add_argument("--no-check", action="store_true", help="do not build and run the result")
    s.set_defaults(fn=cmd_write)

    s = sp.add_parser("try", help="build + run a standalone reproducer and compare its crash with the original")
    s.add_argument("file", help="reproducer file (expected.json is found next to it); a file outside the project runs from a copy")
    s.add_argument("crash", nargs="?", help="compare with this crash instead of expected.json (c3, probe:c3); "
                   "with neither, just run the file and report what happens")
    s.add_argument("--campaign", help="campaign for a crash id like c3 (default: the latest; or write CAMPAIGN:c3)")
    s.add_argument("--expect", help="expected.json (or its directory) from `stitch write`")
    s.add_argument("--rev", help="build against the library at this revision")
    s.add_argument("--timeout", type=int, default=60)
    s.add_argument("--show", action="store_true", help="print the program's output (shown anyway when the crash differs or is missing)")
    s.set_defaults(fn=cmd_try)

    s = sp.add_parser("bisect", help="find the library commit that introduced a crash")
    s.add_argument("crash", help=CRASH_REF)
    s.add_argument("--campaign", help="campaign for a crash id like c3 (default: the latest; or write CAMPAIGN:c3)")
    s.add_argument("--good", help="a revision without the bug (default: search older tags)")
    s.add_argument("--bad", help="a revision with the bug (default: the configured revision)")
    s.add_argument("--strict", action="store_true", help="treat a different crash as 'skip' instead of 'bad'")
    s.add_argument("--max-auto", type=int, default=8, help="max revisions to try when searching for a good one")
    s.add_argument("--fixed", action="store_true", help="find the commit that FIXED the crash (between --bad and --fixed-at)")
    s.add_argument("--fixed-at", help="a revision where the crash is gone (default: HEAD)")
    s.set_defaults(fn=cmd_bisect)

    s = sp.add_parser("sample", help="print values generated by a datatype decoder (or seeds)")
    s.add_argument("datatype")
    s.add_argument("-n", type=int, default=5)
    s.add_argument("--seed", type=int, default=1)
    s.set_defaults(fn=cmd_sample)

    s = sp.add_parser("shell", help="shell in the project image (project mounted at /project)")
    s.add_argument("cmd", nargs=argparse.REMAINDER)
    s.set_defaults(fn=cmd_shell)

    s = sp.add_parser("guide", help="print the agent guide (topics: stubs, probe, triage, reproducers, coverage, stitch-toml, cli, or a language)")
    s.add_argument("topic", nargs="?", help="a file of the guide, or a language: its harness API (api is cpp); default: the overview")
    s.add_argument("--spec", action="store_true", help=argparse.SUPPRESS)
    s.set_defaults(fn=cmd_guide)

    s = sp.add_parser("skill", help="install the STITCH++ agent skill (default: ./.claude/skills/stitch)")
    s.add_argument("dir", nargs="?", help="skills directory to install into")
    s.add_argument("--user", action="store_true", help="install into ~/.claude/skills")
    s.set_defaults(fn=cmd_skill)

    a = ap.parse_args(argv)
    JSON = a.json
    if getattr(a, "project", None):
        # like `git -C`: run as if started there, so relative paths (-o, files) resolve inside it
        a.project = str(Path(a.project).resolve())
        try:
            os.chdir(a.project)
        except OSError as e:
            die(f"cannot enter {a.project}: {e}")
    try:
        a.fn(a)
    except (ProjectError, SourceError, docker.DockerError) as e:
        die(str(e))
    except KeyboardInterrupt:
        die("interrupted", 130)
    except BrokenPipeError:  # output piped into `head` and the like
        try:
            sys.stdout = open(os.devnull, "w")
        except OSError:
            pass
        sys.exit(0)


if __name__ == "__main__":
    main()
