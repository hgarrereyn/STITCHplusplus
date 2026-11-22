// ESM loader hooks (registered by instrument.js): instrument target ES modules.
import { createRequire } from "node:module";
import { fileURLToPath, pathToFileURL } from "node:url";

const require = createRequire(import.meta.url);
const { transform } = require("./transform.js");
const { wanted } = require("./instrument.js");
let targets = null;

export async function initialize(data) {
  targets = data;
}

// ES modules do not search NODE_PATH: let `import x from "stitch"` and `import ... from "<library>"`
// (installed in /opt/node_modules) resolve from harness .mjs files the way require() does.
const NODE_PATH = (process.env.NODE_PATH || "").split(":").filter(Boolean);

export async function resolve(specifier, context, nextResolve) {
  try {
    return await nextResolve(specifier, context);
  } catch (e) {
    const bare = !/^(\.|\/|[a-zA-Z][a-zA-Z0-9+.-]*:)/.test(specifier);
    if (!bare || e?.code !== "ERR_MODULE_NOT_FOUND") throw e;
    for (const dir of NODE_PATH) {
      try {
        return await nextResolve(specifier, { ...context, parentURL: pathToFileURL(dir + "/").href });
      } catch {}
    }
    throw e;
  }
}

export async function load(url, context, nextLoad) {
  const r = await nextLoad(url, context);
  if (targets && r.format === "module" && url.startsWith("file://") && r.source != null) {
    const file = fileURLToPath(url);
    if (wanted(file, targets)) return { ...r, source: transform(String(r.source), file) };
  }
  return r;
}
