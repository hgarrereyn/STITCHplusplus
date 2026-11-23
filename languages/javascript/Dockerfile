# syntax=docker/dockerfile:1.7
# STITCH++ JavaScript image (build context: languages/javascript/).
#   target `toolchain`: Node.js 22 + the stitch runtime package (reproducers)
#   target `base`     : toolchain + stitch-engine (fuzzing)
# Project Dockerfiles install the library into /opt/node_modules (on NODE_PATH):
#   RUN cd /opt && npm install /src        or     RUN cd /opt && npm install yaml@2.6.1
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS toolchain
ARG TARGETARCH
ARG NODE_VERSION=22.21.0
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl xz-utils git python3 build-essential \
    && rm -rf /var/lib/apt/lists/*
RUN arch=$([ "$TARGETARCH" = "arm64" ] && echo arm64 || echo x64) \
    && curl -fsSL "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-${arch}.tar.xz" \
       | tar -xJ -C /usr/local --strip-components=1 \
    && node --version
COPY runtime/package.json /stitch/javascript/node_modules/stitch/package.json
RUN cd /stitch/javascript/node_modules/stitch && npm install --omit=dev --no-audit --no-fund && mkdir -p /opt/node_modules
COPY runtime /stitch/javascript/node_modules/stitch
COPY bin /stitch/bin
RUN chmod +x /stitch/bin/*
ENV PATH=/stitch/bin:$PATH \
    NODE_PATH=/opt/node_modules:/stitch/javascript/node_modules \
    STITCH_LANGUAGE=javascript \
    STITCH_HOME=/stitch

FROM toolchain AS base
COPY --from=engine /stitch/bin/stitch-engine /stitch/bin/stitch-engine
