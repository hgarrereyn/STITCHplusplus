"""Standalone C++ reproducers ("writer mode").

`stitch write CRASH` runs the crashing program once with the runtime's plan mode, which records
every call with its concrete arguments, and turns it into one C++ file:

  int main() {
      stitch::init();
      // #0 parse(text="...")
      Obj<Json> json0 = stitch::call(0, "parse", [&]() -> New<Json> {
          Str<4096, JsonText> text(R"stitch({"a":1})stitch");
          cJSON* root = cJSON_Parse(text.c_str());          <- the harness stub, pasted
          stitch::require(root, "parse failed");
          return root;
      });
      ...

The harness support code (objects.hpp, file-level helpers, STITCH_INIT blocks) is pasted above
main(), and `stitch_standalone.h` provides the harness API without the fuzzer. The result builds
against the library alone. An agent then shrinks it to plain library calls, checking each edit
with `stitch try FILE.cpp` (rebuild + run in the project image + compare the crash).
"""
import json
import re
import shutil
from pathlib import Path

from . import docker, reports
from .project import STITCH_HOME

SHIM = STITCH_HOME / "languages" / "cpp" / "runtime" / "standalone" / "stitch_standalone.h"


# =============================================================================================
# C++ source scanning
# =============================================================================================
def mask(src: str) -> str:
    """Same-length copy of `src` with comments blanked and literal contents replaced by 'x', so
    braces / parens can be matched safely. Newlines are kept."""
    out = list(src)
    i, n = 0, len(src)

    def blank(a, b, keep_quotes=False):
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = "x" if keep_quotes else " "

    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
        elif c == "R" and i + 1 < n and src[i + 1] == '"' and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == "_")):
            m = re.match(r'R"([^(\s]{0,16})\(', src[i:])
            if not m:
                i += 1
                continue
            end = src.find(")" + m.group(1) + '"', i + m.end())
            end = n if end < 0 else end + len(m.group(1)) + 2
            blank(i + 2, end - 1, True)
            i = end
        elif c in "\"'":
            if c == "'" and i > 0 and (src[i - 1].isalnum()) and i + 1 < n and src[i + 1].isalnum():
                i += 1  # digit separator 1'000
                continue
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            blank(i + 1, min(j, n), True)
            i = j + 1
        else:
            i += 1
    return "".join(out)


