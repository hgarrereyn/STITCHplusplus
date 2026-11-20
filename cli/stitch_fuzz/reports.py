"""Shared report helpers: sanitizer output parsing (mirrors engine/src/triage.rs) and formatting."""
import json
import re

SKIP_PREFIX = (
    "__asan", "__interceptor", "___interceptor", "__sanitizer", "__ubsan", "stitch::", "stitch_", "std::", "__libc",
    "abort", "raise", "__pthread", "_start", "main", "__cxa", "__gxx", "operator new", "operator delete", "pthread_kill",
    "gsignal", "__GI_", "__assert", "malloc", "calloc", "realloc", "free", "memcpy", "memmove", "memset", "strlen",
    "strcpy", "strcmp", "strncmp", "memcmp",
)


def _norm(msg):
    return re.sub(r"0x[0-9a-fA-F]+", "ADDR", re.sub(r"\d+", "N", msg))[:120]


def parse_report(output: str, exit_code: int, timed_out=False) -> dict:
    kind, summary, frames, endpoint, node = "", "", [], None, None
    in_stack = stack_done = False
    for raw in output.splitlines():
        l = raw.strip()
        m = re.match(r"\[stitch\] #(\d+) ([\w:]+)", l)
        if m:
            node, endpoint = int(m.group(1)), m.group(2)
            continue
        if l.startswith("[stitch] CHECK FAILED: "):
            kind = "check failed: " + l[len("[stitch] CHECK FAILED: "):].rsplit(" (node", 1)[0]
            summary = l
            continue
        if l.startswith("[stitch] UNCAUGHT EXCEPTION in "):
            rest = l[len("[stitch] UNCAUGHT EXCEPTION in "):]
            kind = "uncaught exception: " + _norm(rest.split(": ", 1)[-1])
            summary = l
            continue
        if "ERROR: LeakSanitizer: detected memory leaks" in l and not kind:
            kind, summary = "memory leak", l
            in_stack = True
            continue
        i = l.find("ERROR: AddressSanitizer: ")
        if i >= 0:
            rest = l[i + len("ERROR: AddressSanitizer: "):]
            k = "SEGV" if rest.startswith("SEGV") else rest.split()[0]
            if not kind.startswith(("check failed", "uncaught")):
                kind, summary = k, l[i:]
            in_stack = True
            continue
        if "runtime error: " in l and not kind:
            loc, msg = l.split(": runtime error: ", 1) if ": runtime error: " in l else ("", l)
            kind = "ubsan: " + _norm(msg)
            summary = l
            frames.append("at " + loc.rsplit("/", 1)[-1])
            in_stack = True
            continue
        if (l.startswith("READ of size") or l.startswith("WRITE of size")) and kind and " " not in kind:
            kind += " " + l.split()[0]
            continue
        if in_stack and not stack_done:
            m = re.match(r"#\d+ 0x[0-9a-f]+ in (.+)", l)
            if m:
                tail = m.group(1)
                p = tail.rfind(" /")
                func, loc = (tail[:p], tail[p + 1:]) if p >= 0 else (tail, "")
                em = re.search(r"Endpoint<&([\w:]+)", func)
                if em:
                    if len(frames) < 3:
                        frames.append(f"{em.group(1)} (stub)")
                    continue
                if func.startswith("void "):
                    func = func[5:]
                if not func.startswith(SKIP_PREFIX) and "/runtime/src/" not in loc and "libc.so" not in loc and len(frames) < 3:
                    frames.append(f"{func.split('(')[0]} {loc.rsplit('/', 1)[-1].split(' ')[0]}")
            elif not l and frames:
                stack_done = True
    crashed = timed_out or exit_code != 0 or bool(kind)
    if timed_out:
        kind = "timeout"
    elif crashed and not kind:
        kind = f"crash (exit {exit_code}, no sanitizer report)"
    sig_fn = kind
    if kind.startswith(("check failed", "uncaught")) or kind == "timeout" or not frames:
        if endpoint:
            sig_fn += f" | in {endpoint}"
    sig = sig_fn
    for i, f in enumerate(frames[:3]):
        if i == 0:
            m = re.match(r"(.*):(\d+):\d+$", f)
            sig += " | " + (f"{m.group(1)}:{m.group(2)}" if m else f)
        else:
            sig += " | " + f.split(":")[0]
        sig_fn += " | " + f.split(":")[0]
    return {"crashed": crashed, "kind": kind, "endpoint": endpoint, "node": node, "frames": frames, "signature": sig,
            "signature_fn": sig_fn, "summary": summary}


def pct(x):
    return f"{100 * x:.0f}%"


def human(n):
    n = float(n)
    for unit in ["", "k", "M", "G"]:
        if abs(n) < 1000:
            return f"{n:.0f}{unit}" if unit == "" or n >= 100 else f"{n:.1f}{unit}"
        n /= 1000
    return f"{n:.1f}T"


def dump(obj):
    print(json.dumps(obj, indent=1))
