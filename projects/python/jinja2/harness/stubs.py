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
import gettext
import io
import json
import logging
import re
import random
import os
import sys
import tempfile

import jinja2
import jinja2.ext
import jinja2.filters
import jinja2.meta
import jinja2.nativetypes
import jinja2.sandbox
import jinja2.tests
import jinja2.utils
import stitch
from jinja2 import nodes
from markupsafe import Markup
from stitch import Bool, Borrow, New, OneOf, Range, Src, Str, Val

Env = stitch.object("Env")
Tmpl = stitch.object("Tmpl")
JinjaText = stitch.datatype("JinjaText")
ExprText = stitch.datatype("ExprText")     # a single Jinja expression

JINJA_DIR = os.path.dirname(jinja2.__file__) + os.sep
# Where Jinja applies template data to Python operations (iterating, calling, hashing, filters,
# helpers like cycler()/namespace()): errors there are the template's (Jinja type-checks lazily).
DATA_MODULES = ("filters.py", "tests.py", "defaults.py", "utils.py", "sandbox.py", "async_utils.py", "nativetypes.py")
# Environment.select_template iterates whatever an {% include %} / {% import %} names
ENV_DATA = {"select_template"}
RUNTIME_DATA = {"call", "__call__", "_invoke", "_to_iterator", "__init__", "cycle", "changed", "__getitem__", "__getattr__"}
LOADER_DATA = {"get_template_key", "split_template_path", "get_loader"}
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
    # a template that extends/includes a non-string name: loaders that build a path or key from it
    if rel == "loaders.py" and func in LOADER_DATA and isinstance(e, (AttributeError, TypeError)):
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


def _gen(s, mode):
    names = ["x", "y", "items", "user", "html", "obj", "n", "v", "loop", "range", "dict", "lipsum", "cycler", "joiner", "namespace", "self", "caller", "varargs", "kwargs"]
    filters = ["upper", "lower", "title", "capitalize", "trim", "striptags", "escape", "e", "safe", "length", "first", "last", "list", "sort",
               "reverse", "unique", "min", "max", "sum", "join(',')", "join", "default('d')", "default(x, true)", "int", "float", "abs", "round(1)",
               "round(2, 'floor')", "string", "pprint", "tojson", "tojson(2)", "urlencode", "urlize", "urlize(10, true)", "wordcount", "wordwrap(5)",
               "truncate(5)", "truncate(3, true, '..', 0)", "center(9)", "indent(2)", "indent(2, true, true)", "format(x)", "replace('a', 'b')",
               "replace('a', 'b', 1)", "batch(2)", "batch(2, 0)", "slice(3)", "slice(2, 'f')", "groupby('a')", "groupby('name', default='z')",
               "dictsort", "dictsort(false, 'value')", "items", "map('upper')", "map(attribute='name')", "map(attribute='name', default='q')",
               "select('odd')", "select('defined')", "reject('none')", "selectattr('a')", "selectattr('id', 'gt', 1)", "rejectattr('name')",
               "attr('name')", "attr('__class__')", "xmlattr", "xmlattr(false)", "random", "filesizeformat", "filesizeformat(true)", "count",
               "forceescape", "unique(attribute='a')", "min(attribute='a')", "sum(attribute='id', start=1)", "trim('x')", "split" , "items|list",
               "ctxname", "envname", "evalauto"]
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

    return expr() if mode == "expr" else block()


@stitch.decoder(JinjaText)
def jinja_text(s):
    return _gen(s, "block")


@stitch.decoder(ExprText)
def expr_text(s):
    return _gen(s, "expr")


@jinja2.pass_context
def _ctxname(ctx, value):
    return f"{value}@{ctx.name}"


@jinja2.pass_environment
def _envname(env, value):
    return f"{env.block_start_string}{value}"


@jinja2.pass_eval_context
def _evalauto(eval_ctx, value):
    return f"{value}:{eval_ctx.autoescape}"


