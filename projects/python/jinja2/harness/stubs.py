"""Jinja2: compile and render templates.

Oracles:
- an exception from inside Jinja's compiler, parser, lexer or runtime that is not a TemplateError
  is a bug (errors raised by template code itself, by filters on bad input, or by builtins called
  from a template are the template's fault and bail);
- the Python source Jinja generates for a template always compiles;
- sync and async rendering agree (same output, or both fail);
- render() == "".join(generate()) and render() of a template from the loader matches from_string;
- a sandboxed template never reaches open / exec / os / subprocess / import (audit hook).
"""
import asyncio
import re
import random
import os
import sys

import jinja2
import jinja2.meta
import jinja2.nativetypes
import jinja2.sandbox
import stitch
from jinja2 import nodes
from markupsafe import Markup
from stitch import Bool, Borrow, New, OneOf, Src, Str

Env = stitch.object("Env")
Tmpl = stitch.object("Tmpl")
JinjaText = stitch.datatype("JinjaText")

JINJA_DIR = os.path.dirname(jinja2.__file__) + os.sep
# Where Jinja applies template data to Python operations (iterating, calling, hashing, filters,
# helpers like cycler()/namespace()): errors there are the template's (Jinja type-checks lazily).
DATA_MODULES = ("filters.py", "tests.py", "defaults.py", "utils.py", "sandbox.py", "async_utils.py", "nativetypes.py")
# Environment.select_template iterates whatever an {% include %} / {% import %} names
ENV_DATA = {"select_template"}
RUNTIME_DATA = {"call", "__call__", "_invoke", "_to_iterator", "__init__", "cycle", "changed", "__getitem__", "__getattr__"}
NAMES = ("index.html", "base.html", "macros.html", "part.html")

sys.setrecursionlimit(3000)


def blame(e):
    """'template' if the exception comes from template code / data handling, else 'jinja'."""
    if isinstance(e, jinja2.TemplateError):
        return "template"
    if isinstance(e, (RecursionError, MemoryError)):
        return "template"  # (deeply nested templates / huge strings)
    tb = e.__traceback__
    last = func = None
    while tb is not None:
        last, func = tb.tb_frame.f_code.co_filename, tb.tb_frame.f_code.co_name
        tb = tb.tb_next
    if last is None or not last.startswith(JINJA_DIR):
        return "template"  # template code, builtins, markupsafe
    rel = last[len(JINJA_DIR):]
    if rel in DATA_MODULES or (rel == "runtime.py" and func in RUNTIME_DATA) or (rel == "environment.py" and func in ENV_DATA and isinstance(e, TypeError)):
        return "template"
    return "jinja"


class Guard:
    """Run library code: template errors bail, Jinja-internal errors propagate (bugs)."""

    def __enter__(self):
        return self

    def __exit__(self, t, e, tb):
        if e is None or not isinstance(e, Exception):
            return False
        if isinstance(e, stitch.Bail if hasattr(stitch, "Bail") else ()):
            return False
        if blame(e) == "template":
            stitch.bail(f"{type(e).__name__}")
        return False


# ---- audit hook: sandboxed rendering must not reach the outside world ----
AUDIT = {"on": False, "hits": []}
# (rendering may compile and import on its own: included templates, lazy imports)
WATCH = ("os.system", "os.exec", "os.posix_spawn", "os.spawn", "os.fork", "os.remove", "os.rename", "os.mkdir", "os.rmdir", "os.chmod",
         "os.kill", "os.putenv", "os.unsetenv", "subprocess.", "shutil.", "socket.", "ctypes.", "pickle.find_class", "builtins.input",
         "sys.settrace", "sys.setprofile", "function.__new__")
# (object.__setattr__ and code.__new__ fire inside Jinja itself: building template modules, rewriting tracebacks)


def _audit(event, args):
    if not AUDIT["on"]:
        return
    if event.startswith(WATCH) or (event == "open" and len(args) > 1 and isinstance(args[1], str) and any(c in args[1] for c in "wax+")):
        AUDIT["hits"].append(event)


sys.addaudithook(_audit)


