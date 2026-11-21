// yaml (eemeli/yaml): parse, edit and print YAML documents.
// Oracles: an error-free document prints to text that parses back to the same JS value, and
// stringify() output parses back to its input.
const stitch = require("stitch");
const { Borrow, New, Str, I32, OneOf, Range, Bool, Src } = stitch;
const YAML = require("yaml");
const { isDeepStrictEqual } = require("util");

const Doc = stitch.object("Doc");
const Node = stitch.object("Node");
const YamlText = stitch.datatype("YamlText");

stitch.decoder(YamlText, (s) => {
  const scalar = () => s.pick(["a", "1", "-2.5", "true", "null", "~", "'q'", '"d\\n"', ".nan", "0x1F", "2002-12-14", s.str(8), s.token()]);
  const lines = [];
  const node = (indent, depth) => {
    const pad = " ".repeat(indent);
    const k = depth > 3 ? 0 : s.choice(6);
    if (k === 0) return scalar();
    if (k === 1) return "\n" + Array.from({ length: s.range(1, 3) }, () => `${pad}- ${node(indent + 2, depth + 1)}`).join("\n");
    if (k === 2) return "\n" + Array.from({ length: s.range(1, 3) }, () => `${pad}${s.pick(["k", "a b", "? x", "&a k", "<<"])}: ${node(indent + 2, depth + 1)}`).join("\n");
    if (k === 3) return `[${Array.from({ length: s.range(0, 3) }, scalar).join(", ")}]`;
    if (k === 4) return `{${Array.from({ length: s.range(0, 2) }, () => `${scalar()}: ${scalar()}`).join(", ")}}`;
    return s.pick(["|\n" + pad + "  text", ">-\n" + pad + "  fold", "*a", "&a " + scalar(), "!!str " + scalar()]);
  };
  while (s.more()) {
    const c = s.choice(5);
    if (c === 0) lines.push(s.token());
    else if (c === 1) lines.push("---");
    else lines.push(`${s.pick(["a", "b", "key", "- x", "? k"])}: ${node(2, 0)}`);
  }
  return lines.join("\n") + "\n";
});

const VERSIONS = ["1.1", "1.2"];
const opts = (version, merge) => ({ version, merge, prettyErrors: false, strict: false, uniqueKeys: false });

function clean(doc) {
  return doc.errors.length === 0 && doc.warnings.length === 0;
}

// errors the API documents for invalid requests
const API_ERROR = /Expected a valid index|Expected YAML collection|Expected a YAML collection|is not a collection|Merge sources must be|cannot be stringified|Unresolved alias|cannot have tags|cannot add|Alias node must be|Excessive alias count/i;
const apiError = (e) => e instanceof Error && API_ERROR.test(e.message);

stitch.endpoint("parse_document", { text: Str(2048, YamlText), version: OneOf(VERSIONS), merge: Bool },
  New(Doc), ({ text, version, merge }) => {
    const doc = YAML.parseDocument(text, opts(version, merge));
    stitch.set(doc, "text", text);
    return doc;
  });

stitch.endpoint("parse_all", { text: Str(2048, YamlText) }, null, ({ text }) => {
  const docs = YAML.parseAllDocuments(text);
  if (Array.isArray(docs)) for (const d of docs) if (clean(d)) d.toString();
}, { bailsOn: [YAML.YAMLParseError, apiError] });

stitch.endpoint("parse_value", { text: Str(1024, YamlText), version: OneOf(VERSIONS) }, null, ({ text, version }) => {
  YAML.parse(text, { version, maxAliasCount: 100 });
}, { bailsOn: [YAML.YAMLParseError, YAML.YAMLWarning, apiError, (e) => e instanceof ReferenceError && /alias/i.test(e.message)] });

stitch.endpoint("new_document", { kind: OneOf("map", "seq", "scalar") }, New(Doc), ({ kind }) => {
  return new YAML.Document(kind === "map" ? { a: 1 } : kind === "seq" ? [1, "x"] : "s");
});

