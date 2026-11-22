"""Docker images and containers.

Images:
  stitch/engine:<hash>              engine build outputs (engine/Dockerfile)
  stitch/<lang>:<hash>              language toolchain + runtime + engine (languages/<lang>/Dockerfile)
  stitch/<lang>-toolchain:<hash>    toolchain + runtime only (used by reproducers)
  stitch-<project>:<srcid>          the project's Dockerfile on top of the language image, one per revision
"""
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from .project import STITCH_HOME


class DockerError(Exception):
    pass


def _run(cmd, **kw):
    kw.setdefault("errors", "replace")  # program output may be any bytes
    return subprocess.run(cmd, text=True, **kw)


def check_docker():
    r = _run(["docker", "info", "--format", "{{.ServerVersion}}"], capture_output=True)
    if r.returncode != 0:
        raise DockerError("docker is not available: " + (r.stderr.strip() or r.stdout.strip())[-300:])


def image_exists(tag) -> bool:
    return _run(["docker", "image", "inspect", tag], capture_output=True).returncode == 0


LANGUAGES = ("cpp", "python", "javascript", "java", "go", "rust", "ruby", "swift")
_SKIP_PARTS = {"target", "standalone", "node_modules", "__pycache__", ".gradle", "build"}


def _tree_hash(paths):
    h = hashlib.sha1()
    for base in paths:
        base = STITCH_HOME / base
        # */standalone/ is not part of any image: the CLI copies the shim next to the file it builds
        files = [base] if base.is_file() else sorted(p for p in base.rglob("*") if p.is_file()
                                                     and not _SKIP_PARTS.intersection(p.relative_to(base).parts)
                                                     and p.suffix != ".pyc")
        for f in files:
            h.update(str(f.relative_to(STITCH_HOME)).encode())
            h.update(f.read_bytes())
    return h.hexdigest()[:12]


def engine_hash():
    return _tree_hash(["engine/Dockerfile", "engine/src", "engine/Cargo.toml", "engine/Cargo.lock"])


# Directories a language image needs besides languages/<lang>/ (copied into its build context
# under the given name): Swift harnesses run on the C/C++ runtime.
EXTRA_DIRS = {"swift": {"cpp-runtime": "languages/cpp/runtime"}}


def toolchain_hash(lang="cpp"):
    return _tree_hash([f"languages/{lang}"] + list(EXTRA_DIRS.get(lang, {}).values()))


def language_context(lang) -> Path:
    """Build context of a language image: languages/<lang>/, plus EXTRA_DIRS copied in."""
    if lang not in EXTRA_DIRS:
        return STITCH_HOME / f"languages/{lang}"
    d = Path(os.environ.get("STITCH_CACHE", Path.home() / ".cache" / "stitch")) / "contexts" / f"{lang}-{toolchain_hash(lang)}"
    if not d.exists():
        tmp = d.with_name(d.name + ".tmp")
        shutil.rmtree(tmp, ignore_errors=True)
        shutil.copytree(STITCH_HOME / f"languages/{lang}", tmp)
        for name, src in EXTRA_DIRS[lang].items():
            shutil.copytree(STITCH_HOME / src, tmp / name)
        tmp.rename(d)
    return d


def base_hash(lang="cpp"):
    return hashlib.sha1(f"{toolchain_hash(lang)}:{engine_hash()}".encode()).hexdigest()[:12]


# Prebuilt images are published per source hash by .github/workflows/release.yml:
#   <registry>/stitch-<lang>:<base_hash(lang)>   <registry>/stitch-<lang>-toolchain:<toolchain_hash(lang)>
# STITCH_REGISTRY=none always builds locally.
DEFAULT_REGISTRY = "ghcr.io/hgarrereyn"


def registry():
    r = os.environ.get("STITCH_REGISTRY", DEFAULT_REGISTRY).strip().rstrip("/")
    return None if r.lower() in ("", "none", "off") else r


def _try_pull(remote: str, local: str, quiet=False) -> bool:
    """Pull a prebuilt image and tag it locally. False if it is not published (or no network)."""
    if not quiet:
        print(f"[stitch] pulling {remote}", file=sys.stderr, flush=True)
    r = _run(["docker", "pull", "-q", remote], capture_output=True)
    if r.returncode != 0:
        return False
    return _run(["docker", "tag", remote, local], capture_output=True).returncode == 0


def build_log(name) -> Path:
    d = Path(os.environ.get("STITCH_CACHE", Path.home() / ".cache" / "stitch")) / "logs"
    d.mkdir(parents=True, exist_ok=True)
    return d / f"{name}.log"


