ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN $CC -fobjc-arc -c KVStore.m -o KVStore.o && ar rcs libkvstore.a KVStore.o
