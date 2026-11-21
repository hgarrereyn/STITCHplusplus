# The package source in /src (the harness package depends on it; stitch-build compiles both).
#   STITCH_BASE  Swift 6.2 + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