def _buildx(args, log: Path, what: str, quiet=False):
    if not quiet:
        print(f"[stitch] building {what} (log: {log})", file=sys.stderr, flush=True)
    with open(log, "w") as f:
        r = _run(["docker", "buildx", "build", "--progress=plain", "--load", *args], stdout=f, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        # STITCH_BASE has no default (the CLI always passes it): Docker's warning about that is noise
        tail = [l for l in log.read_text(errors="replace").splitlines()
                if "InvalidDefaultArgInFrom" not in l and "warning found (use docker --debug" not in l][-60:]
        raise DockerError(f"building {what} failed (full log: {log}):\n" + "\n".join(tail))


class _build_lock:
    """One process at a time builds a given image; the others wait and then reuse it (several
    `stitch` commands started together would otherwise all build the same base image)."""

    def __init__(self, name):
        self.path = Path(os.environ.get("STITCH_CACHE", Path.home() / ".cache" / "stitch")) / "locks" / f"{name}.lock"

    def __enter__(self):
        import fcntl
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.f = open(self.path, "w")
        try:
            fcntl.flock(self.f, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            print(f"[stitch] waiting for another stitch process to finish building {self.path.stem}", file=sys.stderr, flush=True)
            fcntl.flock(self.f, fcntl.LOCK_EX)
        return self

    def __exit__(self, *a):
        self.f.close()


def _check_lang(lang):
    if lang not in LANGUAGES:
        raise DockerError(f"unknown language '{lang}' (one of: {', '.join(LANGUAGES)})")


def ensure_engine(rebuild=False) -> str:
    """The engine image (build outputs only), built once per engine version."""
    h = engine_hash()
    tag = f"stitch/engine:{h}"
    if not rebuild and not image_exists(tag) and registry():
        _try_pull(f"{registry()}/stitch-engine:{h}", tag, quiet=True)
    if rebuild or not image_exists(tag):
        with _build_lock(f"engine-{h}"):
            if rebuild or not image_exists(tag):
                _buildx(["-f", str(STITCH_HOME / "engine/Dockerfile"), "-t", tag, str(STITCH_HOME / "engine")],
                        build_log("engine"), "the STITCH++ engine (once per version, a few minutes)")
    return tag


def _variant(tag, sanitizers):
    san = ",".join(sanitizers) if sanitizers else "none"
    return tag if san == "address" else f"{tag}-{hashlib.sha1(san.encode()).hexdigest()[:6]}"


def base_tag(lang="cpp", sanitizers=("address",)) -> str:
    """Tag ensure_base() would return (without building)."""
    return _variant(f"stitch/{lang}:{base_hash(lang)}", sanitizers)


def ensure_base(lang="cpp", sanitizers=("address",), rebuild=False) -> str:
    """Language image with the engine (built or pulled if missing)."""
    _check_lang(lang)
    h = base_hash(lang)
    tag = f"stitch/{lang}:{h}"
    if not rebuild and not image_exists(tag) and registry():
        if not _try_pull(f"{registry()}/stitch-{lang}:{h}", tag):
            print(f"[stitch] no prebuilt {lang} image for this version ({h})", file=sys.stderr, flush=True)
    if rebuild or not image_exists(tag):
        with _build_lock(f"{lang}-{h}"):
            if rebuild or not image_exists(tag):  # another process may have built it while we waited
                engine = ensure_engine(rebuild=rebuild)
                ctx = language_context(lang)
                _buildx(["-f", str(ctx / "Dockerfile"), "--target", "base", "--build-arg",
                         f"STITCH_ENGINE={engine}", "-t", tag, str(ctx)],
                        build_log(f"base-{lang}"), f"the STITCH++ {lang} image locally (once per version)")
    vtag = _variant(tag, sanitizers)
    if vtag != tag and not image_exists(vtag):
        san = ",".join(sanitizers) if sanitizers else "none"
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "Dockerfile").write_text(f"FROM {tag}\nENV STITCH_SANITIZERS={san}\n")
            _buildx(["-t", vtag, d], build_log("base-variant"), f"base variant ({san})", quiet=True)
    return vtag


def ensure_cov_base(lang="cpp", rebuild=False) -> str:
    """Image for `stitch cov`. C/C++: compiler wrappers switch to source-based coverage."""
    base = ensure_base(lang, rebuild=rebuild)
    tag = f"{base}-cov"
    if rebuild or not image_exists(tag):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "Dockerfile").write_text(f"FROM {base}\nENV STITCH_COVERAGE=1 STITCH_SANITIZERS=none\n")
            _buildx(["-t", tag, d], build_log("base-cov"), "coverage base image", quiet=True)
    return tag


