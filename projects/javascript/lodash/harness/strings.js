// lodash String functions and _.template.
// Oracles: escape/unescape round trip; escapeRegExp makes a pattern that matches the text
// literally; trim*/pad*/repeat/split/startsWith/endsWith/toLower/toUpper agree with the native
// String methods; the case functions are built from the same words; truncate stays within its
// length. template(): text outside delimiters renders verbatim, interpolations render the data,
// and no option (variable, imports, sourceURL) runs code planted in it (canary global).
const stitch = require("stitch");
const { Str, OneOf, Range, Val, Bool } = stitch;
const _ = require("lodash");

const Text = stitch.datatype("Text");
const TplParts = stitch.datatype("TplParts");

const TOKENS = ["&amp;", "&lt;", "&#39;", "&quot;", "<", ">", "&", "\"", "'", "`", "camelCase", "XMLHttpRequest", "foo-bar", "foo_bar",
  "ABCdef", "1st", "a1b2", "é", "é", "Àß", "😀", "👨‍👩", "❤️", "🇺🇸",
  " ", "  ", "\t", "\n", " ", " ", "﻿", "__proto__", "İ", "Ǆ", "\ud800", "-", "_", ".", "*", "+", "?", "^", "$", "(", ")",
  "[", "]", "{", "}", "|", "\\", "/", "’", "it's", "\u0000"];
stitch.decoder(Text, (s) => {
  let out = "";
  while (s.more() && out.length < 120) out += s.coin(0.6) ? s.pick(TOKENS) : s.str(6);
  return out;
});

const ascii = (x) => /^[\x20-\x7e]*$/.test(x);

