"""stitch: build, fuzz, triage and reproduce STITCH harnesses (everything runs in Docker)."""
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
def build(project, rev=None, image_only=False, harness_only=False, no_cache=False, out_dir=None, target="all",
          quiet=False, rebuild_base=False):
    """Build base + project image + harness. Returns dict(image, ident, commit, result)."""
    docker.check_docker()
    st = project.load_state()
    base_tag = docker.base_tag(tuple(project.cfg["build"]["sanitizers"] or ()))
    if (harness_only and st.get("image") and docker.image_exists(st["image"]) and rev is None
            and st.get("base") == base_tag and not rebuild_base):
        image, ident, commit = st["image"], st.get("ident"), st.get("commit")
    else:
        base = docker.ensure_base(tuple(project.cfg["build"]["sanitizers"] or ()), rebuild=rebuild_base)
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
    if r.stderr and not quiet:
        sys.stderr.write(r.stderr)
    try:
        res = json.loads(r.stdout.strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        res = {"ok": False, "errors": [{"file": "stitch-build", "output": (r.stdout + r.stderr)[-3000:]}]}
    info["result"] = res
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
    (cdir / "bin").mkdir(parents=True, exist_ok=True)
    for t in ("fuzzer", "repro", "manifest.json"):
        shutil.copy2(p.build_dir / t, cdir / "bin" / t)
    fz = p.cfg["fuzz"]
    rel = f".stitch/campaigns/{name}"
    args = ["--data", "harness/data", "--quiet", "--timeout-ms", str(fz["timeout_ms"]), "--max-nodes", str(fz["max_nodes"]),
            "--persistent", str(fz.get("persistent", 200))]
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
            "secs": secs, "only": only, "started": time.time(), "container": cname}
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
        return rs + (f"; read missing attr {ms}" if ms else "")

    if never:
        lines.append(f"never succeeds ({len(never)}): the fuzzer stops picking these")
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
        lines.append("outcome depends on the previous call on an input (engine learns this; fix the stub if unexpected):")
        for e, lo, hi in dep[:limit]:
            lines.append(f"  {e['name']}.{lo['input']}: ok {reports.pct(lo['ok_rate'])} after {lo['after']} vs {reports.pct(hi['ok_rate'])} after {hi['after']}")
    if skipped:
        lines.append(f"often skipped because an input object was missing ({len(skipped)}): its producer bailed or returned no object")
        for e in skipped[:limit]:
            lines.append(f"  {e['name']}  skipped {e['skipped_missing_input']}x, ran {e['invoked']}x")
    if unreached:
        lines.append(f"never called ({len(unreached)}): " + ", ".join(e["name"] for e in unreached[:40]) + (" ..." if len(unreached) > 40 else ""))
    return lines


def summary_line(s):
    return (f"{s['elapsed_secs']:.0f}s, {reports.human(s['execs'])} execs ({reports.human(s['execs_per_sec'])}/s), "
            f"{s['edges_covered']}/{s['edges_total']} edges, corpus {s['corpus']}, calls bailing {reports.pct(s.get('call_bail_rate', 0))} "
            f"(programs with a bail {reports.pct(s['exec_bail_rate'])}), "
            f"{s['crashes_saved']} crash/timeout programs saved")


def cmd_fuzz(a):
    p = find_project(a.project)
    jobs = a.jobs or p.cfg["fuzz"]["jobs"]
    meta = start_campaign(p, a.name, secs=a.secs, jobs=jobs, only=a.only, fresh=a.fresh, detach=not a.foreground)
    out(meta, f"campaign '{a.name}' started ({jobs} worker{'s' if jobs > 1 else ''}, container {meta['container']}).\n"
              f"  stitch status {a.name}     progress + what blocks the fuzzer\n"
              f"  stitch crashes {a.name}    triage crashes\n"
              f"  stitch stop {a.name}")


