"""`stitch init` scaffolding."""
from pathlib import Path

TOML = """# STITCH++ project. Guide: `stitch guide`. C/C++ API: `stitch guide cpp`.
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
#   STITCH_BASE  base image: clang, sanitizers, STITCH++ runtime. CC and CXX add instrumentation.
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


PY_TOML = """# STITCH++ project. Guide: `stitch guide`. Python API: `stitch guide python`.
[project]
name = "{name}"
language = "python"

[source]
{source}

[python]
include = []     # import names of the code under test, e.g. ["yaml"] for PyYAML (empty: everything outside the standard library)

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
"""

PY_DOCKERFILE = """# Installs the library under test into the image's Python 3.12 environment (/opt/venv).
#   STITCH_BASE  Python + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${{STITCH_BASE}}
COPY --from=source . /src
RUN pip install --no-cache-dir /src
"""

PY_EXAMPLE = """\"\"\"Stubs: one function per API call. The annotations are the spec.

Borrow[T] uses an object, Take[T] consumes it, New[T] (return) produces one.
Every other parameter is fuzzed: Str[64], Bytes[256], I32, Range[0, 9], OneOf["a", "b"], ...
An exception that is not listed in bails_on is a bug.
\"\"\"
import stitch
from stitch import Borrow, New, Str

# import foo
# Doc = stitch.object("Doc")
#
#
# @stitch.endpoint(bails_on=(foo.ParseError,))
# def parse(text: Str[4096]) -> New[Doc]:
#     return foo.parse(text)
#
#
# @stitch.endpoint
# def render(doc: Borrow[Doc]):
#     doc.render()
"""

JS_TOML = """# STITCH++ project. Guide: `stitch guide`. JavaScript API: `stitch guide javascript`.
[project]
name = "{name}"
language = "javascript"

[source]
{source}

[javascript]
include = []     # npm package names of the code under test, e.g. ["js-yaml"] (empty: every package is instrumented)

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
"""

JS_DOCKERFILE = """# Installs the library under test where the harness finds it (/opt/node_modules, on NODE_PATH).
#   STITCH_BASE  Node.js 22 + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${{STITCH_BASE}}
COPY --from=source . /src
WORKDIR /src
# TypeScript or bundled packages: RUN npm install --ignore-scripts && npm run build
RUN npm install --omit=dev --ignore-scripts
RUN cd /opt && npm install /src
"""

JS_EXAMPLE = """// Stubs: one stitch.endpoint per API call. The argument specs are the spec.
//   Borrow(T) uses an object, Take(T) consumes it, New(T) (returns) produces one.
//   Every other argument is fuzzed: Str(64), Bytes(256), I32, Range(0, 9), OneOf("a", "b"), ...
// An exception that is not listed in bailsOn is a bug, and so is prototype pollution.
const stitch = require("stitch");
const { Borrow, New, Str } = stitch;

// const foo = require("foo");
// const Doc = stitch.object("Doc");
//
// stitch.endpoint("parse", { text: Str(4096) }, New(Doc), ({ text }) => foo.parse(text),
//                 { bailsOn: [SyntaxError] });
//
// stitch.endpoint("render", { doc: Borrow(Doc) }, null, ({ doc }) => doc.render());
"""

JAVA_TOML = """# STITCH++ project. Guide: `stitch guide`. Java API: `stitch guide java`.
[project]
name = "{name}"
language = "java"

[source]
{source}

[java]
classpath = ["/deps/*"]           # the library and its dependencies (jars the Dockerfile puts in /deps)
include = []                      # packages of the code under test, e.g. ["com.example.lib"] (empty: all non-JDK classes)
# sources = ["/src/src/main/java"]  # source roots for coverage reports (default: /src/**/src/main/java)