def env_class(sandboxed):
    return {None: jinja2.Environment, False: jinja2.Environment, True: jinja2.sandbox.SandboxedEnvironment,
            "sandbox": jinja2.sandbox.SandboxedEnvironment,
            "immutable": jinja2.sandbox.ImmutableSandboxedEnvironment}[sandboxed]


def make_env(cls, autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix, **kw):
    if autoescape == "select":
        autoescape = jinja2.select_autoescape(["html", "xml"], default_for_string=True)
    undef = {"default": jinja2.Undefined, "strict": jinja2.StrictUndefined, "chainable": jinja2.ChainableUndefined, "debug": jinja2.DebugUndefined}[undefined]
    exts = [e for bit, e in ((1, "jinja2.ext.do"), (2, "jinja2.ext.loopcontrols"), (4, "jinja2.ext.i18n"), (8, "jinja2.ext.debug")) if ext & bit]
    env = cls(autoescape=autoescape, trim_blocks=trim, lstrip_blocks=lstrip, keep_trailing_newline=keep_nl, undefined=undef, extensions=exts,
              line_statement_prefix=line_prefix, line_comment_prefix="##" if line_prefix else None, **kw)
    if ext & 4:
        env.install_null_translations(newstyle=bool(ext & 16))
    env.filters.update(ctxname=_ctxname, envname=_envname, evalauto=_evalauto)
    return env


UNDEF = OneOf["default", "strict", "chainable", "debug"]
EXT = stitch.Flags[1, 2, 4, 8, 16] if hasattr(stitch, "Flags") else Range  # noqa


def _loader(env):
    if not isinstance(env.loader, jinja2.DictLoader):
        env.loader = jinja2.DictLoader({"base.html": "<{% block b %}base{% endblock %}|{% block c %}{% endblock %}>",
                                        "part.html": "[{{ x }}]", "macros.html": "{% macro m(a=1) %}({{ a }}{{ caller() if caller }}){% endmacro %}"})
    return env.loader.mapping


@stitch.endpoint
def environment(autoescape: OneOf[[False, True, "select"]], trim: Bool, lstrip: Bool, keep_nl: Bool, undefined: UNDEF,
                ext: stitch.Flags[1, 2, 4, 8, 16], line_prefix: OneOf[None, "#", "%%"],
                sandboxed: OneOf[[None, "sandbox", "immutable"]], optimized: Bool) -> New[Env]:
    cls = env_class(sandboxed)
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
    cls = env_class(sandboxed)
    ctx = context(s)
    results = []
    for is_async in (False, True):
        e = make_env(cls, autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix, optimized=optimized, enable_async=is_async)
        e.loader = jinja2.DictLoader(dict(_loader(env)))
        random.seed(1)
        try:
            t = e.from_string(source)
            out = asyncio.run(t.render_async(dict(ctx))) if is_async else t.render(dict(ctx))
            # (object reprs; a filter's lazy result is an async generator in async mode)
            out = re.sub(r"<(async_)?generator object (async_)?(\w+) at 0x[0-9a-f]+>", r"<generator \3>", out)
            results.append(("ok", re.sub(r" at 0x[0-9a-f]+", " at 0x", out)))
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
        stitch.check(isinstance(env.preprocess(source), str), "preprocess() gives text")
        g = env.make_globals({"extra": 1})
        stitch.check(g["extra"] == 1 and "range" in g, "make_globals() layers over the globals")


# ---- more of the Environment / Template API ----

def _addr(x):
    return re.sub(r" at 0x[0-9a-f]+", " at 0x", x)  # (object reprs)


def _outcome(fn):
    """("ok", output) or ("error", exception type) for template-caused failures; Jinja bugs propagate."""
    random.seed(1)
    try:
        with stitch.time_limit(1.0):   # (lipsum(10**20) and the like)
            return ("ok", _addr(str(fn())))
    except Exception as ex:  # noqa: BLE001
        if blame(ex) == "jinja":
            raise
        return ("error", type(ex).__name__)


