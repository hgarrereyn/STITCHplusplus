# The library from source: /src/lib first on the load path (also ahead of a bundled gem of the
# same name).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
ENV RUBYLIB=/src/lib
