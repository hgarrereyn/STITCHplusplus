#!/bin/sh
# Reproduce: check failed: Valid() agrees with encoding/json | in valid
# Requires docker (with buildx). Usage: ./run.sh            run the recorded harness program
#                                       ./run.sh --standalone  build + run standalone/ (the bug without the fuzzer)
set -e
cd "$(dirname "$0")"
ID=c3
docker buildx build --load -q -f stitch/Dockerfile --target toolchain -t stitch-repro-toolchain:$ID stitch >/dev/null
if [ ! -d source ]; then
  git clone --quiet https://github.com/tidwall/gjson source
  git -C source checkout --quiet f0ee9ebde4b619767ae4ac03e8e42addb530f6bc
fi
docker buildx build --load -q -f project/Dockerfile --build-context source=source \
  --build-arg STITCH_BASE=stitch-repro-toolchain:$ID -t stitch-repro:$ID project >/dev/null
if [ "$1" = "--standalone" ]; then
  docker run --rm -v "$PWD/project:/project" -v "$PWD/standalone:/project/standalone" -w /project \
    stitch-repro:$ID sh standalone/build.sh
  exit $?
fi
docker run --rm -v "$PWD/project:/project" -v "$PWD:/repro" -w /project stitch-repro:$ID \
  sh -c 'stitch-build --target repro --out /tmp/stitch-build >/dev/null && /tmp/stitch-build/repro --trace /repro/crash.stp'