def match_close(m: str, i: int) -> int:
    """Index of the bracket closing m[i] (one of ( [ {)."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    op, cl, depth = m[i], pairs[m[i]], 0
    for j in range(i, len(m)):
        if m[j] == op:
            depth += 1
        elif m[j] == cl:
            depth -= 1
            if depth == 0:
                return j
    return len(m) - 1


def split_top(s: str, sep=","):
    """Split on top-level `sep`, returning (start, end) offsets. `<<`/`>>` shifts are not brackets."""
    parts, depth, start, i = [], 0, 0, 0
    while i < len(s):
        ch, two = s[i], s[i:i + 2]
        prev = s[:i].rstrip()[-1:]
        nxt = s[i + 2:].lstrip()[:1]
        if two == "<<" and (prev.isalnum() or prev == ")"):
            i += 2
            continue
        if two == ">>" and (nxt.isdigit() or nxt == "("):
            i += 2
            continue
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        elif ch == sep and depth == 0:
            parts.append((start, i))
            start = i + 1
        i += 1
    if s[start:].strip():
        parts.append((start, len(s)))
    return parts


class Endpoint:
    def __init__(self, **kw):
        self.__dict__.update(kw)


class HarnessIndex:
    """Endpoint definitions and support code of a harness directory."""

    def __init__(self, harness_dir: Path):
        self.dir = harness_dir
        self.endpoints = {}   # name as registered -> Endpoint
        self.files = {}       # path -> (text, masked)
        self.removals = {}    # path -> [(start, end)] spans of endpoint definitions / registrations
        self.aliases = {}     # `using Name = ...;` (last name component -> expansion)
        for f in sorted(harness_dir.rglob("*")):
            if f.suffix in (".hpp", ".h", ".hh", ".cpp", ".cc", ".cxx") and f.is_file():
                t = f.read_text(errors="replace")
                mt = mask(t)
                for u in re.finditer(r"\busing\s+(\w+)\s*=\s*", mt):
                    e = mt.find(";", u.end())
                    if e > 0:
                        self.aliases[u.group(1)] = " ".join(t[u.end():e].split())
        for f in sorted(harness_dir.rglob("*")):
            if f.suffix not in (".cpp", ".cc", ".cxx", ".c") or "data" in f.relative_to(harness_dir).parts[:1]:
                continue
            self._scan(f)

    def _scan(self, f: Path):
        text = f.read_text(errors="replace")
        m = mask(text)
        self.files[f] = (text, m)
        rem = self.removals.setdefault(f, [])
        for reg in re.finditer(r"STITCH_ENDPOINT\s*\(\s*([\w:]+)\s*\)\s*;?[ \t]*\n?", m):
            full = reg.group(1)
            rem.append((reg.start(), reg.end()))
            ep = self._find_def(f, text, m, full)
            if ep:
                self.endpoints[full] = ep
                rem.append((ep.start, ep.end))

    def _find_def(self, f, text, m, full):
        fn = full.split("::")[-1]
        for d in re.finditer(r"\b%s\s*\(" % re.escape(fn), m):
            po = d.end() - 1
            pc = match_close(m, po)
            k = pc + 1
            while k < len(m) and m[k] in " \t\n":
                k += 1
            if k >= len(m) or m[k] != "{":
                continue  # a call or a declaration
            bc = match_close(m, k)
            # start of the definition: after the previous top-level statement / block / directive
            b = d.start() - 1
            while b >= 0 and m[b] not in ";{}":
                b -= 1
            lines_start = b + 1
            seg = m[lines_start:d.start()]
            # skip preprocessor lines between the boundary and the return type
            off = 0
            for line in seg.split("\n")[:-1]:
                if line.strip().startswith("#") or not line.strip():
                    off += len(line) + 1
                else:
                    break
            tstart = lines_start + off
            while tstart < d.start() and m[tstart] in " \t\n":
                tstart += 1
            doc = text[lines_start + off:tstart]
            ret = " ".join(text[tstart:d.start()].split())
            for kw in ("static ", "inline ", "extern "):
                ret = ret.replace(kw, "")
            params = []
            inner_m, inner_t = m[po + 1:pc], text[po + 1:pc]
            for a, z in split_top(inner_m):
                pm = inner_m[a:z].split("=")[0]
                pt = inner_t[a:a + len(pm)].strip()
                ids = list(re.finditer(r"[A-Za-z_]\w*", pm))
                if len(ids) >= 2 and not pm.rstrip().endswith((">", "&", "*")):
                    last = ids[-1]
                    name = last.group(0)
                    typ = inner_t[a:a + last.start()].strip()
                else:
                    name, typ = "", pt
                params.append((typ, name))
            # the definition removal span starts at the beginning of its first line (leading
            # comments included) and eats the trailing newline
            start = text.rfind("\n", 0, tstart) + 1
            while True:
                prev_end = start - 1
                if prev_end <= lines_start:
                    break
                prev_start = text.rfind("\n", 0, prev_end) + 1
                if text[prev_start:prev_end].strip().startswith("//"):
                    start = prev_start
                else:
                    break
            end = bc + 1
            if text[end:end + 1] == "\n":
                end += 1
            body = text[k + 1:bc]
            line = text.count("\n", 0, k) + 1
            return Endpoint(name=full, fn=fn, file=f, ret=ret, params=params, body=body, start=start, end=end,
                            line=line, doc=text[start:tstart].strip(), ns=full.rsplit("::", 1)[0] if "::" in full else "")
        return None

    def support_code(self, used_files, keep_defs=()):
        """Pasted code for the given files: local headers inlined once, endpoint definitions
        (except `keep_defs`) and registrations removed."""
        seen = set()
        chunks = []

        def inline_file(path: Path, text: str):
            lines = []
            for line in text.split("\n"):
                s = line.strip()
                mm = re.match(r'#\s*include\s*"([^"]+)"', s)
                if s.startswith("#pragma once"):
                    continue
                if re.match(r'#\s*include\s*[<"]stitch\.h[>"]', s):
                    continue
                if mm:
                    inc = (path.parent / mm.group(1)).resolve()
                    if inc.exists() and str(inc).startswith(str(self.dir.resolve())):
                        if inc not in seen:
                            seen.add(inc)
                            rel = inc.relative_to(self.dir.resolve().parent)
                            lines.append(f"// ---- {rel} " + "-" * max(4, 90 - len(str(rel))))
                            lines.append(inline_file(inc, inc.read_text(errors="replace")).strip("\n"))
                            lines.append(f"// ---- end of {rel}")
                        continue
                lines.append(line)
            return "\n".join(lines)

        for f in used_files:
            text, m = self.files[f]
            spans = sorted(s for s in self.removals.get(f, []) if not any(
                ep.file == f and ep.start == s[0] and ep.name in keep_defs for ep in self.endpoints.values()))
            kept, pos = [], 0
            for a, z in spans:
                if a < pos:
                    continue
                kept.append(text[pos:a])
                pos = z
            kept.append(text[pos:])
            body = inline_file(f, "".join(kept))
            body = re.sub(r"\n{3,}", "\n\n", body).strip("\n")
            rel = f.resolve().relative_to(self.dir.resolve().parent)
            if body.strip():
                chunks.append(f"// ==== {rel} (helpers) " + "=" * max(4, 80 - len(str(rel))) + "\n" + body)
        return "\n\n".join(chunks)

    def resolve(self, typ: str) -> str:
        t = re.sub(r"^const\s+", "", typ).rstrip("& ").strip()
        for _ in range(5):
            last = t.split("::")[-1]
            if last in self.aliases and "<" not in last:
                t = self.aliases[last]
            else:
                break
        return t

    def files_with_init(self):
        return [f for f, (_, m) in self.files.items() if re.search(r"\bSTITCH_INIT\b", m)]


# =============================================================================================
# Value literals
# =============================================================================================
def c_bytes(b: bytes, indent: str, raw_ok=True) -> str:
    """A C++ string literal for `b` (split over lines when long)."""
    printable = all(32 <= c < 127 or c in (9, 10) for c in b)
    if raw_ok and printable and len(b) > 40 and b")stitch\"" not in b and (b"\n" in b or len(b) > 100):
        return 'R"stitch(' + b.decode() + ')stitch"'
    pieces, cur = [], ""
    for c in b:
        ch = chr(c)
        if ch == "\\":
            cur += "\\\\"
        elif ch == '"':
            cur += '\\"'
        elif ch == "\n":
            cur += "\\n"
        elif ch == "\t":
            cur += "\\t"
        elif ch == "?" and cur.endswith("?"):
            cur += "\\?"  # trigraphs
        elif 32 <= c < 127:
            cur += ch
        else:
            cur += "\\%03o" % c
        if len(cur) >= 96 or (ch == "\n" and len(cur) > 40):
            pieces.append(cur)
            cur = ""
    if cur or not pieces:
        pieces.append(cur)
    if len(pieces) == 1:
        return '"' + pieces[0] + '"'
    return ("\n" + indent + "    ").join('"' + p + '"' for p in pieces)


def int_literal(v: str, size: int, signed: bool) -> str:
    n = int(v)
    if size == 8:
        if signed:
            return "INT64_MIN" if n == -(1 << 63) else f"{n}LL"
        return f"{n}ULL"
    if not signed and n > 0x7FFFFFFF:
        return f"{n}u"
    return str(n)


def template_args(typ: str):
    """Top-level template arguments of `OneOf<A, B>` -> ['A', 'B']."""
    i = typ.find("<")
    if i < 0:
        return []
    inner = typ[i + 1:typ.rfind(">")]
    return [inner[a:z].strip() for a, z in split_top(inner)]


def float_literal(arg) -> str:
    v, size = arg["v"], arg["size"]
    if "nan" in v:
        return "NAN"
    if "inf" in v:
        return "-INFINITY" if v.startswith("-") else "INFINITY"
    return v + ("f" if size == 4 else "")


def param_decl(typ: str, name: str, arg: dict, draws, indent: str, resolve=lambda t: t):
    """Declaration of a value parameter with its concrete value (+ comment lines)."""
    k = arg["kind"]
    typ = re.sub(r"^const\s+", "", typ).rstrip("& ").strip()
    pre = []
    if k == "bool":
        val = "true" if arg["v"] == "true" else "false"
    elif k in ("int", "range"):
        val = int_literal(arg["v"], arg["size"], arg.get("signed", True))
    elif k == "oneof":
        args = template_args(resolve(typ))
        vals = arg.get("values") or []
        val = arg["v"]
        if val in vals and len(args) == len(vals):
            val = args[vals.index(val)]
            if not re.match(r"^[\w:]+$", val):
                val = f"({val})"
    elif k == "flags":
        args = template_args(resolve(typ))
        vals = [int(x) for x in arg.get("values") or []]
        v = int(arg["v"])
        if v == 0:
            val = "0"
        elif len(args) == len(vals):
            parts, cover = [], 0
            for a, x in sorted(zip(args, vals), key=lambda t: -bin(t[1]).count("1")):
                if x and (x & v) == x and (x & ~cover):
                    parts.append(a)
                    cover |= x
            val = " | ".join(parts) if cover == v else str(v)
        else:
            val = str(v)
    elif k == "float":
        val = float_literal(arg)
        pre.append(f"{name} = {arg.get('d')}")
    elif k in ("str", "bytes", "file", "raw", "source"):
        b = bytes.fromhex(arg["hex"])
        lit = c_bytes(b, indent, raw_ok=k in ("str", "file"))
        val = lit if (k == "str" and b"\0" not in b) else f"{lit}, {len(b)}"
        if k == "source":
            for d in draws:
                shown = d.get("v")
                if "hex" in d:
                    shown = c_bytes(bytes.fromhex(d["hex"]), "", raw_ok=False)
                    if len(shown) > 60:
                        shown = shown[:57] + '..."'
                pre.append(f"{name}.{d['op']} -> {shown}")
            if pre:
                pre = [f"draws: " + "; ".join(pre)]
        if arg.get("decoded"):
            pre.append(f"{name}: {len(b)} bytes from the {arg.get('datatype')} decoder")
    else:
        val = arg.get("v", "")
    lines = [f"{indent}// {p}" for p in pre]
    if "\n" in val:
        lines.append(f"{indent}{typ} {name}(\n{indent}    {val});")
    else:
        lines.append(f"{indent}{typ} {name}({val});")
    return lines


# =============================================================================================
# Writer
# =============================================================================================
def read_plan(path: Path):
    calls, draws, done, skips, drops, ended = {}, {}, {}, [], None, False
    for line in path.read_text(errors="replace").splitlines():
        try:
            r = json.loads(line)
        except json.JSONDecodeError:
            continue
        t = r.get("t")
        if t == "call":
            calls[r["node"]] = r
        elif t == "draw":
            draws.setdefault(r["node"], {}).setdefault(r["src"], []).append(r)
        elif t == "done":
            done[r["node"]] = r
        elif t == "skip":
            skips.append(r)
        elif t == "drops":
            drops = r["objs"]
        elif t == "end":
            ended = True
    return calls, draws, done, skips, drops, ended


def reindent(body: str, indent: str) -> str:
    lines = body.split("\n")
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    ws = [len(l) - len(l.lstrip()) for l in lines if l.strip()]
    cut = min(ws) if ws else 0
    return "\n".join((indent + l[cut:]) if l.strip() else "" for l in lines)


def obj_var(typ: str, node: int, out: int, n_out: int) -> str:
    base = re.sub(r"\W", "_", typ).lower()
    return f"{base}{node}" + (f"_{out}" if n_out > 1 else "")


def ret_kind(ret: str):
    """('void'|'new'|'opt'|'tuple', [element types])"""
    r = ret.replace(" ", "")
    if r == "void":
        return "void", []
    if r.startswith("std::tuple<"):
        return "tuple", [e for e in template_args(ret)]
    if r.startswith("std::optional<"):
        return "opt", [ret]
    return "new", [ret]


def write_cpp(index: HarnessIndex, plan: Path, header: list, build_hint: str, prune=True):
    calls, draws, done, skips, drops, ended = read_plan(plan)
    order = sorted(calls)
    crash_node = next((n for n in order if n not in done), None)
    missing = [calls[n]["ep"] for n in order if calls[n]["ep"] not in index.endpoints]
    if missing:
        raise RuntimeError(f"endpoint source not found in harness/: {', '.join(sorted(set(missing)))}")
    used = [index.endpoints[calls[n]["ep"]] for n in order]
    files = []
    for ep in used:
        if ep.file not in files:
            files.append(ep.file)
    for f in index.files_with_init():
        if f not in files:
            files.append(f)
    # endpoint functions that pasted code calls directly stay defined
    names = {e.fn for e in index.endpoints.values()}
    keep, text_pool = set(), "\n".join(e.body for e in used)
    for _ in range(3):
        sup = index.support_code(files, keep)
        hits = {e.name for e in index.endpoints.values() if re.search(r"\b%s\s*\(" % re.escape(e.fn), mask(sup + text_pool))}
        if hits <= keep:
            break
        keep |= hits
    support = index.support_code(files, keep)

    vars_ = {}  # (node, out) -> variable
    body = ["int main() {", "    stitch::init();"]
    for n in order:
        c, ep = calls[n], index.endpoints[calls[n]["ep"]]
        d = done.get(n)
        status = "crash" if n == crash_node else d["status"]
        reason = (d or {}).get("reason", "")
        outs = (d or {}).get("outs", [])
        kind, elems = ret_kind(index.resolve(ep.ret))
        ind = "        "
        # argument summary for the comment line
        summ = []
        oi = si = 0
        decls = []
        for (typ, name), a in zip(ep.params, c["args"]):
            name = name or f"_arg{len(decls)}"
            if "obj" in a:
                src = vars_.get(tuple(a["obj"]), f"/* missing o{a['obj'][0]}.{a['obj'][1]} */")
                t = re.sub(r"^const\s+", "", typ).rstrip("& ").strip()
                decls.append(f"{ind}{t} {name} = {src};")
                summ.append(f"{name}={src}")
                oi += 1
            else:
                dr = []
                if a["kind"] == "source":
                    dr = draws.get(n, {}).get(si, [])
                    si += 1
                decls += param_decl(typ, name, a, dr, ind, index.resolve)
                summ.append(name)
        note = {"crash": "  <-- crashes here in the original run",
                "bail": f'  (bailed in the original run: "{reason}")'}.get(status, "")
        body.append("")
        body.append(f"    // #{n} {ep.name}({', '.join(summ)})  [{ep.file.relative_to(index.dir.parent)}:{ep.line}]{note}")
        lam_ret = "" if kind == "void" else f"() -> {ep.ret}"
        inner = []
        if ep.ns:
            inner.append(f"{ind}using namespace {ep.ns};")
        inner += decls
        inner.append(reindent(ep.body, ind))
        lam = f"[&]{lam_ret} {{\n" + "\n".join(inner) + "\n    }"
        if status == "bail" and reason != "returned null object":
            body.append(f'    stitch::call_bails({n}, "{ep.name}", {lam});')
            continue
        outs_types = c.get("outs", [])
        if kind == "new" and outs == [0]:
            v = obj_var(outs_types[0], n, 0, 1)
            vars_[(n, 0)] = v
            body.append(f'    Obj<{outs_types[0]}> {v} = stitch::call({n}, "{ep.name}", {lam});')
        elif kind == "void" or not outs:
            body.append(f'    stitch::call({n}, "{ep.name}", {lam});')
        else:
            r = f"r{n}"
            body.append(f'    auto {r} = stitch::call({n}, "{ep.name}", {lam});')
            for k in outs:
                v = obj_var(outs_types[k], n, k, len(outs_types))
                vars_[(n, k)] = v
                get = f"std::get<{k}>({r})" if kind == "tuple" else r
                body.append(f"    Obj<{outs_types[k]}> {v} = stitch::unwrap({get});")
        if status == "crash":
            break
    if crash_node is None and drops:
        body.append("")
        body.append("    // end of the program: the fuzzer finalizes live objects (STITCH_DROP), newest first")
        for nd, k in drops:
            if (nd, k) in vars_:
                body.append(f"    {vars_[(nd, k)]}.drop();")
    elif crash_node is not None:
        body.append("")
        body.append(f"    // the original run crashed in call #{crash_node}; nothing after it ran")
    body.append("    return 0;")
    body.append("}")
    if prune:
        support = prune_support(support, "\n".join(body))
    head = ["// " + h if h else "//" for h in header]
    out = head + ["", '#include "stitch_standalone.h"', "", support, "",
                  "// " + "=" * 94, "// The crashing program. Each stitch::call(...) is one harness call with its arguments filled in.",
                  "// " + "=" * 94] + body
    return "\n".join(out) + "\n", {"calls": len(order), "crash_node": crash_node, "skipped": len(skips),
                                    "files": [str(f.relative_to(index.dir.parent)) for f in files]}


# =============================================================================================
# Pruning unused helpers
# =============================================================================================
_NOT_FN = ("struct ", "class ", "enum ", "union ", "namespace ", "extern ", "typedef ", "using ", "STITCH_")


def _items(m: str, a: int, b: int):
    """Top-level items of masked code m[a:b]: (start, end, kind) with kind in
    {'pp', 'ns', 'item'}; for 'ns' also the inner (start, end) of the braces."""
    i, out = a, []
    while i < b:
        while i < b and m[i] in " \t\n":
            i += 1
        if i >= b:
            break
        if m[i] == "#":
            j = m.find("\n", i)
            j = b if j < 0 or j > b else j
            while m[j - 1:j] == "\\" and j < b:  # continued directive
                j = m.find("\n", j + 1)
                j = b if j < 0 else j
            out.append((i, j, "pp", None))
            i = j
            continue
        start, depth, j = i, 0, i
        first_brace = None
        while j < b:
            c = m[j]
            if c in "([":
                depth += 1
            elif c in ")]":
                depth -= 1
            elif c == "{":
                if depth == 0 and first_brace is None:
                    first_brace = j
                close = match_close(m, j)
                j = close + 1
                if depth == 0:
                    k = j
                    while k < b and m[k] in " \t":
                        k += 1
                    if k < b and m[k] == ";":
                        j = k + 1
                    head = m[start:first_brace]
                    if re.match(r"\s*(inline\s+)?namespace\b[^;]*$", head):
                        out.append((start, j, "ns", (first_brace + 1, close)))
                    else:
                        out.append((start, j, "item", first_brace))
                    break
                continue
            elif c == ";" and depth == 0:
                j += 1
                out.append((start, j, "item", None))
                break
            j += 1
        else:
            out.append((start, b, "item", None))
            j = b
        i = j
    return out


def _fn_name(m: str, start: int, brace):
    """Name of the function an item defines, or None (types, variables, macros)."""
    if brace is None:
        return None
    head = " ".join(m[start:brace].split())
    head = re.sub(r"^template\s*<.*?>\s*", "", head) if head.startswith("template") else head
    if head.startswith(_NOT_FN) or "(" not in head or re.search(r"=\s*\[", head):
        return None
    pre = head[:head.index("(")].rstrip()
    mm = re.search(r"([A-Za-z_]\w*)$", pre)
    if not mm or mm.group(1) in ("if", "for", "while", "switch", "return", "operator"):
        return None
    return mm.group(1)


def prune_support(support: str, used: str) -> str:
    """Remove top-level function definitions (also inside namespaces) that nothing references.
    `used` is the other code (main) that may reference them."""
    for _ in range(10):
        m = mask(support)
        fns = []

        def walk(a, b):
            for s0, e0, kind, extra in _items(m, a, b):
                if kind == "ns":
                    walk(*extra)
                elif kind == "item":
                    name = _fn_name(m, s0, extra)
                    if name:
                        fns.append((s0, e0, name))
        walk(0, len(m))
        mu = mask(used)
        dead = []
        for s0, e0, name in fns:
            rest = m[:s0] + m[e0:]
            pat = re.compile(r"\b%s\b" % re.escape(name))
            if not pat.search(rest) and not pat.search(mu):
                dead.append((s0, e0))
        if not dead:
            break
        for s0, e0 in sorted(dead, reverse=True):
            # take the comment lines right above the definition with it
            ls = support.rfind("\n", 0, s0) + 1
            while ls > 0:
                ps = support.rfind("\n", 0, ls - 1) + 1
                if support[ps:ls - 1].strip().startswith("//"):
                    ls = ps
                else:
                    break
            end = e0 + 1 if support[e0:e0 + 1] == "\n" else e0
            support = support[:ls] + support[end:]
    return re.sub(r"\n{3,}", "\n\n", support)


# =============================================================================================
# Build + run in the project image
# =============================================================================================
def compile_cmd(p, src_rel: str, binary="/tmp/stitch-standalone") -> str:
    b = p.cfg["build"]
    is_c = src_rel.endswith(".c")
    std = b.get("std", "c++17")
    cflags = " ".join(_q(x) for x in b.get("cflags", []))
    ldflags = " ".join(_q(x) for x in b.get("ldflags", []))
    d = str(Path(src_rel).parent) or "."
    cc = "clang" if is_c else f"clang++ -std={std}"
    tail = "" if is_c else " -lstdc++"
    return (f"{cc} $(stitch-flags --no-cov) -I{_q(d)} -I.stitch/include {cflags} {_q(src_rel)} "
            f"{ldflags}{tail} -lm -lpthread -o {binary}")


def _q(s):
    import shlex
    return shlex.quote(str(s))


def asan_options(leaks=False):
    """The fuzzer's ASan settings (runtime.cpp __asan_default_options), so aborts and failed
    allocations behave the same."""
    return ("abort_on_error=1:symbolize=1:allocator_may_return_null=1:handle_abort=1:"
            f"detect_stack_use_after_return=0:malloc_context_size=10:detect_leaks={1 if leaks else 0}")


def install_shim(p):
    """Current stitch_standalone.h in <project>/.stitch/include (fallback include dir for `try`)."""
    inc = p.state_dir / "include"
    inc.mkdir(parents=True, exist_ok=True)
    dst = inc / "stitch_standalone.h"
    if not dst.exists() or dst.read_bytes() != SHIM.read_bytes():
        shutil.copy2(SHIM, dst)


def try_cpp(p, image, src_rel: str, timeout=60, leaks=False):
    """Compile + run a standalone reproducer in `image`. Returns dict(status, report, output).
    Leak detection is off unless the expected crash is a leak (minimized code rarely frees)."""
    install_shim(p)
    binary = "/tmp/stitch-standalone"
    script = (compile_cmd(p, src_rel, binary) + " 2>/tmp/cc.log || { cat /tmp/cc.log >&2; exit 201; }\n"
              f"cat /tmp/cc.log >&2; echo '[repro] ---- run ----' >&2\n"
              f"ASAN_OPTIONS={asan_options(leaks)} UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 timeout {timeout} {binary} >&2\n")
    san = p.cfg["build"].get("sanitizers")
    env = {"STITCH_SANITIZERS": ",".join(san) if isinstance(san, list) else str(san)} if san else {}
    r = docker.run(p, image, ["sh", "-c", script], capture=True, env=env, timeout=timeout + 300)
    output = (r.stderr or "") + (r.stdout or "")
    if r.returncode == 201:
        errs = [l for l in output.splitlines() if " error: " in l or l.startswith("error:") or "undefined reference" in l]
        return {"status": "compile_error", "errors": errs[:20], "output": output[-6000:]}
    run_out = output.split("[repro] ---- run ----", 1)[-1]
    timed_out = r.returncode == 124
    rep = reports.parse_report(run_out, r.returncode, timed_out)
    if "[repro] DIVERGED" in run_out:
        line = next(l for l in run_out.splitlines() if "[repro] DIVERGED" in l)
        return {"status": "diverged", "message": line.strip(), "report": rep, "output": run_out[-6000:]}
    if not rep["crashed"]:
        return {"status": "no_crash", "report": rep, "output": run_out[-6000:]}
    return {"status": "crash", "report": rep, "output": run_out[-12000:]}


def crash_key(rep: dict):
    """What must stay the same while minimizing: the sanitizer kind and the innermost library
    frame (function name, no line). Stub frames do not count: stubs get pasted and edited."""
    frames = [f for f in rep.get("frames", []) if not f.endswith("(stub)") and not f.startswith("main")]
    top = frames[0].split(" ")[0] if frames else ""
    return {"kind": rep.get("kind", ""), "frame": top}


def _top_loc(signature: str) -> str:
    """'file:line' of the innermost frame in a signature ("kind | func file:line | ...")."""
    parts = [x.strip() for x in (signature or "").split(" | ")[1:]]
    top = next((x for x in parts if not x.endswith("(stub)") and not x.startswith("in ")), "")
    return top.rsplit(" ", 1)[-1] if " " in top else ""


def compare(expected: dict, got: dict) -> str:
    """'same' | 'different'"""
    if expected["kind"] != got["kind"]:
        return "different"
    if expected["frame"] and got["frame"] and expected["frame"] != got["frame"]:
        # a frame without a name (a function inlined where the backtrace cannot name it) still has
        # its file and line
        if got["frame"] == "<panic>" or expected["frame"] == "<panic>":
            a, b = _top_loc(expected.get("signature", "")), _top_loc(got.get("signature", ""))
            return "same" if a and a == b else "different"
        return "different"
    return "same"


README = """# Standalone reproducer: {title}

