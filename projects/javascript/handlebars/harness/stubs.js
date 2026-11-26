// handlebars: compile and render templates; environments (helpers, partials, decorators); the
// parser, AST visitors and compiler classes; Utils, SafeString, logger and VM.
// Oracles:
// - a compiled template's generated JavaScript is valid (a SyntaxError means template text got
//   into the code) and never runs code planted in the template (canary global);
// - compile(), compile(parse()) and precompile() + template() render the same;
// - parse/print/compile agree: a template that parses compiles; parse and parseWithoutProcessing
//   accept the same templates; Visitor/PrintVisitor walks agree with print(); two Compiler runs
//   on one AST are equal(), and JavaScriptCompiler output equals precompile();
// - escapeExpression leaves no raw HTML specials and round-trips; Utils agree with their contracts;
// - errors other than Handlebars.Exception (and errors from helpers / missing data in strict
//   modes) are bugs; the runtime also reports prototype pollution.
const stitch = require("stitch");
const { Borrow, New, Str, OneOf, Bool, Flags, Src, Range } = stitch;
const Handlebars = require("handlebars");

const Env = stitch.object("Env");
const Tmpl = stitch.object("Tmpl");
const HbsText = stitch.datatype("HbsText");

globalThis.__stitchPwned = false;
const PWN = "__stitchPwned=true";

const PATHS = ["this", ".", "..", "../name", "name", "user.name", "items", "items.[0]", "items.length", "@index", "@key", "@first", "@last",
  "@root.user", "@root", "this.name", "constructor", "__proto__", "__proto__.polluted", "constructor.prototype", "toString", "[a b]",
  "user/name", "../../x", "valueOf", "hasOwnProperty", "lookup", "@partial-block", "x", "n", "html", "fn"];
// helpers in the positions their contract allows (block helpers used inline throw TypeErrors by
// design in handlebars: "options.fn is not a function")
const INLINE = ["upper", "concat", "hash", "throws", "log"];
const BLOCK = ["if", "unless", "each", "with", "noop", "blk"];
const PARTIALS = ["p", "q", "r", "missing", "[p]", "@partial-block", "(lookup . 'name')", "constructor", "toString", "__proto__"];

stitch.decoder(HbsText, (s) => {
  const path = () => (s.coin(0.75) ? s.pick(PATHS) : s.token());
  const param = (d) => {
    switch (s.choice(d < 2 ? 7 : 5)) {
      case 0: return path();
      case 1: return JSON.stringify(s.str(6));
      case 2: return s.pick(["1", "-1", "0.5", "true", "false", "null", "undefined", "'s'", "\"q\""]);
      case 3: return `${s.pick(["a", "b", "key", "fn"])}=${param(d + 1)}`;
      case 4: return s.token();
      default: return s.coin(0.8) ? `(${s.pick(INLINE)} ${param(d + 1)})` : `(lookup ${path()} ${param(d + 1)})`;
    }
  };
  const ws = () => (s.coin(0.2) ? "~" : "");
  const mustache = (d) => {
    const args = [];
    while (s.coin(0.4) && args.length < 4) args.push(param(d));
    if (s.coin(0.1)) return `lookup ${path()} ${param(d)}`;
    const head = s.coin(0.4) ? s.pick(INLINE) : path();
    return [head, ...args].join(" ");
  };
  const block = (d) => {
    let out = "";
    while (s.more()) {
      switch (s.choice(d < 4 ? 14 : 5)) {
        case 0: out += s.str(8); break;
        case 1: out += `{{${ws()}${mustache(d)}${ws()}}}`; break;
        case 2: out += `{{{${ws()}${mustache(d)}${ws()}}}}`; break;
        case 3: out += s.pick(["\n", "  \n", " ", "\r\n"]); break;
        case 4: out += s.pick([`{{!${s.str(5)}}}`, `{{!--${s.str(5)}--}}`, "\\{{escaped}}", "{{&raw}}", "{{else}}", "{{/if}}", "}}", "{{"]); break;
        case 5: case 6: {
          const h = s.pick([...BLOCK, path()]);
          const params = s.coin(0.8) ? " " + param(d) : "";
          const bp = s.coin(0.2) ? ` as |${s.pick(["a", "a b", "item idx"])}|` : "";
          out += `{{${ws()}#${s.coin(0.1) ? "*inline \"p\"" : h + params + bp}${ws()}}}` + block(d + 1) +
            (s.coin(0.3) ? `{{${s.pick(["else", "^", `else if ${param(d)}`, `else ${s.pick(BLOCK)} ${param(d)}`])}}}` + block(d + 1) : "") +
            `{{${ws()}/${s.coin(0.95) ? (s.coin(0.1) ? "*inline" : h) : path()}${ws()}}}`;
          break;
        }
        case 7: out += `{{^${path()}}}` + block(d + 1) + `{{/${path()}}}`; break;
        case 8: out += `{{> ${s.pick(PARTIALS)}${s.coin(0.3) ? " " + param(d) : ""}}}`; break;
        case 9: out += `{{#> ${s.pick(["p", "q", "missing"])}}}` + block(d + 1) + `{{/${s.pick(["p", "q", "missing"])}}}`; break;
        case 10: out += `{{{{${s.pick(["raw", "noop"])}}}}}${s.str(6)}{{{{/${s.pick(["raw", "noop"])}}}}}`; break;
        case 11: out += s.coin(0.5) ? `{{*dec${s.coin(0.3) ? " " + param(d) : ""}}}` : `{{#*dec}}` + block(d + 1) + "{{/dec}}"; break;
        case 12: out += `{{log ${param(d)} level=${s.pick(["\"debug\"", "\"info\"", "\"warn\"", "\"error\"", "1", "\"x\""])}}}`; break;
        default: out += `{{#*inline "${s.pick(["p", "q", "r"])}"}}` + block(d + 1) + "{{/inline}}"; break;
      }
    }
    return out;
  };
  return block(0);
});

