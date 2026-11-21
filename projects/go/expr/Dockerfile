# Puts the library source at /src and downloads its modules.
#   STITCH_BASE  Go + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
# (from v1.15.4 on the module is github.com/expr-lang/expr: rename it back so the harness builds
# at any revision)
RUN cd /src && grep -rl "github.com/expr-lang/expr" --include='*.go' --include=go.mod . | xargs -r sed -i 's|github.com/expr-lang/expr|github.com/antonmedv/expr|g' \
    && go mod download
