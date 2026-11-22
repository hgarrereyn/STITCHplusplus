# Puts the library source at /src (the harness crate depends on it by path).
#   STITCH_BASE  nightly Rust (ASan, SanitizerCoverage) + the STITCH++ runtime crate
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
