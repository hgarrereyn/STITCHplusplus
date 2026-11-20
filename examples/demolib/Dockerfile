# Build environment for the library under test.
# STITCH passes two things:
#   STITCH_BASE  the base image: clang + sanitizers + STITCH runtime. CC/CXX are wrappers that
#                add coverage, cmplog and sanitizer instrumentation, so normal builds just work.
#   source       a build context holding the library source at the requested revision
#                (or your local checkout). Use it with COPY --from=source.
ARG STITCH_BASE
FROM ${STITCH_BASE}

# System dependencies of the library:
# RUN apt-get update && apt-get install -y --no-install-recommends libfoo-dev && rm -rf /var/lib/apt/lists/*

COPY --from=source . /src
WORKDIR /src

# Build a STATIC library with $CC/$CXX (already instrumented). For example:
#   RUN cmake -B build -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF && cmake --build build -j$(nproc)
#   RUN ./configure --disable-shared && make -j$(nproc)
# Helper tools that must not be instrumented: prefix the command with STITCH_NO_INSTR=1.
RUN make libmsg.a CC="$CC"
