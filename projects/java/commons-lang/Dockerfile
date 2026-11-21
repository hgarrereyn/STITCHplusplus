ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN mvn -q -B -DskipTests -Dmaven.javadoc.skip=true -Drat.skip=true -Danimal.sniffer.skip=true -Djapicmp.skip=true \
      -Dmoditect.skip=true -Dspotbugs.skip=true -Dcheckstyle.skip=true -Dpmd.skip=true -Djacoco.skip=true package \
    && cp target/commons-lang3-*[0-9].jar /deps/
