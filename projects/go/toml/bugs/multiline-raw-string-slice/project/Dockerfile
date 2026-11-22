# Puts the library source at /src and downloads its modules.
#   STITCH_BASE  Go + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && go mod download
