ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
# hatch-vcs reads the version from git tags, which the source context does not carry
RUN cd /src && SETUPTOOLS_SCM_PRETEND_VERSION=4.0.0 pip install --no-cache-dir .
