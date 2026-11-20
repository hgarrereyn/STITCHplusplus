# syntax=docker/dockerfile:1.7
# STITCH v2 base image.
#   target `toolchain`: clang + sanitizers + STITCH runtime + compiler wrappers (enough to build
#                       and run reproducers)
#   target `base`     : toolchain + the LibAFL engine (libstitch_engine.a) for fuzzing
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
COPY docker/bin /stitch/bin
RUN chmod +x /stitch/bin/* && mkdir -p /stitch/lib
ENV PATH=/stitch/bin:$PATH \
    CC=/stitch/bin/stitch-cc \
    CXX=/stitch/bin/stitch-cxx \
    STITCH_SANITIZERS=address \
    ASAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer \
    STITCH_HOME=/stitch

FROM toolchain AS engine-build
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:$PATH
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.93.1
COPY engine /src/engine
RUN --mount=type=cache,target=/opt/cargo/registry \
    --mount=type=cache,target=/src/engine/target \
    cd /src/engine && rm -f rust-toolchain.toml && cargo build --release \
    && cp target/release/libstitch_engine.a /stitch/lib/libstitch_engine.a

FROM toolchain AS base
COPY --from=engine-build /stitch/lib/libstitch_engine.a /stitch/lib/libstitch_engine.a
