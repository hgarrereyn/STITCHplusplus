# Builds the library under test.
#   STITCH_BASE  base image: clang, sanitizers, STITCH++ runtime. CC and CXX add instrumentation.
#   source       build context with the library source at the requested version.
ARG STITCH_BASE
FROM ${STITCH_BASE}

# System packages:
# RUN apt-get update && apt-get install -y --no-install-recommends libfoo-dev && rm -rf /var/lib/apt/lists/*

COPY --from=source . /src
WORKDIR /src

# Build a static library. Examples:
#   RUN cmake -B build -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF && cmake --build build -j$(nproc)
#   RUN ./configure --disable-shared && make -j$(nproc)
# Tools that must not be instrumented: RUN STITCH_NO_INSTR=1 make tool
RUN cmake -B build -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_TESTS=OFF && cmake --build build -j$(nproc) && ls build