function setup(env) {
  env.registerHelper("upper", (x) => String(x).toUpperCase());
  env.registerHelper("concat", (...a) => a.slice(0, -1).join(""));
  // (a helper gets its params first and the options object last)
  env.registerHelper("noop", function (...a) { const opts = a[a.length - 1]; return opts && opts.fn ? opts.fn(this) : ""; });
  env.registerHelper("hash", (...a) => JSON.stringify(Object.keys(a[a.length - 1].hash || {})));
  env.registerHelper("throws", () => { throw new HelperError("helper threw"); });
  env.registerHelper("raw", (...a) => { const opts = a[a.length - 1]; return opts && opts.fn ? opts.fn() : ""; });
  env.registerHelper("blk", function (...a) { const opts = a[a.length - 1]; return opts && opts.fn ? opts.fn(this) + (opts.inverse ? opts.inverse(this) : "") : ""; });
  env.registerDecorator("dec", decorator("count"));
  env.registerPartial("p", "<p {{name}}>{{> @partial-block}}</p>");
  env.registerPartial("q", "[{{#each items}}{{this}}{{/each}}]");
  return env;
}
class HelperError extends Error {}

function decorator(kind) {
  return function (program, props) {
    props.decorated = (props.decorated || 0) + 1;
    if (kind === "wrap") return function (context, options) { return "[" + program(context, options) + "]"; };
    if (kind === "throw") throw new HelperError("decorator threw");
    return undefined;
  };
}

stitch.endpoint("env", {}, New(Env), () => setup(Handlebars.create()));

stitch.endpoint("register_partial", { env: Borrow(Env), name: OneOf("p", "q", "r"), source: Str(512, HbsText), kind: OneOf("string", "compiled", "object") }, null, ({ env, name, source, kind }) => {
  if (kind === "string") env.registerPartial(name, source);
  else if (kind === "object") env.registerPartial({ [name]: source, r: "<r>" });
  else {
    let t;
    try {
      t = env.compile(source);
      t();  // compile errors surface on the first call
    } catch (e) {
      if (templateError(e) || e instanceof TypeError || recursion(e)) stitch.bail("partial does not compile");
      throw e;
    }
    env.registerPartial(name, t);
  }
  stitch.check(env.partials[name] !== undefined, "registerPartial() registers the partial");
});

// names include the built-in helpers (overriding them is allowed) and Object.prototype names
const HELPER_NAMES = ["x", "y", "upper", "concat", "if", "each", "lookup", "with", "helperMissing", "blockHelperMissing", "__proto__", "constructor", "toString", "hasOwnProperty"];
const HELPERS = {
  const: () => "C",
  block: function (...a) { const opts = a[a.length - 1]; return opts && opts.fn ? "{" + opts.fn(this) + "}" : "B"; },
  safe: () => new Handlebars.SafeString("<s>"),
  throwing: () => { throw new HelperError("helper threw"); },
  args: (...a) => String(a.length),
};

