// The agent guide: the Markdown files in skill/stitch/reference/, the same files the stitch skill
// and `stitch guide` use. Each is served raw at /agents/<name>.raw.md (src/pages/agents/) and
// rendered at /agents/<name>.md/ (scripts/sync-reference.mjs).
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

const REFERENCE = join(process.cwd(), "..", "skill", "stitch", "reference");

/** name -> raw Markdown, with links between files pointing at the other raw files. */
export function agentGuide(): Record<string, string> {
  const files: Record<string, string> = {};
  for (const f of readdirSync(REFERENCE)) {
    if (f.endsWith(".md")) {
      files[f.slice(0, -3)] = readFileSync(join(REFERENCE, f), "utf8").replace(/\]\((\w+)\.md\)/g, "]($1.raw.md)");
    }
  }
  return files;
}
