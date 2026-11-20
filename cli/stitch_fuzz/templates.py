"""`stitch init` scaffolding."""
from pathlib import Path

TOML = """# STITCH project. Guide: `stitch guide`. API reference: `stitch guide api`.
[project]
name = "{name}"

[source]
{source}

[build]
# How stubs compile and link. Paths are inside the image; the library source is at /src.
cflags = ["-I/src"]
ldflags = []               # e.g. ["/src/build/libfoo.a"] or ["-L/src/build", "-lfoo"]
sanitizers = ["address"]   # add "undefined" for UBSan

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
exceptions = "finding"     # "bail": treat uncaught C++ exceptions from the library as failed calls
"""

DOCKERFILE = """# Builds the library under test.
#   STITCH_BASE  base image: clang, sanitizers, STITCH runtime. CC and CXX add instrumentation.
#   source       build context with the library source at the requested version.
ARG STITCH_BASE
FROM ${{STITCH_BASE}}

# System packages:
# RUN apt-get update && apt-get install -y --no-install-recommends libfoo-dev && rm -rf /var/lib/apt/lists/*

COPY --from=source . /src
WORKDIR /src

# Build a static library. Examples:
#   RUN cmake -B build -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF && cmake --build build -j$(nproc)
#   RUN ./configure --disable-shared && make -j$(nproc)
# Tools that must not be instrumented: RUN STITCH_NO_INSTR=1 make tool
RUN echo "edit {name}/Dockerfile to build the library" && false
"""

OBJECTS = """// Object types and data types shared by all stubs.
#pragma once
#include <stitch.h>
// #include <foo.h>

// STITCH_OBJECT(Handle, foo_handle*);                       // object type and its C handle
// STITCH_DROP(Handle, [](foo_handle* h) { foo_free(h); });  // frees objects left at the end
// STITCH_DATATYPE(FooText);                                  // data type: harness/data/FooText/
"""

EXAMPLE = """// One function per API scenario. The signature is the spec:
//   Borrow<T> uses an object, Take<T> consumes it, returning New<T> produces one.
//   Every other parameter is fuzzed: U32, Range<0, 9>, Bytes<256>, Str<64, Tag>, ...
#include "../objects.hpp"

// New<Handle> create(Str<64> name) {
//     foo_handle* h = foo_create(name.c_str());
//     stitch::require(h, "create failed");
//     return h;
// }
// STITCH_ENDPOINT(create);
"""


def init_project(root: Path, name=None, git=None, rev=None, path=None) -> str:
    root.mkdir(parents=True, exist_ok=True)
    if (root / "stitch.toml").exists():
        raise SystemExit(f"error: {root}/stitch.toml already exists")
    name = name or root.name
    if git:
        source = f'git = "{git}"\nrev = "{rev or "HEAD"}"   # branch, tag or commit; `stitch build --rev X` overrides it'
    else:
        source = f'path = "{path}"   # local checkout, used as-is (uncommitted changes included)'
    (root / "stitch.toml").write_text(TOML.format(name=name, source=source))
    (root / "Dockerfile").write_text(DOCKERFILE.format(name=name))
    (root / ".dockerignore").write_text(".stitch\n")
    (root / ".gitignore").write_text(".stitch/\n")
    h = root / "harness"
    (h / "data").mkdir(parents=True, exist_ok=True)
    (h / "api").mkdir(exist_ok=True)
    (h / "objects.hpp").write_text(OBJECTS)
    (h / "api" / "example.cpp").write_text(EXAMPLE)
    return (f"created {root}\n"
            f"  1. edit Dockerfile until `stitch build --image-only` works\n"
            f"  2. set [build] cflags and ldflags in stitch.toml\n"
            f"  3. write stubs in harness/, then `stitch build` and `stitch probe`")
