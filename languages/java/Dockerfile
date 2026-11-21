# syntax=docker/dockerfile:1.7
# STITCH++ Java image (build context: languages/java/).
#   target `toolchain`: JDK 21 + Maven + the stitch runtime and agent (/stitch/java/stitch.jar),
#                       JaCoCo for `stitch cov` (reproducers)
#   target `base`     : toolchain + stitch-engine (fuzzing)
# Project Dockerfiles put the library and its dependencies in /deps (jars), e.g.
#   RUN mvn -q -DskipTests package dependency:copy-dependencies -DoutputDirectory=/deps && cp target/*.jar /deps/
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      openjdk-21-jdk-headless maven python3 git ca-certificates curl unzip \
    && rm -rf /var/lib/apt/lists/*
ARG JACOCO=0.8.12
RUN mkdir -p /stitch/java /deps \
    && curl -fsSL -o /stitch/java/jacocoagent.jar \
       https://repo1.maven.org/maven2/org/jacoco/org.jacoco.agent/${JACOCO}/org.jacoco.agent-${JACOCO}-runtime.jar \
    && curl -fsSL -o /stitch/java/jacococli.jar \
       https://repo1.maven.org/maven2/org/jacoco/org.jacoco.cli/${JACOCO}/org.jacoco.cli-${JACOCO}-nodeps.jar
COPY runtime /stitch/java/runtime
RUN cd /stitch/java/runtime \
    && javac -g -parameters --add-exports java.base/jdk.internal.org.objectweb.asm=ALL-UNNAMED \
         -d /tmp/stitch-classes $(find src -name '*.java') \
    && printf 'Premain-Class: stitch.rt.Agent\n' > /tmp/manifest.txt \
    && jar cfm /stitch/java/stitch.jar /tmp/manifest.txt -C /tmp/stitch-classes . \
    && rm -rf /tmp/stitch-classes
COPY bin /stitch/bin
RUN chmod +x /stitch/bin/*
ENV PATH=/stitch/bin:$PATH \
    STITCH_LANGUAGE=java \
    STITCH_HOME=/stitch

FROM toolchain AS base
COPY --from=engine /stitch/bin/stitch-engine /stitch/bin/stitch-engine
