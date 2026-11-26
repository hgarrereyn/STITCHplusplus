# stitch.toml

Every STITCH++ project is a folder with a `stitch.toml`, a `Dockerfile` that builds the library,
and `harness/` with the stubs. `stitch init` writes all three. Commands find the project by
looking for `stitch.toml` in the current directory and its parents (`stitch -C DIR` picks one).

```toml
[project]
stitch_version = "0.2.0"   # the STITCH++ release this harness is written for
name = "cjson"
language = "cpp"

[source]
git = "https://github.com/DaveGamble/cJSON"
rev = "v1.7.18"

[build]
cflags = ["-I/src"]
ldflags = ["/src/build/libcjson.a"]

[fuzz]
timeout_ms = 2000
```

Only `[source]` is required. Paths that the build uses (`cflags`, `classpath`, `path = "/src"`, ...)
are paths **inside the image**, where the library source is at `/src`. A relative
`[source] path` is the exception: it is on your machine, relative to the folder of `stitch.toml`.

## The STITCH++ version

`[project] stitch_version` pins the project to one release of STITCH++: one version of the
`stitch-fuzz` CLI, and with it one runtime and one set of Docker images. The images are tagged by a
hash of their sources and never change once published, so the same version always builds and
fuzzes the harness the same way.

The pin is exact. A CLI of any other version stops with:

```
error: this project is pinned to STITCH++ 0.2.0 (stitch.toml [project] stitch_version), but the installed stitch-fuzz is 0.3.0.
  use the pinned version:  pip install stitch-fuzz==0.2.0
                           (or without installing: uvx --from stitch-fuzz==0.2.0 stitch ...)
  or move the project to 0.3.0: read `stitch changelog --since 0.2.0`, update the harness, then run `stitch upgrade`
```

- **To work on a project** (fuzz, triage, write reproducers): install the pinned version, as the
  message says. Do not change the pin to make the error go away.
- **To move a project to a newer release:** with the newer CLI installed, read
  `stitch changelog` (in a project it lists the releases after the pinned one) and make the
  harness changes each entry lists under **Upgrading**. Then `stitch upgrade` rewrites
  `stitch_version`, and `stitch build`, `stitch check` and `stitch probe` confirm the harness
  works. Start new campaigns after an upgrade (`stitch fuzz --fresh`): the old ones were made
  with the old runtime.
- `stitch init` writes the installed version. A project without `stitch_version` runs with any
  release (no check); `stitch upgrade` adds the pin.
- `STITCH_ANY_VERSION=1` turns the error into a warning, for developing STITCH++ itself.

`CHANGELOG.md` (in the repository, and `stitch changelog` offline) describes every release.

## A harness checked into the library's repository

A harness can live in the repository it tests, for example as a `stitch/` folder:

```
mylib/
  src/ ...
  stitch/
    stitch.toml        [source] path = ".."
    Dockerfile
    harness/
    .gitignore         .stitch/ (build output, campaigns)
```

```toml
[project]
stitch_version = "0.2.0"
name = "mylib"

[source]
path = ".."   # the repository this folder is in (uncommitted changes included)
```

Anyone who clones the repository runs it with the version in `stitch.toml`:

```sh
cd mylib/stitch
pip install "stitch-fuzz==$(sed -n 's/^stitch_version *= *"\(.*\)".*/\1/p' stitch.toml)"
stitch setup --lang cpp && stitch build && stitch probe
```

`stitch repro` and `stitch report` made from such a project fetch the library from the
repository's `origin` at the current commit when that commit is pushed and nothing outside
`stitch/` is modified, instead of bundling the source.

## [project]

| Key | Default | Meaning |
|---|---|---|
| `stitch_version` | none: any release | The STITCH++ release this harness is written for (above). |
| `name` | the folder name | Project name, used in image and container names (lowercased; other characters than letters, digits, `-_.` become `-`). |
| `language` | `"cpp"` | Harness language: `cpp` (C and C++), `objc`, `python`, `javascript`, `java`, `go`, `rust`, `ruby`, `swift`. Also accepted: `c`, `c++`, `js`, `node`, `typescript`, `golang`, `py`, `rb`, `kotlin`, `objective-c`. |

## [source]

The library under test. Set `git` or `path`.

| Key | Default | Meaning |
|---|---|---|
| `git` | | Repository URL. Mirrored once under `~/.cache/stitch/git`; every revision builds from it. |
| `rev` | `"HEAD"` | Branch, tag or commit to build. `stitch build --rev X` (and `exec`, `try`, `cov`) override it; `stitch bisect` walks the history. |
| `path` | | A local directory, used as-is with uncommitted changes. Relative to the folder of `stitch.toml`. If it is a git checkout, `--rev` and `bisect` work on its history. |
| `submodules` | `false` | Check out git submodules. |
| `keep_git` | `false` | Keep `.git` in `/src` (for builds that run `git describe`). |

## [docker]

| Key | Default | Meaning |
|---|---|---|
| `dockerfile` | `"Dockerfile"` | The Dockerfile that builds the library, relative to the project folder. It starts `ARG STITCH_BASE` / `FROM ${STITCH_BASE}` and copies the source with `COPY --from=source . /src`. |
| `build_args` | `{}` | Extra `--build-arg` values for it, e.g. `{ CMAKE_FLAGS = "-DFOO=ON" }`. |
| `context_files` | `[]` | Files and folders of the project the Dockerfile `COPY`s (patches, scripts). Reproducers include them. |
| `platform` | none | `--platform` for building and running, e.g. `"linux/amd64"` for a library that does not build on arm64. |

