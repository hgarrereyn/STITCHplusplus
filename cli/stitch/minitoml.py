"""TOML loading: tomllib (3.11+), tomli, or a small fallback parser for the subset STITCH uses."""
import re

try:
    import tomllib as _toml  # type: ignore
except ImportError:  # pragma: no cover
    try:
        import tomli as _toml  # type: ignore
    except ImportError:
        _toml = None


def loads(text):
    if _toml is not None:
        return _toml.loads(text)
    return _Parser(text).parse()


class _Parser:
    def __init__(self, text):
        self.s = text
        self.i = 0

    def err(self, msg):
        line = self.s.count("\n", 0, self.i) + 1
        raise ValueError(f"stitch.toml line {line}: {msg}")

    def ws(self, newlines=False):
        while self.i < len(self.s):
            c = self.s[self.i]
            if c in " \t\r" or (newlines and c == "\n"):
                self.i += 1
            elif c == "#":
                while self.i < len(self.s) and self.s[self.i] != "\n":
                    self.i += 1
            else:
                break

    def parse(self):
        root, cur = {}, None
        cur = root
        while True:
            self.ws(True)
            if self.i >= len(self.s):
                return root
            if self.s[self.i] == "[":
                arr = self.s.startswith("[[", self.i)
                self.i += 2 if arr else 1
                j = self.s.index("]", self.i)
                keys = [k.strip().strip('"') for k in self.s[self.i:j].split(".")]
                self.i = j + (2 if arr else 1)
                t = root
                for k in keys[:-1]:
                    t = t.setdefault(k, {})
                    if isinstance(t, list):
                        t = t[-1]
                if arr:
                    t.setdefault(keys[-1], []).append({})
                    cur = t[keys[-1]][-1]
                else:
                    cur = t.setdefault(keys[-1], {})
                continue
            key = self.key()
            self.ws()
            if self.i >= len(self.s) or self.s[self.i] != "=":
                self.err("expected '='")
            self.i += 1
            self.ws()
            t = cur
            for k in key[:-1]:
                t = t.setdefault(k, {})
            t[key[-1]] = self.value()

    def key(self):
        parts = []
        while True:
            self.ws()
            if self.s[self.i] in "\"'":
                parts.append(self.string())
            else:
                m = re.compile(r"[A-Za-z0-9_\-]+").match(self.s, self.i)
                if not m:
                    self.err("bad key")
                parts.append(m.group(0))
                self.i = m.end()
            self.ws()
            if self.i < len(self.s) and self.s[self.i] == ".":
                self.i += 1
                continue
            return parts

    def string(self):
        q = self.s[self.i]
        if self.s.startswith(q * 3, self.i):
            end = self.s.index(q * 3, self.i + 3)
            v = self.s[self.i + 3:end]
            self.i = end + 3
            return v.lstrip("\n")
        self.i += 1
        out = []
        while self.s[self.i] != q:
            c = self.s[self.i]
            if c == "\\" and q == '"':
                n = self.s[self.i + 1]
                out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"', "r": "\r"}.get(n, n))
                self.i += 2
                continue
            out.append(c)
            self.i += 1
        self.i += 1
        return "".join(out)

    def value(self):
        self.ws()
        c = self.s[self.i]
        if c in "\"'":
            return self.string()
        if c == "[":
            self.i += 1
            arr = []
            while True:
                self.ws(True)
                if self.s[self.i] == "]":
                    self.i += 1
                    return arr
                arr.append(self.value())
                self.ws(True)
                if self.s[self.i] == ",":
                    self.i += 1
        if c == "{":
            self.i += 1
            d = {}
            while True:
                self.ws()
                if self.s[self.i] == "}":
                    self.i += 1
                    return d
                k = self.key()
                self.ws()
                self.i += 1  # '='
                d[k[-1]] = self.value()
                self.ws()
                if self.s[self.i] == ",":
                    self.i += 1
        m = re.compile(r"true|false|[+-]?[0-9_]+(\.[0-9_]+)?([eE][+-]?[0-9]+)?").match(self.s, self.i)
        if not m:
            self.err("bad value")
        self.i = m.end()
        t = m.group(0)
        if t in ("true", "false"):
            return t == "true"
        t = t.replace("_", "")
        return float(t) if ("." in t or "e" in t.lower()) else int(t)
