# syntax=docker/dockerfile:1.7
# STITCH++ Swift image (build context: languages/swift/ plus languages/cpp/runtime as cpp-runtime/,
# assembled by the CLI). Swift harnesses run on the C/C++ runtime through bridge/bridge.cpp.
#   target `toolchain`: Swift 6.2 + the Stitch package + the runtime library (reproducers)
#   target `base`     : toolchain + the engine library (fuzzing)
# Project Dockerfiles put the package source in /src: COPY --from=source . /src
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS swift
ARG SWIFT_VERSION=6.2.3
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      binutils ca-certificates curl git gnupg2 libc6-dev libcurl4-openssl-dev libedit2 libgcc-13-dev libncurses-dev \
      libpython3-dev libsqlite3-0 libstdc++-13-dev libxml2-dev libz3-dev pkg-config python3 tzdata unzip zlib1g-dev \
      llvm-18 \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /usr/bin/llvm-symbolizer-18 /usr/local/bin/llvm-symbolizer
RUN arch="$(uname -m)"; suffix="$([ "$arch" = aarch64 ] && echo -aarch64 || true)"; \
    curl -fsSL "https://download.swift.org/swift-${SWIFT_VERSION}-release/ubuntu2404${suffix}/swift-${SWIFT_VERSION}-RELEASE/swift-${SWIFT_VERSION}-RELEASE-ubuntu24.04${suffix}.tar.gz" \
      | tar -xz -C / --strip-components=1 \
    && swift --version

FROM swift AS toolchain
ENV STITCH_LANGUAGE=swift \
    STITCH_HOME=/stitch \
    ASAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer \
    PATH=/stitch/bin:$PATH
COPY cpp-runtime /stitch/cpp-runtime
COPY bridge /stitch/swift/bridge
COPY runtime /stitch/swift/Stitch
COPY bin /stitch/bin
# the C/C++ runtime + bridge, with ASan and without coverage instrumentation
RUN set -e; mkdir -p /stitch/swift/lib /tmp/rt && cd /tmp/rt \
    && F="-std=c++17 -O2 -g -fPIC -fsanitize=address -fno-omit-frame-pointer -I/stitch/cpp-runtime/include -I/stitch/swift/Stitch/Sources/CStitch/include" \
    && clang++ $F -c /stitch/cpp-runtime/src/runtime.cpp -o runtime.o \
    && clang++ $F -Dmain=stitch_repro_main -c /stitch/cpp-runtime/src/main_repro.cpp -o main_repro.o \
    && clang++ $F -c /stitch/swift/bridge/bridge.cpp -o bridge.o \
    && ar rcs /stitch/swift/lib/libstitch_swift_rt.a runtime.o main_repro.o bridge.o \
    && rm -rf /tmp/rt && chmod +x /stitch/bin/*

FROM toolchain AS base
COPY --from=engine /stitch/lib/libstitch_engine.a /stitch/lib/libstitch_engine.a