def _contexts(s):
    """A function that builds the same fresh template context each time (rendering may mutate it)."""
    raw = s.rest()
    return lambda: context(stitch.Source(raw))


@stitch.endpoint
def overlay(env: Borrow[Env], trim: Bool, autoescape: OneOf[[None, False, True]], keep_nl: Bool) -> New[Env]:
    kw = {"trim_blocks": trim, "keep_trailing_newline": keep_nl}
    if autoescape is not None:
        kw["autoescape"] = autoescape
    o = env.overlay(**kw)
    stitch.check(o.overlayed, "overlay() marks the copy as an overlay")
    stitch.check(type(o) is type(env), "overlay() keeps the environment class")
    opts = list(eval(stitch.get(env, "opts")))
    opts[1], opts[3] = trim, keep_nl
    if autoescape is not None:
        opts[0] = autoescape
    stitch.set(o, "opts", repr(tuple(opts)))
    return o


@stitch.endpoint
def add_extension(env: Borrow[Env], ext: OneOf[[1, 2, 8]]):
    env.add_extension({1: "jinja2.ext.do", 2: "jinja2.ext.loopcontrols", 8: "jinja2.ext.debug"}[ext])
    opts = list(eval(stitch.get(env, "opts")))
    opts[5] |= ext
    stitch.set(env, "opts", repr(tuple(opts)))
    stitch.check(any(type(e).__module__ == "jinja2.ext" for e in env.iter_extensions()), "the extension is listed")


SPECIAL = re.compile(r"\b(self|loop|super|caller|varargs|kwargs)\b")


@stitch.endpoint
def compile_expression(env: Borrow[Env], source: Str[256, ExprText], undefined_to_none: Bool, s: Src[128]):
    """An expression evaluates to what `{{ (expr) }}` prints."""
    ctx = _contexts(s)
    with Guard():
        fn = env.compile_expression(source, undefined_to_none=undefined_to_none)
    with Guard():
        random.seed(1)
        with stitch.time_limit(1.0):
            val = fn(ctx())
    if undefined_to_none:
        stitch.check(not isinstance(val, jinja2.Undefined), "undefined_to_none turns Undefined into None")
        return
    if env.autoescape is not False or SPECIAL.search(source) or isinstance(env, jinja2.nativetypes.NativeEnvironment):
        return
    try:
        t = env.from_string("{{ (" + source + ") }}")
    except jinja2.TemplateSyntaxError:
        return
    got = _outcome(lambda: t.render(ctx()))
    stitch.require(got[0] == "ok", "the template fails")
    want = _outcome(lambda: val)
    stitch.log(f"expression: {want!r:.200}  template: {got!r:.200}")
    stitch.check(got == want, "compile_expression() agrees with {{ expr }}")


@stitch.endpoint
def select_template(env: Borrow[Env], s: Src[16], with_template: Bool):
    names = [s.pick(list(NAMES) + ["missing.html", "x/../base.html"]) for _ in range(s.range(1, 3))]
    if with_template:
        with Guard():
            names.append(env.from_string("tmpl"))
    with Guard():
        try:
            t = env.select_template(names) if s.coin() else env.get_or_select_template(names)
        except jinja2.TemplatesNotFound as e:
            stitch.check(not any(n in _loader(env) for n in names if isinstance(n, str)), "an existing template is found")
            stitch.bail("not found")
        stitch.check(t.name in names or t in names, "the template is one of the names")


@stitch.endpoint
def list_templates(env: Borrow[Env], ext: OneOf[[None, "html", "txt"]]):
    got = env.list_templates(extensions=[ext] if ext else None)
    want = sorted(n for n in _loader(env) if ext is None or n.endswith("." + ext))
    stitch.check(got == want, "list_templates() lists the loader's templates")
    env.join_path("part.html", "index.html")


