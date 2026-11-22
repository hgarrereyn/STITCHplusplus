const stitch = require("stitch");
const { Borrow, New, Str, Flags, OneOf, Bool } = stitch;
const marked = require("marked");

const MarkdownText = stitch.datatype("MarkdownText");
const Tokens = stitch.object("Tokens");       // block tokens: result of lexer (array with .links)
const InlineTokens = stitch.object("InlineTokens");  // result of lexInline
const Html = stitch.object("Html");           // string produced by a parser/renderer

// Always pass options per call; never use marked.setOptions/use (they mutate global defaults).
const opts = (f) => ({
  ...marked.getDefaults(),   // Lexer/Parser (unlike marked.parse) do not merge defaults: headerPrefix would be "undefined"
  gfm: !(f & 1), breaks: !!(f & 2), pedantic: !!(f & 4), smartLists: !!(f & 8),
  smartypants: !!(f & 16), headerIds: !(f & 32), mangle: !(f & 64), silent: false,
});
const F = Flags(1, 2, 4, 8, 16, 32, 64);
const NF = Flags(1, 2, 8, 16, 32);   // oracles: no pedantic; mangle is random (Math.random), so off
const det = (f) => ({ ...opts(f & ~64), mangle: false });

// Rebuild the source text from a token tree (raw of top-level tokens).
const rawOf = (toks) => toks.map((t) => t.raw).join("");

// Safe to compare on tokens: JSON.stringify; links is a property of the array, not serialized.

stitch.endpoint("parse", { md: Str(2048, MarkdownText), f: F }, New(Html), ({ md, f }) => {
  const out = marked.parse(md, opts(f));
  stitch.check(typeof out === "string", "parse returns a string");
  return out;
});

stitch.endpoint("parse_inline", { md: Str(512, MarkdownText), f: F }, New(Html), ({ md, f }) => {
  const out = marked.parseInline(md, opts(f));
  stitch.check(typeof out === "string", "parseInline returns a string");
  return out;
});

stitch.endpoint("lex", { md: Str(2048, MarkdownText), f: F }, New(Tokens), ({ md, f }) => {
  const toks = marked.lexer(md, opts(f));
  stitch.check(Array.isArray(toks), "lexer returns an array");
  return toks;
});

stitch.endpoint("lex_inline", { md: Str(512, MarkdownText), f: F }, New(InlineTokens), ({ md, f }) =>
  marked.Lexer.lexInline(md, opts(f)));

stitch.endpoint("lex_static", { md: Str(2048, MarkdownText), f: F }, New(Tokens), ({ md, f }) =>
  marked.Lexer.lex(md, opts(f)));

stitch.endpoint("parser", { t: Borrow(Tokens), f: F }, New(Html), ({ t, f }) => marked.parser(t, opts(f)));

stitch.endpoint("parser_static", { t: Borrow(Tokens), f: F }, New(Html), ({ t, f }) =>
  marked.Parser.parse(t, opts(f)));

// Oracle: parse == parser(lexer)
stitch.endpoint("check_parse_eq_parser_lexer", { md: Str(2048, MarkdownText), f: F }, null, ({ md, f }) => {
  const o = det(f);
  stitch.check(marked.parse(md, o) === marked.parser(marked.lexer(md, o), o), "parse(md) === parser(lexer(md))");
});

// Oracle: lexer output is deterministic and raw fields reassemble the input.
// marked normalizes \r\n and tabs before lexing, so compare against the normalized text.
stitch.endpoint("check_lexer_raw", { md: Str(2048, MarkdownText), f: NF }, null, ({ md, f }) => {
  const toks = marked.lexer(md, det(f));
  const norm = md.replace(/\r\n|\r/g, "\n").replace(/\t/g, "    ");
  // link reference definitions are consumed without a token (stored in toks.links)
  if (Object.keys(toks.links).length === 0)
    stitch.check(rawOf(toks) === norm, "top-level token raws concatenate to the normalized input");
  stitch.check(JSON.stringify(toks) === JSON.stringify(marked.lexer(md, det(f))), "lexer is deterministic");
});

