# syntax=docker/dockerfile:1.7
# STITCH++ Ruby image (build context: languages/ruby/).
#   target `toolchain`: Ruby 3.4 (YJIT, Prism) + the stitch runtime (/stitch/ruby) (reproducers)
#   target `base`     : toolchain + stitch-engine (fuzzing)
# Project Dockerfiles install the library as a gem: `RUN cd /src && gem build *.gemspec && gem install ./*.gem`.
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS toolchain
ARG RUBY_VERSION=3.4.9
ARG RUBY_SHA256=7bb4d4f5e807cc27251d14d9d6086d182c5b25875191e44ab15b709cd7a7dd9c
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential autoconf bison ca-certificates curl git python3 rustc lcov \
      libssl-dev libyaml-dev zlib1g-dev libffi-dev libgmp-dev libreadline-dev \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL -o /tmp/ruby.tar.gz "https://cache.ruby-lang.org/pub/ruby/3.4/ruby-${RUBY_VERSION}.tar.gz" \
    && echo "${RUBY_SHA256}  /tmp/ruby.tar.gz" | sha256sum -c - \
    && mkdir /tmp/ruby && tar -xzf /tmp/ruby.tar.gz -C /tmp/ruby --strip-components=1 \
    && cd /tmp/ruby && ./configure --prefix=/usr/local --enable-yjit --disable-install-doc --enable-shared \
    && make -j"$(nproc)" && make install && cd / && rm -rf /tmp/ruby /tmp/ruby.tar.gz \
    && ruby --version && ruby -e 'require "prism"; p Prism::VERSION' \
    && gem install --no-document bundler
ENV PATH=/stitch/bin:$PATH \
    STITCH_LANGUAGE=ruby \
    STITCH_HOME=/stitch \
    GEM_HOME=/usr/local/bundle \
    BUNDLE_SILENCE_ROOT_WARNING=1
ENV PATH=/usr/local/bundle/bin:$PATH
COPY runtime /stitch/ruby
COPY bin /stitch/bin
RUN chmod +x /stitch/bin/* /stitch/ruby/bin/* && ln -sf /stitch/ruby/bin/stitch-rb /stitch/bin/stitch-rb \
    && mkdir -p /usr/local/bundle && chmod -R a+rwX /usr/local/bundle

FROM toolchain AS base
COPY --from=engine /stitch/bin/stitch-engine /stitch/bin/stitch-engine