# what a sandboxed template must never see: a new random value per render (so the fuzzer cannot
# learn it from comparisons and write it into the template)
PRIVATE = ["\x01PRIVATE\x01"]


class Canary:
    """Context object with tempting attributes."""

    name = "canary"
    items_ = [1, 2, 3]

    def method(self, x=1):
        return x * 2

    def _private(self):
        return PRIVATE[0]

    def __reduce__(self):
        return (str, (PRIVATE[0],))

    def __repr__(self):
        return "Canary()"


def context(s):
    """Template variables: x, y, items, user, html, obj, n."""
    def value(depth=0):
        k = s.choice(9 if depth < 2 else 6)
        if k == 0:
            return s.range(-5, 100)
        if k == 1:
            return s.str(10)
        if k == 2:
            return s.pick([None, True, False, 0.5, -0.0, float("inf"), 10**20])
        if k == 3:
            return Markup(s.pick(["<b>x</b>", "&amp;", "a < b"]))
        if k == 4:
            return s.pick(["a", "b", "<script>", "a,b,c", "  pad  ", "é"])
        if k == 5:
            return Canary()
        if k == 6:
            return [value(depth + 1) for _ in range(s.choice(4))]
        if k == 7:
            return {s.pick(["a", "b", "name", "id", "x"]): value(depth + 1) for _ in range(s.choice(4))}
        return tuple(value(depth + 1) for _ in range(s.choice(3)))

    ctx = {"x": s.range(-3, 30), "y": s.str(8), "items": [value() for _ in range(s.choice(5))],
           "user": {"name": s.str(6), "id": s.range(0, 9), "tags": ["a", "b"]}, "html": Markup("<i>it</i>"), "obj": Canary()}
    while s.more():
        ctx[s.pick(["x", "y", "n", "items", "user", "v"])] = value()
    return ctx