@stitch.endpoint
def stream(t: Borrow[Tmpl], size: Range[0, 6], s: Src[256]):
    """TemplateStream with and without buffering gives the render() output."""
    ctx = _contexts(s)
    want = _outcome(lambda: t.render(ctx()))
    stitch.require(want[0] == "ok", "render fails")

    def streamed():
        st = t.stream(ctx())
        if size > 1:
            st.enable_buffering(size)
        else:
            st.disable_buffering()
        if size % 2:
            out = io.StringIO()
            st.dump(out)
            return out.getvalue()
        return "".join(st)

    got = _outcome(streamed)
    stitch.check(got == want, "stream() agrees with render()")


@stitch.endpoint
def new_context(t: Borrow[Tmpl], shared: Bool, s: Src[128]):
    vars = context(s)
    with Guard():
        ctx = t.new_context(vars, shared=shared)
        for k, v in vars.items():
            stitch.check(ctx[k] is v and k in ctx and ctx.get(k) is v, "the context holds the variables")
            stitch.check(ctx.resolve_or_missing(k) is v, "resolve_or_missing() finds a variable")
        all_vars = ctx.get_all()
        stitch.check(all(k in all_vars for k in vars), "get_all() has the variables")
        if not shared:
            stitch.check("range" in all_vars, "an unshared context sees the globals")
        t.make_module(vars, shared=shared)


@stitch.endpoint
def template_info(t: Borrow[Tmpl], line: Range[0, 200]):
    t.get_corresponding_lineno(line)
    t.debug_info
    t.is_up_to_date
    with Guard():
        str(t.module)


def _arity(fn):
    """Call a filter or test; passing it the wrong number of arguments is the caller's mistake."""
    try:
        return fn()
    except TypeError as e:
        tb = e.__traceback__
        while tb.tb_next is not None:
            tb = tb.tb_next
        if tb.tb_frame.f_code.co_name == "_filter_test_common":
            stitch.bail("wrong arguments")
        raise


FILTER_NAMES = tuple(sorted(jinja2.filters.FILTERS)) + ("ctxname", "envname", "evalauto")
TEST_NAMES = tuple(sorted(jinja2.tests.TESTS))
ARGS = (1, 2, -1, 0, "a", "name", "id", True, None, "x,y", 2.5, "upper", "odd")


@stitch.endpoint
def call_filter(env: Borrow[Env], name: OneOf[FILTER_NAMES], s: Src[128], nargs: Range[0, 3]):
    ctx = context(s)
    value = s.pick(list(ctx.values()))
    args = [s.pick(ARGS) for _ in range(nargs)]
    with Guard():
        c = env.from_string("").new_context(ctx)
        _arity(lambda: env.call_filter(name, value, args, context=c, eval_ctx=nodes.EvalContext(env)))


@stitch.endpoint
def call_test(env: Borrow[Env], name: OneOf[TEST_NAMES], s: Src[128], nargs: Range[0, 1]):
    ctx = context(s)
    value = s.pick(list(ctx.values()))
    args = [s.pick(ARGS) for _ in range(nargs)]
    with Guard():
        c = env.from_string("").new_context(ctx)
        r = _arity(lambda: env.call_test(name, value, args, context=c, eval_ctx=nodes.EvalContext(env)))
    if name not in ("filter", "test"):   # (these two give whatever a name lookup gives)
        stitch.check(isinstance(r, bool) or r is NotImplemented or r in (0, 1), "tests return booleans")


ATTRS = ("name", "method", "_private", "__class__", "__globals__", "__code__", "mro", "format", "format_map",
         "append", "update", "pop", "clear", "items", "gi_frame", "cr_frame", "f_locals", "func_code", "__init__",
         "__subclasses__", "__reduce__", "copy", "upper", "real")


def _target(kind):
    def gen():
        yield 1

    return {"canary": Canary(), "str": "abc", "list": [1, 2], "dict": {"a": 1}, "set": {1}, "func": _target,
            "int": 7, "gen": gen(), "class": Canary, "markup": Markup("<b>")}[kind]


