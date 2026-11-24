// A program as code: one line per call, in the reader's language, calling the stubs of the home
// page harness. Objects get names by type (st, st2, snap, ...), colored like their ports; fuzzed
// values are orange, like the inputs that come in from the right of a block.
import { ENDPOINTS, nameOf } from "./showcase";
import type { Call } from "./program";
import type { ObjType } from "./glyph";

type Style = {
  /** a call whose result is bound to `v` */
  bind: (v: string, call: string, ep: string) => string;
  /** a call whose result is not used */
  stmt: (call: string, ep: string) => string;
  /** an object argument; `take` when the call uses it up */
  arg?: (v: string, take: boolean) => string;
  str?: (s: string) => string;
  /** no parentheses on a call without arguments (Ruby) */
  bare?: boolean;
};

const kw = (s: string) => `<span class="c-kw">${s}</span>`;
const semi = (s: string) => `${s};`;
// endpoints that can fail without it being a bug, per the harness (Go, Rust and Swift spell it out)
const FAILS = new Set(["load", "put", "snapshot", "merge"]);

const STYLES: Record<string, Style> = {
  python: { bind: (v, c) => `${v} = ${c}`, stmt: (c) => c },
  ruby: { bind: (v, c) => `${v} = ${c}`, stmt: (c) => c, bare: true },
  javascript: { bind: (v, c) => `${kw("const")} ${v} = ${c};`, stmt: semi },
  java: { bind: (v, c) => `${kw("var")} ${v} = ${c};`, stmt: semi },
  cpp: { bind: (v, c) => `${kw("auto")} ${v} = ${c};`, stmt: semi },
  objc: { bind: (v, c) => `${kw("KVStore")} *${v} = ${c};`, stmt: semi, str: (s) => `@${s}` },
  go: {
    bind: (v, c, ep) => (FAILS.has(ep) ? `${v}, _ := ${c}` : `${v} := ${c}`),
    stmt: (c) => c,
  },
  rust: {
    bind: (v, c, ep) => `${kw("let")} ${v} = ${c}${FAILS.has(ep) ? "?" : ""};`,
    stmt: (c, ep) => `${c}${FAILS.has(ep) ? "?" : ""};`,
    arg: (v, take) => (take ? v : `&${v}`),
  },
  swift: {
    bind: (v, c, ep) => `${kw("let")} ${v} = ${FAILS.has(ep) || ep === "roundtrip" ? `${kw("try")} ` : ""}${c}`,
    stmt: (c, ep) => `${FAILS.has(ep) || ep === "roundtrip" ? `${kw("try")} ` : ""}${c}`,
  },
};

const esc = (s: string) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
const BASE: Record<ObjType, string> = { Store: "st", Snapshot: "snap" };
const CLASS: Record<ObjType, string> = { Store: "c-store", Snapshot: "c-snap" };

/** Each call as one line of HTML. */
export function programCode(calls: Call[], lang: string): string[] {
  const style = STYLES[lang] ?? STYLES.python;
  const names = new Map<string, { name: string; type: ObjType }>();
  const count: Record<ObjType, number> = { Store: 0, Snapshot: 0 };
  const fresh = (type: ObjType) => {
    const n = ++count[type];
    return `${BASE[type]}${n > 1 ? n : ""}`;
  };
  const ref = (o: string) => names.get(o)!;
  let next = 0; // object ids follow the order calls make them: o0, o1, ...

  return calls.map((c) => {
    const e = ENDPOINTS.find((x) => x.id === c.ep)!;
    const args = e.in.map((p, k) => {
      const v = ref(c.uses[k]);
      const text = style.arg ? style.arg(v.name, !!p.take) : v.name;
      return `<span class="${CLASS[v.type]}">${esc(text)}</span>`;
    });
    for (const f of e.fuzz) {
      const raw = (c.values?.[f.name] ?? `${f.name}=""`).slice(f.name.length + 1);
      args.push(`<span class="c-str">${esc(style.str ? style.str(raw) : raw)}</span>`);
    }
    const fn = `<span class="c-fn">${esc(nameOf(lang, c.ep))}</span>`;
    const call = args.length || !style.bare ? `${fn}(${args.join(", ")})` : fn;

    // a new object is bound to a name; a borrowed one keeps its name
    const made = e.out.filter((p) => !e.in.some((q) => q.name === p.name && !q.take));
    if (!made.length) return style.stmt(call, c.ep);
    const v = fresh(made[0].type);
    names.set(`o${next++}`, { name: v, type: made[0].type });
    return style.bind(`<span class="${CLASS[made[0].type]}">${v}</span>`, call, c.ep);
  });
}

/** The lines as list items, each at the height of its block: `rows` (plus `offset`) out of `height`. */
export function codeItems(lines: string[], rows: number[], height: number, opts: { offset?: number; crash?: number } = {}) {
  return lines.map((l, i) => {
    const top = (((opts.offset ?? 0) + rows[i]) / height) * 100;
    return `<li data-i="${i}" class="${i === opts.crash ? "crash" : ""}" style="top:${top.toFixed(3)}%;--i:${i}"><span class="ln">${l}</span></li>`;
  }).join("");
}

/** Hovering a block lights its line, and the other way round. */
export function linkRows(root: HTMLElement) {
  const lit = (i: string | null) => {
    root.querySelectorAll(".hot").forEach((e) => e.classList.remove("hot"));
    if (i === null) return;
    root.querySelectorAll(`.prog-code li[data-i="${i}"]:not(.leaving), .program:not(.leaving) .call[data-i="${i}"], .prog-graph > .blocks .call[data-i="${i}"]`)
      .forEach((e) => e.classList.add("hot"));
  };
  root.addEventListener("pointerover", (e) => {
    const t = (e.target as Element).closest<HTMLElement>(".prog-code li, .call");
    lit(t?.dataset.i ?? null);
  });
  root.addEventListener("pointerleave", () => lit(null));
}