stitch.endpoint("print_roundtrip", { doc: Borrow(Doc) }, null, ({ doc }) => {
  stitch.require(clean(doc), "document has errors");
  let value;
  try {
    value = doc.toJS({ maxAliasCount: 100 });
  } catch (e) {
    stitch.bail("toJS failed");
  }
  let text;
  try {
    text = doc.toString();
  } catch (e) {
    stitch.bail("document cannot be printed");
  }
  let again;
  try {
    again = YAML.parseDocument(text, opts(doc.options.version, doc.options.merge));
  } catch (e) {
    stitch.check(false, "printed document parses");
  }
  stitch.log("printed:", JSON.stringify(text));
  if (again.errors.length) stitch.log("error:", again.errors[0].message);
  stitch.check(again.errors.length === 0, "printed document has no errors");
  stitch.check(isDeepStrictEqual(again.toJS({ maxAliasCount: 100 }), value), "printed document has the same value");
});

const PATH_KEYS = ["a", "b", "key", 0, 1, "x y"];
const pathOf = (s) => Array.from({ length: s.range(1, 3) }, () => s.pick(PATH_KEYS));

stitch.endpoint("set_in", { doc: Borrow(Doc), s: Src(16), value: OneOf(1, "str", null, true, 2.5, [], {}, "multi\nline", "#", "- x") },
  null, ({ doc, s, value }) => {
    doc.setIn(pathOf(s), value);
  }, { bailsOn: [apiError] });

stitch.endpoint("delete_in", { doc: Borrow(Doc), s: Src(16) }, null, ({ doc, s }) => {
  doc.deleteIn(pathOf(s));
}, { bailsOn: [apiError] });

stitch.endpoint("get_in", { doc: Borrow(Doc), s: Src(16), keep: Bool }, New(Node), ({ doc, s, keep }) => {
  const n = doc.getIn(pathOf(s), keep);
  stitch.require(n && typeof n === "object" && YAML.isNode(n), "not a node");
  return n;
});

stitch.endpoint("node_ops", { doc: Borrow(Doc), node: Borrow(Node), op: OneOf("comment", "anchor", "flow", "range", "clone") },
  null, ({ doc, node, op }) => {
    if (op === "comment") node.commentBefore = "c";
    else if (op === "anchor") node.anchor = "a1";
    else if (op === "flow") node.flow = true;
    else if (op === "range") node.range = null;
    else doc.contents = node.clone ? node.clone() : node;
  }, { bailsOn: [apiError] });

stitch.endpoint("create_alias", { doc: Borrow(Doc), node: Borrow(Node) }, null, ({ doc, node }) => {
  const alias = doc.createAlias(node, "x");
  doc.add(alias);
}, { bailsOn: [apiError] });

stitch.endpoint("visit", { doc: Borrow(Doc), action: OneOf("none", "remove", "skip", "break", "replace") }, null, ({ doc, action }) => {
  YAML.visit(doc, {
    Scalar(key, node) {
      if (action === "remove") return YAML.visit.REMOVE;
      if (action === "skip") return YAML.visit.SKIP;
      if (action === "break") return YAML.visit.BREAK;
      if (action === "replace" && !node.stitchReplaced) {
        const n = doc.createNode(String(node.value) + "!");
        n.stitchReplaced = true; // the visitor visits replacements too
        return n;
      }
      return undefined;
    },
  });
});

// lineWidth: 0 (no folding) or at least the documented minimum content width (20)
stitch.endpoint("stringify_roundtrip", { s: Src(64), indent: Range(1, 8), lineWidth: OneOf(0, 20, 21, 40, 80) }, null, ({ s, indent, lineWidth }) => {
  const value = (d) => {
    const k = d > 2 ? s.choice(4) : s.choice(6);
    if (k === 0) return s.i32();
    if (k === 1) return s.str(10);
    if (k === 2) return s.pick([true, false, null, 1.5, -0, "", "null", "yes", "~", "0x10", "#", "- a", ": b"]);
    if (k === 3) return s.pick(["", " lead", "trail ", "a\nb", "\t", "'", '"']);
    if (k === 4) return Array.from({ length: s.range(0, 3) }, () => value(d + 1));
    const o = {};
    for (let i = s.range(0, 3); i > 0; i--) o[s.pick(["a", "b", "", " k", "1", "null"])] = value(d + 1);
    return o;
  };
  const v = value(0);
  const text = YAML.stringify(v, { indent, lineWidth });
  stitch.log("value:", JSON.stringify(v), "\n  text:", JSON.stringify(text));
  const again = YAML.parse(text);
  stitch.log("parsed:", JSON.stringify(again));
  stitch.check(isDeepStrictEqual(again, JSON.parse(JSON.stringify(v))) || isDeepStrictEqual(again, v), "stringify/parse roundtrip");
});
