// /llms.txt: the agent guide as one markdown file, for agents that read plain text.
import type { APIRoute } from "astro";
import { getCollection } from "astro:content";

const ORDER = ["agents", "agents/workflow", "agents/stubs", "agents/probe", "agents/triage"];

export const GET: APIRoute = async ({ site }) => {
  const docs = await getCollection("docs");
  const base = new URL(import.meta.env.BASE_URL, site).href.replace(/\/$/, "");
  const pages = ORDER.map((id) => {
    const entry = docs.find((d) => d.id === id);
    if (!entry) throw new Error(`llms.txt: missing page ${id}`);
    // site-absolute links become full URLs
    const body = (entry.body ?? "").replaceAll("](/STITCHplusplus/", `](${base}/`);
    return `# ${entry.data.title}\n\n${body.trim()}\n`;
  });
  const languages = docs
    .filter((d) => d.id.startsWith("reference/languages/"))
    .map((d) => `- [${d.data.title}](${base}/${d.id}/)`)
    .join("\n");
  const text = [
    "# STITCH++ agent guide",
    "",
    "> API fuzzing for libraries in C/C++, Objective-C, Python, JavaScript, Java, Go, Rust, Ruby and Swift.",
    "",
    pages.join("\n"),
    "# Language references",
    "",
    "Read your language's page before writing stubs (or run `stitch guide <lang>`).",
    "",
    languages,
    "",
  ].join("\n");
  return new Response(text, { headers: { "Content-Type": "text/plain; charset=utf-8" } });
};
