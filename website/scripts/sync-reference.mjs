// Copies the language pages from skill/stitch/reference/ and the CLI reference from docs/cli.md
// into the site, so each page has one source. Runs before `astro dev` and `astro build`.
import { mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const out = join(root, "website", "src", "content", "docs", "reference", "languages");
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

function page(markdown, title, order, description, source) {
  const body = markdown.replace(/^# .*\n+/, ""); // the title comes from frontmatter
  const front = [
    "---",
    `title: ${JSON.stringify(title)}`,
    `description: ${JSON.stringify(description)}`,
    `sidebar:\n  order: ${order}`,
    `editUrl: ${JSON.stringify(`${repo.replace("/blob/", "/edit/")}/${source}`)}`,
    "---",
    "",
    `<!-- Generated from ${source} by website/scripts/sync-reference.mjs. Edit that file. -->`,
    "",
  ].join("\n");
  return front + body;
}

mkdirSync(out, { recursive: true });
const available = new Set(readdirSync(join(root, "skill/stitch/reference")));
PAGES.forEach(([name, title], i) => {
  const source = `skill/stitch/reference/${name}.md`;
  if (!available.has(`${name}.md`)) throw new Error(`missing ${source}`);
  const md = readFileSync(join(root, source), "utf8");
  const description = name === "reproducers"
    ? "Turn a crash into a short standalone file."
    : `The ${title} harness API: project setup, objects, parameters, data types and reproducers.`;
  writeFileSync(join(out, `${name}.md`), page(md, title, i + 1, description, source));
});

// CLI reference: rewrite links that point into the repository.
let cli = readFileSync(join(root, "docs/cli.md"), "utf8")
  .replace("](../skill/stitch/reference/reproducers.md)", "](/STITCHplusplus/reference/languages/reproducers/)")
  .replace(/\]\(\.\.\/([^)]+)\)/g, `](${repo}/$1)`)
  .replace(/`(\.github\/workflows\/[^`]+)`/g, `[\`$1\`](${repo}/$1)`);
writeFileSync(
  join(root, "website/src/content/docs/reference/cli.md"),
  page(cli, "CLI", 1, "Every stitch command.", "docs/cli.md"),
);

console.log(`synced ${PAGES.length} language pages and the CLI reference`);
