// ESM loader hooks (registered by instrument.js): instrument target ES modules.
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const { transform } = require("./transform.js");
const { wanted } = require("./instrument.js");
let targets = null;

export async function initialize(data) {
  targets = data;
}

export async function load(url, context, nextLoad) {
  const r = await nextLoad(url, context);
  if (targets && r.format === "module" && url.startsWith("file://") && r.source != null) {
    const file = fileURLToPath(url);
    if (wanted(file, targets)) return { ...r, source: transform(String(r.source), file) };
  }
  return r;
}
