# syntax=docker/dockerfile:1.7
# STITCH++ engine image (build context: engine/). Holds only build outputs:
#   /stitch/lib/libstitch_engine.a   linked into C/C++ harnesses (in-process, fork server)
#   /stitch/lib/libstitch_engine.so  loaded by Rust harnesses (in-process, fork server)
#   /stitch/bin/stitch-engine        drives worker runtimes (every other language)
# Language images copy from it; it is never run on its own.
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends build-essential curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo PATH=/opt/cargo/bin:$PATH
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.93.1
COPY . /src/engine
RUN --mount=type=cache,target=/opt/cargo/registry \
    --mount=type=cache,target=/src/engine/target \
    cd /src/engine && rm -f rust-toolchain.toml && cargo build --release \
    && mkdir -p /stitch/lib /stitch/bin \
    && cp target/release/libstitch_engine.a target/release/libstitch_engine.so /stitch/lib/ \
    && cp target/release/stitch-engine /stitch/bin/

FROM scratch
COPY --from=build /stitch /stitch
