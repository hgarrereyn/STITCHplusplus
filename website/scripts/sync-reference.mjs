// Copies the agent guide in skill/stitch/reference/ into the site, so each page has one source:
// - every file as a rendered page at /agents/<name>.md/ (the raw file is at /agents/<name>.raw.md,
//   src/pages/agents/[name].raw.md.ts)
// - the language files and reproducers.md under Reference > Languages, and cli.md as Reference > CLI.
// Runs before `astro dev` and `astro build`.
import { mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const ref = join(root, "skill", "stitch", "reference");
const out = join(root, "website", "src", "content", "docs", "reference", "languages");
const agentsOut = join(root, "website", "src", "content", "docs", "agents");
const repo = "https://github.com/hgarrereyn/STITCHplusplus/blob/main";

// sidebar order and titles
const PAGES = [
  ["cpp", "C/C++"],
  ["python", "Python"],
  ["javascript", "JavaScript"],
  ["java", "Java"],
  ["go", "Go"],
  ["rust", "Rust"],
  ["ruby", "Ruby"],
  ["swift", "Swift"],
  ["objc", "Objective-C"],
  ["reproducers", "Reproducers"],
];

function page(markdown, title, order, description, source, extra = []) {
  const body = markdown.replace(/^# .*\n+/, ""); // the title comes from frontmatter
  const front = [
    "---",
    `title: ${JSON.stringify(title)}`,
    `description: ${JSON.stringify(description)}`,
    `sidebar:\n  order: ${order}`,
    `editUrl: ${JSON.stringify(`${repo.replace("/blob/", "/edit/")}/${source}`)}`,
    ...extra,
    "---",
    "",
    `<!-- Generated from ${source} by website/scripts/sync-reference.mjs. Edit that file. -->`,
    "",
  ].join("\n");
  return front + body;
}

mkdirSync(out, { recursive: true });
const available = new Set(readdirSync(ref));
PAGES.forEach(([name, title], i) => {
  const source = `skill/stitch/reference/${name}.md`;
  if (!available.has(`${name}.md`)) throw new Error(`missing ${source}`);
  const md = readFileSync(join(root, source), "utf8");
  const description = name === "reproducers"
    ? "Turn a crash into a short standalone file."
    : `The ${title} harness API: project setup, objects, parameters, data types and reproducers.`;
  writeFileSync(join(out, `${name}.md`), page(md, title, i + 1, description, source));
});

// CLI reference: links to other files of the guide go to the reference pages.
const cli = readFileSync(join(ref, "cli.md"), "utf8")
  .replace("](reproducers.md)", "](/reference/languages/reproducers/)");
writeFileSync(
  join(root, "website/src/content/docs/reference/cli.md"),
  page(cli, "CLI", 1, "Every stitch command.", "skill/stitch/reference/cli.md"),
);

// stitch.toml reference
writeFileSync(
  join(root, "website/src/content/docs/reference/stitch-toml.md"),
  page(readFileSync(join(ref, "stitch-toml.md"), "utf8"), "stitch.toml", 2,
       "Every key of stitch.toml, and how a project pins its STITCH++ version.", "skill/stitch/reference/stitch-toml.md"),
);

// The agent guide, rendered: every file of skill/stitch/reference/ at /agents/<name>.md/, with links
// between files kept on the rendered pages.
rmSync(agentsOut, { recursive: true, force: true });
mkdirSync(agentsOut, { recursive: true });
const guide = [...available].filter((f) => f.endsWith(".md")).map((f) => f.slice(0, -3));
for (const name of guide) {
  const source = `skill/stitch/reference/${name}.md`;
  const md = readFileSync(join(root, source), "utf8").replace(/\]\((\w+)\.md\)/g, (m, target) => {
    if (!guide.includes(target)) throw new Error(`${source}: link to missing ${target}.md`);
    return `](/agents/${target}.md/)`;
  });
  const title = md.match(/^# (.*)/)?.[1].replace(/`/g, "") ?? name;
  writeFileSync(
    join(agentsOut, `${name}.md`),
    page(md, title, 0, `${name}.md from the STITCH++ agent guide, as agents read it.`, source, [
      `slug: agents/${name}.md`,
      `raw: /agents/${name}.raw.md`,
    ]),
  );
}

console.log(`synced ${PAGES.length} language pages, the CLI and stitch.toml references, and ${guide.length} agent guide pages`);
