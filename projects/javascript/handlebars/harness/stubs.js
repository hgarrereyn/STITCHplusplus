// handlebars: compile and render templates.
// Oracles:
// - a compiled template's generated JavaScript is valid (a SyntaxError means template text got
//   into the code) and never runs code planted in the template (canary global);
// - compile() and precompile() + template() render the same;
// - parse/print/compile agree: a template that parses compiles;
// - errors other than Handlebars.Exception (and errors from helpers / missing data in strict
//   modes) are bugs; the runtime also reports prototype pollution.
const stitch = require("stitch");
const { Borrow, New, Str, OneOf, Bool, Flags, Src } = stitch;
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
const BLOCK = ["if", "unless", "each", "with", "noop"];

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
      switch (s.choice(d < 4 ? 12 : 5)) {
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
        case 8: out += `{{> ${s.pick(["p", "q", "(lookup . 'name')", "missing", "[p]", "@partial-block"])}${s.coin(0.3) ? " " + param(d) : ""}}}`; break;
        case 9: out += `{{#> ${s.pick(["p", "q", "missing"])}}}` + block(d + 1) + `{{/${s.pick(["p", "q", "missing"])}}}`; break;
        case 10: out += `{{{{${s.pick(["raw", "noop"])}}}}}${s.str(6)}{{{{/${s.pick(["raw", "noop"])}}}}}`; break;
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
  env.registerHelper("noop", function (opts) { return opts && opts.fn ? opts.fn(this) : ""; });
  env.registerHelper("hash", (opts) => JSON.stringify(Object.keys((opts && opts.hash) || {})));
  env.registerHelper("throws", () => { throw new HelperError("helper threw"); });
  env.registerHelper("raw", (opts) => (opts && opts.fn ? opts.fn() : ""));
  env.registerPartial("p", "<p {{name}}>{{> @partial-block}}</p>");
  env.registerPartial("q", "[{{#each items}}{{this}}{{/each}}]");
  return env;
}
class HelperError extends Error {}

stitch.endpoint("env", {}, New(Env), () => setup(Handlebars.create()));

stitch.endpoint("register_partial", { env: Borrow(Env), name: OneOf("p", "q", "r"), source: Str(512, HbsText) }, null, ({ env, name, source }) => {
  env.registerPartial(name, source);
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
const templateError = (e) => e instanceof Handlebars.Exception || e instanceof HelperError || builtinMisuse(e) ||
  (e instanceof Error && /^Parse error on line|^Lexical error on line/.test(e.message));
// partials that include themselves recurse until the stack runs out: the template's fault
const recursion = (e) => e instanceof RangeError && /Maximum call stack/.test(e.message);
const dataError = (opts) => (e) => templateError(e) || recursion(e) || ((opts.strict || opts.assumeObjects) && e instanceof TypeError);

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

const RUNTIME = Flags(1, 2, 4);
function runtimeOptions(r) {
  return { allowProtoPropertiesByDefault: !!(r & 1), allowProtoMethodsByDefault: !!(r & 2), data: r & 4 ? { extra: 1 } : undefined };
}

stitch.endpoint("render", { t: Borrow(Tmpl), s: Src(256), rt: RUNTIME }, null, ({ t, s, rt }) => {
  const o = compileOptions(stitch.get(t, "opts", 0));
  const ctx = context(s);
  const ropts = runtimeOptions(rt);
  let out;
  globalThis.__stitchPwned = false;
  try {
    out = t(ctx, ropts);
  } catch (e) {
    if (dataError(o)(e)) stitch.bail("render error");
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
    if (dataError(o)(e)) return;
    throw e;
  }
  if (out !== out2) stitch.log(`precompiled: ${String(out2).slice(0, 300)}`);
  stitch.check(out === out2, "compile() and precompile()+template() render the same");
});

stitch.endpoint("parse_print", { source: Str(1024, HbsText) }, null, ({ source }) => {
  let ast;
  try {
    ast = Handlebars.parse(source);
  } catch (e) {
    if (templateError(e)) stitch.bail("parse error");
    throw e;
  }
  const printed = Handlebars.print(ast);
  stitch.check(typeof printed === "string", "print returns text");
  try {
    Handlebars.precompile(ast, {});
  } catch (e) {
    if (templateError(e)) return;  // (semantic errors: e.g. mismatched block names)
    throw e;
  }
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
