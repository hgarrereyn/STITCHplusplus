#!/bin/sh
# End-to-end test on examples/demolib: build, check, fuzz, triage, minimize, repro, bisect.
# STITCH_SMOKE_SECS (default 90): fuzzing time; raise it on a busy machine.
# The demo library has two planted bugs with known introducing commits:
#   "allow 16 character keys" (off-by-one)  and  "debug level hook" (magic value, needs cmplog).
set -eu
cd "$(dirname "$0")/.."
STITCH="$PWD/bin/stitch"
P=examples/demolib
[ -d examples/demolib-src/.git ] || git clone -q examples/demolib-src.bundle examples/demolib-src
fail() { echo "FAIL: $*"; exit 1; }

$STITCH -C $P build >/dev/null || fail build
$STITCH -C $P check --json | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["ok"] and d["endpoints"] == 8, d' || fail check
$STITCH -C $P fuzz --name smoke --secs "${STITCH_SMOKE_SECS:-90}" --fresh --foreground >/dev/null 2>&1 || true
$STITCH -C $P crashes smoke --json > /tmp/stitch-smoke-crashes.json || fail crashes
python3 - <<'EOF' || fail "expected both planted bugs"
import json
b = json.load(open("/tmp/stitch-smoke-crashes.json"))["buckets"]
lines = {f.split()[1].split(":")[1] for x in b for f in x["frames"][:1]}
print("crash lines in msg.c:", sorted(lines))
assert {"47", "51"} <= lines, lines
EOF
for id in $(python3 -c 'import json; print(" ".join(b["id"] for b in json.load(open("/tmp/stitch-smoke-crashes.json"))["buckets"]))'); do
  $STITCH -C $P minimize smoke:$id --secs 30 >/dev/null || fail "minimize $id"
done
C1=$(python3 -c 'import json; b=json.load(open("/tmp/stitch-smoke-crashes.json"))["buckets"]; print([x["id"] for x in b if x["frames"][0].endswith(":47:40")][0])')
C2=$(python3 -c 'import json; b=json.load(open("/tmp/stitch-smoke-crashes.json"))["buckets"]; print([x["id"] for x in b if ":51:" in x["frames"][0]][0])')
$STITCH -C $P repro smoke:$C1 -o /tmp/stitch-smoke-repro > /tmp/stitch-smoke-repro.log 2>&1 || fail repro
grep -q "verified: same crash" /tmp/stitch-smoke-repro/verified.txt || fail "repro not verified"
SA="$PWD/$P/.stitch/standalone/smoke"   # absolute: -C resolves relative paths inside the project
$STITCH -C $P --json write smoke:$C1 -o "$SA" | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["check"]["status"] == "same", d' || fail "write"
$STITCH -C $P try "$SA/repro.cpp" >/dev/null || fail "try generated"
printf 'int main() { return 0; }\n' > "$SA/nocrash.c"
set +e; $STITCH -C $P try "$SA/nocrash.c" >/dev/null; rc=$?; set -e
[ $rc = 1 ] || fail "try must reject a program that does not crash (rc=$rc)"
# a program with a Val parameter (append_all) replays from its recorded choices
python3 - "$STITCH" "$P" "$PWD/$P/.stitch/standalone/smoke-val" <<'EOF' || fail "write (Val program)"
import json, pathlib, subprocess, sys
stitch, p, out = sys.argv[1:]
camp = pathlib.Path(p) / ".stitch/campaigns/smoke"
progs = [f for f in sorted(camp.glob("w*/corpus/*")) + sorted(camp.glob("w*/corpus/.*")) if f.is_file() and b"append_all" in f.read_bytes()]
w = lambda f, *a: json.loads(subprocess.run([stitch, "-C", p, "--json", "write", str(f.resolve()), "-o", out, *a], capture_output=True, text=True).stdout)
for f in progs[:12]:
    if "stitch::Val<Chunks" in pathlib.Path(w(f, "--no-check")["cpp"]).read_text():
        assert w(f)["check"]["status"] == "same"
        sys.exit(0)
sys.exit(f"no corpus program runs append_all ({len(progs)} mention it)")
EOF
$STITCH -C $P --json bisect smoke:$C1 | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["first_bad_commit"]["subject"] == "allow 16 character keys", d' || fail "bisect off-by-one"
$STITCH -C $P --json bisect smoke:$C2 | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["first_bad_commit"]["subject"] == "debug level hook", d' || fail "bisect magic value"
$STITCH -C $P --json cov smoke --crashes --no-html | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["runs"]["programs"] > 0 and d["runs"]["invalid"] == 0, d["runs"]
h, f = d["library"]["lines"]
assert f > 0 and h / f > 0.5, d["library"]
assert any(x["file"].endswith("msg.c") for x in d["library"]["files"])
' || fail "cov"
grep -q "^SF:$(cd $P && pwd)/harness/" $P/.stitch/cov/smoke/lcov.info || fail "cov: lcov paths"
echo "smoke test passed"
