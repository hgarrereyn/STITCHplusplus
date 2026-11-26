# Changelog

Each STITCH++ release is one version of the `stitch-fuzz` CLI together with the runtime and Docker
images it uses. A project pins one release in `stitch.toml` (`[project] stitch_version`), and the
CLI refuses to run a project pinned to another release.

To move a project to a newer release: read the entries after its pinned version
(`stitch changelog --since <pinned>`), make the harness changes listed under **Upgrading**, then
run `stitch upgrade` with the new CLI, and `stitch build`, `stitch check` and `stitch probe`.

Each entry has:

- **Changes**: what is new or different.
- **Upgrading**: what a harness, `stitch.toml` or project `Dockerfile` must change to move to this
  release, or "Nothing" when an existing harness works unchanged. Results can still differ
  (another runtime finds different crashes); campaigns and reproducers made with the old release
  are not affected.

Reproducers made with `stitch repro` or `stitch report` are frozen: they run with Docker alone,
on the images of the release that made them, whatever STITCH++ release is installed later.

## 0.2.0

First release of STITCH++ (the `stitch-fuzz` package; earlier 0.1.x versions are the original
STITCH, a different tool).

### Changes

- Harness languages: C/C++, Objective-C, Python, JavaScript, Java, Go, Rust, Ruby and Swift.
- Commands: `setup`, `init`, `build`, `check`, `probe`, `fuzz`, `status`, `stop`, `crashes`,
  `exec`, `minimize`, `cov`, `repro`, `write`, `try`, `report`, `bisect`, `sample`, `shell`,
  `guide`, `skill`, `changelog`, `upgrade`.
- Projects pin their release in `stitch.toml` (`[project] stitch_version`); `stitch init` writes it.
- Prebuilt images on `ghcr.io/hgarrereyn/`, tagged by source hash, never rebuilt once published.
- `stitch report`: an issue-sized reproducer (Dockerfile + file + `run.sh` + `ISSUE.md`).
  Reproducers pin package versions to the date the library was built (apt snapshot, pip, npm).

### Upgrading

Nothing: first release. A project without `stitch_version` runs with any release; add
`stitch_version = "0.2.0"` under `[project]` (or run `stitch upgrade`) to pin it.
