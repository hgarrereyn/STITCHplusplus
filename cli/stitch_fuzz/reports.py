"""Shared report helpers: crash report parsing (mirrors engine/src/triage.rs) and formatting.

Reports come from sanitizers (C/C++, Rust), or from a language runtime:
  [stitch] EXCEPTION in <stub>: <kind>: <message>     followed by `#N 0x0 in <func> <file>:<line>` frames
  [stitch] CHECK FAILED: <what> (node N)
"""
import json
import re

SKIP_PREFIX = (
    "__rust_try", "std::panicking", "std::panic::", "core::ops::function", "core::panicking", "core::ptr::", "<core::",
    "<alloc::", "<std::", "alloc::", "__asan", "__interceptor", "___interceptor", "__sanitizer", "__ubsan", "stitch::", "stitch_", "std::", "__libc",
    "abort", "raise", "__pthread", "_start", "main", "__cxa", "__gxx", "operator new", "operator delete", "pthread_kill",
    "gsignal", "__GI_", "__assert", "malloc", "calloc", "realloc", "free", "memcpy", "memmove", "memset", "strlen",
    "strcpy", "strcmp", "strncmp", "memcmp",
)


def objc_method(func):
    """'-[Class(Category) selector:]': an Objective-C method name (kept whole: spaces, parentheses)."""
    return func.startswith(("-[", "+[")) and func.endswith("]")


def frame_func(frame):
    """The function of a frame ("func file:line")."""
    if frame.startswith(("-[", "+[")) and "] " in frame:
        return frame[:frame.index("] ") + 1]
    return frame.split(" ")[0]


def _norm(msg):
    return re.sub(r"0x[0-9a-fA-F]+", "ADDR", re.sub(r"\d+", "N", msg))[:120]


def _stub_loc(loc):
    """Frames in harness code or a standalone reproducer: not part of the bug's identity."""
    return "/harness/" in loc or "/.stitch/standalone/" in loc or "/standalone/" in loc


def _python_traceback(output):
    """kind, frames (innermost first) of the last Python traceback in output, or None."""
    lines = output.splitlines()
    starts = [i for i, l in enumerate(lines) if l.startswith("Traceback (most recent call last):")]
    if not starts:
        return None
    frames, kind = [], None
    for l in lines[starts[-1] + 1:]:
        m = re.match(r'\s+File "([^"]+)", line (\d+), in (.+)', l)
        if m:
            path, ln, fn = m.groups()
            frames.append((fn, path, ln))
            continue
        if l.startswith((" ", "\t")) or not l.strip():
            continue
        m = re.match(r"([A-Za-z_][\w.]*)(?::|$)", l)
        if m:
            kind = m.group(1)
        break
    if not kind:
        return None
    out = []
    for fn, path, ln in reversed(frames):
        out.append(f"{fn} (stub)" if _stub_loc(path) else f"{fn} {path.rsplit('/', 1)[-1]}:{ln}")
    return kind, out


def _js_stack(output):
    """kind, frames of the last uncaught JavaScript error (Node prints `Type: msg` + `    at ...`)."""
    lines = output.splitlines()
    heads = [i for i in range(1, len(lines)) if lines[i].startswith("    at ") and not lines[i - 1].startswith("    at ")]
    if not heads:
        return None
    i = heads[-1]
    # the error line is the first of the block before the frames: messages can span several
    # lines (e.g. "TypeError: x\nPlease report this to ...")
    m = None
    for j in range(i - 1, max(-1, i - 40), -1):
        if not lines[j].strip():
            break
        mm = re.match(r"(?:Uncaught\s+)?([A-Za-z_$][\w$.]*)(?::\s|:?$)", lines[j].strip())
        if mm:
            m = mm
    if not m:
        return None
    frames = []
    for l in lines[i:]:
        if not l.startswith("    at "):
            break
        fm = re.match(r"\s+at (?:(.*?) \()?(.+?):(\d+):\d+\)?$", l)
        if not fm or fm.group(2).startswith("node:") or not fm.group(2).startswith("/"):
            continue
        fn, path, ln = fm.group(1) or f"anonymous@{fm.group(2).rsplit('/', 1)[-1]}", fm.group(2), fm.group(3)
        frames.append(f"{fn} (stub)" if _stub_loc(path) else f"{fn} {path.rsplit('/', 1)[-1]}:{ln}")
    return m.group(1), frames


