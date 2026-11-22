ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
# pom targets Java 7 (unsupported by JDK 21): compile the sources directly.
RUN mkdir -p /tmp/yaml && javac -nowarn -encoding UTF-8 --release 8 -d /tmp/yaml $(find src/main/java -name '*.java') \
    && if [ -d src/main/resources ]; then cp -r src/main/resources/. /tmp/yaml/; fi \
    && jar cf /deps/snakeyaml.jar -C /tmp/yaml .
