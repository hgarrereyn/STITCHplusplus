# Releasing

A release publishes the `stitch-fuzz` package to PyPI and the Docker images to
`ghcr.io/hgarrereyn/`. It runs from [`.github/workflows/release.yml`](.github/workflows/release.yml)
when you push a tag `v<version>`.

## Cutting a release

1. On main, set `__version__` in `cli/stitch_fuzz/__init__.py`. This is the only place the version
   is set: `pyproject.toml` reads it from there. Merge the change once CI is green.
2. Tag that commit on main and push the tag:
   ```sh
   git tag v0.2.0 && git push origin v0.2.0
   ```
3. The workflow:
   - checks that the tag matches `__version__` and is on main, and builds the wheel and sdist;
   - builds and pushes the amd64 and arm64 images that are not published yet;
   - checks that every image can be pulled without logging in, and that the built wheel pulls its
     image instead of building it;
   - publishes to PyPI, then creates a GitHub Release whose notes list each image's digest.

To build and push images without releasing (from any branch), run the workflow by hand: Actions >
release > Run workflow. It publishes to PyPI only if you tick `publish_pypi`.

## How images are versioned

The CLI never asks for `latest`. Each image tag is a hash of the sources that go into that image,
and the CLI computes the hashes from the files it ships (`stitch_fuzz/docker.py`):

| Image | Tag | Used by |
|---|---|---|
| `stitch-engine` | hash of `engine/` | the language images |
| `stitch-<lang>` | hash of the toolchain + engine | `stitch setup`, fuzzing |
| `stitch-<lang>-toolchain` | hash of `languages/<lang>/` | reproducers (`stitch repro`) |

So each `stitch-fuzz` version pulls the images it was released with, and nothing newer. If an
image's sources did not change between releases, its hash does not change and the new release
reuses the image already published.

The workflow never pushes a hash tag that already exists. A hash tag names the same image forever,
even though rebuilding it later would give different bytes (newer apt packages). Reproducers made
by `stitch repro` pin the toolchain image by digest, and fall back to building it from the copy of
`languages/<lang>/` they contain.

Tagged releases also get alias tags `:<version>` (`stitch-python:0.2.0`) for people to read.
Nothing relies on them.

A version that is not published (a checkout of main, a fork) builds its images locally the first
time; `STITCH_REGISTRY=none` always builds locally.

## One-time setup

### PyPI

`stitch-fuzz` already exists on PyPI (0.1.2 is the original STITCH). Add this repository as a
trusted publisher there: pypi.org > Your projects > stitch-fuzz > Manage > Publishing > Add a new
publisher > GitHub:

| Field | Value |
|---|---|
| Owner | `hgarrereyn` |
| Repository | `STITCHplusplus` |
| Workflow | `release.yml` |
| Environment | `pypi` |

On GitHub, create the `pypi` environment (Settings > Environments > New environment). Add yourself
as a required reviewer if you want to approve each upload by hand.

### GHCR visibility

GitHub creates a container package the first time a workflow pushes it, and makes it **private**.
On the first release, the `verify` job fails and lists every package that is not public yet, with
the link to its settings page. For each one, open the link, go to Danger Zone > Change
visibility > Public, then re-run the failed jobs of the workflow. There are 19 packages: the
engine, plus an image and a toolchain image for each of the 9 languages. This is needed only once
per package, which means again only when a new language is added.

The workflow labels each image with `org.opencontainers.image.source`, so the packages show up on
the repository page.
