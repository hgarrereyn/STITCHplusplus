// /llms.txt: the agent guide's overview, with links made absolute.
import type { APIRoute } from "astro";
import { agentGuide } from "../agent-guide";

export const GET: APIRoute = ({ site }) => {
  const base = new URL(`${import.meta.env.BASE_URL.replace(/\/$/, "")}/agents/`, site).href;
  const text = agentGuide().overview.replace(/\]\((\w+)\.md\)/g, `](${base}$1.md)`);
  return new Response(text, { headers: { "Content-Type": "text/plain; charset=utf-8" } });
};
