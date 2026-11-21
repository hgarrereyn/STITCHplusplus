# chrono from source. Old releases deny lints (dead_code, ...) that newer compilers report:
# the deny attributes are removed so the crate builds with today's toolchain.
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN sed -i -E 's/^#!\[deny\(([^)]*)\)\]/\/\/ (deny removed for newer compilers: \1)/' /src/src/lib.rs
