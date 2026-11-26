ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
# No runtime dependencies: compile the sources directly (Java 8 target, as in the pom; skips the
# Maven test/plugin downloads). src/main/java9/module-info.java is left out.
RUN mkdir -p /tmp/yaml && javac -nowarn -encoding UTF-8 --release 8 -d /tmp/yaml $(find src/main/java -name '*.java') \
    && if [ -d src/main/resources ]; then cp -r src/main/resources/. /tmp/yaml/; fi \
    && jar cf /deps/snakeyaml.jar -C /tmp/yaml .
