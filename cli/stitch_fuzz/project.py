"""Project discovery and configuration (stitch.toml)."""
import copy
import json
import os
from pathlib import Path

from . import minitoml

_PKG = Path(__file__).resolve().parent
# Installed package: runtime, Docker files, engine sources and the skill ship in stitch_fuzz/assets.
# Source checkout: they live at the repository root.
STITCH_HOME = _PKG / "assets" if (_PKG / "assets" / "docker").is_dir() else _PKG.parents[1]

DEFAULTS = {
    "project": {"name": None},
    "source": {"git": None, "rev": None, "path": None, "submodules": False, "keep_git": False},
    "docker": {"dockerfile": "Dockerfile", "platform": None, "build_args": {}},
    "build": {"cflags": [], "ldflags": [], "sanitizers": ["address"], "std": "c++17"},
    "fuzz": {"timeout_ms": 2000, "max_nodes": 24, "jobs": 1, "exceptions": "finding", "persistent": 200},
}


class ProjectError(Exception):
    pass


def _merge(base, over):
    out = copy.deepcopy(base)
    for k, v in (over or {}).items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _merge(out[k], v)
        else:
            out[k] = v
    return out


class Project:
    def __init__(self, root: Path):
        self.root = root.resolve()
        raw = minitoml.loads((self.root / "stitch.toml").read_text())
        self.cfg = _merge(DEFAULTS, raw)
        if not self.cfg["project"]["name"]:
            self.cfg["project"]["name"] = self.root.name
        name = self.cfg["project"]["name"]
        self.name = "".join(c if c.isalnum() or c in "-_." else "-" for c in name.lower())
        src = self.cfg["source"]
        if src.get("path"):
            p = Path(src["path"]).expanduser()
            src["path"] = str((self.root / p).resolve() if not p.is_absolute() else p)

    # paths -------------------------------------------------------------------------------
    @property
    def state_dir(self) -> Path:
        d = self.root / ".stitch"
        d.mkdir(exist_ok=True)
        return d

    @property
    def build_dir(self) -> Path:
        return self.state_dir / "build"

    def campaign_dir(self, name: str) -> Path:
        return self.state_dir / "campaigns" / name

    def campaigns(self):
        d = self.state_dir / "campaigns"
        return sorted(p.name for p in d.iterdir() if p.is_dir()) if d.exists() else []

    @property
    def dockerfile(self) -> Path:
        return self.root / self.cfg["docker"]["dockerfile"]

    def rel(self, p) -> str:
        try:
            return str(Path(p).resolve().relative_to(self.root))
        except ValueError:
            return str(p)

    # persistent state ------------------------------------------------------------------------
    def load_state(self):
        f = self.state_dir / "state.json"
        return json.loads(f.read_text()) if f.exists() else {}

    def save_state(self, **kw):
        st = self.load_state()
        st.update(kw)
        (self.state_dir / "state.json").write_text(json.dumps(st, indent=1))
        return st


def find_project(start=None) -> Project:
    p = Path(start or os.getcwd()).resolve()
    for d in [p] + list(p.parents):
        if (d / "stitch.toml").exists():
            return Project(d)
    raise ProjectError("no stitch.toml found here or in any parent directory (run `stitch init` first)")
