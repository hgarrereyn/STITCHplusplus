#!/usr/bin/env python3
"""End-to-end test of each language on its demo project (examples/<lang>-demo).

    tests/languages.py                  every language
    tests/languages.py python go        some of them
    tests/languages.py --secs 120 rust  longer fuzzing

Per language: build, check, probe, fuzz, crashes (a planted bug in the library), minimize, exec --trace,
write (the standalone file reproduces the crash; a program with a Val parameter replays), try (a file that does not crash is rejected),
repro (a verified Docker folder), report (a plain reproducer crashes in its own Dockerfile) and cov
(lcov with library lines).
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STITCH = str(ROOT / "bin" / "stitch")

# language -> (library file that a planted bug crashes in, a program that does not crash)
DEMOS = {
    "python": ("__init__.py", "nocrash.py", 'print("ok")\n'),
    "javascript": ("index.js", "nocrash.js", 'console.log("ok");\n'),
    "java": ("Store.java", "NoCrash.java", "public class NoCrash { public static void main(String[] a) {} }\n"),
    "go": ("kv.go", "nocrash.go", "package main\n\nfunc main() {}\n"),
    "rust": ("lib.rs", "nocrash.rs", "fn main() {}\n"),
    "ruby": ("kvrb.rb", "nocrash.rb", 'puts "ok"\n'),
    "swift": ("KV.swift", "nocrash.swift", "import Stitch\n#if canImport(Glibc)\nimport Glibc\n#endif\nexit(reproMain {})\n"),
    "objc": ("KVStore.m", "nocrash.m", "#import <Foundation/Foundation.h>\n\nint main(void) { return 0; }\n"),
}

# a plain reproducer of the planted '#!x' bug in load() (library code only, as after shrinking): `stitch report`
PLAIN = {
    "python": ("min.py", 'from kvlib import load\n\nload("k=#!x")\n'),
    "javascript": ("min.js", 'const { load } = require("kvjs");\n\nload("k=#!x");\n'),
    "java": ("Min.java", "import demo.kv.Store;\n\npublic class Min {\n    public static void main(String[] a) {\n"
                         "        Store.load(\"k=#!x\");\n    }\n}\n"),
    "go": ("min.go", 'package main\n\nimport "example.com/kvgo"\n\nfunc main() {\n\tkvgo.Load("k=#!x")\n}\n'),
    "rust": ("min.rs", 'fn main() {\n    let _ = kvrs::load("k=#!x");\n}\n'),
    "ruby": ("min.rb", 'require "kvrb"\n\nKvrb.load("k=#!x")\n'),
    "swift": ("min.swift", 'import KVSwift\n\n_ = try? load("k=#!x")\n'),
    "objc": ("min.m", '#import <Foundation/Foundation.h>\n#import "KVStore.h"\n\nint main(void) {\n'
                      '    @autoreleasepool {\n        [KVStore storeWithText:@"k=#!x" error:NULL];\n    }\n    return 0;\n}\n'),
}

# how each writer replays a Val parameter
VAL_LITERAL = {"python": "stitch.value(", "javascript": "stitch.value(", "java": "Standalone.value(",
               "go": "stitch.Value(", "rust": "Val::replay(", "ruby": "Stitch.value(", "swift": "Val(replay:",
               "objc": "STValue("}


class Fail(Exception):
    pass


def run(project, *args, check=True, timeout=1800):
    r = subprocess.run([STITCH, "-C", str(project), "--json", *args], capture_output=True, text=True,
                       timeout=timeout)
    if check and r.returncode != 0:
        raise Fail(f"stitch {' '.join(args)} exited {r.returncode}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
    return r


def js(r):
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError:
        raise Fail(f"not JSON: {r.stdout[-1000:]}\n{r.stderr[-1000:]}")


def test(lang, secs):
    lib_file, nocrash_name, nocrash = DEMOS[lang]
    p = ROOT / "examples" / f"{lang}-demo"
    step = lambda s: print(f"  {lang}: {s}", flush=True)

    step("build")
    run(p, "build")
    d = js(run(p, "check"))
    if not d.get("ok") or not d.get("endpoints"):
        raise Fail(f"check: {d}")

    step("probe")
    d = js(run(p, "probe", "--secs", "10", timeout=600))
    if not d.get("execs") or not d.get("edges_covered") or not isinstance(d.get("report"), list):
        raise Fail(f"probe: {({k: d.get(k) for k in ('execs', 'edges_covered', 'corpus')})}")

    step(f"fuzz {secs}s")
    run(p, "fuzz", "--name", "lang-test", "--secs", str(secs), "--fresh", "--foreground", check=False,
        timeout=secs + 600)
    buckets = js(run(p, "crashes", "lang-test"))["buckets"]
    in_lib = [b for b in buckets if any(lib_file in f for f in b["frames"])]
    if not in_lib:
        raise Fail(f"no crash in {lib_file}: {[(b['kind'], b['frames'][:1]) for b in buckets]}")
    c = "lang-test:" + in_lib[0]["id"]
    step(f"{len(buckets)} buckets; {c}: {in_lib[0]['kind']} in {in_lib[0]['frames'][0]}")

    step("minimize, exec --trace")
    run(p, "minimize", c, "--secs", "20")
    d = js(run(p, "exec", c, "--trace", check=False))
    if not d.get("crashed") or d.get("kind") != in_lib[0]["kind"]:
        raise Fail(f"exec --trace: {d.get('kind')} (expected {in_lib[0]['kind']})")

    step("write, try")
    out = p / ".stitch" / "standalone" / "lang-test"
    shutil.rmtree(out, ignore_errors=True)
    w = js(run(p, "write", c, "-o", str(out)))
    if w.get("check", {}).get("status") != "same":
        raise Fail(f"write: {w}")
    run(p, "try", w["file"])
    (out / nocrash_name).write_text(nocrash)
    r = run(p, "try", str(out / nocrash_name), check=False)
    if r.returncode != 1:
        raise Fail(f"try must reject a program that does not crash (rc={r.returncode})\n{r.stdout[-1000:]}")

    step("write a program with a Val parameter")
    corpus = p / ".stitch" / "campaigns" / "lang-test"
    stub = b"putAll" if lang == "java" else b"put_all"
    progs = [f for f in sorted(corpus.glob("w*/corpus/*")) + sorted(corpus.glob("w*/corpus/.*"))
             if f.is_file() and stub in f.read_bytes()]
    out = p / ".stitch" / "standalone" / "lang-test-val"
    w = None
    for prog in progs[:12]:  # a program whose call actually runs (not skipped for a missing object)
        shutil.rmtree(out, ignore_errors=True)
        w = js(run(p, "write", str(prog), "-o", str(out), "--no-check"))
        if VAL_LITERAL[lang] in Path(w["file"]).read_text():
            break
    else:
        raise Fail(f"none of {len(progs)} corpus programs that mention {stub.decode()} runs it")
    w = js(run(p, "write", str(prog), "-o", str(out)))
    if w.get("check", {}).get("status") != "same":
        raise Fail(f"write (Val program): {w}")

    step("repro")
    with tempfile.TemporaryDirectory() as t:
        run(p, "repro", c, "-o", f"{t}/r")
        if "verified: same crash" not in Path(f"{t}/r/verified.txt").read_text():
            raise Fail("repro not verified")

    step("report")
    out = p / ".stitch" / "standalone" / "lang-test-plain"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    name, code = PLAIN[lang]
    (out / name).write_text(code)
    with tempfile.TemporaryDirectory() as t:
        d = js(run(p, "report", str(out / name), "-o", f"{t}/r", check=False))
        if (d.get("check") or {}).get("status") != "crash":
            raise Fail(f"report: the plain reproducer does not crash in its own Dockerfile: {d}")
        for f in ("Dockerfile", "run.sh", "ISSUE.md"):
            if not (Path(t) / "r" / f).exists():
                raise Fail(f"report: no {f}")

    step("cov")
    d = js(run(p, "cov", "lang-test", "--crashes", "--no-html"))
    hit, found = d["library"]["lines"]
    if d["runs"]["programs"] == 0 or d["runs"]["invalid"] or not found or hit / found < 0.5:
        raise Fail(f"cov: runs {d['runs']}, library lines {hit}/{found}")
    step(f"cov: {d['runs']['programs']} programs, library lines {hit}/{found}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("langs", nargs="*", help=" ".join(DEMOS))
    ap.add_argument("--secs", type=int, default=60)
    a = ap.parse_args()
    if set(a.langs) - set(DEMOS):
        ap.error(f"unknown language: {' '.join(set(a.langs) - set(DEMOS))}")
    failed = []
    for lang in a.langs or DEMOS:
        print(f"{lang}:", flush=True)
        try:
            test(lang, a.secs)
            print(f"  {lang}: passed", flush=True)
        except (Fail, subprocess.TimeoutExpired) as e:
            print(f"  {lang}: FAIL {e}", flush=True)
            failed.append(lang)
    if failed:
        sys.exit(f"failed: {' '.join(failed)}")
    print("all languages passed")


if __name__ == "__main__":
    main()
