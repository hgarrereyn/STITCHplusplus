# syntax=docker/dockerfile:1.7
# STITCH Rust image (build context: languages/rust/).
#   target `toolchain`: nightly Rust (ASan, SanitizerCoverage) + llvm tools + the stitch crate
#                       (/stitch/rust/stitch) (reproducers)
#   target `base`     : toolchain + stitch-engine (fuzzing)
# Project Dockerfiles put the library source in /src: COPY --from=source . /src
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS toolchain
ARG RUST_NIGHTLY=nightly-2025-06-15
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git python3 build-essential pkg-config libssl-dev llvm-18 \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /usr/bin/llvm-symbolizer-18 /usr/local/bin/llvm-symbolizer \
    && ln -sf /usr/bin/llvm-cxxfilt-18 /usr/local/bin/llvm-cxxfilt
ENV RUSTUP_HOME=/opt/rustup CARGO_HOME=/opt/cargo PATH=/stitch/bin:/opt/cargo/bin:$PATH
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain ${RUST_NIGHTLY} \
    && rustup component add llvm-tools \
    && rustc --version && chmod -R a+rwX /opt/cargo /opt/rustup
ENV STITCH_LANGUAGE=rust \
    STITCH_HOME=/stitch \
    ASAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer
COPY runtime /stitch/rust/stitch
COPY bin /stitch/bin
RUN chmod +x /stitch/bin/*

FROM toolchain AS base
COPY --from=engine /stitch/bin/stitch-engine /stitch/bin/stitch-engine