@stitch.endpoint
def sandbox_attr(env: Borrow[Env], kind: OneOf[["canary", "str", "list", "dict", "set", "func", "int", "gen", "class", "markup"]],
                 attr: OneOf[ATTRS], item: Bool):
    """The sandbox hides internal and private attributes (and mutators, when immutable)."""
    obj = _target(kind)
    with Guard():
        v = env.getitem(obj, attr) if item else env.getattr(obj, attr)
    internal = jinja2.sandbox.is_internal_attribute(obj, attr)
    mutates = jinja2.sandbox.modifies_known_mutable(obj, attr)
    if isinstance(env, jinja2.sandbox.SandboxedEnvironment) and not isinstance(obj, dict):
        if attr.startswith("_") or internal:
            stitch.check(isinstance(v, jinja2.Undefined), "the sandbox hides an unsafe attribute")
        if isinstance(env, jinja2.sandbox.ImmutableSandboxedEnvironment) and mutates:
            stitch.check(isinstance(v, jinja2.Undefined), "the immutable sandbox hides a mutating method")


BASE = "<{% block b %}base{% endblock %}|{% block c %}{% endblock %}>"
PART = "[{{ x }}]"
MACROS = "{% macro m(a=1) %}({{ a }}{{ caller() if caller }}){% endmacro %}"


@stitch.endpoint
def loader_agrees(source: Str[1024, JinjaText], kind: OneOf[["function", "prefix", "choice", "filesystem", "module", "zip",
                                                              "bytecode"]], s: Src[128]):
    """Every loader (and the bytecode cache) gives the template DictLoader gives."""
    mapping = {"index.html": source, "base.html": BASE, "part.html": PART, "macros.html": MACROS}
    ctx = _contexts(s)
    ref = jinja2.Environment(loader=jinja2.DictLoader(mapping))
    want = _outcome(lambda: ref.get_template("index.html").render(ctx()))
    name = "index.html"
    with tempfile.TemporaryDirectory() as tmp:
        if kind == "function":
            loader = jinja2.FunctionLoader(lambda n: mapping.get(n))
        elif kind == "prefix":
            stitch.require("self" not in source, "the template name differs")
            loader = jinja2.ChoiceLoader([jinja2.PrefixLoader({"p": jinja2.DictLoader(mapping)}), jinja2.DictLoader(mapping)])
            name = "p/index.html"
        elif kind == "choice":
            loader = jinja2.ChoiceLoader([jinja2.DictLoader({}), jinja2.FunctionLoader(lambda n: None),
                                          jinja2.DictLoader(mapping)])
        elif kind == "filesystem":
            stitch.require("\r" not in source, "text files translate line endings")
            for n, src in mapping.items():
                with open(os.path.join(tmp, n), "w", encoding="utf-8", newline="") as f:
                    f.write(src)
            loader = jinja2.FileSystemLoader([os.path.join(tmp, "none"), tmp])
            stitch.check(sorted(loader.list_templates()) == sorted(mapping), "FileSystemLoader lists the files")
        elif kind in ("module", "zip"):
            target = os.path.join(tmp, "out.zip" if kind == "zip" else "out")
            logged = []
            ref.compile_templates(target, zip="deflated" if kind == "zip" else None, ignore_errors=True,
                                  log_function=logged.append)
            loader = jinja2.ModuleLoader(target)
        else:
            loader = jinja2.DictLoader(mapping)
        env = jinja2.Environment(loader=loader, bytecode_cache=jinja2.FileSystemBytecodeCache(tmp) if kind == "bytecode" else None)

        def run():
            t = env.get_template(name)
            if kind == "bytecode":
                env.cache.clear()
                t = env.get_template(name)     # (from the bytecode cache)
            return t.render(ctx())

        got = _outcome(run)
        if kind == "bytecode":
            env.bytecode_cache.clear()
    stitch.log(f"dict: {want!r:.200}  {kind}: {got!r:.200}")
    # (errors may differ in type: a missing or non-string template name fails differently per loader)
    stitch.check(got[0] == want[0], f"{kind} loader fails like DictLoader")
    if got[0] == "ok":
        stitch.check(got == want, f"{kind} loader renders like DictLoader")