stitch.endpoint("register_helper", { env: Borrow(Env), name: OneOf(...HELPER_NAMES), kind: OneOf(...Object.keys(HELPERS)), many: Bool }, null, ({ env, name, kind, many }) => {
  if (many) env.registerHelper({ [name]: HELPERS[kind], [name === "y" ? "z" : "y"]: HELPERS.const });
  else env.registerHelper(name, HELPERS[kind]);
  if (name !== "__proto__") stitch.check(env.helpers[name] === HELPERS[kind], "registerHelper() registers the helper");
});

stitch.endpoint("register_decorator", { env: Borrow(Env), kind: OneOf("count", "wrap", "throw"), many: Bool }, null, ({ env, kind, many }) => {
  const fn = decorator(kind);
  if (many) env.registerDecorator({ dec: fn });
  else env.registerDecorator("dec", fn);
  stitch.check(env.decorators.dec === fn, "registerDecorator() registers the decorator");
});

// only names the harness registered and did not declare in knownHelpers: removing helperMissing,
// inline or a known helper is the caller's mistake
stitch.endpoint("unregister", { env: Borrow(Env), what: OneOf("Helper", "Partial", "Decorator"), name: OneOf("x", "y", "blk", "raw", "hash", "p", "q", "r", "dec") }, null, ({ env, what, name }) => {
  env["unregister" + what](name);
  const table = { Helper: env.helpers, Partial: env.partials, Decorator: env.decorators }[what];
  stitch.check(!Object.prototype.hasOwnProperty.call(table, name), `unregister${what}() removes it`);
});

stitch.endpoint("register_errors", { env: Borrow(Env), which: OneOf("helper", "decorator", "partial") }, null, ({ env, which }) => {
  // the documented misuse errors are Handlebars.Exceptions
  try {
    if (which === "helper") env.registerHelper({ a: HELPERS.const }, HELPERS.const);
    else if (which === "decorator") env.registerDecorator({ a: decorator("count") }, decorator("count"));
    else env.registerPartial("p", undefined);
  } catch (e) {
    stitch.check(e instanceof Handlebars.Exception, "registration misuse throws Handlebars.Exception");
    return;
  }
  stitch.check(false, "registration misuse throws");
});

const OPTS = Flags(1, 2, 4, 8, 16, 32, 64, 128, 256);
function compileOptions(o) {
  return {
    strict: !!(o & 1), assumeObjects: !!(o & 2), knownHelpersOnly: !!(o & 4), noEscape: !!(o & 8), compat: !!(o & 16),
    preventIndent: !!(o & 32), explicitPartialContext: !!(o & 64), ignoreStandalone: !!(o & 128), data: !(o & 256),
    knownHelpers: { upper: true, concat: true, noop: true },
  };
}

// errors that are the template's or the data's: Handlebars.Exception, helpers throwing, and the
// TypeErrors strict / assumeObjects modes produce on missing data by design
// (jison parse errors are plain Errors with a `hash`)
// TypeErrors thrown inside the built-in helpers on wrong arity are their (known) contract
const builtinMisuse = (e) => e instanceof TypeError && /\/helpers\/[a-z-]+\.js:/.test(String(e.stack).split("\n")[1] || "");
// (a decorator the environment does not have is reported with a plain Error)
const templateError = (e) => e instanceof Handlebars.Exception || e instanceof HelperError || builtinMisuse(e) ||
  (e instanceof Error && /^Missing decorator: /.test(e.message)) ||
  (e instanceof Error && /^Parse error on line|^Lexical error on line/.test(e.message));
// partials that include themselves recurse until the stack runs out: the template's fault
const recursion = (e) => e instanceof RangeError && /Maximum call stack/.test(e.message);
// a context value used as a helper (`{{name arg}}` where name is not a function) is the data's fault
const notCallable = (e) => e instanceof TypeError && /\.call is not a function/.test(e.message);
const dataError = (opts) => (e) => templateError(e) || recursion(e) || notCallable(e) || ((opts.strict || opts.assumeObjects) && e instanceof TypeError);

