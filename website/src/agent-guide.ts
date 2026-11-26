// The agent guide: plain Markdown files served at /agents/<name>.md.
// overview.md and the topic files live in website/agents/; the language files, reproducers.md and
// stitch-toml.md and cli.md come from skill/stitch/reference/ and docs/cli.md, so they have one source.
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

const ROOT = join(process.cwd(), "..");
const REPO = "https://github.com/hgarrereyn/STITCHplusplus/blob/main";
const LANGS = ["cpp", "objc", "python", "javascript", "java", "go", "rust", "ruby", "swift"];

export function agentGuide(): Record<string, string> {
  const files: Record<string, string> = {};
  for (const f of readdirSync(join(ROOT, "website/agents"))) {
    if (f.endsWith(".md")) files[f.slice(0, -3)] = readFileSync(join(ROOT, "website/agents", f), "utf8");
  }
  for (const name of [...LANGS, "reproducers", "stitch-toml"]) {
    files[name] = readFileSync(join(ROOT, "skill/stitch/reference", `${name}.md`), "utf8");
  }
  files.cli = readFileSync(join(ROOT, "docs/cli.md"), "utf8")
    .replace("](../skill/stitch/reference/reproducers.md)", "](reproducers.md)")
    .replace(/\]\(\.\.\/([^)]+)\)/g, `](${REPO}/$1)`);
  return files;
}
