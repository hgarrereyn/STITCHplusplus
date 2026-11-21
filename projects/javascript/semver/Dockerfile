ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && npm install --omit=dev --ignore-scripts --no-audit --no-fund && cd /opt && npm install --ignore-scripts --no-audit --no-fund /src
