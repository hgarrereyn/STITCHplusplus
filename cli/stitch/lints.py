"""Static checks on a harness: manifest-level (constructibility) and source-level (attributes)."""
import re
from collections import defaultdict
from pathlib import Path

LIVENESS_KEYS = {"alive", "freed", "dead", "destroyed", "valid", "live", "deleted", "closed_flag", "is_freed", "is_alive"}

_ATTR_RE = re.compile(r"stitch::(set|get|get_str|get_obj\s*<[^>]*>|has|del)\s*\(\s*([^,()]+(?:\([^()]*\))?)\s*,\s*\"([^\"]+)\"")
_GLOBAL_RE = re.compile(r"stitch::(global_set|global_set_str|global_get|global_get_str|global_has)\s*\(\s*\"([^\"]+)\"")


def strip_comments(src):
    src = re.sub(r"//[^\n]*", "", src)
    return re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)


def attr_lints(project):
    """Attribute keys that are read but never set, or set but never read."""
    sets, gets = defaultdict(list), defaultdict(list)
    for f in sorted(project.root.glob("harness/**/*")):
        if f.suffix not in (".cpp", ".cc", ".cxx", ".hpp", ".h"):
            continue
        code = strip_comments(f.read_text(errors="replace"))
        for m in _ATTR_RE.finditer(code):
            op, key = m.group(1), m.group(3)
            line = code.count("\n", 0, m.start()) + 1
            loc = f"{project.rel(f)}:{line}"
            (sets if op == "set" else gets)[key].append(loc)
        for m in _GLOBAL_RE.finditer(code):
            op, key = m.group(1), "global:" + m.group(2)
            line = code.count("\n", 0, m.start()) + 1
            (sets if op.startswith("global_set") else gets)[key].append(f"{project.rel(f)}:{line}")
    out = []
    for k, locs in sorted(gets.items()):
        if k not in sets:
            out.append({"level": "error", "check": "attr-read-never-set", "key": k, "where": locs[:5],
                        "message": f"attribute '{k}' is read but never set: every guard on it fails (or always takes the default)"})
    for k, locs in sorted(sets.items()):
        if k not in gets:
            out.append({"level": "info", "check": "attr-write-only", "key": k, "where": locs[:5],
                        "message": f"attribute '{k}' is set but never read (dead state; remove it or use it in a guard/check)"})
        if k.lower() in LIVENESS_KEYS:
            out.append({"level": "warning", "check": "liveness-flag", "key": k, "where": locs[:3],
                        "message": f"'{k}' looks like a liveness flag: prefer Take<T> or obj.consume() so dead objects leave the pool"})
    return out


def manifest_lints(m):
    out = []
    types = [o["name"] for o in m["objects"]]
    produced = {o["type"] for e in m["endpoints"] for o in e["outputs"]}
    consumed = {i["type"] for e in m["endpoints"] for i in e["inputs"]}
    # constructibility fixpoint
    ok = set()
    changed = True
    while changed:
        changed = False
        for e in m["endpoints"]:
            if all(i["type"] in ok for i in e["inputs"]):
                for o in e["outputs"]:
                    if o["type"] not in ok:
                        ok.add(o["type"])
                        changed = True
    for t in types:
        if t not in produced:
            out.append({"level": "error", "check": "no-producer", "type": t,
                        "message": f"no endpoint returns New<{t}>: endpoints taking {t} can never run"})
        elif t not in ok:
            out.append({"level": "error", "check": "not-constructible", "type": t,
                        "message": f"{t} is only produced by endpoints that need objects nobody can construct"})
        if t not in consumed:
            out.append({"level": "info", "check": "never-consumed", "type": t,
                        "message": f"{t} is produced but no endpoint takes it as input"})
    for e in m["endpoints"]:
        bad = [i["type"] for i in e["inputs"] if i["type"] not in ok]
        if bad:
            out.append({"level": "error", "check": "unreachable-endpoint", "endpoint": e["name"],
                        "message": f"{e['name']} needs {', '.join(sorted(set(bad)))} which cannot be constructed"})
    for d in m["datatypes"]:
        pass
    return out


def datatype_lints(project, m):
    out = []
    used = {p["datatype"] for e in m["endpoints"] for p in e["params"] if p.get("datatype")}
    for d in m["datatypes"]:
        folder = project.root / "harness" / "data" / d["name"]
        has_seeds = (folder / "seeds").is_dir() and any((folder / "seeds").iterdir())
        if not d["decoder"] and not has_seeds:
            out.append({"level": "warning", "check": "datatype-empty", "datatype": d["name"],
                        "message": f"datatype {d['name']} has no decoder (STITCH_DECODER) and no seeds in harness/data/{d['name']}/seeds/: values are random bytes"})
        if d["name"] not in used:
            out.append({"level": "info", "check": "datatype-unused", "datatype": d["name"],
                        "message": f"datatype {d['name']} is declared but no parameter uses it"})
    return out
