"""`stitch init` scaffolding."""
from pathlib import Path

TOML = """# STITCH v2 project. Run `stitch guide` for the workflow and `stitch guide --spec` for the API.
[project]
name = "{name}"

[source]
{source}

[build]
# How the harness compiles and links against the library *inside the project image*.
# The library source is at /src (see Dockerfile).
cflags = ["-I/src"]
ldflags = []            # e.g. ["/src/build/libfoo.a"] or ["-L/src/build", "-lfoo"]
sanitizers = ["address"]   # add "undefined" for UBSan

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
exceptions = "finding"  # or "bail": treat uncaught C++ exceptions from the library as bails
"""

DOCKERFILE = """# Build environment for the library under test.
# STITCH passes two things:
#   STITCH_BASE  the base image: clang + sanitizers + STITCH runtime. CC/CXX are wrappers that
#                add coverage, cmplog and sanitizer instrumentation, so normal builds just work.
#   source       a build context holding the library source at the requested revision
#                (or your local checkout). Use it with COPY --from=source.
ARG STITCH_BASE
FROM ${{STITCH_BASE}}

# System dependencies of the library:
# RUN apt-get update && apt-get install -y --no-install-recommends libfoo-dev && rm -rf /var/lib/apt/lists/*

COPY --from=source . /src
WORKDIR /src

# Build a STATIC library with $CC/$CXX (already instrumented). For example:
#   RUN cmake -B build -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF && cmake --build build -j$(nproc)
#   RUN ./configure --disable-shared && make -j$(nproc)
# Helper tools that must not be instrumented: prefix the command with STITCH_NO_INSTR=1.
RUN echo "edit {name}/Dockerfile to build the library" && false
"""

OBJECTS = """// Object types and data types shared by all stubs.
#pragma once
#include <stitch.h>
// #include <foo.h>

// STITCH_OBJECT(Handle, foo_handle*);          // an object type and its C handle type
// STITCH_DROP(Handle, [](foo_handle* h) { foo_free(h); });   // optional: free leftovers
// STITCH_DATATYPE(FooText);                     // a data type: harness/data/FooText/{seeds/,*.dict}
"""

EXAMPLE = """// Endpoints: one function per API scenario. The signature is the spec:
//   Borrow<T> (use, keep), Take<T> (consume), return New<T> (produce);
//   every other parameter is fuzz data (U32, Range<0,9>, Bytes<256>, Str<64, Tag>, ...).
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
        source = f'git = "{git}"\nrev = "{rev or "HEAD"}"           # branch, tag or commit; `stitch build --rev X` overrides'
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
            f"  2. set [build] cflags/ldflags in stitch.toml, write stubs in harness/, then `stitch check`\n"
            f"  3. `stitch probe` to see what bails, then `stitch fuzz`")
