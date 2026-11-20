"""Library source: a git repository at some revision, or a local directory used as-is.

Git sources are mirrored once under ~/.cache/stitch2/git and exported per commit, so building
any revision (for bisection) is cheap after the first fetch.
"""
import hashlib
import os
import shutil
import subprocess
import tarfile
from pathlib import Path

CACHE = Path(os.environ.get("STITCH_CACHE", Path.home() / ".cache" / "stitch2"))


class SourceError(Exception):
    pass


def _git(*args, cwd=None, check=True):
    r = subprocess.run(["git", *args], cwd=cwd, text=True, capture_output=True)
    if check and r.returncode != 0:
        raise SourceError(f"git {' '.join(args)} failed: {r.stderr.strip()[-800:]}")
    return r.stdout.strip()


class Source:
    """Resolved source for one build: `context` is the directory passed as the `source` build
    context; `ident` is a short id used in image tags."""

    def __init__(self, project):
        self.project = project
        cfg = project.cfg["source"]
        self.git_url = cfg.get("git")
        self.path = cfg.get("path")
        self.default_rev = cfg.get("rev") or "HEAD"
        self.submodules = bool(cfg.get("submodules"))
        self.keep_git = bool(cfg.get("keep_git"))
        if not self.git_url and not self.path:
            raise SourceError("stitch.toml needs [source] git = URL or path = DIR")

    # ---------------------------------------------------------------- git helpers
    @property
    def is_git(self):
        return bool(self.git_url) or (self.path and (Path(self.path) / ".git").exists())

    def repo(self) -> Path:
        """A git directory with the full history (mirror of the URL, or the local checkout)."""
        if self.git_url:
            key = hashlib.sha1(self.git_url.encode()).hexdigest()[:16]
            mirror = CACHE / "git" / key
            if not mirror.exists():
                mirror.parent.mkdir(parents=True, exist_ok=True)
                print(f"[stitch] cloning {self.git_url} (once)...", flush=True)
                _git("clone", "--mirror", "--quiet", self.git_url, str(mirror))
            return mirror
        return Path(self.path)

    def fetch(self):
        if self.git_url:
            _git("fetch", "--quiet", "--prune", "--tags", "origin", cwd=self.repo(), check=False)

    def resolve(self, rev=None) -> str:
        rev = rev or self.default_rev
        repo = self.repo()
        r = subprocess.run(["git", "rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}"], cwd=repo, text=True, capture_output=True)
        if r.returncode != 0 and self.git_url:
            self.fetch()
            r = subprocess.run(["git", "rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}"], cwd=repo, text=True, capture_output=True)
        if r.returncode != 0:
            raise SourceError(f"unknown revision '{rev}'")
        return r.stdout.strip()

    def describe(self, commit) -> dict:
        repo = self.repo()
        fmt = _git("show", "-s", "--format=%H%n%an%n%aI%n%s", commit, cwd=repo).split("\n", 3)
        d = {"commit": fmt[0], "author": fmt[1], "date": fmt[2], "subject": fmt[3] if len(fmt) > 3 else ""}
        if self.git_url and "github.com" in self.git_url:
            base = self.git_url[:-4] if self.git_url.endswith(".git") else self.git_url
            d["url"] = f"{base}/commit/{fmt[0]}"
        return d

    # ---------------------------------------------------------------- materialize
    def materialize(self, rev=None):
        """Returns (context_dir, ident, commit_or_None)."""
        if self.path and rev is None:
            p = Path(self.path)
            if not p.is_dir():
                raise SourceError(f"source path {p} does not exist")
            commit = None
            if (p / ".git").exists():
                commit = _git("rev-parse", "HEAD", cwd=p, check=False) or None
            return p, "local", commit
        if not self.is_git:
            raise SourceError("a revision was requested but the source is not a git repository")
        commit = self.resolve(rev)
        repo = self.repo()
        key = hashlib.sha1((str(repo) + commit + str(self.submodules) + str(self.keep_git)).encode()).hexdigest()[:10]
        dest = CACHE / "src" / f"{key}-{commit[:12]}"
        if not (dest / ".stitch-complete").exists():
            if dest.exists():
                shutil.rmtree(dest)
            dest.mkdir(parents=True)
            if self.submodules or self.keep_git:
                _git("clone", "--quiet", "--no-checkout", str(repo), str(dest))
                _git("checkout", "--quiet", commit, cwd=dest)
                if self.submodules:
                    url = self.git_url
                    if url:
                        _git("remote", "set-url", "origin", url, cwd=dest)
                    _git("submodule", "update", "--init", "--recursive", "--quiet", cwd=dest)
                if not self.keep_git:
                    shutil.rmtree(dest / ".git", ignore_errors=True)
            else:
                tar = dest.parent / f"{dest.name}.tar"
                _git("archive", "--format=tar", "-o", str(tar), commit, cwd=repo)
                with tarfile.open(tar) as t:
                    t.extractall(dest)
                tar.unlink()
            (dest / ".stitch-complete").write_text(commit)
        return dest, commit[:12], commit

    def snapshot_tar(self, out: Path):
        """Archive the local source directory (for self-contained reproducers)."""
        p = Path(self.path)
        skip = {".git", ".stitch", "node_modules", "__pycache__"}
        with tarfile.open(out, "w:gz") as t:
            for root, dirs, files in os.walk(p):
                dirs[:] = [d for d in dirs if d not in skip]
                for f in files:
                    full = Path(root) / f
                    t.add(full, arcname=str(full.relative_to(p)))