function context(s) {
  const value = (d) => {
    switch (s.choice(d < 2 ? 8 : 5)) {
      case 0: return s.range(-5, 100);
      case 1: return s.str(8);
      case 2: return s.pick([null, undefined, true, false, 0, "", "<b>&</b>"]);
      case 3: return new Handlebars.SafeString("<i>safe</i>");
      case 4: return s.pick(["a", "b", "x"]);
      case 5: { const a = []; const n = s.choice(4); for (let i = 0; i < n; i++) a.push(value(d + 1)); return a; }
      case 6: { const o = {}; const n = s.choice(4); for (let i = 0; i < n; i++) o[s.pick(["a", "b", "name", "items", "fn"])] = value(d + 1); return o; }
      default: return () => "fnval";
    }
  };
  const ctx = { name: s.str(6), user: { name: "u", tags: ["a", "b"] }, items: [1, "two", { name: "three" }], html: "<b>", n: s.range(0, 9) };
  while (s.more()) ctx[s.pick(["name", "items", "user", "x", "n", "fn"])] = value(0);
  return ctx;
}

stitch.endpoint("compile", { env: Borrow(Env), source: Str(1024, HbsText), opts: OPTS }, New(Tmpl), ({ env, source, opts }) => {
  const o = compileOptions(opts);
  let spec;
  try {
    spec = env.precompile(source, o);
  } catch (e) {
    if (templateError(e)) stitch.bail("compile error");
    throw e;
  }
  try {
    new Function("return " + spec);  // eslint-disable-line no-new-func
  } catch (e) {
    stitch.log(String(spec).slice(0, 2000));
    stitch.check(!(e instanceof SyntaxError), `precompiled template is valid JavaScript (${e.message})`);
  }
  const t = env.compile(source, o);
  stitch.set(t, "opts", opts);
  t.__source = source;
  t.__spec = spec;
  t.__env = env;
  return t;
});

const RUNTIME = Flags(1, 2, 4, 8, 16, 32, 64);
function runtimeOptions(r) {
  const o = { allowProtoPropertiesByDefault: !!(r & 1), allowProtoMethodsByDefault: !!(r & 2), data: r & 4 ? { extra: 1 } : undefined };
  if (r & 8) o.helpers = { upper: (x) => `rt:${x}`, rt: () => "RT" };
  if (r & 16) o.partials = { q: "<rt-q {{name}}>" };
  if (r & 32) { o.allowedProtoProperties = { constructor: true, __proto__: false }; o.allowedProtoMethods = { toString: true, valueOf: false }; }
  if (r & 64) o.allowCallsToHelperMissing = true;
  return o;
}

