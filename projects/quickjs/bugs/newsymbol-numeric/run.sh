#!/bin/sh
# Reproduce: ABRT | JS_NewSymbolFromAtom quickjs.c:3277 | JS_NewSymbol quickjs.c | new_symbol values.cpp
# Requires docker (with buildx). Usage: ./run.sh            run the recorded harness program
#                                       ./run.sh --standalone  build + run standalone/ (plain C/C++ against the library)
set -e
cd "$(dirname "$0")"
ID=c5
docker buildx build --load -q -f stitch/docker/base.Dockerfile --target toolchain -t stitch-repro-toolchain:$ID stitch >/dev/null
if [ ! -d source ]; then
  git clone --quiet https://github.com/quickjs-ng/quickjs source
  git -C source checkout --quiet 97d23e51dcc835e42c4cd419fb4adee13baf0074
fi
docker buildx build --load -q -f project/Dockerfile --build-context source=source \
  --build-arg STITCH_BASE=stitch-repro-toolchain:$ID -t stitch-repro:$ID project >/dev/null
if [ "$1" = "--standalone" ]; then
  docker run --rm -v "$PWD/standalone:/standalone" -w /standalone stitch-repro:$ID \
    sh -c "$(cat standalone/build.sh)"
  exit $?
fi
docker run --rm -v "$PWD/project:/project" -v "$PWD:/repro" -w /project stitch-repro:$ID \
  sh -c 'stitch-build --target repro --out /tmp/stitch-build >/dev/null && /tmp/stitch-build/repro --trace /repro/crash.stp'