@stitch.decoder(JinjaText)
def jinja_text(s):
    names = ["x", "y", "items", "user", "html", "obj", "n", "v", "loop", "range", "dict", "lipsum", "cycler", "joiner", "namespace", "self", "caller", "varargs", "kwargs"]
    filters = ["upper", "lower", "title", "capitalize", "trim", "striptags", "escape", "e", "safe", "length", "first", "last", "list", "sort",
               "reverse", "unique", "min", "max", "sum", "join(',')", "join", "default('d')", "default(x, true)", "int", "float", "abs", "round(1)",
               "round(2, 'floor')", "string", "pprint", "tojson", "tojson(2)", "urlencode", "urlize", "urlize(10, true)", "wordcount", "wordwrap(5)",
               "truncate(5)", "truncate(3, true, '..', 0)", "center(9)", "indent(2)", "indent(2, true, true)", "format(x)", "replace('a', 'b')",
               "replace('a', 'b', 1)", "batch(2)", "batch(2, 0)", "slice(3)", "slice(2, 'f')", "groupby('a')", "groupby('name', default='z')",
               "dictsort", "dictsort(false, 'value')", "items", "map('upper')", "map(attribute='name')", "map(attribute='name', default='q')",
               "select('odd')", "select('defined')", "reject('none')", "selectattr('a')", "selectattr('id', 'gt', 1)", "rejectattr('name')",
               "attr('name')", "attr('__class__')", "xmlattr", "xmlattr(false)", "random", "filesizeformat", "filesizeformat(true)", "count",
               "forceescape", "unique(attribute='a')", "min(attribute='a')", "sum(attribute='id', start=1)", "trim('x')", "split" , "items|list"]
    tests = ["defined", "undefined", "none", "number", "string", "mapping", "sequence", "iterable", "callable", "odd", "even",
             "divisibleby 3", "divisibleby(0)", "sameas x", "escaped", "in items", "eq 1", "lt 3", "ge x", "boolean", "true", "false", "integer", "float", "upper", "lower", "filter", "test"]

    def lit():
        return s.pick(["1", "0", "-1", "2.5", "1e3", "1_000", "'s'", '"q"', "'<b>'", "true", "none", "None", "[]", "{}", "()", "(1,)", "0b11", "0o7", "0xff",
                       "'%s-%d'", "'{}'", "999999999999999999999"])

    def expr(d=0):
        k = s.choice(13 if d < 3 else 4)
        if k == 0:
            return lit()
        if k == 1:
            return s.pick(names)
        if k == 2:
            return "'" + s.str(6).replace("\\", "").replace("'", "") + "'"
        if k == 3:
            return s.token()
        if k == 4:
            return expr(d + 1) + "|" + s.pick(filters)
        if k == 5:
            return expr(d + 1) + " is " + ("not " if s.coin(0.2) else "") + s.pick(tests)
        if k == 6:
            return expr(d + 1) + s.pick([" + ", " - ", " * ", " / ", " // ", " % ", " ** ", " ~ ", " and ", " or ", " in ", " not in ", " == ", " != ", " < ", " >= "]) + expr(d + 1)
        if k == 7:
            return s.pick(["not ", "-", "+"]) + expr(d + 1)
        if k == 8:
            return expr(d + 1) + s.pick([".name", ".id", "[0]", "[-1]", "[1:]", "[::-1]", "['a']", ".items()", ".keys()", ".method(2)", "._private()", ".__class__", ".mro", ".append(1)", ".update({})", "[x]", ".tags"])
        if k == 9:
            return expr(d + 1) + " if " + expr(d + 1) + (" else " + expr(d + 1) if s.coin(0.7) else "")
        if k == 10:
            return "[" + ", ".join(expr(d + 1) for _ in range(s.choice(3))) + "]"
        if k == 11:
            return "{" + ", ".join(f"{lit()}: {expr(d + 1)}" for _ in range(s.choice(3))) + "}"
        return s.pick(["range", "dict", "lipsum", "cycler", "joiner", "namespace", "caller", "m", "super", "loop.cycle", "self.b"]) + "(" + ", ".join(
            expr(d + 1) if s.coin(0.7) else f"{s.pick(['a', 'n', 'x', 'html', 'min', 'max'])}={expr(d + 1)}" for _ in range(s.choice(3))) + ")"

    def ws():
        return s.pick(["", "", "-", "+"]) if s.coin(0.3) else ""

    def tag(body):
        return "{%" + ws() + " " + body + " " + ws() + "%}"

    def block(d=0):
        out = []
        while s.more():
            k = s.choice(16 if d < 4 else 5)
            if k == 0:
                out.append(s.str(8))
            elif k == 1:
                out.append("{{" + ws() + " " + expr() + " " + ws() + "}}")
            elif k == 2:
                out.append("{#" + s.str(6) + "#}")
            elif k == 3:
                out.append(s.pick(["\n", "  \n", "\t", " "]))
            elif k == 4:
                out.append(s.str(4))  # stray / garbage
            elif k == 5:
                out.append(tag("if " + expr()) + block(d + 1) + (tag("elif " + expr()) + block(d + 1) if s.coin(0.3) else "") + (tag("else") + block(d + 1) if s.coin(0.4) else "") + tag("endif"))
            elif k == 6:
                target = s.pick(["i", "k, v", "a, b", "i, (j, k)", "loop"])
                out.append(tag(f"for {target} in {expr()}" + (" if " + expr() if s.coin(0.2) else "") + (" recursive" if s.coin(0.15) else "")) + block(d + 1) + (tag("else") + block(d + 1) if s.coin(0.3) else "") + tag("endfor"))
            elif k == 7:
                out.append(tag(f"set {s.pick(['a', 'n', 'x', 'ns.v', 'a, b'])} = {expr()}") if s.coin(0.7) else tag(f"set {s.pick(['a', 'x'])}" + ("|upper" if s.coin(0.3) else "")) + block(d + 1) + tag("endset"))
            elif k == 8:
                out.append(tag(f"macro m({s.pick(['', 'a', 'a, b=1', 'a=x, n=[]', 'caller'])})") + block(d + 1) + tag("endmacro"))
            elif k == 9:
                out.append(tag(f"call{s.pick(['', '(z)'])} m({expr()})") + block(d + 1) + tag("endcall"))
            elif k == 10:
                out.append(tag(f"filter {s.pick(['upper', 'indent(2)', 'trim', 'e', 'replace(\"a\", \"b\")'])}") + block(d + 1) + tag("endfilter"))
            elif k == 11:
                out.append(tag(f"with a = {expr()}") + block(d + 1) + tag("endwith"))
            elif k == 12:
                out.append(tag(f"block {s.pick(['b', 'c', 'b scoped', 'c required'])}") + block(d + 1) + tag("endblock"))
            elif k == 13:
                out.append(tag(s.pick(["extends 'base.html'", "include 'part.html'", "include ['nope.html', 'part.html'] ignore missing", "include 'part.html' without context",
                                       "import 'macros.html' as mm", "from 'macros.html' import m as mmm with context", "extends x", "include y ignore missing"])))
            elif k == 14:
                out.append(tag(s.pick(["break", "continue", "do items.append(1)", "do x", "trans", "endtrans", "pluralize", "autoescape true", "endautoescape", "raw"])) + (s.str(6) + tag("endraw") if s.coin(0.3) else ""))
            else:
                out.append(tag("autoescape " + s.pick(["true", "false", "x"])) + block(d + 1) + tag("endautoescape"))
        return "".join(out)

    return block()


