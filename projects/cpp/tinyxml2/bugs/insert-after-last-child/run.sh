#!/bin/sh
# Reproduce: SEGV | tinyxml2::XMLNode::InsertAfterChild tinyxml2.cpp:931 | insert_after insert.cpp | insert_after (stub)
# Requires docker (with buildx). Usage: ./run.sh            run the recorded harness program
#                                       ./run.sh --standalone  build + run standalone/ (the bug without the fuzzer)
set -e
cd "$(dirname "$0")"
ID=tinyxml2-c1
# on Linux, run as the calling user so files written into this folder stay yours
U=""; [ "$(uname)" = Linux ] && U="--user $(id -u):$(id -g) -e HOME=/tmp"
docker buildx build --load -q -f stitch/Dockerfile --target toolchain -t stitch-repro-toolchain:$ID stitch >/dev/null
if [ ! -d source ]; then
  git clone --quiet https://github.com/leethomason/tinyxml2 source
  git -C source checkout --quiet 397f22581b6f3969208f2cc41f36d4b39c39ad39
fi
docker buildx build --load -q -f project/Dockerfile --build-context source=source \
  --build-arg STITCH_BASE=stitch-repro-toolchain:$ID -t stitch-repro:$ID project >/dev/null
if [ "$1" = "--standalone" ]; then
  docker run --rm $U -v "$PWD/standalone:/standalone" -w /standalone stitch-repro:$ID \
    sh -c "$(cat standalone/build.sh)"
  exit $?
fi
docker run --rm $U -v "$PWD/project:/project" -v "$PWD:/repro" -w /project stitch-repro:$ID \
  sh -c 'stitch-build --target repro --out /tmp/stitch-build >/dev/null &&
    (timeout -s KILL 300 /tmp/stitch-build/repro --trace /repro/crash.stp; rc=$?;
     if [ $rc = 137 ]; then echo "[stitch] TIMEOUT: still running after 300 s" >&2; fi; exit $rc)'