def _java_stack(output):
    """kind, frames of an uncaught Java exception (the root cause of the last chain)."""
    lines = output.splitlines()
    heads = [i for i, l in enumerate(lines) if re.match(r'(Exception in thread "[^"]*" |Caused by: )[\w.$]+', l)]
    if not heads:
        return None
    i = heads[-1]
    m = re.match(r'(?:Exception in thread "[^"]*" |Caused by: )([\w.$]+)', lines[i])
    kind = m.group(1)
    if kind == "stitch.CheckFailed":  # Stitch.check in a standalone reproducer
        kind = "check failed: " + lines[i].split("stitch.CheckFailed", 1)[1].lstrip(": ").strip()
    if kind.startswith("java.lang.") and kind.count(".") == 2:
        kind = kind[len("java.lang."):]
    frames = []
    for l in lines[i + 1:]:
        fm = re.match(r"\s+at (?:[\w.]+/)?([\w.$]+)\.([\w$<>]+)\(([^:)]*)(?::(\d+))?\)", l)
        if not fm:
            if l.strip().startswith("..."):
                continue
            break
        cls, meth, file, ln = fm.groups()
        if cls.startswith(("stitch.", "jdk.internal.", "java.lang.reflect")):
            continue
        simple = cls.rsplit(".", 1)[-1]
        frames.append(f"{simple}.{meth} (stub)" if simple in ("Repro", "Min") else f"{simple}.{meth} {file}:{ln or 0}")
    return kind, frames


def _ruby_backtrace(output):
    """kind, frames of an uncaught Ruby exception as Ruby prints it:
    `path:line:in 'Klass#meth': message (Kind)` then `\tfrom path:line:in 'func'` lines."""
    lines = output.splitlines()
    head_re = re.compile(r"(\S+?):(\d+):in [`'](.+?)': ")
    heads = [i for i, l in enumerate(lines) if head_re.match(l)]
    if not heads:
        return None
    i = heads[-1]
    kind = None
    for l in lines[i:i + 30]:  # the message may span lines; "(Kind)" ends its last line
        if l.startswith("\tfrom "):
            break
        km = re.search(r"\(([A-Z][\w:]*)\)$", l)
        if km:
            kind = km.group(1)
    if not kind:
        return None
    frames = []
    m = head_re.match(lines[i])
    locs = [(m.group(1), m.group(2), m.group(3))]
    for l in lines[i + 1:]:
        fm = re.match(r"\tfrom (\S+?):(\d+):in [`'](.+?)'$", l)
        if fm:
            locs.append(fm.groups())
        elif not l.startswith("\t"):
            continue
    for path, ln, fn in locs:
        frames.append(f"{fn} (stub)" if _stub_loc(path) else f"{fn} {path.rsplit('/', 1)[-1]}:{ln}")
        if len(frames) >= 8:
            break
    return kind, frames


def go_kind(msg):
    """'runtime error: index out of range [5] with length 3' -> 'index out of range'."""
    k = msg[len("runtime error: "):] if msg.startswith("runtime error: ") else msg
    k = re.split(r"[\[:]", k, 1)[0].strip()
    return k[len("invalid memory address or "):] if k.startswith("invalid memory address or ") else k


def go_short(msg):
    """A panic message as a bucket kind (same rule as the Go runtime's shortMsg)."""
    k = msg.replace("\n", " ")
    if len(k) > 48:
        k = k[:48]
        if k.rfind(" ") > 0:
            k = k[:k.rfind(" ")]
    return re.sub(r"[0-9]+", "N", k.replace(": ", " - ")).strip().rstrip(" -:")


