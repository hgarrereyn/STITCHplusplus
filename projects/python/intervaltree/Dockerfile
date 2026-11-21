ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN pip install --no-cache-dir sortedcontainers && pip install --no-cache-dir --no-deps /src
