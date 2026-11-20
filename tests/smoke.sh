#!/bin/sh
# End-to-end test on examples/demolib: build, check, fuzz, triage, minimize, repro, bisect.
# The demo library has two planted bugs with known introducing commits:
#   "allow 16 character keys" (off-by-one)  and  "debug level hook" (magic value, needs cmplog).
set -eu
cd "$(dirname "$0")/.."
STITCH="$PWD/bin/stitch"
P=examples/demolib
[ -d examples/demolib-src/.git ] || git clone -q examples/demolib-src.bundle examples/demolib-src
fail() { echo "FAIL: $*"; exit 1; }

$STITCH -C $P build >/dev/null || fail build
$STITCH -C $P check --json | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["ok"] and d["endpoints"] == 7, d' || fail check
$STITCH -C $P fuzz --name smoke --secs 90 --fresh --foreground >/dev/null 2>&1 || true
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
$STITCH -C $P --json write smoke:$C1 -o $P/.stitch/standalone/smoke | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["check"]["status"] == "same", d' || fail "write"
$STITCH -C $P try $P/.stitch/standalone/smoke/repro.cpp >/dev/null || fail "try generated"
printf 'int main() { return 0; }\n' > $P/.stitch/standalone/smoke/nocrash.c
set +e; $STITCH -C $P try $P/.stitch/standalone/smoke/nocrash.c >/dev/null; rc=$?; set -e
[ $rc = 1 ] || fail "try must reject a program that does not crash (rc=$rc)"
$STITCH -C $P --json bisect smoke:$C1 | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["first_bad_commit"]["subject"] == "allow 16 character keys", d' || fail "bisect off-by-one"
$STITCH -C $P --json bisect smoke:$C2 | python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["first_bad_commit"]["subject"] == "debug level hook", d' || fail "bisect magic value"
echo "smoke test passed"
