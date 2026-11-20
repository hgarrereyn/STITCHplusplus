"""Docker images and containers.

Images:
  stitch/base:<hash>          toolchain + runtime + engine, built from this checkout
  stitch/toolchain:<hash>     toolchain + runtime only (used by reproducers)
  stitch-<project>:<srcid>    the project's Dockerfile on top of the base, one per source revision
"""
import hashlib
import json
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

from .project import STITCH_HOME


class DockerError(Exception):
    pass


def _run(cmd, **kw):
    return subprocess.run(cmd, text=True, **kw)


def check_docker():
    r = _run(["docker", "info", "--format", "{{.ServerVersion}}"], capture_output=True)
    if r.returncode != 0:
        raise DockerError("docker is not available: " + (r.stderr.strip() or r.stdout.strip())[-300:])


def image_exists(tag) -> bool:
    return _run(["docker", "image", "inspect", tag], capture_output=True).returncode == 0


def _tree_hash(paths):
    h = hashlib.sha1()
    for base in paths:
        base = STITCH_HOME / base
        # runtime/standalone/ is not part of any image: the CLI copies the shim next to the file it builds
        files = [base] if base.is_file() else sorted(p for p in base.rglob("*") if p.is_file() and "target" not in p.parts
                                                     and "standalone" not in p.parts)
        for f in files:
            h.update(str(f.relative_to(STITCH_HOME)).encode())
            h.update(f.read_bytes())
    return h.hexdigest()[:12]


def stitch_hash():
    return _tree_hash(["docker/base.Dockerfile", "docker/bin", "runtime", "engine/src", "engine/Cargo.toml", "engine/Cargo.lock"])


def toolchain_hash():
    return _tree_hash(["docker/base.Dockerfile", "docker/bin", "runtime"])


# Prebuilt images are published per source hash by .github/workflows/release.yml:
#   <registry>/stitch-base:<stitch_hash()>   <registry>/stitch-toolchain:<toolchain_hash()>
# STITCH_REGISTRY=none always builds locally.
DEFAULT_REGISTRY = "ghcr.io/hgarrereyn"


def registry():
    r = os.environ.get("STITCH_REGISTRY", DEFAULT_REGISTRY).strip().rstrip("/")
    return None if r.lower() in ("", "none", "off") else r


def _try_pull(remote: str, local: str, quiet=False) -> bool:
    """Pull a prebuilt image and tag it locally. False if it is not published (or no network)."""
    if not quiet:
        print(f"[stitch] pulling {remote}", flush=True)
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
        print(f"[stitch] building {what} (log: {log})", flush=True)
    with open(log, "w") as f:
        r = _run(["docker", "buildx", "build", "--progress=plain", "--load", *args], stdout=f, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        tail = log.read_text(errors="replace").splitlines()[-60:]
        raise DockerError(f"building {what} failed (full log: {log}):\n" + "\n".join(tail))


def base_tag(sanitizers=("address",)) -> str:
    """Tag ensure_base() would return (without building)."""
    tag = f"stitch/base:{stitch_hash()}"
    san = ",".join(sanitizers) if sanitizers else "none"
    return tag if san == "address" else f"{tag}-{hashlib.sha1(san.encode()).hexdigest()[:6]}"


def ensure_base(sanitizers=("address",), rebuild=False) -> str:
    """Tag of the base image for this checkout (built if missing)."""
    h = stitch_hash()
    tag = f"stitch/base:{h}"
    if not rebuild and not image_exists(tag) and registry():
        if not _try_pull(f"{registry()}/stitch-base:{h}", tag):
            print(f"[stitch] no prebuilt base image for this version ({h})", flush=True)
    if rebuild or not image_exists(tag):
        _buildx(["-f", str(STITCH_HOME / "docker/base.Dockerfile"), "--target", "base", "-t", tag, "-t", "stitch/base:latest", str(STITCH_HOME)],
                build_log("base"), "the STITCH base image locally (once per version, about 10 minutes)")
    san = ",".join(sanitizers) if sanitizers else "none"
    if san == "address":
        return tag
    vtag = f"{tag}-{hashlib.sha1(san.encode()).hexdigest()[:6]}"
    if not image_exists(vtag):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "Dockerfile").write_text(f"FROM {tag}\nENV STITCH_SANITIZERS={san}\n")
            _buildx(["-t", vtag, d], build_log("base-variant"), f"base variant ({san})", quiet=True)
    return vtag


def ensure_toolchain() -> str:
    h = toolchain_hash()
    tag = f"stitch/toolchain:{h}"
    if not image_exists(tag) and registry():
        _try_pull(f"{registry()}/stitch-toolchain:{h}", tag, quiet=True)
    if not image_exists(tag):
        _buildx(["-f", str(STITCH_HOME / "docker/base.Dockerfile"), "--target", "toolchain", "-t", tag, str(STITCH_HOME)],
                build_log("toolchain"), "the STITCH toolchain image")
    return tag


def ensure_dockerignore(project):
    f = project.root / ".dockerignore"
    lines = f.read_text().splitlines() if f.exists() else []
    if ".stitch" not in [l.strip().rstrip("/") for l in lines]:
        f.write_text("\n".join(lines + [".stitch"]) + "\n")


def build_project_image(project, base_tag, src_dir: Path, ident: str, no_cache=False, quiet=False) -> str:
    tag = f"stitch-{project.name}:{ident}"
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
    _buildx(args + [str(project.root)], build_log(f"{project.name}-{ident}"), f"project image {tag}", quiet=quiet)
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