stitch.endpoint("render", { t: Borrow(Tmpl), s: Src(256), rt: RUNTIME }, null, ({ t, s, rt }) => {
  const o = compileOptions(stitch.get(t, "opts", 0));
  const ctx = context(s);
  const ropts = runtimeOptions(rt);
  // calling helperMissing / blockHelperMissing by name is refused (a TypeError) unless allowed
  const isDataError = (e) => dataError(o)(e) || (e instanceof TypeError && /helperMissing/i.test(t.__source) && !ropts.allowCallsToHelperMissing);
  let out;
  globalThis.__stitchPwned = false;
  try {
    out = t(ctx, ropts);
  } catch (e) {
    if (isDataError(e)) stitch.bail("render error");
    throw e;
  }
  stitch.check(!globalThis.__stitchPwned, "template ran planted code");
  stitch.log(`out: ${String(out).slice(0, 300)}`);
  // the same template through precompile + Handlebars.template()
  const tmpl2 = t.__env.template(eval("(" + t.__spec + ")"));  // eslint-disable-line no-eval
  let out2;
  try {
    out2 = tmpl2(ctx, ropts);
  } catch (e) {
    if (isDataError(e)) return;
    throw e;
  }
  if (out !== out2) stitch.log(`precompiled: ${String(out2).slice(0, 300)}`);
  // string partials registered on the environment are compiled when first used: with the
  // template's options under compile(), with the defaults under a precompiled template
  const stringPartials = /\{\{~?\s*#?>/.test(t.__source) && (stitch.get(t, "opts", 0) & 255);
  if (!stringPartials) stitch.check(out === out2, "compile() and precompile()+template() render the same");
  // and through an AST parsed separately (compile() applies the whitespace control itself)
  const ast = t.__env.parseWithoutProcessing(t.__source);
  let out3;
  try {
    out3 = t.__env.compile(ast, o)(ctx, ropts);
  } catch (e) {
    if (isDataError(e)) return;
    throw e;
  }
  stitch.check(out === out3, "compile(source) and compile(parseWithoutProcessing(source)) render the same");
});

stitch.endpoint("parse_print", { source: Str(1024, HbsText) }, null, ({ source }) => {
  let ast, raw;
  try {
    raw = Handlebars.parseWithoutProcessing(source);
  } catch (e) {
    if (!templateError(e)) throw e;
  }
  try {
    ast = Handlebars.parse(source);
  } catch (e) {
    stitch.check(raw === undefined, "parse() fails only where parseWithoutProcessing() fails");
    if (templateError(e)) stitch.bail("parse error");
    throw e;
  }
  stitch.check(raw !== undefined, "parseWithoutProcessing() accepts what parse() accepts");
  const printed = Handlebars.print(ast);
  stitch.check(typeof printed === "string", "print returns text");
  stitch.check(new Handlebars.PrintVisitor().accept(ast) === printed, "PrintVisitor#accept == print()");
  // a mutating Visitor that changes nothing leaves the tree as it was, and sees every path
  const H = Handlebars.AST.helpers;
  const v = new Handlebars.Visitor();
  v.mutating = true;
  let paths = 0;
  v.PathExpression = function (path) {
    paths++;
    stitch.check(H.simpleId(path) === (path.parts.length === 1 && !H.scopedId(path) && !path.depth), "AST.helpers.simpleId agrees with the path");
  };
  for (const type of ["MustacheStatement", "SubExpression", "BlockStatement"]) {
    v[type] = function (node) {
      stitch.check(typeof H.helperExpression(node) === "boolean", "AST.helpers.helperExpression");
      return Handlebars.Visitor.prototype[type].call(this, node);
    };
  }
  stitch.check(v.accept(ast) === ast, "a mutating Visitor returns the program");
  stitch.check(Handlebars.print(ast) === printed, "a no-op mutating Visitor keeps the tree");
  if (/PATH:/.test(printed)) stitch.check(paths > 0, "Visitor visits the paths print() shows");
  try {
    Handlebars.precompile(ast, {});
  } catch (e) {
    if (templateError(e)) return;  // (semantic errors: e.g. mismatched block names)
    throw e;
  }
});

stitch.endpoint("compiler_api", { env: Borrow(Env), source: Str(1024, HbsText), opts: OPTS }, null, ({ env, source, opts }) => {
  let ast, c1, c2, spec;
  try {
    // the option defaults precompile() applies
    const options = () => { const o = compileOptions(opts); if (o.compat) o.useDepths = true; return o; };
    ast = env.parse(source, options());
    const o1 = options();
    c1 = new env.Compiler().compile(ast, o1);
    c2 = new env.Compiler().compile(ast, options());
    spec = new env.JavaScriptCompiler().compile(c1, o1);
  } catch (e) {
    if (templateError(e)) stitch.bail("compile error");
    throw e;
  }
  stitch.check(c1.equals(c2) && c2.equals(c1), "two Compiler runs on one AST are equal()");
  stitch.check(spec === env.precompile(source, compileOptions(opts)), "JavaScriptCompiler output == precompile()");
});

const ENTITY = /&(?:amp|lt|gt|quot|#x27|#x60|#x3D);/g;
const unescapeHtml = (e) => e.replace(ENTITY, (m) => ({ "&amp;": "&", "&lt;": "<", "&gt;": ">", "&quot;": "\"", "&#x27;": "'", "&#x60;": "`", "&#x3D;": "=" })[m]);

stitch.endpoint("utils", { text: Str(256, HbsText), rev: Range(5, 10), s: Src(8) }, null, ({ text, rev, s }) => {
  const U = Handlebars.Utils;
  const e = U.escapeExpression(text);
  stitch.check(!/[<>"'`=]/.test(e) && !/&/.test(e.replace(ENTITY, "")), "escapeExpression() leaves no raw HTML specials");
  stitch.check(unescapeHtml(e) === text, "escapeExpression() round-trips");
  stitch.check(Handlebars.escapeExpression === U.escapeExpression, "Handlebars.escapeExpression is Utils.escapeExpression");
  const safe = new Handlebars.SafeString(text);
  stitch.check(U.escapeExpression(safe) === text && safe.toHTML() === text && String(safe) === text, "SafeString passes through unescaped");
  stitch.check(U.escapeExpression(null) === "" && U.escapeExpression(undefined) === "" && U.escapeExpression(false) === "false" && U.escapeExpression(0) === "0", "escapeExpression() of non-strings");
  stitch.check(U.isEmpty([]) && U.isEmpty("") && U.isEmpty(null) && U.isEmpty(false) && !U.isEmpty(0) && !U.isEmpty([0]) && !U.isEmpty(text || "x"), "isEmpty()");
  stitch.check(U.isArray([]) && !U.isArray(text) && U.isFunction(U.isArray) && !U.isFunction(text) && U.toString.call([]) === "[object Array]", "isArray/isFunction/toString");
  const chars = Array.from(text);
  const c = chars.length ? s.pick(chars) : "x";
  stitch.check(U.indexOf(chars, c) === chars.indexOf(c), "indexOf() == Array#indexOf");
  const src = { a: "A", [text]: 1 };
  const ext = U.extend({ b: 2 }, src);
  stitch.check(Object.keys(src).every((k) => k === "__proto__" || ext[k] === src[k]) && ext.b === (text === "b" ? 1 : 2), "extend() copies own properties");
  const frame = U.createFrame(src);
  stitch.check(frame._parent === src && frame.a === src.a && Handlebars.createFrame === U.createFrame, "createFrame() copies and links the parent");
  const params = [1];
  stitch.check(U.blockParams(params, ["x"]) === params && params.path[0] === "x", "blockParams() sets the ids");
  stitch.check(U.appendContextPath("", text) === text && U.appendContextPath("a", text) === `a.${text}`, "appendContextPath()");
  // logger: level names map to their index, anything else through parseInt
  const L = Handlebars.logger;
  const lv = L.lookupLevel(text);
  const idx = L.methodMap.indexOf(text.toLowerCase());
  stitch.check(Object.is(lv, idx >= 0 ? idx : parseInt(text, 10)), "logger.lookupLevel()");
  Handlebars.log("debug", "below the default level");
  L.log(0, "below the default level");
  // VM: revisions in [LAST_COMPATIBLE_COMPILER_REVISION, COMPILER_REVISION] run, others throw
  let ok = true;
  try {
    Handlebars.VM.checkRevision([rev, "x"]);
  } catch (err) {
    stitch.check(err instanceof Handlebars.Exception, "checkRevision() throws Handlebars.Exception");
    ok = false;
  }
  stitch.check(ok === (rev >= Handlebars.LAST_COMPATIBLE_COMPILER_REVISION && rev <= Handlebars.COMPILER_REVISION), "checkRevision() accepts the compatible revisions");
  stitch.check(Handlebars.VM.noop() === "" && Handlebars.noConflict() === Handlebars && typeof Handlebars.REVISION_CHANGES[Handlebars.COMPILER_REVISION] === "string", "VM.noop/noConflict/REVISION_CHANGES");
  // a name the compiler accepts as an identifier is used in a property access
  if (Handlebars.JavaScriptCompiler.isValidJavaScriptVariableName(text)) {
    try {
      new Function("o", `return o.${text};`);  // eslint-disable-line no-new-func
    } catch (err) {
      stitch.check(false, `isValidJavaScriptVariableName(${JSON.stringify(text)}) is a valid property name (${err.message})`);
    }
  }
  Handlebars.create().resetLoggedPropertyAccesses();
});

stitch.endpoint("canary", { env: Borrow(Env), text: Str(64), where: OneOf("mustache", "string", "partial", "hash", "path") }, null, ({ env, text, where }) => {
  // template text that tries to break out of a string or identifier context in the generated code
  const payload = `${text}${PWN}`;
  const src = {
    mustache: `{{${payload}}}`, string: `{{upper "${payload}"}}`, partial: `{{> "${payload}"}}`, hash: `{{hash ${text}=${JSON.stringify(PWN)}}}`, path: `{{[${payload}]}}`,
  }[where];
  globalThis.__stitchPwned = false;
  try {
    const t = env.compile(src, { strict: text.length % 2 === 1, compat: text.length % 3 === 0 });
    t({});
  } catch (e) {
    if (!(templateError(e) || e instanceof TypeError)) throw e;
  }
  stitch.check(!globalThis.__stitchPwned, "template text executed as code");
});
