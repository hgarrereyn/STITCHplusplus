#!/usr/bin/env python3
"""Fidelity test for `stitch write`: convert corpus programs (and crashes) of each project to
standalone C++, build and run them, and check they behave like the fuzzer's run:
  - compiles against the library alone
  - every call has the same outcome (ok / bail + reason)
  - same final result (no crash, or the same crash kind + innermost library frame)

usage: tests/standalone_fidelity.py [-n 40] [--seed 1] PROJECT_DIR...
"""
import argparse
import json
import random
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "cli"))
from stitch_fuzz import reports, standalone  # noqa: E402
from stitch_fuzz.cli import build  # noqa: E402
from stitch_fuzz.project import Project  # noqa: E402


def programs(p, n, rng):
    corpus, crashes = [], []
    for c in p.campaigns():
        for w in sorted((p.campaign_dir(c)).glob("w*")):
            corpus += [f for f in (w / "corpus").glob("*") if f.is_file() and not f.name.startswith(".")]
            crashes += [f for f in (w / "crashes").glob("*") if f.is_file() and not f.name.startswith(".")]
    rng.shuffle(corpus)
    rng.shuffle(crashes)
    k = min(len(crashes), n // 4)
    return corpus[:n - k] + crashes[:k]


def run_project(root: Path, n: int, seed: int):
    p = Project(root)
    rng = random.Random(seed)
    progs = programs(p, n, rng)
    info = build(p, harness_only=True, quiet=True)
    assert info["result"].get("ok"), f"{p.name}: harness build failed"
    image = info["image"]
    work = p.state_dir / "fidelity"
    import shutil
    shutil.rmtree(work, ignore_errors=True)
    plans = standalone.plan_many(p, image, progs, work / "plans")
    index = standalone.HarnessIndex(p.root / "harness")
    srcs, rows = [], []
    for i, (plan, out, rc) in enumerate(plans):
        rep = reports.parse_report(out, rc, rc == 124)
        row = {"i": i, "program": p.rel(progs[i]), "orig": rep}
        try:
            cpp, meta = standalone.write_cpp(index, plan, [f"fidelity {i}"], "")
        except Exception as e:  # noqa: BLE001
            row["result"] = f"write error: {e}"
            rows.append(row)
            continue
        d = work / str(i)
        d.mkdir(parents=True)
        (d / "repro.cpp").write_text(cpp)
        row["calls"] = meta["calls"]
        row["lines"] = cpp.count("\n")
        row["src"] = d / "repro.cpp"
        srcs.append(d / "repro.cpp")
        rows.append(row)
    results = dict(zip(srcs, standalone.try_many(p, image, srcs))) if srcs else {}
    for row in rows:
        if "src" not in row:
            continue
        t = results[row.pop("src")]
        plan = work / "plans" / f"{row['i']}.jsonl"
        want = standalone.call_outcomes_plan(plan)
        if t["status"] == "compile_error":
            row["result"] = "compile error"
            row["detail"] = t["errors"][:3]
            continue
        got = t["calls"]
        mism = [(k, want[k], got.get(k, "crash" if t["status"] == "crash" else "missing")) for k in want
                if want[k] != got.get(k, "crash" if t["status"] == "crash" else "missing")]
        orig = row["orig"]
        if orig["crashed"]:
            final_ok = t["status"] == "crash" and standalone.compare(standalone.crash_key(orig), standalone.crash_key(t["report"])) == "same"
        else:
            final_ok = t["status"] == "no_crash"
        if not mism and final_ok:
            row["result"] = "faithful"
        else:
            row["result"] = "call outcome differs" if mism else "final result differs"
            row["detail"] = {"calls": mism[:3], "orig": orig["kind"] or "no crash",
                             "got": (t.get("report") or {}).get("kind") or t["status"],
                             "tail": [l for l in t["output"].splitlines() if l.strip()][-6:]}
    return p.name, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("projects", nargs="+")
    ap.add_argument("-n", type=int, default=40)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--json", help="write the full report here")
    a = ap.parse_args()
    report = {}
    total = Counter()
    for root in a.projects:
        name, rows = run_project(Path(root).resolve(), a.n, a.seed)
        c = Counter(r["result"] if not r["result"].startswith("write error") else "write error" for r in rows)
        total.update(c)
        report[name] = rows
        crashes = sum(1 for r in rows if r["orig"]["crashed"])
        calls = sum(r.get("calls", 0) for r in rows)
        print(f"{name:10s} {len(rows):3d} programs ({crashes} crashing, {calls} calls): " + ", ".join(f"{k} {v}" for k, v in c.most_common()))
        for r in rows:
            if r["result"] != "faithful":
                print(f"    [{r['i']}] {r['result']}: {json.dumps(r.get('detail'))[:400]}")
    print("TOTAL " + ", ".join(f"{k} {v}" for k, v in total.most_common()))
    if a.json:
        Path(a.json).write_text(json.dumps(report, indent=1, default=str))
    sys.exit(0 if set(total) <= {"faithful"} else 1)


if __name__ == "__main__":
    main()