def cmd_probe(a):
    p = find_project(a.project)
    name = "probe"
    if not JSON:
        print(f"[stitch] probing for {a.secs}s...", flush=True)
    start_campaign(p, name, secs=a.secs, jobs=1, only=a.only, fresh=True, detach=False)
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
def triage(p, name):
    cdir = p.campaign_dir(name)
    meta = json.loads((cdir / "campaign.json").read_text())
    image = meta["image"] if docker.image_exists(meta["image"]) else p.load_state().get("image")
    buckets = {}
    for w in worker_dirs(cdir):
        if not (w / "crashes").exists() or not any((w / "crashes").glob("*.stp")):
            continue
        rel = p.rel(w)
        r = docker.run(p, image, [f".stitch/campaigns/{name}/bin/fuzzer", "triage", "--crashes", f"{rel}/crashes"], capture=True)
        try:
            bs = json.loads(r.stdout)
        except json.JSONDecodeError:
            die(f"triage failed: {r.stderr[-2000:]}")
        for b in bs:
            b["example"] = f"{rel}/crashes/{b['example']}"
            t = buckets.get(b["signature"])
            if not t:
                buckets[b["signature"]] = b
            else:
                t["count"] += b["count"]
                if b["example_nodes"] < t["example_nodes"]:
                    t["example"], t["example_nodes"] = b["example"], b["example_nodes"]
    bl = sorted(buckets.values(), key=lambda b: -b["count"])
    for i, b in enumerate(bl, 1):
        b["id"] = f"c{i}"
        b.pop("files", None)
    (cdir / "triage.json").write_text(json.dumps(bl, indent=1))
    return bl


def cmd_crashes(a):
    p = find_project(a.project)
    name = resolve_campaign(p, a.name)
    bl = triage(p, name)
    if JSON:
        reports.dump({"campaign": name, "buckets": bl})
        return
    if not bl:
        print(f"no reproducible crashes in '{name}'")
        return
    print(f"{len(bl)} unique crash{'es' if len(bl) != 1 else ''} in '{name}':")
    for b in bl:
        print(f"  {b['id']:4s} x{b['count']:<5d} {b['kind']}  in {b.get('endpoint') or '?'}")
        for f in b["frames"][:3]:
            print(f"         {f}")
        print(f"         example ({b['example_nodes']} calls): {b['example']}")
    print("next: stitch exec <id> --trace | stitch minimize <id> | stitch repro <id> | stitch bisect <id>")


def resolve_crash(p, ref):
    """A crash reference: path to a .stp, 'c3' (latest triaged campaign) or 'campaign:c3'."""
    if ref.endswith(".stp") or os.path.exists(ref):
        f = Path(ref).resolve()
        if not f.exists():
            die(f"{ref} not found")
        return f, None
    camp, _, cid = ref.rpartition(":")
    camp = camp or resolve_campaign(p, None)
    tj = p.campaign_dir(camp) / "triage.json"
    if not tj.exists():
        triage(p, camp)
    for b in json.loads(tj.read_text()):
        if b["id"] == cid:
            return (p.root / b["example"]).resolve(), b
    die(f"unknown crash '{ref}' (see `stitch crashes {camp}`)")


def in_project(p, f: Path) -> str:
    try:
        return str(f.relative_to(p.root))
    except ValueError:
        tmp = p.state_dir / "tmp"
        tmp.mkdir(exist_ok=True)
        dst = tmp / f.name
        shutil.copy2(f, dst)
        return str(dst.relative_to(p.root))


def run_repro(p, image, binary_rel, prog_rel, timeout=60):
    try:
        r = docker.run(p, image, ["sh", "-c", f"{binary_rel} --trace {prog_rel}"], capture=True, timeout=timeout)
        return r.stderr, r.returncode, False
    except subprocess.TimeoutExpired as e:
        return (e.stderr or b"").decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or ""), -1, True


