ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN pip install --no-cache-dir setuptools_scm six && cd /src && SETUPTOOLS_SCM_PRETEND_VERSION=2.9.0 pip install --no-cache-dir .
