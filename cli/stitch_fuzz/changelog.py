"""CHANGELOG.md (shipped with the package) and `stitch upgrade`: move a project's pinned release."""
import re
from pathlib import Path

from .project import STITCH_HOME, version_key

CHANGELOG = STITCH_HOME / "CHANGELOG.md"


def entries():
    """[(version, markdown of its entry)], newest first, from the `## <version>` sections."""
    if not CHANGELOG.exists():
        return []
    text = CHANGELOG.read_text()
    parts = re.split(r"^## +(\S+).*$", text, flags=re.M)
    return [(parts[i], parts[i + 1].strip()) for i in range(1, len(parts), 2)]


def between(since=None, until=None):
    """Entries newer than `since` (exclusive) up to `until` (inclusive), oldest first."""
    out = [(v, t) for v, t in entries()
           if (since is None or version_key(v) > version_key(since)) and (until is None or version_key(v) <= version_key(until))]
    return sorted(out, key=lambda e: version_key(e[0]))


def render(es) -> str:
    return "\n\n".join(f"## {v}\n\n{t}" for v, t in es)


_PIN = re.compile(r'^(\s*stitch_version\s*=\s*)(["\'])[^"\']*\2', re.M)


def set_pin(toml: Path, version: str):
    """Write [project] stitch_version = "<version>", keeping the rest of the file (and comments)."""
    text = toml.read_text()
    if _PIN.search(text):
        text = _PIN.sub(lambda m: f'{m.group(1)}"{version}"', text, count=1)
    elif re.search(r"^\[project\]\s*$", text, re.M):
        text = re.sub(r"^\[project\]\s*$", f'[project]\nstitch_version = "{version}"', text, count=1, flags=re.M)
    else:
        text = f'[project]\nstitch_version = "{version}"\n\n' + text
    toml.write_text(text)