@stitch.endpoint
def direct_template(source: Str[1024, JinjaText], s: Src[128]):
    """jinja2.Template(source) renders like Environment().from_string(source)."""
    ctx = _contexts(s)
    want = _outcome(lambda: jinja2.Environment().from_string(source).render(ctx()))
    got = _outcome(lambda: jinja2.Template(source).render(ctx()))
    stitch.check(got == want, "Template(source) agrees with Environment().from_string(source)")


def _upper_gettext(text):
    return text.upper()


def _upper_ngettext(singular, plural, n):
    return (singular if n == 1 else plural).upper()


@stitch.endpoint
def i18n(source: Str[512, JinjaText], mode: OneOf[["null", "translations", "callables", "uninstall"]], newstyle: Bool,
         s: Src[128]):
    env = jinja2.Environment(extensions=["jinja2.ext.i18n", "jinja2.ext.do"], loader=jinja2.DictLoader(
        {"base.html": BASE, "part.html": PART, "macros.html": MACROS}))
    if mode == "translations":
        env.install_gettext_translations(gettext.NullTranslations(), newstyle=newstyle)
    elif mode == "callables":
        env.install_gettext_callables(_upper_gettext, _upper_ngettext, newstyle=newstyle,
                                      pgettext=lambda c, m: f"{c}:{m}", npgettext=lambda c, a, b, n: f"{c}:{a if n == 1 else b}")
    else:
        env.install_null_translations(newstyle=newstyle)
        if mode == "uninstall":
            env.uninstall_gettext_translations(None)
    with Guard():
        list(env.extract_translations(source))
    with Guard():
        list(jinja2.ext.babel_extract(io.BytesIO(source.encode("utf-8", "surrogatepass")), ("_", "gettext", "ngettext"),
                                      ["NOTE:"], {"extensions": "jinja2.ext.do", "trimmed": "true", "newstyle_gettext": "1"}))
    with Guard():
        random.seed(1)
        env.from_string(source).render(context(s))


@stitch.endpoint
def helpers(s: Src[128], n: Range[1, 5], lo: Range[1, 30], hi: Range[1, 100], html: Bool):
    """Template globals and utils: cycler, joiner, namespace, lipsum, htmlsafe_json_dumps."""
    ctx = context(s)
    items = [s.pick(ARGS) for _ in range(s.range(1, 4))]
    c = jinja2.utils.Cycler(*items)
    for i in range(n):
        stitch.check(c.current == items[i % len(items)], "cycler.current follows next()")
        c.next()
    c.reset()
    stitch.check(c.current == items[0], "cycler.reset() starts over")
    j = jinja2.utils.Joiner(", ")
    stitch.check(j() == "" and j() == ", ", "joiner skips the first call")
    ns = jinja2.utils.Namespace({"a": 1}, b=2)
    stitch.check(ns.a + ns.b == 3 and "a" in repr(ns), "namespace attributes")
    stitch.require(lo < hi, "min not below max")   # (words are drawn from range(min, max))
    random.seed(1)
    text = jinja2.utils.generate_lorem_ipsum(n=n, html=html, min=lo, max=hi)
    stitch.check(text.count("<p>") == n if html else text.count("\n\n") == n - 1, "lipsum gives n paragraphs")
    value = s.pick(list(ctx.values()))
    try:
        plain = json.loads(json.dumps(value))
    except (TypeError, ValueError):
        return
    out = jinja2.utils.htmlsafe_json_dumps(value)
    stitch.check(not any(ch in out for ch in "<>&'"), "htmlsafe_json_dumps() escapes HTML characters")
    stitch.check(json.dumps(json.loads(out)) == json.dumps(plain), "htmlsafe_json_dumps() is JSON for the value")
    stitch.check(jinja2.is_undefined(jinja2.Undefined()) and not jinja2.is_undefined(value), "is_undefined()")
    jinja2.clear_caches()