def make_env(cls, autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix, **kw):
    undef = {"default": jinja2.Undefined, "strict": jinja2.StrictUndefined, "chainable": jinja2.ChainableUndefined, "debug": jinja2.DebugUndefined}[undefined]
    exts = [e for bit, e in ((1, "jinja2.ext.do"), (2, "jinja2.ext.loopcontrols"), (4, "jinja2.ext.i18n"), (8, "jinja2.ext.debug")) if ext & bit]
    env = cls(autoescape=autoescape, trim_blocks=trim, lstrip_blocks=lstrip, keep_trailing_newline=keep_nl, undefined=undef, extensions=exts,
              line_statement_prefix=line_prefix, line_comment_prefix="##" if line_prefix else None, **kw)
    if ext & 4:
        env.install_null_translations(newstyle=bool(ext & 16))
    return env


UNDEF = OneOf["default", "strict", "chainable", "debug"]
EXT = stitch.Flags[1, 2, 4, 8, 16] if hasattr(stitch, "Flags") else Range  # noqa


def _loader(env):
    if not isinstance(env.loader, jinja2.DictLoader):
        env.loader = jinja2.DictLoader({"base.html": "<{% block b %}base{% endblock %}|{% block c %}{% endblock %}>",
                                        "part.html": "[{{ x }}]", "macros.html": "{% macro m(a=1) %}({{ a }}{{ caller() if caller }}){% endmacro %}"})
    return env.loader.mapping


@stitch.endpoint
def environment(autoescape: Bool, trim: Bool, lstrip: Bool, keep_nl: Bool, undefined: UNDEF, ext: stitch.Flags[1, 2, 4, 8, 16],
                line_prefix: OneOf[None, "#", "%%"], sandboxed: Bool, optimized: Bool) -> New[Env]:
    cls = jinja2.sandbox.SandboxedEnvironment if sandboxed else jinja2.Environment
    env = make_env(cls, autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix, optimized=optimized)
    _loader(env)
    stitch.set(env, "opts", repr((autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix, sandboxed, optimized)))
    return env


def _render(env, tmpl, ctx):
    sandboxed = isinstance(env, jinja2.sandbox.SandboxedEnvironment)
    AUDIT["hits"] = []
    AUDIT["on"] = sandboxed
    try:
        return tmpl.render(ctx)
    finally:
        AUDIT["on"] = False
        if sandboxed:
            stitch.check(not AUDIT["hits"], f"sandboxed template reached {AUDIT['hits'][:3]}")


def _render_checked(env, tmpl, ctx):
    PRIVATE[0] = "\x01" + os.urandom(8).hex() + "\x01"
    out = _render(env, tmpl, ctx)
    if isinstance(env, jinja2.sandbox.SandboxedEnvironment):
        stitch.check(PRIVATE[0] not in str(out), "sandboxed template read a private attribute")
    return out