def build_repro_at(p, rev):
    """Library image at `rev` + a reproducer binary from the current harness. Returns (image, binary_rel)."""
    src = Source(p)
    ctx, ident, commit = src.materialize(rev)
    base = docker.ensure_base(tuple(p.cfg["build"]["sanitizers"] or ()))
    image = docker.build_project_image(p, base, ctx, ident, quiet=True)
    outdir = f".stitch/rev/{ident}"
    r = docker.run(p, image, ["stitch-build", "--json", "--target", "repro", "--out", outdir], capture=True)
    if r.returncode != 0:
        die(f"the harness does not build against {rev} ({ident}):\n{(r.stderr or '')[-2000:]}")
    return image, f"{outdir}/repro", commit


def cmd_exec(a):
    p = find_project(a.project)
    f, _ = resolve_crash(p, a.program)
    if a.rev:
        image, binary, commit = build_repro_at(p, a.rev)
        if not JSON:
            print(f"[stitch] running against {a.rev} ({commit[:12] if commit else 'local'})", file=sys.stderr)
    else:
        info = build(p, harness_only=True, quiet=True)
        if not info["result"].get("ok"):
            die("harness build failed")
        image, binary = info["image"], ".stitch/build/repro"
    output, code, to = run_repro(p, image, binary, in_project(p, f), timeout=a.timeout)
    rep = reports.parse_report(output, code, to)
    if JSON:
        reports.dump({**rep, "output": output[-20000:]})
    else:
        lines = output.splitlines()
        if not a.trace:
            lines = [l for l in lines if not l.startswith("[stitch] ") or "CHECK FAILED" in l or "UNCAUGHT" in l]
        print("\n".join(lines[-400:]))
        print(f"\nresult: {'CRASH ' + rep['signature'] if rep['crashed'] else 'no crash'}")


def cmd_minimize(a):
    p = find_project(a.project)
    f, b = resolve_crash(p, a.crash)
    name = resolve_campaign(p, None) if b else None
    st = p.load_state()
    binrel = f".stitch/campaigns/{name}/bin/fuzzer" if name else ".stitch/build/fuzzer"
    dst = Path(a.output) if a.output else f.with_name(f.stem + ".min.stp")
    r = docker.run(p, st["image"], [binrel, "minimize", in_project(p, f), "-o", p.rel(dst.resolve()) if str(dst.resolve()).startswith(str(p.root)) else in_project(p, dst), "--secs", str(a.secs)], capture=True)
    try:
        res = json.loads(r.stdout.strip().splitlines()[-1])
    except Exception:
        die(f"minimize failed: {r.stderr[-2000:]}")
    out(res, f"{res['calls_before']} -> {res['calls_after']} calls, still crashes: {res['crashes']} ({res['signature']})\n  {res['output']}")


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
    os.execvp("docker", docker.run_args(p, st["image"], tty=True) + (a.cmd or ["bash"]))


SKILL = STITCH_HOME / "skill" / "stitch"
GUIDES = {"": "SKILL.md", "api": "reference/harness-api.md", "reproducers": "reference/reproducers.md"}


def cmd_guide(a):
    topic = "api" if a.spec else (a.topic or "")
    if topic not in GUIDES:
        die(f"unknown topic '{topic}' (one of: {', '.join(t for t in GUIDES if t)})")
    text = (SKILL / GUIDES[topic]).read_text()
    if text.startswith("---\n"):  # skill frontmatter
        text = text.split("\n---\n", 1)[1].lstrip("\n")
    print(text)


def install_skill(dest: Path) -> Path:
    """Copy the STITCH agent skill to dest/stitch (replacing an older copy)."""
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
    tag = docker.ensure_base(rebuild=a.rebuild)
    res = {"ok": True, "version": __version__, "base_image": tag}
    if a.skill or a.skill_dir:
        res["skill"] = str(install_skill(skill_dir(a.user, a.skill_dir)))
    out(res, f"ready: base image {tag}" + (f"\nskill installed in {res['skill']}" if "skill" in res else "")
        + "\nnext: stitch init DIR --git URL --rev REV   (guide: stitch guide)")


def cmd_init(a):
    from .templates import init_project
    root = Path(a.dir).resolve()
    msg = init_project(root, name=a.name, git=a.git, rev=a.rev, path=a.path)
    out({"ok": True, "project": str(root)}, msg)


