# Compiled with javac --release 8 (the pom's target; Maven would pull OSGi/pax-exam test tooling).
# Runtime dependencies (commons-io, commons-lang3, commons-codec) and the optional codecs' jars come
# from Maven Central at the versions the pom names (zstd-jni's native library is not used: no zstd stubs).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN M=https://repo1.maven.org/maven2 \
    && curl -fsSL -o /deps/xz.jar $M/org/tukaani/xz/1.12/xz-1.12.jar \
    && curl -fsSL -o /deps/brotli-dec.jar $M/org/brotli/dec/0.1.2/dec-0.1.2.jar \
    && curl -fsSL -o /deps/zstd-jni.jar $M/com/github/luben/zstd-jni/1.5.7-28/zstd-jni-1.5.7-28.jar \
    && curl -fsSL -o /deps/commons-io.jar $M/commons-io/commons-io/2.22.0/commons-io-2.22.0.jar \
    && curl -fsSL -o /deps/commons-lang3.jar $M/org/apache/commons/commons-lang3/3.22.0/commons-lang3-3.22.0.jar \
    && curl -fsSL -o /deps/asm.jar $M/org/ow2/asm/asm/9.9/asm-9.9.jar \
    && curl -fsSL -o /deps/commons-codec.jar $M/commons-codec/commons-codec/1.23.0/commons-codec-1.23.0.jar \
    && mkdir -p /tmp/cc \
    && javac --release 8 -g -nowarn -encoding UTF-8 -cp "/deps/*" -d /tmp/cc $(find src/main/java -name '*.java') \
    && if [ -d src/main/resources ]; then cp -r src/main/resources/. /tmp/cc/; fi \
    && jar cf /deps/commons-compress.jar -C /tmp/cc .