@stitch.endpoint
def set_template(env: Borrow[Env], name: OneOf[NAMES], source: Str[1024, JinjaText]):
    _loader(env)[name] = source
    env.cache.clear() if env.cache is not None else None


@stitch.endpoint
def from_string(env: Borrow[Env], source: Str[1024, JinjaText]) -> New[Tmpl]:
    with Guard():
        code = env.compile(source, raw=True)
        try:
            compile(code, "<generated>", "exec")
        except SyntaxError as e:
            stitch.log(code)
            stitch.check(False, f"generated Python compiles ({e.msg})")
        return env.from_string(source)


@stitch.endpoint
def get_template(env: Borrow[Env], name: OneOf[NAMES]) -> New[Tmpl]:
    with Guard():
        return env.get_template(name)


@stitch.endpoint
def render(t: Borrow[Tmpl], s: Src[256]):
    ctx = context(s)
    with Guard():
        random.seed(1)
        out = _render_checked(t.environment, t, ctx)
    with Guard():
        random.seed(1)
        streamed = "".join(t.generate(ctx))
    stitch.log(f"render: {out!r:.300}")
    addr = lambda x: re.sub(r" at 0x[0-9a-f]+", " at 0x", x)  # (object reprs)
    stitch.check(addr(out) == addr(streamed), "render() == ''.join(generate())")


@stitch.endpoint
def module(t: Borrow[Tmpl], s: Src[64]):
    """Attributes of the template module: exported variables and macros (called)."""
    with Guard():
        m = t.make_module(context(s))
        stitch.log(f"exports: {sorted(t.module.__dict__) if hasattr(t, 'module') else ''}")
        for k, v in list(vars(m).items()):
            if callable(v) and not k.startswith("_"):
                v(*([1] if s.coin() else []))
        str(m)


@stitch.endpoint
def async_agrees(env: Borrow[Env], source: Str[512, JinjaText], s: Src[128]):
    """The async renderer gives the same result as the sync one."""
    opts = eval(stitch.get(env, "opts"))  # (rebuild both from the same options)
    autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix, sandboxed, optimized = opts
    cls = jinja2.sandbox.SandboxedEnvironment if sandboxed else jinja2.Environment
    ctx = context(s)
    results = []
    for is_async in (False, True):
        e = make_env(cls, autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix, optimized=optimized, enable_async=is_async)
        e.loader = jinja2.DictLoader(dict(_loader(env)))
        random.seed(1)
        try:
            t = e.from_string(source)
            out = asyncio.run(t.render_async(dict(ctx))) if is_async else t.render(dict(ctx))
            results.append(("ok", re.sub(r" at 0x[0-9a-f]+", " at 0x", out)))  # (object reprs)
        except Exception as ex:  # noqa: BLE001
            if blame(ex) == "jinja":
                raise
            results.append(("error", type(ex).__name__))
    stitch.log(f"sync: {results[0]!r:.200}  async: {results[1]!r:.200}")
    stitch.require(results[0][0] == "ok" or results[1][0] == "ok", "both failed")
    stitch.check(results[0][0] == results[1][0], "sync and async rendering both succeed or both fail")
    stitch.check(results[0] == results[1], "sync and async rendering give the same output")


@stitch.endpoint
def native(source: Str[512, JinjaText], s: Src[128]):
    """NativeEnvironment renders to Python values."""
    env = jinja2.nativetypes.NativeEnvironment()
    with Guard():
        env.from_string(source).render(context(s))


@stitch.endpoint
def analyze(env: Borrow[Env], source: Str[1024, JinjaText]):
    """Parse and inspect the AST; the lexer's token stream covers the source."""
    with Guard():
        ast = env.parse(source)
        jinja2.meta.find_undeclared_variables(ast)
        list(jinja2.meta.find_referenced_templates(ast))
        list(ast.find_all(nodes.Name))
        toks = list(env.lex(source))
        stitch.check(all(isinstance(v, str) for _, _, v in toks), "token values are text")
