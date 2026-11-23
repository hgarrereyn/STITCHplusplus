# The `stitch` CLI

Every command works the same way for every language. Run a command from inside a project (any
directory below its `stitch.toml`), or pass `-C <dir>`. Add `--json` for machine-readable output.
`stitch <command> --help` lists every flag.

## Setup

| Command | What it does |
|---|---|
| `stitch setup [--lang L ...]` | Check Docker and get the language images (`cpp` by default; `python`, `javascript`, `java`, `go`, `rust`, `ruby`, `swift`, `objc`, or `all`). Pulls prebuilt images when they exist, and otherwise builds them. |
| `stitch skill [DIR] [--user]` | Install the agent skill into `./.claude/skills` (or `DIR`, or `~/.claude/skills`). |
| `stitch guide [TOPIC]` | Print the workflow guide, or a language's harness API (`cpp`, `python`, ...), or `reproducers`. |

## Write a harness

| Command | What it does |
|---|---|
| `stitch init DIR --git URL --rev REV [--lang L]` | Create a project: `stitch.toml`, a `Dockerfile` that builds the library, and `harness/`. Use `--path` for a local checkout. |
| `stitch build [--rev REV]` | Build the library image and the harness. `--harness-only` reuses the image. |
| `stitch check` | Build plus static checks on unreachable stubs, attribute keys and data types. |
| `stitch sample DATATYPE [-n N]` | Print values produced by a data type's decoder or seeds. |
| `stitch shell [CMD]` | Open a shell in the project image, with the project at `/project`. |

## Fuzz

| Command | What it does |
|---|---|
| `stitch probe [STUBS] [--secs N]` | Short run and a health report: which stubs fail, why, and what blocks progress. |
| `stitch fuzz [--jobs N] [--secs N] [--name NAME]` | Start a campaign in the background. `--fresh` discards the corpus, `--foreground` stays attached. |
| `stitch status [NAME] [--mutations]` | Campaign progress, coverage and the health report. |
| `stitch stop [NAME]` | Stop a campaign. |
| `stitch cov [CAMPAIGN ...]` | Replay the corpus with the language's coverage tool. Writes `lcov.info`, HTML and a summary with never-called functions. |

## Triage

Crash ids (`c1`, `c2`, ...) refer to the latest campaign. Use `probe:c3` or `--campaign NAME` for
another campaign, or pass a `.stp` program file.

| Command | What it does |
|---|---|
| `stitch crashes [NAME]` | Re-run and bucket crashes (including timeouts) into unique ids. |
| `stitch exec CRASH [--trace \| --args]` | Run a program with the current harness and print its calls and the report. |
| `stitch exec CRASH --rev REV` | Run it against another library revision, e.g. `HEAD`, to see if it is fixed upstream. |
| `stitch minimize CRASH` | Reduce a crashing program. Crash ids use the minimized program afterwards. |
| `stitch bisect CRASH [--good REV] [--bad REV]` | Find the commit that introduced the crash, or with `--fixed`, the one that fixed it. |

## Reproducers

| Command | What it does |
|---|---|
| `stitch repro CRASH` | Write a self-contained Docker reproducer folder and verify it from scratch. |
| `stitch write CRASH` | Write the crash as one standalone source file in the harness language, with every argument filled in. |
| `stitch try FILE` | Build and run an edited standalone file and check that it still fails the same way. |

See [`skill/stitch/reference/reproducers.md`](../skill/stitch/reference/reproducers.md) for the
`write` / `try` workflow.

## Development

Run `bin/stitch` from a checkout, or `pip install -e .`. Tests are in [`tests/`](../tests/):
`smoke.sh` runs the whole C/C++ pipeline, `languages.py` runs every command for each language.
Pushing a `vX.Y.Z` tag publishes the images to GHCR and the package to PyPI
(`.github/workflows/release.yml`).