`repro.cpp` is the crashing program as plain C++: each harness call is pasted into `main()` with
its arguments filled in. `stitch_standalone.h` stands in for the STITCH++ runtime (no fuzzer).

    stitch try {rel}/repro.cpp        # build in the project image, run, compare with expected.json

Minimize: copy `repro.cpp` to `min.cpp` and shrink it step by step, running `stitch try min.cpp`
after every edit (it must keep saying SAME CRASH). Goal: only library calls with literal
arguments, no `stitch_standalone.h`, a few lines a maintainer can read.

Build by hand (inside `stitch shell`):

    {cmd}
"""


def write(p, crash: Path, bucket, outdir: Path = None, run_check=True, verbose=True):
    from .cli import build, die, in_project
    info = build(p, harness_only=True, quiet=True)
    if not info["result"].get("ok"):
        die("harness build failed (run `stitch build`)")
    image = info["image"]
    minimized = crash.with_name(crash.stem + ".min.stp")
    if minimized.exists() and crash.suffix == ".stp" and not crash.stem.endswith(".min"):
        crash = minimized
    rid = (bucket["id"] if bucket else crash.stem[:16]).replace(":", "-")
    d = (outdir or (p.state_dir / "standalone" / rid)).resolve()
    try:
        rel = d.relative_to(p.root.resolve())
    except ValueError:
        die(f"{d} must be inside the project directory (it is mounted into the container)")
    d.mkdir(parents=True, exist_ok=True)
    # 1. run with plan mode
    plan_rel = str(rel / "plan.jsonl")
    prog_rel = in_project(p, crash)
    import subprocess
    try:
        r = docker.run(p, image, ["sh", "-c", f".stitch/build/repro --trace --plan {plan_rel} {prog_rel}"], capture=True, timeout=300)
        output, code, to = r.stderr, r.returncode, False
    except subprocess.TimeoutExpired:
        output, code, to = "", -1, True
    rep = reports.parse_report(output, code, to)
    (d / "trace.txt").write_text(output)
    title = (f"{rep['kind']} in {rep['frames'][0].split(' ')[0]}" if rep["frames"] else rep["kind"]) if rep["crashed"] else "runs without crashing"
    expected = {**crash_key(rep), "signature": rep["signature"], "from": p.rel(crash), "crash_id": rid}
    if p.language != "cpp":
        return _write_lang(p, image, d, rel, rid, title, crash, expected, run_check)
    # 2. write the C++
    index = HarnessIndex(p.root / "harness")
    cmd = compile_cmd(p, str(rel / "repro.cpp"), "repro")
    header = [f"Standalone reproducer: {p.name} {rid}: {title}",
              f"Generated by `stitch write` from {p.rel(crash)}.",
              "Every call of the crashing program is below, with the harness stub code pasted in and its",
              "arguments filled in. stitch_standalone.h stands in for the STITCH++ runtime (no fuzzer).",
              "",
              f"Check:  stitch try {rel}/repro.cpp      (build in the project image, run, compare)",
              f"Build:  {cmd}"]
    cpp, meta = write_cpp(index, d / "plan.jsonl", header, cmd)
    (d / "repro.cpp").write_text(cpp)
    shutil.copy2(SHIM, d / "stitch_standalone.h")
    (d / "expected.json").write_text(json.dumps(expected, indent=1) + "\n")
    (d / "README.md").write_text(README.format(title=title, rel=rel, cmd=cmd))
    res = {"ok": True, "dir": str(d), "cpp": str(d / "repro.cpp"), "expected": expected, **meta,
           "lines": cpp.count("\n")}
    if run_check:
        t = try_cpp(p, image, str(rel / "repro.cpp"), leaks="leak" in expected["kind"])
        if t["status"] == "compile_error":
            # pruning removed something that was needed after all: paste all helpers
            cpp, meta = write_cpp(index, d / "plan.jsonl", header, cmd, prune=False)
            (d / "repro.cpp").write_text(cpp)
            res.update(meta, lines=cpp.count("\n"), pruned=False)
            t = try_cpp(p, image, str(rel / "repro.cpp"), leaks="leak" in expected["kind"])
        res["check"] = verdict(expected, t)
    return res


LANG_README = """# Standalone reproducer: {title}

