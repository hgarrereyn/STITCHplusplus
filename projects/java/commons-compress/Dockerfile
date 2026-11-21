# Compiled with javac --release 8 (the 1.20 pom targets Java 7 and pulls OSGi test tooling; the
# Java 8 API still has java.util.jar.Pack200, which the pack200 codec needs to compile). The optional
# codecs' jars (and, for newer releases, commons-io / lang3 / codec) come from Maven Central (zstd-jni's native library is not used: no zstd stubs).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN M=https://repo1.maven.org/maven2 \
    && curl -fsSL -o /deps/xz.jar $M/org/tukaani/xz/1.10/xz-1.10.jar \
    && curl -fsSL -o /deps/brotli-dec.jar $M/org/brotli/dec/0.1.2/dec-0.1.2.jar \
    && curl -fsSL -o /deps/zstd-jni.jar $M/com/github/luben/zstd-jni/1.5.7-4/zstd-jni-1.5.7-4.jar \
    && curl -fsSL -o /deps/commons-io.jar $M/commons-io/commons-io/2.20.0/commons-io-2.20.0.jar \
    && curl -fsSL -o /deps/commons-lang3.jar $M/org/apache/commons/commons-lang3/3.18.0/commons-lang3-3.18.0.jar \
    && curl -fsSL -o /deps/asm.jar $M/org/ow2/asm/asm/9.8/asm-9.8.jar \
    && curl -fsSL -o /deps/commons-codec.jar $M/commons-codec/commons-codec/1.19.0/commons-codec-1.19.0.jar \
    && mkdir -p /tmp/cc \
    && javac --release 8 -g -nowarn -encoding UTF-8 -cp "/deps/*" -d /tmp/cc $(find src/main/java -name '*.java') \
    && jar cf /deps/commons-compress.jar -C /tmp/cc .
