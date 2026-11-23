let marked; try { marked = require("marked"); } catch (e) { marked = require("/opt/node_modules/marked/lib/marked.cjs"); }
// lexer throws TypeError on a table followed by a line holding only a vertical tab
console.log("RESULT", marked.lexer("| a |\n|---|\n\x0b").length);