def ensure_toolchain(lang="cpp") -> str:
    """Language image without the engine (enough to build and run reproducers)."""
    _check_lang(lang)
    h = toolchain_hash(lang)
    tag = f"stitch/{lang}-toolchain:{h}"
    if not image_exists(tag) and registry():
        _try_pull(f"{registry()}/stitch-{lang}-toolchain:{h}", tag, quiet=True)
    if not image_exists(tag):
        ctx = language_context(lang)
        _buildx(["-f", str(ctx / "Dockerfile"), "--target", "toolchain", "-t", tag, str(ctx)], build_log(f"toolchain-{lang}"), f"the STITCH++ {lang} toolchain image")
    return tag


def ensure_dockerignore(project):
    f = project.root / ".dockerignore"
    lines = f.read_text().splitlines() if f.exists() else []
    if ".stitch" not in [l.strip().rstrip("/") for l in lines]:
        f.write_text("\n".join(lines + [".stitch"]) + "\n")


def build_project_image(project, base_tag, src_dir: Path, ident: str, no_cache=False, quiet=False, variant="") -> str:
    tag = f"stitch-{project.name}:{ident}" + (f"-{variant}" if variant else "")
    ensure_dockerignore(project)
    if not project.dockerfile.exists():
        raise DockerError(f"{project.rel(project.dockerfile)} not found (it describes how to build the library)")
    args = ["-f", str(project.dockerfile), "--build-context", f"source={src_dir}", "--build-arg", f"STITCH_BASE={base_tag}", "-t", tag]
    for k, v in (project.cfg["docker"].get("build_args") or {}).items():
        args += ["--build-arg", f"{k}={v}"]
    if project.cfg["docker"].get("platform"):
        args += ["--platform", project.cfg["docker"]["platform"]]
    if no_cache:
        args.append("--no-cache")
    _buildx(args + [str(project.root)], build_log(f"{project.name}-{ident}" + (f"-{variant}" if variant else "")), f"project image {tag}", quiet=quiet)
    return tag


def run_args(project, image, *, name=None, detach=False, tty=False, extra_mounts=(), env=None, workdir="/project"):
    args = ["docker", "run"]
    args += ["-d"] if detach else ["--rm"]
    if tty and sys.stdin.isatty():
        args += ["-it"]
    if name:
        args += ["--name", name]
    args += ["-v", f"{project.root}:/project", "-w", workdir, "--shm-size=1g", "--cap-add=SYS_PTRACE"]
    for src, dst in extra_mounts:
        args += ["-v", f"{src}:{dst}"]
    if platform.system() == "Linux":
        args += ["--user", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/tmp"]
    for k, v in (env or {}).items():
        args += ["-e", f"{k}={v}"]
    if project.cfg["docker"].get("platform"):
        args += ["--platform", project.cfg["docker"]["platform"]]
    return args + [image]


def build_errors(r, limit=3000) -> str:
    """Why a captured `stitch-build --json` run failed: the compiler output from its JSON result
    (stdout), else whatever it printed."""
    lines = (r.stdout or "").strip().splitlines()
    try:
        res = json.loads(lines[-1]) if lines else {}
    except json.JSONDecodeError:
        res = {}
    text = "\n".join(f"--- {e.get('file', 'build')} ---\n{(e.get('output') or '').rstrip()}"
                     for e in res.get("errors", []) if (e.get("output") or "").strip())
    if not text:
        text = ((r.stderr or "") + (r.stdout or "")).strip() or f"stitch-build exited with {r.returncode} and printed nothing"
    return text[-limit:]


def run(project, image, cmd, *, capture=False, check=False, timeout=None, **kw):
    args = run_args(project, image, **kw) + list(cmd)
    r = _run(args, capture_output=capture, timeout=timeout)
    if check and r.returncode != 0:
        raise DockerError(f"command failed in container: {' '.join(cmd)}\n{(r.stderr or '')[-3000:] if capture else ''}")
    return r


def container_state(name):
    r = _run(["docker", "inspect", name, "--format", "{{json .State}}"], capture_output=True)
    if r.returncode != 0:
        return None
    return json.loads(r.stdout)


def stop(name):
    _run(["docker", "stop", "-t", "5", name], capture_output=True)


def remove(name):
    _run(["docker", "rm", "-f", name], capture_output=True)
