ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && javac -g -d /tmp/kv $(find . -name '*.java') && jar cf /deps/kv.jar -C /tmp/kv .
