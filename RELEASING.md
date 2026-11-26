# Releasing

A release publishes the `stitch-fuzz` package to PyPI and the Docker images to
`ghcr.io/hgarrereyn/`. Three workflows:

| Workflow | Runs | Does |
|---|---|---|
| [`publish-pypi.yml`](.github/workflows/publish-pypi.yml) | on a pushed tag `v<version>` | the release: images, then PyPI, then a GitHub Release |
| [`publish-testpypi.yml`](.github/workflows/publish-testpypi.yml) | by hand, any branch | uploads a test build to TestPyPI and installs it back |
| [`images.yml`](.github/workflows/images.yml) | called by `publish-pypi.yml`, or by hand on any branch | builds and pushes the Docker images |

## Cutting a release

1. On main, set `__version__` in `cli/stitch_fuzz/__init__.py`. This is the only place the version
   is set: `pyproject.toml` reads it from there. Add a `## <version>` entry to `CHANGELOG.md`
   with **Changes** and **Upgrading** (what a harness, `stitch.toml` or project `Dockerfile` must
   change to move to it, or "Nothing"): projects pin a release in `stitch.toml`, and agents read
   this to upgrade them. CI and the release workflow fail without the entry. Merge the change once
   CI is green.
2. Optional rehearsal: Actions > publish-testpypi > Run workflow (on main). It uploads
   `<version>.dev<run number>` to TestPyPI and installs it from there. Untick "dev" to upload the
   exact version instead (TestPyPI accepts each version only once).
3. Tag the commit on main and push the tag:
   ```sh
   git tag v0.2.0 && git push origin v0.2.0
   ```
4. `publish-pypi.yml` then:
   - **prepare**: checks that the tag matches `__version__`, is on main and has a changelog entry,
     and builds the wheel and sdist once;
   - **images**: builds and pushes the amd64 and arm64 images that are not published yet, tags
     them `:<version>`, and checks that every image can be pulled without logging in;
   - **wheel**: checks that the built wheel pulls its image instead of building it;
   - **pypi**: uploads the wheel and sdist to PyPI;
   - **github**: creates a GitHub Release with the files, each image's digest, and the changelog entry.

   If a job fails (for example on the first release, see GHCR visibility below), fix the cause and
   use "Re-run failed jobs": the finished jobs are not repeated.

To build and push images without releasing (from any branch): Actions > images > Run workflow.

## How images are versioned

The CLI never asks for `latest`. Each image tag is a hash of the sources that go into that image,
and the CLI computes the hashes from the files it ships (`stitch_fuzz/docker.py`):

| Image | Tag | Used by |
|---|---|---|
| `stitch-engine` | hash of `engine/` | the language images |
| `stitch-<lang>` | hash of the toolchain + engine | `stitch setup`, fuzzing |
| `stitch-<lang>-toolchain` | hash of `languages/<lang>/` | reproducers (`stitch repro`, `stitch report`) |

So each `stitch-fuzz` version pulls the images it was released with, and nothing newer. A project
pins its release in `stitch.toml` (`[project] stitch_version`), and the CLI refuses another one, so
a project keeps building on the same images until someone runs `stitch upgrade`. If an
image's sources did not change between releases, its hash does not change and the new release
reuses the image already published.

`images.yml` never pushes a hash tag that already exists. A hash tag names the same image forever,
even though rebuilding it later would give different bytes (newer apt packages). Reproducers made
by `stitch repro` and `stitch report` pin the toolchain image by digest.

Releases also get alias tags `:<version>` (`stitch-python:0.2.0`) for people to read. Nothing
relies on them.

A version that is not published (a checkout of main, a fork, a TestPyPI dev build) builds its
images locally the first time, unless `images.yml` has published them; `STITCH_REGISTRY=none`
always builds locally.

## One-time setup

### PyPI and TestPyPI

Trusted publishers (Manage > Publishing on each site), owner `hgarrereyn`, repository
`STITCHplusplus`:

| Site | Workflow | Environment |
|---|---|---|
| pypi.org | `publish-pypi.yml` | `pypi` |
| test.pypi.org | `publish-testpypi.yml` | `testpypi` |

The upload jobs run in the GitHub environments `pypi` and `testpypi`, which GitHub creates on
their first run. If the publisher on PyPI names an environment, it must be this one; if it names
none, any works. To approve each PyPI upload by hand, add yourself as a required reviewer of the
`pypi` environment (Settings > Environments).

### GHCR visibility

GitHub creates a container package the first time a workflow pushes it, and makes it **private**.
On the first run of `images.yml`, its `public` job fails and lists every package that is not
public yet, with the link to its settings page. For each one, open the link, go to Danger Zone >
Change visibility > Public, then re-run the failed jobs. There are 19 packages: the engine, plus
an image and a toolchain image for each of the 9 languages. This is needed only once per package,
which means again only when a new language is added. Running `images.yml` by hand before the first
release gets this done early.

The workflow labels each image with `org.opencontainers.image.source`, so the packages show up on
the repository page.