def _go_panic(output):
    """kind, frames of a Go panic or fatal error printed by the Go runtime."""
    lines = output.splitlines()
    head = next((i for i, l in enumerate(lines) if l.startswith(("panic: ", "fatal error: "))), None)
    if head is None:
        return None
    l = lines[head]
    if l.startswith("fatal error: "):
        kind = l[len("fatal error: "):].strip()
    else:
        msg = l[len("panic: "):].strip()
        kind = go_kind(msg) if msg.startswith("runtime error: ") else "panic " + go_short(msg)
    frames, i = [], head + 1
    while i < len(lines) and not re.match(r"goroutine \d+ .*\[running", lines[i]):
        i += 1
    i += 1
    while i + 1 < len(lines) and lines[i].strip():
        fn, loc = lines[i].strip(), lines[i + 1].strip()
        i += 2
        name = fn[:fn.rfind("(")] if fn.endswith(")") else fn  # drop the argument list
        if name.startswith(("runtime.", "reflect.", "stitch.", "main.")) or "created by" in fn:
            continue
        name = name.rsplit("/", 1)[-1].replace("(*", "").replace("(", "").replace(")", "").replace("%2e", ".")
        m = re.match(r"(\S+?):(\d+)", loc)
        if m:
            path = m.group(1)
            frames.append(f"{name} (stub)" if _stub_loc(path) else f"{name} {path.rsplit('/', 1)[-1]}:{m.group(2)}")
        if len(frames) >= 8:
            break
    return kind, frames


def _rust_panic(output):
    """kind, frames of an uncaught Rust panic (`thread '...' panicked at FILE:LINE:COL:` + message)."""
    lines = output.splitlines()
    heads = [i for i, l in enumerate(lines) if re.match(r"thread '.*' panicked at ", l)]
    if not heads:
        return None
    i = heads[-1]
    msg = lines[i + 1].strip() if i + 1 < len(lines) else "panic"
    kind = re.sub(r"\d+", "N", msg.split(": ")[0])[:60].strip()
    frames = []
    for j in range(i + 2, len(lines)):
        m = re.match(r"\s*\d+: (.+)$", lines[j])
        if not m:
            continue
        fn = re.sub(r"::h[0-9a-f]{16}$", "", m.group(1).strip())
        at = lines[j + 1].strip()[3:] if j + 1 < len(lines) and lines[j + 1].strip().startswith("at ") else ""
        if not at or "/rustc/" in at or fn.startswith(("std::", "core::", "alloc::", "rust_begin_unwind", "__rust", "stitch::",
                                                     "<core::", "<alloc::", "<std::")):
            continue
        path, _, rest = at.partition(":")
        frames.append(f"{fn} (stub)" if _stub_loc(path) or "/app/crate/src/" in path else f"{fn} {path.rsplit('/', 1)[-1]}:{rest.split(':')[0]}")
        if len(frames) >= 8:
            break
    if not frames:
        m = re.match(r"thread '.*' panicked at (.+?):(\d+):\d+:", lines[i])
        if m:
            frames.append(f"<panic> {m.group(1).rsplit('/', 1)[-1]}:{m.group(2)}")
    return kind, frames


TIMEOUT_LINE = "[stitch] TIMEOUT"  # printed by reproducer scripts that stop a slow program


_OVERFLOW = ("stack-overflow", "StackOverflowError", "RecursionError", "SystemStackError", "stack overflow",
             "overflowed its stack", "stack exceeds")


def is_stack_overflow(kind: str, summary: str) -> bool:
    """Stack exhaustion, as each language reports it (engine/src/triage.rs has the same list)."""
    return any(k in kind for k in _OVERFLOW) or "Maximum call stack size exceeded" in summary