## [build]

How the harness compiles. `cflags`, `ldflags`, `std` and `sources` apply to C/C++ and
Objective-C; `sanitizers` to every compiled language.

| Key | Default | Meaning |
|---|---|---|
| `cflags` | `[]` | Compiler flags for the harness, e.g. `["-I/src/include"]`. |
| `ldflags` | `[]` | Libraries to link, e.g. `["/src/build/libfoo.a"]` or `["-L/src/build", "-lfoo"]`. Objective-C: GNUstep Base is linked already. |
| `sanitizers` | `["address"]` | `"address"`, `"undefined"` (UBSan), or `[]`. Also used by the library build (the compiler wrappers read it) and by Rust when `[rust] sanitizers` is not set. |
| `std` | `"c++17"` | `-std=` for C++ and Objective-C++ harness files. |
| `sources` | C/C++: `harness/**/*.{cpp,cc,cxx,c}`; Objective-C: `harness/**/*.{m,mm,c}` | Harness source globs, relative to the project folder. |

## [fuzz]

| Key | Default | Meaning |
|---|---|---|
| `timeout_ms` | `2000` (`3000` in a new Java project) | Time per program. A slower program is reported as a timeout. |
| `max_nodes` | `24` | Most calls per program. |
| `jobs` | `1` | Workers of `stitch fuzz` (`--jobs` overrides). |
| `exceptions` | `"finding"` | An uncaught exception the stub did not declare is a bug (`"finding"`), or just a failed call (`"bail"`). Every language except Swift (whose errors are values). |
| `persistent` | C/C++, Objective-C, Swift: `200`; Rust: `5000`; others: `0` | Programs a worker process runs before it is replaced (`0`: one process until it crashes or grows too large). Lower it if state leaks between programs. |

## Language sections

Each language reads its own section. The language pages (`stitch guide <language>`) show them in
context.

### [python]

| Key | Default | Meaning |
|---|---|---|
| `include` | every module outside the standard library | Import names of the code under test, e.g. `["yaml"]`: coverage and comparison tracing are limited to them. |
| `exclude` | `[]` | Module prefixes to leave out, e.g. `["tomlkit._compat"]`. |

### [javascript]

| Key | Default | Meaning |
|---|---|---|
| `include` | every package | npm package names (or paths) to instrument, e.g. `["js-yaml"]`. |
| `node_options` | `["--max-old-space-size=4096"]` | Extra `node` options. |

### [java]

| Key | Default | Meaning |
|---|---|---|
| `classpath` | `["/deps/*"]` | The library and its dependencies (jars the Dockerfile puts in `/deps`). |
| `include` | every non-JDK class | Packages of the code under test, e.g. `["com.example.lib"]`. |
| `sources` | `/src/**/src/main/java` | Source roots, for coverage reports. |
| `jvm_options` | `["-Xmx2g", "-Xss8m", "-XX:+UseSerialGC"]` | JVM options. |

### [go]

| Key | Default | Meaning |
|---|---|---|
| `module` | from go.mod or the git URL (`stitch init`) | Import path of the library: the `module` line of its go.mod. |
| `replace` | `"/src"` when `/src/go.mod` exists | Where that module's source is in the image. |
| `instrument` | `["all"]`: every package but the standard library | Packages to instrument. Also the coverage package list. |
| `race` | `false` | Build with the race detector (data races are reported). |
| `tags` | `[]` | Extra build tags. |

### [rust]

| Key | Default | Meaning |
|---|---|---|
| `[rust.dependencies]` | `<name> = { path = "/src" }` (`stitch init`) | The harness crate's dependencies, written as in `Cargo.toml`: the library (its crate name from `/src/Cargo.toml`; a workspace member: `path = "/src/<crate>"`) and anything else the stubs use. |
| `overflow_checks` | `true` | Integer overflow panics. |
| `sanitizers` | `[build] sanitizers` | `["address"]` adds AddressSanitizer; `[]` on targets without it. |

### [swift]

| Key | Default | Meaning |
|---|---|---|
| `dependencies` | `['.package(path: "/src")']` (`stitch init`) | SwiftPM dependencies of the harness. A path dependency on `/src` is named `src`. |
| `products` | the first `.library` in `/src/Package.swift` (`stitch init`) | Library products the harness imports, e.g. `['.product(name: "SwiftSoup", package: "src")']`. |
| `build_jobs` | SwiftPM's default | Parallel compile jobs (lower it when the build runs out of memory). |
| `swiftc_flags` | `[]` | Extra compiler flags, e.g. `["-no-whole-module-optimization"]` for a big module that otherwise compiles in one process. |

### [ruby]

| Key | Default | Meaning |
|---|---|---|
| `include` | everything but the standard library (`stitch init` writes `["/src/lib"]`) | Code under test: path prefixes, or names of installed gems. |
| `exclude` | `[]` | Gem names or path prefixes to leave out. |

### [objc]

| Key | Default | Meaning |
|---|---|---|
| `arc` | `true` | Compile the harness with ARC. |
