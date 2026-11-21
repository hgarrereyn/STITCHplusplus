# Installs the library under test into the image's Python 3.12 environment (/opt/venv).
#   STITCH_BASE  Python + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN pip install --no-cache-dir "markupsafe==2.0.1" /src
