ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF -DQJS_BUILD_LIBC=ON \
 && cmake --build build -j$(nproc) --target qjs
