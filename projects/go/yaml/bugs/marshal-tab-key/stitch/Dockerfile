# syntax=docker/dockerfile:1.7
# STITCH Go image (build context: languages/go/).
#   target `toolchain`: Go + the stitch runtime module (/stitch/go/stitch) (reproducers)
#   target `base`     : toolchain + stitch-engine (fuzzing)
# Project Dockerfiles put the library source in /src and download its modules:
#   RUN cd /src && go mod download
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS toolchain
ARG TARGETARCH
ARG GO_VERSION=1.24.2
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git python3 build-essential \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL "https://go.dev/dl/go${GO_VERSION}.linux-${TARGETARCH}.tar.gz" | tar -xz -C /usr/local \
    && /usr/local/go/bin/go version
ENV PATH=/stitch/bin:/usr/local/go/bin:$PATH \
    GOPATH=/go \
    GOMODCACHE=/go/pkg/mod \
    GOTOOLCHAIN=local \
    GOFLAGS=-mod=mod \
    STITCH_LANGUAGE=go \
    STITCH_HOME=/stitch
RUN mkdir -p /go/pkg/mod && chmod -R a+rwX /go
COPY runtime /stitch/go/stitch
COPY bin /stitch/bin
RUN chmod +x /stitch/bin/*

FROM toolchain AS base
COPY --from=engine /stitch/bin/stitch-engine /stitch/bin/stitch-engine
