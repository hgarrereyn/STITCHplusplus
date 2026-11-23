"""Bisect the library history for the commit that introduced a crash.

Each tested revision gets its own project image (Docker layer caching makes unchanged steps
free) and a reproducer binary built from the *current* harness. Revisions where the library or
the harness does not build are skipped.
"""
import hashlib
import json
import re
import subprocess
import time
from pathlib import Path

from . import docker, reports
from .source import Source, SourceError


def _git(repo, *args, check=True):
    r = subprocess.run(["git", *args], cwd=repo, text=True, capture_output=True)
    if check and r.returncode != 0:
        raise SourceError(f"git {' '.join(args)}: {r.stderr.strip()[-500:]}")
    return (r.stdout + r.stderr).strip()


def _first_commit(out, term):
    """The commit `git bisect` settled on, if it did: "<sha> is the first bad commit" (newer git
    quotes the term: "<sha> is the first 'bad' commit")."""
    m = re.search(rf"([0-9a-f]{{7,64}}) is the first '?{term}'? commit", out)
    return m.group(1) if m else None


def bisect(p, crash: Path, good=None, bad=None, strict=False, max_auto=8, verbose=True, fixed=None):
    from .cli import in_project, run_repro

    def log(msg):
        if verbose:
            print(f"[bisect] {msg}", flush=True)

    src = Source(p)
    if not src.is_git:
        raise SourceError("bisect needs a git source ([source] git = URL, or a local path that is a git checkout)")
    base = docker.ensure_base(p.language, p.sanitizers)
    crash_rel = in_project(p, crash)
    chash = hashlib.sha1(crash.read_bytes()).hexdigest()[:12]
    bdir = p.state_dir / "bisect"
    bdir.mkdir(exist_ok=True)
    cache_f = bdir / f"results-{chash}.json"
    cache = json.loads(cache_f.read_text()) if cache_f.exists() else {}
    target = {"sig": None}

    def test(commit):
        if commit in cache:
            r = cache[commit]
        else:
            r = _test_uncached(commit)
            cache[commit] = r
            cache_f.write_text(json.dumps(cache, indent=1))
        if r["status"] in ("bad", "good", "skip") and r.get("signature") and target["sig"] and r["status"] == "bad":
            if r["signature"] != target["sig"]:
                r = dict(r, status="skip" if strict else "bad", note="different crash: " + r["signature"])
        why = ""
        if r["status"] == "skip" and r.get("detail"):  # the last line that says something
            lines = [l.strip() for l in r["detail"].splitlines() if l.strip() and not l.strip().startswith("---")]
            why = f": {lines[-1][:160]}" if lines else ""
        log(f"{commit[:12]}  {r['status']:5s} {r.get('note') or r.get('signature') or ''}{why}")
        return r

    def _test_uncached(commit):
        try:
            ctx, ident, _ = src.materialize(commit)
            image = docker.build_project_image(p, base, ctx, ident, quiet=True)
        except (docker.DockerError, SourceError) as e:
            return {"status": "skip", "note": "library does not build", "final": True, "detail": str(e)[-400:]}
        outdir = f".stitch/bisect/{commit[:12]}"
        r = docker.run(p, image, ["stitch-build", "--json", "--target", "repro", "--out", outdir], capture=True)
        if r.returncode != 0:
            return {"status": "skip", "note": "harness does not compile against this version", "final": True,
                    "detail": docker.build_errors(r, 600)}
        output, code, to = run_repro(p, image, f"{outdir}/repro", crash_rel)
        rep = reports.parse_report(output, code, to)
        if rep["crashed"] and not to and not re.search(r"^\[stitch\] #\d+ ", output, re.M):
            # failed before the first call: the harness does not load against this version (an import
            # or a module format the library changed), which says nothing about the bug
            return {"status": "skip", "note": "harness does not load against this version", "final": True,
                    "detail": output[-600:]}
        if not rep["crashed"]:
            return {"status": "good", "final": True}
        return {"status": "bad", "signature": rep["signature_fn"], "final": True}

    repo = src.repo()
    if src.git_url:
        src.fetch()
    bad_c = src.resolve(bad)
    log(f"checking the crash at {bad or src.default_rev} ({bad_c[:12]})")
    rb = test(bad_c)
    if rb["status"] != "bad":
        return {"ok": False, "error": f"the crash does not reproduce at {bad_c[:12]} ({rb['status']}: {rb.get('note', '')})"}
    target["sig"] = rb["signature"]  # function-level signature (line numbers move between commits)

    if fixed is not None:
        return _bisect_fix(p, src, repo, bdir, bad_c, fixed or "HEAD", test, log)

    tested = []
    good_c = None
    if good:
        good_c = src.resolve(good)
        rg = test(good_c)
        if rg["status"] != "good":
            return {"ok": False, "error": f"--good {good} is not good ({rg['status']}: {rg.get('note') or rg.get('signature')})"}
    else:
        # search older revisions: tags first (newest to oldest, exponentially spaced), then ancestors
        tags = [t for t in _git(repo, "tag", "--merged", bad_c, "--sort=-creatordate").split("\n") if t]
        cands = []
        for t in tags:
            c = src.resolve(t)
            if c != bad_c and c not in cands:
                cands.append(c)
        if not cands:
            for k in range(0, 12):
                r = subprocess.run(["git", "rev-parse", "--verify", "--quiet", f"{bad_c}~{2 ** k}"], cwd=repo, text=True, capture_output=True)
                if r.returncode == 0:
                    cands.append(r.stdout.strip())
        idx, i = [], 0
        while i < len(cands) and len(idx) < max_auto:
            idx.append(i)
            i = 2 * i + 1
        if cands and len(idx) < max_auto and (len(cands) - 1) not in idx:
            idx.append(len(cands) - 1)
        for i in idx:
            r = test(cands[i])
            tested.append((cands[i], r["status"]))
            if r["status"] == "good":
                good_c = cands[i]
                break
        if not good_c:
            oldest = cands[idx[-1]][:12] if idx else bad_c[:12]
            return {"ok": True, "found": False, "message": (f"crash present (or unbuildable) in every revision tried back to {oldest}"
                                                     " (an older bug: --good REV; fixed in a later release: --fixed finds the fix)"),
                    "tested": [{"commit": c, "status": s} for c, s in tested]}

    # git bisect in a private clone (no checkout needed: we build from exported trees)
    work = bdir / "repo"
    if not work.exists():
        _git(bdir, "clone", "--quiet", "--no-checkout", str(repo), str(work))
    else:
        _git(work, "fetch", "--quiet", str(repo), "+refs/heads/*:refs/remotes/origin/*", "--tags", check=False)
    _git(work, "bisect", "reset", check=False)
    out = _git(work, "bisect", "start", "--no-checkout", bad_c, good_c)
    first_bad = None
    steps = 0
    while steps < 64:
        steps += 1
        first_bad = _first_commit(out, "bad")
        if first_bad:
            break
        if "only 'skip'ped commits left" in out:
            break
        head = _git(work, "rev-parse", "BISECT_HEAD")
        r = test(head)
        out = _git(work, "bisect", r["status"] if r["status"] in ("good", "bad") else "skip", check=False)
    log_out = _git(work, "bisect", "log", check=False)
    _git(work, "bisect", "reset", check=False)
    res = {"ok": True, "found": bool(first_bad), "good": good_c, "bad": bad_c, "signature": target["sig"], "steps": steps}
    if first_bad:
        res["first_bad_commit"] = src.describe(first_bad)
        msg = res["first_bad_commit"]
        log(f"first bad commit: {msg['commit'][:12]} {msg['subject']} ({msg['author']}, {msg['date'][:10]})" + (f"\n         {msg['url']}" if msg.get("url") else ""))
    else:
        res["message"] = "could not narrow down to one commit (skipped revisions); see bisect_log"
        res["bisect_log"] = log_out
        log(res["message"])
    return res


