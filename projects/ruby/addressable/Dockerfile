# The library from source: /src/lib first on the load path (also ahead of a bundled gem of the
# same name). addressable needs public_suffix (>= 2.0.2, < 8.0).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN gem install --no-document public_suffix -v 7.0.5
ENV RUBYLIB=/src/lib