stitch.endpoint("check_inline_raw", { md: Str(512, MarkdownText), f: NF }, null, ({ md, f }) => {
  const toks = marked.Lexer.lexInline(md, det(f));
  stitch.check(rawOf(toks) === md, "inline token raws concatenate to the input");
});

// Oracle: smartypants/headerIds etc. never make output non-string; silent mode never throws.
stitch.endpoint("check_silent", { md: Str(1024, MarkdownText), f: F }, null, ({ md, f }) => {
  const out = marked.parse(md, { ...opts(f), silent: true });
  stitch.check(typeof out === "string", "silent parse returns a string");
});

stitch.endpoint("check_parse_deterministic", { md: Str(1024, MarkdownText), f: F }, null, ({ md, f }) => {
  stitch.check(marked.parse(md, det(f)) === marked.parse(md, det(f)), "parse is deterministic");
});

// walkTokens: visits tokens; collect types and check each token has a type and raw string.
stitch.endpoint("walk_tokens", { t: Borrow(Tokens) }, null, ({ t }) => {
  let n = 0;
  marked.walkTokens(t, (tok) => {
    n++;
    stitch.check(typeof tok.type === "string", "token has a type");
    stitch.check(typeof tok.raw === "string", "token has raw");
    // table cells ({text, tokens}) have no type/raw in v4

  });
  stitch.check(n >= t.length, "walkTokens visits at least the top-level tokens");
});

// walkTokens callback that rewrites text tokens, then render.
stitch.endpoint("walk_rewrite_render", { md: Str(1024, MarkdownText), f: F, tag: OneOf("X", "<b>", "&", "") }, New(Html), ({ md, f, tag }) => {
  const o = opts(f);
  const toks = marked.lexer(md, o);
  marked.walkTokens(toks, (tok) => { if (tok.type === "text" && !tok.tokens) tok.text = tag + tok.text; });
  return marked.parser(toks, o);
});

// Custom renderer
stitch.endpoint("custom_renderer", { md: Str(1024, MarkdownText), f: F, pick: OneOf(0, 1, 2) }, New(Html), ({ md, f, pick }) => {
  const r = new marked.Renderer();
  if (pick === 0) r.heading = (text, level, raw, slugger) => `<h${level} id="${slugger.slug(raw)}">${text}</h${level}>\n`;
  if (pick === 1) { r.link = () => false; r.image = () => false; r.code = () => false; }
  if (pick === 2) { r.paragraph = (t) => t + "\n"; r.html = (h) => h.toUpperCase(); r.text = (t) => t; }
  const out = marked.parse(md, { ...opts(f), renderer: r });
  stitch.check(typeof out === "string", "custom renderer output is a string");
  return out;
});

// Renderer used directly through the Parser on lexer output
stitch.endpoint("parser_with_renderer", { t: Borrow(Tokens), f: F }, New(Html), ({ t, f }) => {
  const r = new marked.Renderer();
  r.strong = (x) => `<b>${x}</b>`;
  return new marked.Parser({ ...opts(f), renderer: r }).parse(t);
});

stitch.endpoint("parser_inline", { t: Borrow(InlineTokens), f: F }, New(Html), ({ t, f }) =>
  new marked.Parser(opts(f)).parseInline(t));

// Slugger and TextRenderer
stitch.endpoint("slugger", { a: Str(64), b: Str(64) }, null, ({ a, b }) => {
  const s = new marked.Slugger();
  const x = s.slug(a), y = s.slug(a);
  stitch.check(typeof x === "string" && x !== y || x === "" , "slug of repeated text is made unique");
  s.slug(b);
});

stitch.endpoint("lex_then_inline_only", { md: Str(512, MarkdownText), f: F }, null, ({ md, f }) => {
  const o = det(f);
  const a = marked.parseInline(md, o);
  const b = marked.Parser.parseInline(marked.Lexer.lexInline(md, o), o);
  stitch.check(a === b, "parseInline == Parser.parseInline(Lexer.lexInline)");
});
