import type { APIRoute, GetStaticPaths } from "astro";
import { agentGuide } from "../../agent-guide";

export const getStaticPaths: GetStaticPaths = () =>
  Object.entries(agentGuide()).map(([name, text]) => ({ params: { name }, props: { text } }));

export const GET: APIRoute = ({ props }) =>
  new Response(props.text as string, { headers: { "Content-Type": "text/markdown; charset=utf-8" } });
