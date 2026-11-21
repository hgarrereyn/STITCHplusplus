#!/bin/sh
# Reproduce: Negative value is not representable | CCMModeWorker.init CCM.swift:117 | CCMModeWorker.__allocating_init CCM.swift | CCM.worker CCM.swift
# Requires docker (with buildx). Usage: ./run.sh            run the recorded harness program
#                                       ./run.sh --standalone  build + run standalone/ (the bug without the fuzzer)
set -e
cd "$(dirname "$0")"
ID=cryptoswift-c2
docker buildx build --load -q -f stitch/Dockerfile --target toolchain -t stitch-repro-toolchain:$ID stitch >/dev/null
if [ ! -d source ]; then
  git clone --quiet https://github.com/krzyzanowskim/CryptoSwift source
  git -C source checkout --quiet 19b3c3ceed117c5cc883517c4e658548315ba70b
fi
docker buildx build --load -q -f project/Dockerfile --build-context source=source \
  --build-arg STITCH_BASE=stitch-repro-toolchain:$ID -t stitch-repro:$ID project >/dev/null
if [ "$1" = "--standalone" ]; then
  docker run --rm -v "$PWD/project:/project" -v "$PWD/standalone:/project/standalone" -w /project \
    stitch-repro:$ID sh standalone/build.sh
  exit $?
fi
docker run --rm -v "$PWD/project:/project" -v "$PWD:/repro" -w /project stitch-repro:$ID \
  sh -c 'stitch-build --target repro --out /tmp/stitch-build >/dev/null &&
    (timeout -s KILL 300 /tmp/stitch-build/repro --trace /repro/crash.stp; rc=$?;
     if [ $rc = 137 ]; then echo "[stitch] TIMEOUT: still running after 300 s" >&2; fi; exit $rc)'
