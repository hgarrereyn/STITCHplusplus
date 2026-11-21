ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /opt && npm install /src