stitch.endpoint("escape", { s: Str(256, Text) }, null, ({ s }) => {
  const e = _.escape(s);
  stitch.check(!/[<>"']/.test(e), "escape() leaves no raw < > \" '");
  stitch.check(_.unescape(e) === s, "unescape(escape(s)) == s");
  stitch.check(new RegExp(`^(?:${_.escapeRegExp(s)})$`).test(s), "escapeRegExp(s) matches s literally");
  if (ascii(s)) stitch.check(_.deburr(s) === s, "deburr() keeps ASCII text");
});

stitch.endpoint("trim_pad", { s: Str(128, Text), chars: OneOf(undefined, "", " ", "ab", "-_", "😀", "éx"), n: Range(0, 80) }, null, ({ s, chars, n }) => {
  if (chars === undefined) {
    stitch.check(_.trim(s) === s.trim() && _.trimStart(s) === s.trimStart() && _.trimEnd(s) === s.trimEnd(), "trim/trimStart/trimEnd() == the String methods");
  } else {
    const cs = _.toArray(chars);
    for (const [fn, atStart, atEnd] of [["trim", true, true], ["trimStart", true, false], ["trimEnd", false, true]]) {
      const r = _[fn](s, chars), rs = _.toArray(r);
      stitch.check(s.includes(r), `${fn}(s, chars) is a substring of s`);
      if (rs.length) stitch.check(!(atStart && cs.includes(rs[0])) && !(atEnd && cs.includes(rs[rs.length - 1])), `${fn}(s, chars) strips the chars`);
    }
  }
  const ps = _.padStart(s, n, chars), pe = _.padEnd(s, n, chars), p = _.pad(s, n, chars);
  stitch.check(ps.endsWith(s) && pe.startsWith(s) && p.includes(s), "pad*() keep s");
  if (ascii(s) && (chars === undefined || (chars !== "" && ascii(chars)))) {
    const want = Math.max(n, s.length);
    stitch.check(ps.length === want && pe.length === want && p.length === want, "pad*() pad to the length");
  }
  stitch.check(_.repeat(s, n % 20) === s.repeat(n % 20), "repeat() == String#repeat");
});

stitch.endpoint("search", { s: Str(128, Text), t: Str(16, Text), pos: Range(-2, 140), limit: OneOf(undefined, 0, 1, 3), sep: OneOf("", ",", " ", "a", "--") }, null, ({ s, t, pos, limit, sep }) => {
  if (pos >= 0 && pos <= s.length) {
    stitch.check(_.startsWith(s, t, pos) === s.startsWith(t, pos) && _.endsWith(s, t, pos) === s.endsWith(t, pos), "startsWith/endsWith() == the String methods");
  }
  stitch.check(_.startsWith(s, t) === s.startsWith(t) && _.endsWith(s, t) === s.endsWith(t), "startsWith/endsWith() without a position");
  if (ascii(s)) stitch.check(JSON.stringify(_.split(s, sep, limit)) === JSON.stringify(s.split(sep, limit)), "split() == String#split for ASCII text");
  stitch.check(_.replace(s, t, "$&!") === s.replace(t, "$&!"), "replace() == String#replace");
  for (const w of _.words(s)) stitch.check(s.includes(w), "words() are substrings");
  stitch.check(JSON.stringify(_.words(s, /\w+/g)) === JSON.stringify(s.match(/\w+/g) || []), "words(s, pattern) == s.match(pattern)");
});

stitch.endpoint("cases", { s: Str(128, Text) }, null, ({ s }) => {
  stitch.check(_.toLower(s) === s.toLowerCase() && _.toUpper(s) === s.toUpperCase(), "toLower/toUpper() == the String methods");
  const out = {};
  for (const fn of ["camelCase", "kebabCase", "lowerCase", "snakeCase", "startCase", "upperCase", "capitalize", "lowerFirst", "upperFirst"]) out[fn] = _[fn](s);
  stitch.check(out.capitalize === _.upperFirst(s.toLowerCase()), "capitalize(s) == upperFirst(lower(s))");
  if (ascii(s)) {
    const lw = out.lowerCase ? out.lowerCase.split(" ") : [];
    stitch.check(JSON.stringify(out.kebabCase ? out.kebabCase.split("-") : []) === JSON.stringify(lw), "kebabCase() and lowerCase() have the same words");
    stitch.check(JSON.stringify(out.snakeCase ? out.snakeCase.split("_") : []) === JSON.stringify(lw), "snakeCase() and lowerCase() have the same words");
    stitch.check(out.upperCase === out.lowerCase.toUpperCase() && out.startCase.toLowerCase() === out.lowerCase, "upperCase/startCase() agree with lowerCase()");
    stitch.check(/^[A-Za-z0-9]*$/.test(out.camelCase) && out.camelCase.toLowerCase() === lw.join(""), "camelCase() joins the words");
    stitch.check(out.upperFirst === s.charAt(0).toUpperCase() + s.slice(1) && out.lowerFirst === s.charAt(0).toLowerCase() + s.slice(1), "upperFirst/lowerFirst() change the first character");
  }
});

const SEPARATORS = [undefined, " ", ",", /,? +/, /\s/, /(?=a)/];
stitch.endpoint("truncate", { s: Str(256, Text), length: Range(-1, 64), omission: OneOf(undefined, "", "...", "…", "[cut]"), sep: Range(0, SEPARATORS.length - 1) }, null,
  ({ s, length, omission, sep }) => {
    const opts = { length, separator: SEPARATORS[sep] };
    if (omission !== undefined) opts.omission = omission;
    const om = omission === undefined ? "..." : omission;
    const r = _.truncate(s, opts);
    if (!ascii(s)) return;
    if (s.length <= length) return stitch.check(r === s, "truncate() keeps text within the length");
    stitch.check(r.endsWith(om) && s.startsWith(r.slice(0, r.length - om.length)), "truncate() is a prefix plus the omission");
    stitch.check(r.length <= Math.max(length, om.length), "truncate() stays within the length");
  });

// ---- template ----
const KEYS = ["a", "b", "c"];
const DELIMS = {
  erb: { interp: (x) => `<%= ${x} %>`, esc: (x) => `<%- ${x} %>`, evaluate: (x) => `<% ${x} %>`, opts: {} },
  es: { interp: (x) => `\${${x}}`, esc: (x) => `<%-${x}%>`, evaluate: (x) => `<%${x}%>`, opts: {} },
  mustache: {
    interp: (x) => `{{ ${x} }}`, esc: (x) => `{{- ${x} }}`, evaluate: (x) => `{% ${x} %}`,
    opts: { interpolate: /\{\{([\s\S]+?)\}\}/g, escape: /\{\{-([\s\S]+?)\}\}/g, evaluate: /\{%([\s\S]+?)%\}/g },
  },
};
// text that must not open a delimiter
const literal = (t) => { while (/<%|\$\{|\{\{|\{%/.test(t)) t = t.replace(/<%/g, "<").replace(/\$\{/g, "$").replace(/\{\{/g, "{").replace(/\{%/g, "{"); return t; };

stitch.decoder(TplParts, (s) => {
  const parts = [];
  while (s.more() && parts.length < 12) {
    const c = s.choice(6);
    parts.push(c < 2 ? ["lit", s.coin(0.5) ? s.pick(["'", "\\", "\n", "\r", " ", " ", "`", "*/", "//", "\\'", "');__stitchPwned=true;('", "%>", "}", "\\u0027", "</script>", "__p", "obj"]) + s.str(6) : s.str(10)]
      : [s.pick(["interp", "esc", "print", "upper"]), s.pick(KEYS)]);
  }
  return parts;
});

const VALUES = [null, undefined, "", "x", "<b>&\"'</b>", 0, -0, 1.5, NaN, [1, [2, "<"]], true, "__proto__", "${a}", "<%= b %>"];

globalThis.__stitchPwned = false;
const PWN = "__stitchPwned=true";
const templateError = (e) => e instanceof Error && /^Invalid `(variable|imports)` option passed into `_.template`$/.test(e.message);

stitch.endpoint("template", { parts: Val(TplParts, 256), style: OneOf("erb", "es", "mustache"), named: Bool, va: Range(0, VALUES.length - 1), vb: Range(0, VALUES.length - 1), vc: Range(0, VALUES.length - 1) },
  null, ({ parts, style, named, va, vb, vc }) => {
    const d = DELIMS[style], ref = (k) => (named ? `data.${k}` : k);
    const data = { a: VALUES[va], b: VALUES[vb], c: VALUES[vc] };
    let src = "", want = "", lit = "";
    // (in mustache style a "{" right before "{{" would join the delimiter)
    const flush = () => { const t = style === "mustache" ? literal(lit).replace(/\{+$/, "") : literal(lit); src += t; want += t; lit = ""; };
    for (const [kind, x] of parts) {
      if (kind === "lit") { lit += x; continue; }
      flush();
      const v = data[x];
      if (kind === "interp") { src += d.interp(ref(x)); want += v == null ? "" : String(v); }
      else if (kind === "esc") { src += d.esc(ref(x)); want += _.escape(v); }
      else if (kind === "print") { src += d.evaluate(`print(${ref(x)})`); want += [v].join(""); }
      else { src += d.interp(`_.toUpper(${ref(x)})`); want += _.toUpper(v); }
    }
    flush();
    stitch.log(`source: ${JSON.stringify(src).slice(0, 400)}`);
    globalThis.__stitchPwned = false;
    const t = _.template(src, { ...d.opts, ...(named ? { variable: "data" } : {}) });
    const out = t(data);
    stitch.check(!globalThis.__stitchPwned, "template text ran as code");
    if (out !== want) stitch.log(`out:  ${JSON.stringify(out).slice(0, 300)}\nwant: ${JSON.stringify(want).slice(0, 300)}`);
    stitch.check(out === want, "template renders literal text verbatim and the data in place");
    stitch.check(typeof t.source === "string", "compiled template has its source");
  });

stitch.endpoint("template_options", { text: Str(64, Text), where: OneOf("variable", "imports", "sourceURL"), payload: Bool }, null, ({ text, where, payload }) => {
  // option values that try to break out of the generated function's parameter list or comment
  const value = payload ? `${text}${PWN}` : text;
  const opts = where === "variable" ? { variable: value } : where === "imports" ? { imports: { [value]: 1 } } : { sourceURL: value };
  globalThis.__stitchPwned = false;
  let t;
  try {
    t = _.template("ok", opts);
  } catch (e) {
    stitch.check(!globalThis.__stitchPwned, `template ${where} option ran as code`);
    // a variable name that passes validation can still be a JS syntax error
    if (templateError(e) || (where !== "sourceURL" && e instanceof SyntaxError)) stitch.bail("rejected option");
    throw e;
  }
  const out = t({});
  stitch.check(!globalThis.__stitchPwned, `template ${where} option ran as code`);
  stitch.check(out === "ok", "template output unaffected by the option");
});
