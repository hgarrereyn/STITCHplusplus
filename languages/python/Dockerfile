# syntax=docker/dockerfile:1.7
# STITCH++ Python image (build context: languages/python/).
#   target `toolchain`: Python 3.12 venv (/opt/venv) + the stitch runtime package (reproducers)
#   target `base`     : toolchain + stitch-engine (fuzzing)
# Project Dockerfiles install the library into /opt/venv: `RUN pip install /src`.
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      python3 python3-venv python3-dev python3-pip build-essential git ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir "coverage>=7.6,<8"
ENV PATH=/stitch/bin:/opt/venv/bin:$PATH \
    VIRTUAL_ENV=/opt/venv \
    PYTHONDONTWRITEBYTECODE=1 \
    STITCH_LANGUAGE=python \
    STITCH_HOME=/stitch
COPY runtime /stitch/python
# the runtime is on the venv's path through a .pth file, not PYTHONPATH, so a project Dockerfile
# can set PYTHONPATH (ENV PYTHONPATH=/src) without losing it
RUN echo /stitch/python > "$(/opt/venv/bin/python3 -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])')/stitch-runtime.pth"
COPY bin /stitch/bin
RUN chmod +x /stitch/bin/*

FROM toolchain AS base
COPY --from=engine /stitch/bin/stitch-engine /stitch/bin/stitch-engine
