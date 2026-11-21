# networkx from source; numpy/scipy for the algorithms that need them (pagerank, spectral, ...).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN pip install --no-cache-dir "numpy<2" "scipy<1.12" && pip install --no-cache-dir --no-deps /src
