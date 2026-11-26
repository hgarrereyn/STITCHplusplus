# libxml2 as a static library (no Python, no compression libraries, no threads, no modules).
# Schematron is off by default since 2.14: turn it on.
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN cmake -B build -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Debug -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_ZLIB=OFF \
      -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_MODULES=OFF \
      -DLIBXML2_WITH_THREADS=OFF -DLIBXML2_WITH_SCHEMATRON=ON \
    && cmake --build build -j$(nproc)