def _bisect_fix(p, src, repo, bdir, broken_c, fixed_rev, test, log):
    """Find the first commit after `broken_c` where the crash no longer reproduces."""
    if src.git_url:
        src.fetch()
    fixed_c = src.resolve(fixed_rev)
    rf = test(fixed_c)
    if rf["status"] != "good":
        return {"ok": True, "found": False, "message": f"still crashes at {fixed_rev} ({fixed_c[:12]}): not fixed"}
    work = bdir / "repo"
    if not work.exists():
        _git(bdir, "clone", "--quiet", "--no-checkout", str(repo), str(work))
    else:
        _git(work, "fetch", "--quiet", str(repo), "+refs/heads/*:refs/remotes/origin/*", "--tags", check=False)
    _git(work, "bisect", "reset", check=False)
    out = _git(work, "bisect", "start", "--no-checkout", "--term-old=broken", "--term-new=fixed", fixed_c, broken_c)
    first = None
    steps = 0
    while steps < 64:
        steps += 1
        first = _first_commit(out, "fixed")
        if first:
            break
        if "only 'skip'ped commits left" in out:
            break
        head = _git(work, "rev-parse", "BISECT_HEAD")
        r = test(head)
        term = {"bad": "broken", "good": "fixed"}.get(r["status"], "skip")
        out = _git(work, "bisect", term, check=False)
    _git(work, "bisect", "reset", check=False)
    res = {"ok": True, "found": bool(first), "broken": broken_c, "fixed": fixed_c, "steps": steps}
    if first:
        res["first_fixed_commit"] = src.describe(first)
        m = res["first_fixed_commit"]
        log(f"first fixed commit: {m['commit'][:12]} {m['subject']} ({m['author']}, {m['date'][:10]})" + (f"\n         {m['url']}" if m.get("url") else ""))
    return res