def cmd_repro(a):
    from .repro import make_repro
    p = find_project(a.project)
    f, b = resolve_crash(p, a.crash)
    d = make_repro(p, f, b, Path(a.output) if a.output else None, run=not a.no_run)
    out({"ok": True, "dir": str(d)}, f"reproducer: {d}\n  run it anywhere with docker: {d}/run.sh")


def cmd_bisect(a):
    from .bisect import bisect
    p = find_project(a.project)
    f, b = resolve_crash(p, a.crash)
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
            files = [f for d in dirs if d.is_dir() for f in d.glob("*.stp")]
            labels.append(x)
        elif os.path.isdir(x):
            files = list(Path(x).resolve().rglob("*.stp"))
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
    f, b = resolve_crash(p, a.crash)
    res = standalone.write(p, f, b, Path(a.output) if a.output else None, run_check=not a.no_check)
    if JSON:
        reports.dump({k: v for k, v in res.items() if k != "check"} | ({"check": {k: v for k, v in res["check"].items() if k != "output"}} if "check" in res else {}))
        return
    d = Path(res["dir"])
    print(f"wrote {p.rel(d / 'repro.cpp')} ({res['lines']} lines, {res['calls']} calls"
          + (f", crash in call #{res['crash_node']}" if res["crash_node"] is not None else "") + ")")
    print(f"  expected: {res['expected']['kind'] or 'no crash'}" + (f" in {res['expected']['frame']}" if res['expected']['frame'] else ""))
    if "check" in res:
        _print_verdict(res["check"], res["expected"], p.rel(d / "repro.cpp"))
    print(f"next: cp {p.rel(d / 'repro.cpp')} {p.rel(d / 'min.cpp')}, shrink it, and run `stitch try {p.rel(d / 'min.cpp')}` after each edit")