@stitch.endpoint
def urlize(text: Str[256, JinjaText], trim: OneOf[[None, 0, 5, 30]], rel: OneOf[[None, "nofollow", "a b"]],
           target: OneOf[[None, "_blank"]], schemes: OneOf[[None, ("ftp:",), ("x-y:", "tel:")]]):
    """urlize() escapes everything but the links it makes."""
    out = jinja2.utils.urlize(text, trim_url_limit=trim, rel=rel, target=target, extra_schemes=schemes)
    rest = re.sub(r'<a href="[^"<>]*"( rel="[^"<>]*")?( target="[^"<>]*")?>|</a>', "", out)
    stitch.check("<" not in rest and ">" not in rest, "urlize() output has no markup besides its links")


LruOps = stitch.datatype("LruOps")


@stitch.decoder(LruOps)
def lru_ops(s):
    ops = []
    while s.more():
        ops.append((s.choice(7), s.range(0, 6), s.range(0, 99)))
    return ops


@stitch.endpoint
def lru_cache(capacity: Range[1, 5], ops: Val[LruOps, 256]):
    """LRUCache against a model: the least recently used key goes first."""
    c = jinja2.utils.LRUCache(capacity)
    model = {}   # key -> value, in use order (oldest first)

    def touch(k):
        model[k] = model.pop(k)

    for op, k, v in ops:
        if op == 0:
            c[k] = v
            model.pop(k, None)
            model[k] = v
            while len(model) > capacity:
                del model[next(iter(model))]
        elif op == 1:
            got = c.get(k)
            stitch.check(got == model.get(k), "get() agrees with the model")
            if k in model:
                touch(k)
        elif op == 2:
            got = c.setdefault(k, v)
            if k in model:
                stitch.check(got == model[k], "setdefault() returns the stored value")
                touch(k)
            else:
                model[k] = v
                while len(model) > capacity:
                    del model[next(iter(model))]
        elif op == 3:
            if k in model:
                del c[k]
                del model[k]
        elif op == 4:
            stitch.check((k in c) == (k in model), "`in` agrees with the model")
        elif op == 5:
            c = c.copy()
        else:
            stitch.check(c.keys() == list(reversed(model)), "keys() lists the most recent first")
        stitch.check(len(c) == len(model), "len() agrees with the model")
    c.items(), c.values(), repr(c)
    c.clear()


@stitch.endpoint
def logging_undefined(name: OneOf[["x", "y", "obj"]], op: OneOf[["str", "iter", "bool", "len", "attr"]]):
    logger = logging.getLogger("stitch-jinja")
    logger.propagate = False
    U = jinja2.make_logging_undefined(logger, jinja2.Undefined)
    u = U(name=name)
    try:
        {"str": str, "iter": list, "bool": bool, "len": len, "attr": lambda u: u.anything}[op](u)
    except jinja2.UndefinedError:
        pass
    stitch.check(jinja2.is_undefined(u), "a logging undefined is undefined")


@stitch.endpoint
def async_stream(env: Borrow[Env], source: Str[512, JinjaText], s: Src[128]):
    """generate_async() and make_module_async() agree with render_async()."""
    autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix, sandboxed, optimized = eval(stitch.get(env, "opts"))
    e = make_env(env_class(sandboxed), autoescape, trim, lstrip, keep_nl, undefined, ext, line_prefix,
                 optimized=optimized, enable_async=True)
    e.loader = jinja2.DictLoader(dict(_loader(env)))
    ctx = _contexts(s)
    with Guard():
        t = e.from_string(source)

    async def gen():
        return "".join([x async for x in t.generate_async(ctx())])

    async def module():
        m = await t.make_module_async(ctx())
        return str(m)

    want = _outcome(lambda: asyncio.run(t.render_async(ctx())))
    got = _outcome(lambda: asyncio.run(gen()))
    stitch.check(got == want, "generate_async() agrees with render_async()")
    _outcome(lambda: asyncio.run(module()))
