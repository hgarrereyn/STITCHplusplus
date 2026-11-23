# syntax=docker/dockerfile:1.7
# STITCH++ Objective-C image (build context: languages/objc/ plus languages/cpp as cpp/, assembled
# by the CLI). Objective-C harnesses run on the C/C++ runtime through runtime/src/stitch_objc.mm.
#   target `toolchain`: clang 18 + libobjc2 + libdispatch + GNUstep Base + the runtime (builds and runs reproducers)
#   target `base`     : toolchain + the engine library (libstitch_engine.a) for fuzzing
ARG STITCH_ENGINE=stitch/engine:latest
FROM ${STITCH_ENGINE} AS engine

FROM ubuntu:24.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      clang-18 llvm-18 lld-18 libclang-rt-18-dev libc++-18-dev libc++abi-18-dev \
      build-essential cmake ninja-build meson make autoconf automake libtool pkg-config \
      git ca-certificates curl wget xz-utils unzip python3 python3-pip file gdb \
      zlib1g-dev libssl-dev bison flex \
      libffi-dev libxml2-dev libxslt1-dev libicu-dev libgnutls28-dev libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/* \
    && for t in clang clang++ llvm-symbolizer llvm-ar llvm-nm llvm-ranlib llvm-cov llvm-profdata lld ld.lld; do \
         [ -e /usr/bin/$t-18 ] && ln -sf /usr/bin/$t-18 /usr/local/bin/$t; done
# The Objective-C runtime (libobjc2: ARC, blocks, Objective-C and C++ exceptions in one model),
# Grand Central Dispatch (swift-corelibs-libdispatch) and GNUstep Base (Foundation, which includes
# <dispatch/dispatch.h> as on Apple platforms), built with clang under /usr/local. Ubuntu's GNUstep
# packages use GCC's runtime, which clang cannot use for ARC. libdispatch brings its own blocks
# runtime; programs link libobjc directly, so every block symbol binds to libobjc's.
ARG LIBOBJC2=v2.2.1 LIBDISPATCH=swift-6.0.3-RELEASE GNUSTEP_MAKE=make-2_9_2 GNUSTEP_BASE=base-1_30_0
RUN set -e; mkdir -p /usr/src/gnustep && cd /usr/src/gnustep \
    && git clone -q --depth 1 --branch ${LIBOBJC2} --recurse-submodules --shallow-submodules https://github.com/gnustep/libobjc2 \
    && cmake -S libobjc2 -B libobjc2/build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
         -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local \
    && ninja -C libobjc2/build install >/dev/null \
    && git clone -q --depth 1 --branch ${LIBDISPATCH} https://github.com/swiftlang/swift-corelibs-libdispatch libdispatch \
    && cmake -S libdispatch -B libdispatch/build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
         -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr/local -DINSTALL_PRIVATE_HEADERS=YES -DBUILD_TESTING=OFF \
    && ninja -C libdispatch/build install >/dev/null \
    && git clone -q --depth 1 --branch ${GNUSTEP_MAKE} https://github.com/gnustep/tools-make \
    && (cd tools-make && ./configure CC=clang CXX=clang++ --with-layout=fhs --with-library-combo=ng-gnu-gnu \
          --with-runtime-abi=gnustep-2.2 >/dev/null && make install >/dev/null) \
    && git clone -q --depth 1 --branch ${GNUSTEP_BASE} https://github.com/gnustep/libs-base \
    && (cd libs-base && . /usr/local/share/GNUstep/Makefiles/GNUstep.sh \
        && ./configure CC=clang CXX=clang++ >/tmp/base-configure.log 2>&1 || (tail -40 /tmp/base-configure.log; false)) \
    && (cd libs-base && . /usr/local/share/GNUstep/Makefiles/GNUstep.sh \
        && make -j"$(nproc)" >/tmp/base-make.log 2>&1 || (tail -60 /tmp/base-make.log; false)) \
    && (cd libs-base && . /usr/local/share/GNUstep/Makefiles/GNUstep.sh && make install >/dev/null) \
    && cd / && rm -rf /usr/src/gnustep /tmp/base-*.log && ldconfig
COPY cpp/runtime /stitch/runtime
COPY cpp/bin /stitch/bin
COPY runtime /stitch/objc
COPY symbolizer /stitch/objc/symbolizer
COPY bin /stitch/bin
# compiler and linker flags for Objective-C (what gnustep-config prints, without the per-user
# GNUstep domain and the build-type flags)
RUN set -e; chmod +x /stitch/bin/* /stitch/objc/symbolizer/*; mkdir -p /stitch/lib \
    && gnustep-config --objc-flags | tr ' ' '\n' | grep -E '^(-D|-f|-pthread$|-I/usr/)' | grep -vxE -- '-fPIC' \
         | tr '\n' ' ' > /stitch/objc/objc-flags \
    && (gnustep-config --base-libs | tr ' ' '\n' | grep -E '^(-l|-f|-pthread$|-rdynamic$|-L/usr/)'; echo -ldispatch) \
         | tr '\n' ' ' > /stitch/objc/link-flags \
    && echo "objc: $(cat /stitch/objc/objc-flags)" && echo "link: $(cat /stitch/objc/link-flags)"
ENV PATH=/stitch/bin:$PATH \
    CC=/stitch/bin/stitch-cc \
    CXX=/stitch/bin/stitch-cxx \
    STITCH_SANITIZERS=address \
    STITCH_LANGUAGE=objc \
    ASAN_SYMBOLIZER_PATH=/stitch/objc/symbolizer/llvm-symbolizer \
    STITCH_HOME=/stitch

FROM toolchain AS base
COPY --from=engine /stitch/lib/libstitch_engine.a /stitch/lib/libstitch_engine.a