`{fname}` replays the crashing program without the fuzzer: the harness code, then every call with
its arguments filled in. `{shim}` stands in for the STITCH++ runtime.

    stitch try {rel}/{fname}     # run it in the project image and compare the crash

Shrink it to plain library code: delete calls the crash does not need, inline the stubs, drop the
shim. Run `stitch try` after every step (`stitch guide reproducers`).
"""


def _write_lang(p, image, d, rel, rid, title, crash, expected, run_check):
    from . import writers
    w = writers.get(p.language)
    src_rel = str(rel / w.FILENAME)
    header = [f"Standalone reproducer: {p.name} {rid}: {title}",
              f"Generated by `stitch write` from {p.rel(crash)}.",
              "Every call of the crashing program is below, with its arguments filled in.",
              f"{w.SHIM} stands in for the STITCH++ runtime (no fuzzer).",
              "",
              f"Check:  stitch try {src_rel}      (run in the project image, compare the crash)",
              f"Run:    {w.build_hint(src_rel)}"]
    text, meta = w.write(p, d / "plan.jsonl", header)
    (d / w.FILENAME).write_text(text)
    w.install_shim(d)
    (d / "expected.json").write_text(json.dumps(expected, indent=1) + "\n")
    (d / "README.md").write_text(LANG_README.format(title=title, rel=rel, fname=w.FILENAME, shim=w.SHIM))
    res = {"ok": True, "dir": str(d), "file": str(d / w.FILENAME), "expected": expected, **meta,
           "lines": text.count("\n")}
    if run_check:
        res["check"] = verdict(expected, try_file(p, image, src_rel))
    return res


def try_file(p, image, src_rel: str, timeout=60, leaks=False):
    """Build (if needed) and run a standalone reproducer. dict(status, report, output)."""
    if p.language == "cpp":
        return try_cpp(p, image, src_rel, timeout=timeout, leaks=leaks)
    from . import writers
    w = writers.get(p.language)
    r = docker.run(p, image, ["sh", "-c", w.run_script(p, src_rel, timeout)], capture=True, timeout=timeout + 600)
    output = (r.stderr or "") + (r.stdout or "")
    if r.returncode == 201:
        errs = [l for l in output.splitlines() if "error" in l.lower()]
        return {"status": "compile_error", "errors": errs[:20], "output": output[-6000:]}
    run_out = output.split("[repro] ---- run ----", 1)[-1]
    rep = reports.parse_report(run_out, r.returncode, r.returncode == 124)
    if "[repro] DIVERGED" in run_out:
        line = next(l for l in run_out.splitlines() if "[repro] DIVERGED" in l)
        return {"status": "diverged", "message": line.strip(), "report": rep, "output": run_out[-6000:]}
    if not rep["crashed"]:
        return {"status": "no_crash", "report": rep, "output": run_out[-6000:]}
    return {"status": "crash", "report": rep, "output": run_out[-12000:]}


def shim_name(language):
    if language == "cpp":
        return "stitch_standalone.h"
    from . import writers
    return writers.get(language).SHIM


def verdict(expected, t):
    v = {"status": t["status"]}
    if not expected.get("kind"):  # the original program did not crash
        if t["status"] in ("no_crash", "crash"):
            v["status"] = "same" if t["status"] == "no_crash" else "different"
            if t["status"] == "crash":
                v["got"] = {**crash_key(t["report"]), "signature": t["report"]["signature"]}
        v["output"] = t["output"]
        if t["status"] == "compile_error":
            v["errors"] = t["errors"]
        if t["status"] == "diverged":
            v["message"] = t["message"]
        return v
    if t["status"] == "crash":
        got = crash_key(t["report"])
        same = compare(expected, {**got, "signature": t["report"]["signature"]}) == "same"
        # an oracle failure ("check failed: X") may be reproduced by plain code that aborts with
        # the same message, e.g. assert(ok && "X")
        what = expected["kind"][len("check failed: "):] if expected["kind"].startswith("check failed: ") else None
        if what and not same and what in t["output"] and ("ABRT" in got["kind"] or "Assertion" in t["output"]
                                                         or "CHECK FAILED" in t["output"] or "exit" in got["kind"]):
            same = True
        v["status"] = "same" if same else "different"
        v["got"] = {**got, "signature": t["report"]["signature"]}
    if t["status"] == "compile_error":
        v["errors"] = t["errors"]
    if t["status"] == "diverged":
        v["message"] = t["message"]
    v["output"] = t["output"]
    return v


def code_lines(text: str) -> int:
    """Lines that are not blank and not only a comment."""
    n = 0
    for l in text.splitlines():
        t = l.strip()
        if t and not t.startswith(("//", "/*", "*")) and not (
                t.startswith("#") and not re.match(r"#\s*(include|define|if|ifdef|ifndef|endif|else|elif|pragma|undef)\b", t)):
            n += 1
    return n


def find_expected(f: Path):
    for d in [f.parent, *f.parents]:
        e = d / "expected.json"
        if e.exists():
            return json.loads(e.read_text())
        if (d / "stitch.toml").exists():
            break
    return None


VERDICT_TEXT = {
    "same": "SAME CRASH",
    "different": "DIFFERENT CRASH",
    "no_crash": "NO CRASH",
    "diverged": "DIVERGED",
    "compile_error": "COMPILE ERROR",
}


# =============================================================================================
# Batches (one container per batch): used by tests/standalone_fidelity.py
# =============================================================================================
def _batch_script(p, name: str, body: str) -> str:
    d = p.state_dir / "tmp"
    d.mkdir(parents=True, exist_ok=True)
    f = d / name
    f.write_text(body)
    return str(f.relative_to(p.root))


def plan_many(p, image, progs, outdir: Path, timeout=30):
    """Run each program with plan mode. Returns [(plan.jsonl, output, exit code)]."""
    outdir.mkdir(parents=True, exist_ok=True)
    rel = outdir.resolve().relative_to(p.root.resolve())
    lines = ["#!/bin/sh"]
    from .cli import in_project
    for i, f in enumerate(progs):
        lines.append(f"timeout {timeout} .stitch/build/repro --trace --plan {rel}/{i}.jsonl {_q(in_project(p, Path(f)))} "
                     f">/dev/null 2>{rel}/{i}.log; echo $? > {rel}/{i}.rc")
    script = _batch_script(p, "plan_many.sh", "\n".join(lines) + "\n")
    docker.run(p, image, ["sh", script], capture=True, timeout=timeout * len(progs) + 120)
    res = []
    for i in range(len(progs)):
        rc = (outdir / f"{i}.rc").read_text().strip() if (outdir / f"{i}.rc").exists() else "-1"
        res.append((outdir / f"{i}.jsonl", (outdir / f"{i}.log").read_text(errors="replace") if (outdir / f"{i}.log").exists() else "", int(rc or -1)))
    return res


def try_many(p, image, srcs, timeout=30, leaks=False):
    """Compile (in parallel) and run standalone files with STITCH_REPRO_TRACE=1.
    Returns [dict(status, output, report)] like try_cpp."""
    install_shim(p)
    one = ["#!/bin/sh", "# usage: one.sh SRC  (writes SRC.cc / SRC.run / SRC.rc)", 'src="$1"; bin="/tmp/bin-$(echo "$src" | tr / _)"',
           compile_cmd(p, "$SRC_PLACEHOLDER", "$bin").replace("'$SRC_PLACEHOLDER'", '"$src"').replace("$SRC_PLACEHOLDER", '"$src"').replace("-I. ", '-I"$(dirname "$src")" ', 1)
           + ' 2>"$src.cc" || { echo 201 > "$src.rc"; exit 0; }',
           f'STITCH_REPRO_TRACE=1 ASAN_OPTIONS={asan_options(leaks)} timeout {timeout} "$bin" >"$src.run" 2>&1; echo $? > "$src.rc"; rm -f "$bin"']
    one_rel = _batch_script(p, "one.sh", "\n".join(one) + "\n")
    lst = _batch_script(p, "srcs.txt", "\n".join(str(Path(s).resolve().relative_to(p.root.resolve())) for s in srcs) + "\n")
    san = p.cfg["build"].get("sanitizers")
    env = {"STITCH_SANITIZERS": ",".join(san) if isinstance(san, list) else str(san)} if san else {}
    docker.run(p, image, ["sh", "-c", f'xargs -P "$(nproc)" -n 1 sh {one_rel} < {lst}'], capture=True, env=env,
               timeout=(timeout + 60) * len(srcs) + 300)
    res = []
    for s in srcs:
        s = Path(s)
        rc_f = Path(str(s) + ".rc")
        rc = int(rc_f.read_text().strip() or -1) if rc_f.exists() else -1
        if rc == 201:
            out = Path(str(s) + ".cc").read_text(errors="replace")
            errs = [l for l in out.splitlines() if " error: " in l or l.startswith("error:") or "undefined reference" in l]
            res.append({"status": "compile_error", "errors": errs[:20], "output": out[-6000:]})
            continue
        out = Path(str(s) + ".run").read_text(errors="replace") if Path(str(s) + ".run").exists() else ""
        rep = reports.parse_report(out, rc, rc == 124)
        st = "diverged" if "[repro] DIVERGED" in out else "crash" if rep["crashed"] else "no_crash"
        res.append({"status": st, "report": rep, "output": out[-12000:], "calls": call_outcomes_run(out)})
    return res


def call_outcomes_plan(plan: Path):
    """{node: 'ok' | 'bail:<reason>' | 'crash'} for the calls that ran."""
    calls, _, done, _, _, _ = read_plan(plan)
    out = {}
    for n in sorted(calls):
        d = done.get(n)
        out[n] = "crash" if d is None else ("ok" if d["status"] == "ok" else "bail:" + d.get("reason", ""))
    return out


def call_outcomes_run(output: str):
    out = {}
    for l in output.splitlines():
        m = re.match(r"\[repro\] #(\d+) \S+ (ok|bail)(?: (.*))?$", l.strip())
        if m:
            out[int(m.group(1))] = "ok" if m.group(2) == "ok" else "bail:" + (m.group(3) or "")
    return out
