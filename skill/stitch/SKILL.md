---
name: stitch
description: Fuzz the API of a C, C++, Objective-C, Python, JavaScript, Java, Go, Rust, Ruby or Swift library with STITCH++. Covers the Dockerfile that builds the library, harness stubs, fuzzing campaigns, the health report, crash triage, coverage and reproducers. Use when asked to fuzz a library, write or fix a STITCH++ harness, or triage STITCH++ crashes.
---

# STITCH++

STITCH++ fuzzes a library through its API. You write **stubs**: small typed functions that each
make one API call. The fuzzer chains stubs into **programs**, passes objects from one call to the
next, generates the arguments, and reports crashes, undeclared exceptions and failed checks.
Everything builds and runs in containers.

**Start by reading [reference/overview.md](reference/overview.md)**: the concepts, setup, and the
workflow checklist to follow. Then read your language's file before writing stubs.

The guide:

| File | Read it when |
|---|---|
| [overview.md](reference/overview.md) | first: concepts, setup and the workflow |
| [stubs.md](reference/stubs.md) | writing stubs and data types |
| [probe.md](reference/probe.md) | reading the `stitch probe` / `stitch status` report |
| [triage.md](reference/triage.md) | deciding whether a crash is a harness bug or a library bug |
| [reproducers.md](reference/reproducers.md) | turning a crash into a short standalone file; frozen reproducers for maintainers (`stitch report`) |
| [coverage.md](reference/coverage.md) | reading `stitch cov` and closing coverage gaps |
| [stitch-toml.md](reference/stitch-toml.md) | every key of `stitch.toml`; the pinned STITCH++ version (`stitch_version`) |
| [cli.md](reference/cli.md) | looking up a command or a file under `.stitch/` |

Languages (project setup, `Dockerfile`, `stitch.toml`, harness syntax, reproducers):
[cpp.md](reference/cpp.md) (C/C++), [objc.md](reference/objc.md), [python.md](reference/python.md),
[javascript.md](reference/javascript.md), [java.md](reference/java.md), [go.md](reference/go.md),
[rust.md](reference/rust.md), [ruby.md](reference/ruby.md), [swift.md](reference/swift.md).