def _print_verdict(v, expected, name, show=False):
    from .standalone import VERDICT_TEXT
    head = VERDICT_TEXT.get(v["status"], v["status"])
    exp = (expected["kind"] + (f" in {expected['frame']}" if expected.get("frame") else "")) if expected.get("kind") else "no crash"
    if v["status"] == "same":
        print(f"{head}: {name} -> {exp}")
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
    try:
        rel = f.relative_to(p.root.resolve())
    except ValueError:
        die(f"{a.file} must be inside the project directory")
    if a.expect:
        e = Path(a.expect)
        e = e / "expected.json" if e.is_dir() else e
        expected = json.loads(e.read_text())
    else:
        expected = standalone.find_expected(f)
    if expected is None:
        die("no expected.json next to the file (pass --expect DIR from `stitch write`)")
    if a.rev:
        image = build(p, rev=a.rev, image_only=True, quiet=True)["image"]
    else:
        image = build(p, harness_only=True, image_only=True, quiet=True)["image"]
    t = standalone.try_cpp(p, image, str(rel), timeout=a.timeout, leaks="leak" in expected["kind"])
    v = standalone.verdict(expected, t)
    v["lines"] = standalone.code_lines(f.read_text(errors="replace"))
    v["uses_shim"] = "stitch_standalone.h" in f.read_text(errors="replace")
    if JSON:
        reports.dump({**v, "expected": expected, "output": v["output"][-8000:]})
    else:
        _print_verdict(v, expected, p.rel(f), show=a.show)
        if v["status"] == "same":
            print(f"  {v['lines']} code lines" + (", still uses stitch_standalone.h" if v["uses_shim"] else ", plain library code"))
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
    s.set_defaults(fn=cmd_setup)

    s = sp.add_parser("init", help="create a new project (stitch.toml, Dockerfile, harness/)")
    s.add_argument("dir", nargs="?", default=".")
    s.add_argument("--name")
    g = s.add_mutually_exclusive_group(required=True)
    g.add_argument("--git", help="library git URL")
    g.add_argument("--path", help="local library checkout")
    s.add_argument("--rev", help="default revision (git)")
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

    s = sp.add_parser("probe", help="short fuzzing run + report of what blocks progress")
    s.add_argument("only", nargs="?", help="endpoint names/globs to focus on, comma separated (e.g. 'tree/*,parse')")
    s.add_argument("--secs", type=int, default=60)
    s.set_defaults(fn=cmd_probe)

    s = sp.add_parser("fuzz", help="start a campaign in the background")
    s.add_argument("--name", default="main")
    s.add_argument("--secs", type=int)
    s.add_argument("--jobs", type=int)
    s.add_argument("--only")
    s.add_argument("--fresh", action="store_true", help="discard the existing corpus")
    s.add_argument("--foreground", action="store_true")
    s.set_defaults(fn=cmd_fuzz)

    s = sp.add_parser("status", help="campaign progress and health report")
    s.add_argument("name", nargs="?")
    s.add_argument("--limit", type=int, default=15)
    s.add_argument("--mutations", action="store_true", help="also show which mutation operators find new coverage")
    s.set_defaults(fn=cmd_status)

    s = sp.add_parser("stop", help="stop a campaign")
    s.add_argument("name", nargs="?")
    s.set_defaults(fn=cmd_stop)

    s = sp.add_parser("crashes", help="triage crashes into unique buckets")
    s.add_argument("name", nargs="?")
    s.set_defaults(fn=cmd_crashes)

    s = sp.add_parser("exec", help="run a program (.stp or crash id) with the current harness")
    s.add_argument("program")
    s.add_argument("--trace", action="store_true", help="print every call with its arguments")
    s.add_argument("--timeout", type=int, default=60)
    s.add_argument("--rev", help="run against the library at this revision (e.g. HEAD: is it fixed upstream?)")
    s.set_defaults(fn=cmd_exec)

    s = sp.add_parser("minimize", help="reduce a crashing program")
    s.add_argument("crash")
    s.add_argument("-o", "--output")
    s.add_argument("--secs", type=int, default=120)
    s.set_defaults(fn=cmd_minimize)

    s = sp.add_parser("repro", help="write a self-contained dockerized reproducer")
    s.add_argument("crash")
    s.add_argument("-o", "--output")
    s.add_argument("--no-run", action="store_true", help="do not verify the reproducer")
    s.set_defaults(fn=cmd_repro)

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

    s = sp.add_parser("write", help="turn a crash into a standalone C++ reproducer (harness code pasted in, values filled in)")
    s.add_argument("crash")
    s.add_argument("-o", "--output", help="output directory inside the project (default .stitch/standalone/<id>)")
    s.add_argument("--no-check", action="store_true", help="do not build and run the result")
    s.set_defaults(fn=cmd_write)

    s = sp.add_parser("try", help="build + run a standalone reproducer and compare its crash with the original")
    s.add_argument("file", help=".cpp/.c file inside the project (expected.json is found next to it)")
    s.add_argument("--expect", help="expected.json (or its directory) from `stitch write`")
    s.add_argument("--rev", help="build against the library at this revision")
    s.add_argument("--timeout", type=int, default=60)
    s.add_argument("--show", action="store_true", help="print the program output")
    s.set_defaults(fn=cmd_try)

    s = sp.add_parser("bisect", help="find the library commit that introduced a crash")
    s.add_argument("crash")
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

    s = sp.add_parser("guide", help="print the guide (topics: api, reproducers)")
    s.add_argument("topic", nargs="?", help="api: harness API reference; reproducers: stitch write / try")
    s.add_argument("--spec", action="store_true", help=argparse.SUPPRESS)
    s.set_defaults(fn=cmd_guide)

    s = sp.add_parser("skill", help="install the STITCH agent skill (default: ./.claude/skills/stitch)")
    s.add_argument("dir", nargs="?", help="skills directory to install into")
    s.add_argument("--user", action="store_true", help="install into ~/.claude/skills")
    s.set_defaults(fn=cmd_skill)

    a = ap.parse_args(argv)
    JSON = a.json
    try:
        a.fn(a)
    except (ProjectError, SourceError, docker.DockerError) as e:
        die(str(e))
    except KeyboardInterrupt:
        die("interrupted", 130)


if __name__ == "__main__":
    main()
