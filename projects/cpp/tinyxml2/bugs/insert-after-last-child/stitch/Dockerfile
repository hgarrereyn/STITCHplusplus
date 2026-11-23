# syntax=docker/dockerfile:1.7
# STITCH++ C/C++ image (build context: languages/cpp/).
#   target `toolchain`: clang + sanitizers + runtime + compiler wrappers (builds and runs reproducers)
#   target `base`     : toolchain + the engine library (libstitch_engine.a) for fuzzing
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      clang-18 llvm-18 lld-18 libclang-rt-18-dev libc++-18-dev libc++abi-18-dev \
      build-essential cmake ninja-build meson make autoconf automake libtool pkg-config \
      git ca-certificates curl wget xz-utils unzip python3 python3-pip file gdb \
      zlib1g-dev libssl-dev bison flex gettext autopoint texinfo \
    && rm -rf /var/lib/apt/lists/* \
    && for t in clang clang++ llvm-symbolizer llvm-ar llvm-nm llvm-ranlib llvm-cov llvm-profdata lld ld.lld; do \
         [ -e /usr/bin/$t-18 ] && ln -sf /usr/bin/$t-18 /usr/local/bin/$t; done
COPY runtime /stitch/runtime
COPY bin /stitch/bin
RUN chmod +x /stitch/bin/* && mkdir -p /stitch/lib
ENV PATH=/stitch/bin:$PATH \
    CC=/stitch/bin/stitch-cc \
    CXX=/stitch/bin/stitch-cxx \
    STITCH_SANITIZERS=address \
    STITCH_LANGUAGE=cpp \
    ASAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer \
    STITCH_HOME=/stitch

FROM toolchain AS base
COPY --from=engine /stitch/lib/libstitch_engine.a /stitch/lib/libstitch_engine.a
