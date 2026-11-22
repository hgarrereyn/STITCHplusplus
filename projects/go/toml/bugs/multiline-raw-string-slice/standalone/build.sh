set -e
stitch-build >/dev/null
d=/project/.stitch/build/app/mod/cmd/stitchtry && rm -rf $d && mkdir -p $d && cp standalone/repro.go $d/main.go && cd /project/.stitch/build/app/mod && GOCACHE=/project/.stitch/gocache go build -o /tmp/stitch-repro ./cmd/stitchtry 2>/tmp/build.log || { cat /tmp/build.log >&2; rm -rf $d; exit 201; }; rm -rf $d; echo '[repro] ---- run ----' >&2; timeout 60 /tmp/stitch-repro >&2
