ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN mkdir -p /tmp/json && javac -nowarn -encoding UTF-8 -d /tmp/json $(find src/main/java -name '*.java') \
    && jar cf /deps/json.jar -C /tmp/json .
