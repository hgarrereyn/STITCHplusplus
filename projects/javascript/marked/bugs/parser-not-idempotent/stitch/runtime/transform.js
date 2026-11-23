"use strict";
// Babel plugin: coverage counters and comparison hooks for the code under test (the Jazzer.js
// approach). Used by the CommonJS hook (instrument.js) and the ESM loader (loader.mjs).
//
//   block entries, branch arms, loop bodies, catch blocks, `&&`/`||`/`??` right sides,
//   `?:` arms                              ->  __stitch_cov[ID]++
//   a === b, a == b, a !== b, a != b, <, <=, >, >=  ->  __stitch_cmp(a, b, OP)
//   switch (x) { case "a": ... }           ->  switch (__stitch_sw(x, ["a", ...]))
// IDs hash (file, source offset, site), so they are stable across processes.
const babel = require("@babel/core");

const MAP_SIZE = 1 << 16; // = instrument.js MAP_SIZE (this module also runs in the ESM loader thread)
const CMP_OPS = { "===": 0, "!==": 1, "==": 2, "!=": 3, "<": 4, "<=": 5, ">": 6, ">=": 7 };

function hash(s) {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619) >>> 0;
  return h;
}

function plugin({ types: t }) {
  const DONE = Symbol("stitch");
  let file = "";
  const id = (node, tag) => hash(`${file}:${node.start ?? 0}:${tag}`) & (MAP_SIZE - 1);
  const incExpr = (n) => t.updateExpression("++", t.memberExpression(t.identifier("__stitch_cov"), t.numericLiteral(n), true));
  const incStmt = (n) => t.expressionStatement(incExpr(n));
  const seq = (n, e) => t.sequenceExpression([incExpr(n), e]);

  function prepend(path, key, tag) {
    const s = path.get(key);
    if (!s.node) return;
    if (!s.isBlockStatement()) s.replaceWith(t.blockStatement([s.node]));
    path.get(key).unshiftContainer("body", incStmt(id(path.node, tag)));
  }
  const skipCmp = (n) => (t.isUnaryExpression(n) && n.operator === "typeof") || t.isNullLiteral(n)
    || (t.isIdentifier(n) && n.name === "undefined");

  return {
    visitor: {
      Program(path, state) {
        file = state.filename || "";
      },
      Function(path) {
        if (path.node[DONE]) return;
        path.node[DONE] = true;
        if (path.isArrowFunctionExpression() && !t.isBlockStatement(path.node.body)) {
          path.get("body").replaceWith(seq(id(path.node, "fn"), path.node.body));
        } else if (t.isBlockStatement(path.node.body)) {
          path.get("body").unshiftContainer("body", incStmt(id(path.node, "fn")));
        }
      },
      IfStatement(path) {
        if (path.node[DONE]) return;
        path.node[DONE] = true;
        prepend(path, "consequent", "then");
        if (path.node.alternate) prepend(path, "alternate", "else");
        else path.node.alternate = t.blockStatement([incStmt(id(path.node, "else"))]);
      },
      Loop(path) {
        if (path.node[DONE]) return;
        path.node[DONE] = true;
        prepend(path, "body", "loop");
      },
      SwitchCase(path) {
        if (path.node[DONE]) return;
        path.node[DONE] = true;
        path.node.consequent.unshift(incStmt(id(path.node, "case")));
      },
      CatchClause(path) {
        if (path.node[DONE]) return;
        path.node[DONE] = true;
        path.get("body").unshiftContainer("body", incStmt(id(path.node, "catch")));
      },
      ConditionalExpression(path) {
        if (path.node[DONE]) return;
        path.node[DONE] = true;
        path.node.consequent = seq(id(path.node, "c1"), path.node.consequent);
        path.node.alternate = seq(id(path.node, "c2"), path.node.alternate);
      },
      LogicalExpression(path) {
        if (path.node[DONE]) return;
        path.node[DONE] = true;
        path.node.right = seq(id(path.node, "rhs"), path.node.right);
      },
      BinaryExpression(path) {
        const n = path.node;
        if (n[DONE] || !(n.operator in CMP_OPS) || t.isPrivateName(n.left) || skipCmp(n.left) || skipCmp(n.right)) return;
        n[DONE] = true;
        const call = t.callExpression(t.identifier("__stitch_cmp"), [n.left, n.right, t.numericLiteral(CMP_OPS[n.operator])]);
        call[DONE] = true;
        path.replaceWith(call);
      },
      SwitchStatement(path) {
        const n = path.node;
        if (n[DONE]) return;
        n[DONE] = true;
        const lits = n.cases.map((c) => c.test).filter((x) => t.isStringLiteral(x) || t.isNumericLiteral(x));
        if (lits.length) {
          n.discriminant = t.callExpression(t.identifier("__stitch_sw"), [n.discriminant, t.arrayExpression(lits.map((x) => t.cloneNode(x)))]);
        }
      },
    },
  };
}

// Instrumented files are cached by content: a worker restarted after a timeout skips Babel.
const CACHE = require("path").join(require("os").tmpdir(), "stitch-babel-cache");
const VERSION = "1";

function transform(src, filename) {
  const fs = require("fs");
  const key = require("crypto").createHash("sha1").update(VERSION).update(filename).update("\0").update(src).digest("hex");
  const cached = require("path").join(CACHE, key + ".js");
  try {
    return fs.readFileSync(cached, "utf8");
  } catch (e) { /* not cached */ }
  const out = transformUncached(src, filename);
  try {
    fs.mkdirSync(CACHE, { recursive: true });
    const tmp = `${cached}.${process.pid}`;
    fs.writeFileSync(tmp, out);
    fs.renameSync(tmp, cached);
  } catch (e) { /* cache is optional */ }
  return out;
}

function transformUncached(src, filename) {
  try {
    const r = babel.transformSync(src, {
      filename,
      plugins: [plugin],
      babelrc: false,
      configFile: false,
      sourceType: "unambiguous",
      retainLines: true,
      compact: false,
      comments: true,
      parserOpts: { allowReturnOutsideFunction: true, allowHashBang: true, errorRecovery: true },
    });
    return r && r.code ? r.code : src;
  } catch (e) {
    if (process.env.STITCH_DEBUG) process.stderr.write(`[stitch] not instrumented: ${filename}: ${e.message}\n`);
    return src;
  }
}

module.exports = { transform, MAP_SIZE };