def parse_report(output: str, exit_code: int, timed_out=False) -> dict:
    timed_out = timed_out or TIMEOUT_LINE in output
    kind, summary, frames, endpoint, node = "", "", [], None, None
    in_stack = stack_done = False
    lang_exception = False  # a runtime's EXCEPTION report (Objective-C aborts after it: ASan's ABRT follows)
    py = died = None
    if "[stitch] EXCEPTION in " not in output and "[stitch] CHECK FAILED" not in output:
        py = (_python_traceback(output) or _java_stack(output) or _go_panic(output) or _rust_panic(output)
              or _js_stack(output) or _ruby_backtrace(output))
    if py:
        kind, frames = py
        summary = next((l for l in reversed(output.splitlines()) if l.startswith(kind)), kind)
        stack_done = True
    # Swift: "Fatal error: <message>" or a trap frame "Swift runtime failure: <message>" is the kind
    swift_failure = None
    for raw in output.splitlines():
        l = raw.strip()
        if "Fatal error: " in l and swift_failure is None:
            swift_failure = _norm(l.split("Fatal error: ", 1)[1])
        if " in Swift runtime failure: " in l:
            if swift_failure is None:
                msg = l.split(" in Swift runtime failure: ", 1)[1]
                msg = msg.rsplit(" <compiler-generated>", 1)[0].rsplit(" /", 1)[0]
                swift_failure = _norm(msg)
            continue
        m = re.match(r"\[stitch\] #(\d+) ([\w:]+)", l)
        if m:
            node, endpoint = int(m.group(1)), m.group(2)
            continue
        if l.startswith("[stitch] WORKER DIED ("):
            # if nothing else explains it; named as below for a process that exits without a report
            died = "crash (" + l[len("[stitch] WORKER DIED ("):].rstrip(")") + ", no error report)"
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
        if l.startswith("[stitch] EXCEPTION in "):
            # language runtimes: "<stub>: <kind>: <message>", then innermost-first frames
            rest = l[len("[stitch] EXCEPTION in "):]
            ep, _, after = rest.partition(": ")
            kind = _norm(after.split(": ", 1)[0]) if after else "exception"
            lang_exception = True
            endpoint = endpoint or ep
            summary = l
            in_stack = True
            continue
        if "ERROR: LeakSanitizer: detected memory leaks" in l and not kind:
            kind, summary = "memory leak", l
            in_stack = True
            continue
        i = l.find("ERROR: AddressSanitizer: ")
        if i >= 0:
            rest = l[i + len("ERROR: AddressSanitizer: "):]
            k = "SEGV" if rest.startswith("SEGV") else rest.split()[0]
            # (the ABRT of a runtime that aborts after reporting an exception is not the bug)
            if not kind.startswith(("check failed", "uncaught")) and not (lang_exception and k == "ABRT"):
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
                if func.startswith("stitch_invoke_"):  # Objective-C: the stub's trampoline; the runtime is below
                    stack_done = True
                    continue
                if func.startswith("void "):
                    func = func[5:]
                func = re.sub(r"::h[0-9a-f]{16}$", "", func)  # Rust legacy mangling
                if _stub_loc(loc) and "0x0 in" in l:  # language runtime frame in harness code
                    if len(frames) < 3:
                        frames.append(f"{func} (stub)")
                    continue
                swift_skip = ("<compiler-generated>" in loc or "/stitch/swift/" in loc or "/stitch/cpp-runtime/" in loc
                              or func == "worker_main" or func.startswith(("specialized ", "$sS", "$ss")))
                objc_skip = ("libobjc.so" in loc or "libgnustep-base" in loc or "/usr/src/gnustep/" in loc
                             or "/stitch/objc/" in loc)
                if (not func.startswith(SKIP_PREFIX) and "/runtime/src/" not in loc and "libc.so" not in loc and "/rustc/" not in loc
                        and "/stitch/rust/" not in loc and not swift_skip and not objc_skip and len(frames) < 3):
                    name = func if objc_method(func) else func.split('(')[0]
                    frames.append(f"{name} {loc.rsplit('/', 1)[-1].split(' ')[0]}")
            elif not l and frames:
                stack_done = True
    if died and not kind:
        kind = died
    if swift_failure and (not kind or kind.startswith(("TRAP", "ILL")) or kind == "ABRT"):
        kind = swift_failure
    crashed = timed_out or exit_code != 0 or bool(kind)
    if timed_out:
        kind = "timeout"
    elif crashed and not kind:
        kind = f"crash (exit {exit_code}, no error report)"
    # a stack overflow is bucketed by the stub, not by where the stack ran out (as in the engine)
    overflow = is_stack_overflow(kind, summary)
    sig_fn = kind
    if kind.startswith(("check failed", "uncaught")) or kind == "timeout" or not frames or overflow:
        if endpoint:
            sig_fn += f" | in {endpoint}"
    sig = sig_fn
    for i, f in enumerate([] if overflow else frames[:3]):
        nopos = re.sub(r"(?<!:):\d+(:\d+)?$", "", f)  # function names may contain "::"
        if i == 0:
            m = re.match(r"(.*[^:]):(\d+):\d+$", f)
            sig += " | " + (f"{m.group(1)}:{m.group(2)}" if m else f)
        else:
            sig += " | " + nopos
        sig_fn += " | " + nopos
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
