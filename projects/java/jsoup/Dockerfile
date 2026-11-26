ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN mvn -q -B -o -DskipTests -Dmaven.javadoc.skip=true -Danimal.sniffer.skip=true -Djapicmp.skip=true package 2>/dev/null \
    || mvn -q -B -DskipTests -Dmaven.javadoc.skip=true -Danimal.sniffer.skip=true -Djapicmp.skip=true package
RUN cp target/jsoup-*.jar /deps/ && rm -f /deps/*-sources.jar /deps/*-javadoc.jar /deps/*-examples.jar