[fuzz]
timeout_ms = 3000
max_nodes = 24
jobs = 1
"""

JAVA_DOCKERFILE = """# Builds the library under test and puts it, with its dependencies, in /deps.
#   STITCH_BASE  JDK 21 + Maven + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${{STITCH_BASE}}
COPY --from=source . /src
WORKDIR /src
# includeScope=runtime keeps test dependencies out of /deps; copy the main jar, not -sources/-tests jars.
RUN mvn -q -B -DskipTests -Dmaven.javadoc.skip=true -Drat.skip=true package \\
      dependency:copy-dependencies -DincludeScope=runtime -DoutputDirectory=/deps \\
    && cp $(ls target/*.jar | grep -vE -- '-(sources|tests|test-sources|javadoc)\\.jar$') /deps/
# Gradle: RUN ./gradlew jar && cp build/libs/*.jar /deps/
"""

JAVA_EXAMPLE = """// Stubs: one public static method per API call, marked @Endpoint. The signature is the spec:
//   parameters of other types are objects (borrowed; @Take consumes), a return value is a new object,
//   String / byte[] / int / long / boolean / double / enums / stitch.Source parameters are fuzzed
//   (@Str(max = 64), @Range(lo = 0, hi = 9), @OneOf(strings = {...}), ...).
// A Throwable that is not listed in bailsOn is a bug.
import stitch.*;

public class Stubs {
    // @Endpoint(bailsOn = {IllegalArgumentException.class})
    // public static Doc parse(@Str(max = 4096) String text) {
    //     return Doc.parse(text);
    // }
    //
    // @Endpoint
    // public static void render(Doc doc) {
    //     doc.render();
    // }
}
"""

GO_TOML = """# STITCH++ project. Guide: `stitch guide`. Go API: `stitch guide go`.
[project]
name = "{name}"
language = "go"

[source]
{source}

[go]
module = "example.com/{module}"    # import path of the library (its source is at /src)
# instrument = ["example.com/{module}/..."]   # packages to instrument (default: all but std)
# race = true                       # build with the race detector

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
"""

GO_DOCKERFILE = """# Puts the library source at /src and downloads its modules.
#   STITCH_BASE  Go + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${{STITCH_BASE}}
COPY --from=source . /src
RUN cd /src && go mod download
"""

GO_EXAMPLE = """// Stubs: register one function per API call in init(). The signature is the spec:
// parameters of registered object types are objects ("name:take" consumes), results are new
// objects (a trailing error is a bail), and string, []byte, ints, floats, bool, stitch.Path and
// *stitch.Source parameters are fuzzed ("text:max=4096", "n:range=0..9", "mode:oneof=1|2").
// A panic is a bug.
package harness

import (
	"stitch"
	// "example.com/foo"
)

func init() {
	// stitch.Object[*foo.Doc]("Doc", nil)
	// stitch.Endpoint("parse", foo.Parse, "text:max=4096")
	// stitch.Endpoint("render", render, "doc")
	_ = stitch.Bail
}

// func render(doc *foo.Doc) { doc.Render() }
"""

RUST_TOML = """# STITCH++ project. Guide: `stitch guide`. Rust API: `stitch guide rust`.
[project]
name = "{name}"
language = "rust"

[source]
{source}

[rust.dependencies]                 # the harness crate's dependencies (the library is at /src)
{module} = {{ path = "/src" }}      # rename to the library's crate name (`name` in /src/Cargo.toml)

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
"""

RUST_DOCKERFILE = """# Puts the library source at /src (the harness crate depends on it by path).
#   STITCH_BASE  nightly Rust (ASan, SanitizerCoverage) + the STITCH++ runtime crate
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${{STITCH_BASE}}
COPY --from=source . /src
"""

RUST_EXAMPLE = """// Stubs: one endpoint! per API call. The signature is the spec:
//   Borrow<T> uses an object, Take<T> consumes it, New<T> (returned) produces one;
//   other parameters are fuzzed: Str<64>, Bytes<256>, u32, Range<0, 9>, OneOf<C>, Src<64>.
// Returning Err is a bail; a panic (including integer overflow) or an ASan error is a bug.
use stitch::*;

// object!(Doc, foo::Document);
//
// endpoint! {
//     fn parse(text: Str<4096>) -> Result<New<Doc>, foo::Error> {
//         Ok(New::new(foo::parse(&text)?))
//     }
// }
//
// endpoint! {
//     fn render(doc: Borrow<Doc>) {
//         doc.render();
//     }
// }
"""

RB_TOML = """# STITCH++ project. Guide: `stitch guide`. Ruby API: `stitch guide ruby`.
[project]
name = "{name}"
language = "ruby"

[source]
{source}

[ruby]
include = []     # gem names (or path prefixes) of the code under test, e.g. ["rexml"] (empty: all but the standard library)

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
"""

RB_DOCKERFILE = """# Installs the library under test as a gem into the image's Ruby 3.4.
#   STITCH_BASE  Ruby + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${{STITCH_BASE}}
COPY --from=source . /src
RUN cd /src && gem build *.gemspec && gem install --local ./*.gem
"""

RB_EXAMPLE = """# Stubs: one Stitch.endpoint per API call. The specs hash is the signature.
#
# Borrow[T] uses an object, Take[T] consumes it, New[T] (outputs) produces one.
# Every other parameter is fuzzed: Str[64], Bytes[256], I32, Int[0, 9], OneOf["a", "b"], ...
# An exception that is not listed in bails_on: is a bug.
require "stitch"
include Stitch::DSL

# require "foo"
# Doc = Stitch.object("Doc")
#
# Stitch.endpoint :parse, { text: Str[4096] }, New[Doc], bails_on: [Foo::ParseError] do |text|
#   Foo.parse(text)
# end
#
# Stitch.endpoint :render, { doc: Borrow[Doc] } do |doc|
#   doc.render
# end
"""

SWIFT_TOML = """# STITCH++ project. Guide: `stitch guide`. Swift API: `stitch guide swift`.
[project]
name = "{name}"
language = "swift"

[source]
{source}

[swift]
dependencies = ['.package(path: "/src")']                            # SwiftPM dependencies of the harness
products = ['.product(name: "{name}", package: "src")']             # the library products it imports

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
"""

SWIFT_DOCKERFILE = """# The package source in /src (the harness package depends on it; stitch-build compiles both).
#   STITCH_BASE  Swift 6.2 + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${{STITCH_BASE}}
COPY --from=source . /src
"""

SWIFT_EXAMPLE = """// Stubs: one `let NAME = endpoint("name") { ... }` per API call. The closure's types are the spec.
//
// Borrow<T> uses an object, Take<T> consumes it, New<T> (returned) produces one.
// Every other argument is fuzzed: Str<64, Plain>, Bytes<256, Plain>, Int32, Between<0, 9>, Pick<E>, ...
// A thrown error is a bail; traps (bounds, overflow, nil unwrap), ASan reports and failed checks are bugs.
import Stitch

// import Foo
// enum Doc: StitchObject { typealias Handle = Foo.Document }
//
// let parse = endpoint("parse") { (text: Str<4096, Plain>) throws -> New<Doc> in
//     New(try Foo.parse(text.value))
// }
//
// let render = endpoint("render") { (doc: Borrow<Doc>) in
//     _ = doc.value.render()
// }
"""

OBJC_TOML = """# STITCH++ project. Guide: `stitch guide`. Objective-C API: `stitch guide objc`.
[project]
name = "{name}"
language = "objc"

[source]
{source}

[build]
# How stubs compile and link. Paths are inside the image; the library source is at /src.
cflags = ["-I/src"]
ldflags = []               # e.g. ["/src/build/libfoo.a"]; GNUstep Base (Foundation) is linked already
sanitizers = ["address"]   # add "undefined" for UBSan

# [objc]
# arc = true               # compile the harness with ARC (default)

[fuzz]
timeout_ms = 2000
max_nodes = 24
jobs = 1
exceptions = "finding"     # "bail": treat Objective-C exceptions from the library as failed calls
"""

OBJC_DOCKERFILE = """# Builds the library under test.
#   STITCH_BASE  base image: clang, libobjc2, GNUstep Base, sanitizers, STITCH++ runtime. CC and CXX add
#                instrumentation, and the Objective-C runtime flags for .m / .mm files.
#   source       build context with the library source at the requested version.
ARG STITCH_BASE
FROM ${{STITCH_BASE}}

# System packages:
# RUN apt-get update && apt-get install -y --no-install-recommends libfoo-dev && rm -rf /var/lib/apt/lists/*

COPY --from=source . /src
WORKDIR /src

# Build a static library. Examples:
#   RUN for f in Sources/*.m; do $CC -fobjc-arc -ISources -c "$f" -o "${{f%.m}}.o"; done && ar rcs libfoo.a Sources/*.o
#   RUN cmake -B build -DBUILD_SHARED_LIBS=OFF && cmake --build build -j$(nproc)
RUN echo "edit {name}/Dockerfile to build the library" && false
"""

OBJC_EXAMPLE = """// Stubs: STITCH_ENDPOINT(ReturnType, name, parameters...) { body }. The declaration is the spec:
//   Borrow(T) uses an object, Take(T) consumes it, returning New(T) produces one.
//   Every other parameter is fuzzed: Str(64), Bytes(256), Range(0, 9), int32_t, double, BOOL, ...
// An Objective-C exception that escapes a stub is a bug, like ASan reports and failed STCheck.
#import <Stitch/Stitch.h>

// #import "Foo.h"
// STITCH_OBJECT(Doc, FooDocument *);                         // object type and its handle
// STITCH_DATATYPE(FooText);                                   // data type: harness/data/FooText/
//
// STITCH_ENDPOINT(New(Doc), parse, Str(4096, FooText) text) {
//     NSError *err = nil;
//     FooDocument *doc = [FooDocument documentWithString:text error:&err];
//     STRequire(doc != nil, @"parse error");                  // not a bug: invalid input
//     return doc;
// }
//
// STITCH_ENDPOINT(void, render, Borrow(Doc) doc) {
//     (void)[doc render];
// }
"""

LANGS = {
    "cpp": {"toml": TOML, "dockerfile": DOCKERFILE, "files": {"harness/objects.hpp": OBJECTS, "harness/api/example.cpp": EXAMPLE},
            "steps": ["edit Dockerfile until `stitch build --image-only` works",
                      "set [build] cflags and ldflags in stitch.toml",
                      "write stubs in harness/, then `stitch build` and `stitch probe`"]},
    "python": {"toml": PY_TOML, "dockerfile": PY_DOCKERFILE, "files": {"harness/stubs.py": PY_EXAMPLE},
               "steps": ["edit Dockerfile until `stitch build --image-only` works (the library must pip install)",
                         "optional: set [python] include in stitch.toml to the library's import name (narrower instrumentation and coverage)",
                         "write stubs in harness/*.py, then `stitch build` and `stitch probe`"]},
    "javascript": {"toml": JS_TOML, "dockerfile": JS_DOCKERFILE, "files": {"harness/stubs.js": JS_EXAMPLE},
                   "steps": ["edit Dockerfile until `stitch build --image-only` works (the package must load from /opt/node_modules)",
                             "optional: set [javascript] include in stitch.toml to the library's package name (narrower instrumentation and coverage)",
                             "write stubs in harness/*.js, then `stitch build` and `stitch probe`"]},
    "java": {"toml": JAVA_TOML, "dockerfile": JAVA_DOCKERFILE, "files": {"harness/Stubs.java": JAVA_EXAMPLE},
             "steps": ["edit Dockerfile until `stitch build --image-only` works (jars end up in /deps)",
                       "optional: set [java] include in stitch.toml to the library's packages (narrower instrumentation and coverage)",
                       "write stubs in harness/*.java, then `stitch build` and `stitch probe`"]},
    "go": {"toml": GO_TOML, "dockerfile": GO_DOCKERFILE, "files": {"harness/stubs.go": GO_EXAMPLE},
           "steps": ["edit Dockerfile until `stitch build --image-only` works",
                     "set [go] module in stitch.toml to the library's module path",
                     "write stubs in harness/*.go (package harness), then `stitch build` and `stitch probe`"]},
    "swift": {"toml": SWIFT_TOML, "dockerfile": SWIFT_DOCKERFILE, "files": {"harness/stubs.swift": SWIFT_EXAMPLE},
              "steps": ["edit Dockerfile until `stitch build --image-only` works (the package source in /src)",
                        "set [swift] dependencies and products in stitch.toml",
                        "write stubs in harness/*.swift, then `stitch build` and `stitch probe`"]},
    "ruby": {"toml": RB_TOML, "dockerfile": RB_DOCKERFILE, "files": {"harness/stubs.rb": RB_EXAMPLE},
             "steps": ["edit Dockerfile until `stitch build --image-only` works (the library must gem install)",
                       "optional: set [ruby] include in stitch.toml to the library's gem name (narrower instrumentation and coverage)",
                       "write stubs in harness/*.rb, then `stitch build` and `stitch probe`"]},
    "objc": {"toml": OBJC_TOML, "dockerfile": OBJC_DOCKERFILE, "files": {"harness/stubs.m": OBJC_EXAMPLE},
             "steps": ["edit Dockerfile until `stitch build --image-only` works",
                       "set [build] cflags and ldflags in stitch.toml",
                       "write stubs in harness/*.m, then `stitch build` and `stitch probe`"]},
    "rust": {"toml": RUST_TOML, "dockerfile": RUST_DOCKERFILE, "files": {"harness/stubs.rs": RUST_EXAMPLE},
             "steps": ["edit Dockerfile until `stitch build --image-only` works",
                       "in stitch.toml [rust.dependencies], name the library dependency after its crate",
                       "write stubs in harness/*.rs, then `stitch build` and `stitch probe`"]},
}


def init_project(root: Path, name=None, git=None, rev=None, path=None, language="cpp") -> str:
    from .project import normalize_language
    language = normalize_language(language)
    if language not in LANGS:
        raise SystemExit(f"error: unknown language '{language}' (one of: {', '.join(LANGS)})")
    t = LANGS[language]
    root.mkdir(parents=True, exist_ok=True)
    if (root / "stitch.toml").exists():
        raise SystemExit(f"error: {root}/stitch.toml already exists")
    name = name or root.name
    module = "".join(c if c.isalnum() else "_" for c in name.lower())
    if git:
        source = f'git = "{git}"\nrev = "{rev or "HEAD"}"   # branch, tag or commit; `stitch build --rev X` overrides it'
    else:
        source = f'path = "{path}"   # local checkout, used as-is (uncommitted changes included)'
    (root / "stitch.toml").write_text(t["toml"].format(name=name, source=source, module=module))
    (root / "Dockerfile").write_text(t["dockerfile"].format(name=name))
    (root / ".dockerignore").write_text(".stitch\n")
    (root / ".gitignore").write_text(".stitch/\n")
    (root / "harness" / "data").mkdir(parents=True, exist_ok=True)
    for rel, content in t["files"].items():
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(content)
    steps = "\n".join(f"  {i}. {s}" for i, s in enumerate(t["steps"], 1))
    return f"created {root} ({language})\n{steps}"
