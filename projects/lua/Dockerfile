ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
# lua's makefile hardcodes gcc and warning flags: override CC/CFLAGS (CC is the instrumenting wrapper)
RUN make -j$(nproc) liblua.a CC="$CC" AR="ar rc" RANLIB=ranlib CFLAGS="-Wall -DLUA_USE_LINUX -DLUAI_ASSERT -DLUA_USE_APICHECK" MYLIBS="-ldl"
