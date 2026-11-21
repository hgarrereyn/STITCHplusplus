# Guava compiled from source with javac (its annotation dependencies from Maven Central). The
# same Dockerfile builds later releases (they add no compile-time dependencies beyond these).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN M=https://repo1.maven.org/maven2 && mkdir -p /tmp/anno \
    && curl -fsSL -o /deps/failureaccess.jar $M/com/google/guava/failureaccess/1.0.1/failureaccess-1.0.1.jar \
    && curl -fsSL -o /tmp/anno/jsr305.jar $M/com/google/code/findbugs/jsr305/3.0.2/jsr305-3.0.2.jar \
    && curl -fsSL -o /tmp/anno/checker-qual.jar $M/org/checkerframework/checker-qual/3.42.0/checker-qual-3.42.0.jar \
    && curl -fsSL -o /tmp/anno/ep.jar $M/com/google/errorprone/error_prone_annotations/2.26.1/error_prone_annotations-2.26.1.jar \
    && curl -fsSL -o /tmp/anno/j2objc.jar $M/com/google/j2objc/j2objc-annotations/3.0.0/j2objc-annotations-3.0.0.jar \
    && mkdir -p /tmp/g && javac -source 8 -target 8 -Xlint:-options -g -nowarn -encoding UTF-8 -cp "/deps/*:/tmp/anno/*" -d /tmp/g $(find guava/src -name '*.java') \
    && jar cf /deps/guava.jar -C /tmp/g . && cp /tmp/anno/*.jar /deps/
