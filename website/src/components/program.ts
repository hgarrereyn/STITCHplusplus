// Programs drawn with endpoint glyphs: each call is a block, each object flows from the call that
// made or last used it into the next call that uses it. Used by the animated graph and the crash.
import { glyph, glyphWidth, wire, BOX_H, type ObjType, type Placed } from "./glyph";
import { ENDPOINTS, type Endpoint } from "./showcase";

/** A call: its endpoint, the object fed to each object input, and the fuzzed values. */
export type Call = { ep: string; uses: string[]; values?: Record<string, string>; crash?: boolean };

const PITCH = 66;
const TOP = 14;

const ep = (id: string) => ENDPOINTS.find((e) => e.id === id)!;

/** Object ids that each call outputs, in the order of its output ports. */
function outputs(calls: Call[]) {
  let fresh = 0;
  return calls.map((c) => {
    const e = ep(c.ep);
    return e.out.map((p) => {
      const i = e.in.findIndex((q) => q.name === p.name && !q.take);
      return i >= 0 ? c.uses[i] : `o${fresh++}`;
    });
  });
}

/** Draws a program; `name` gives each endpoint's name in the reader's language. */
export function renderProgram(calls: Call[], name: (id: string) => string, opts: { values?: boolean; width?: number } = {}) {
  const outs = outputs(calls);
  const widths = calls.map((c) => glyphWidth(ep(c.ep), name(c.ep)));
  const maxW = Math.max(...widths, 120);
  const cx = 18 + maxW / 2;
  let right = cx + maxW / 2;  // the rightmost point of any block or its fuzzed inputs
  const blocks: string[] = [], wires: string[] = [];
  const placed: { ins: Placed[]; outs: Placed[] }[] = [];

  calls.forEach((c, i) => {
    const e = ep(c.ep), w = widths[i];
    const g = glyph(e, name(c.ep), cx - w / 2, TOP + i * PITCH, {
      values: opts.values ? c.values : undefined,
      cls: c.crash ? "crash" : undefined,
    });
    blocks.push(`<g class="call" style="--i:${i}">${g.svg}</g>`);
    right = Math.max(right, cx - w / 2 + g.extent);
    placed.push({ ins: g.ins, outs: g.outs });
  });

  // edges: (from call, out port) -> (to call, in port)
  type Edge = { a: Placed; b: Placed; from: number; to: number; type: ObjType };
  const edges: Edge[] = [];
  const last = new Map<string, { call: number; port: number }>();
  calls.forEach((c, i) => {
    const e = ep(c.ep);
    e.in.forEach((p, k) => {
      const src = last.get(c.uses[k]);
      if (src) edges.push({ a: placed[src.call].outs[src.port], b: placed[i].ins[k], from: src.call, to: i, type: p.type });
    });
    outs[i].forEach((o, k) => last.set(o, { call: i, port: k }));
  });

  // calls that a wire skips: give it a lane on the right, one per overlapping interval
  const lanes: [number, number][][] = [];
  const laneX = (n: number) => right + 14 + n * 11;
  for (const ed of edges) {
    let lane: number | null = null;
    if (ed.to > ed.from + 1) {
      let n = lanes.findIndex((used) => used.every(([s, t]) => ed.to <= s || ed.from >= t));
      if (n < 0) { lanes.push([]); n = lanes.length - 1; }
      lanes[n].push([ed.from, ed.to]);
      lane = laneX(n);
    }
    wires.push(`<path class="wire w-${ed.type === "Store" ? "store" : "snap"}" d="${wire(ed.a, ed.b, lane)}" pathLength="1" style="--i:${ed.to}" />`);
  }
  const width = Math.max(opts.width ?? 0, laneX(lanes.length) + 6);
  const height = TOP + (calls.length - 1) * PITCH + BOX_H + 14;
  return { svg: `<g class="wires">${wires.join("")}</g><g class="blocks">${blocks.join("")}</g>`, width, height };
}

const VALUES: Record<string, string[]> = {
  key: ['"tmp"', '"a"', '"k=v"', '"tmp1"', '""', '"x"'],
  value: ['"1"', '"#!x"', '"hello"', '""', '"a=b"'],
  text: ['"a=1\\nb=2"', '"tmp="', '"#!=#!"', '"k=v"', '""'],
};
const pick = <T,>(xs: T[], r: () => number) => xs[Math.floor(r() * xs.length)];

/** A random valid program: every input is fed an object of its type that is still available. */
export function randomProgram(r: () => number = Math.random): Call[] {
  for (let attempt = 0; attempt < 50; attempt++) {
    const n = 4 + Math.floor(r() * 3);
    const live: Record<ObjType, string[]> = { Store: [], Snapshot: [] };
    const calls: Call[] = [];
    let fresh = 0;
    for (let i = 0; i < n; i++) {
      const can = ENDPOINTS.filter((e) =>
        (i === 0 ? e.in.length === 0 : true) &&
        e.in.every((p) => live[p.type].length > 0) &&
        !(i > 0 && e.in.length === 0 && r() < 0.6));
      const weights = can.map((e) => (e.id === "merge" ? 4 : e.id === "snapshot" ? 3 : e.id === "roundtrip" && i < n - 1 ? 0.5 : 1));
      let t = r() * weights.reduce((a, b) => a + b, 0), e: Endpoint = can[0];
      for (let k = 0; k < can.length; k++) { t -= weights[k]; if (t <= 0) { e = can[k]; break; } }
      const uses = e.in.map((p) => pick(live[p.type], r));
      e.in.forEach((p, k) => { if (p.take) live[p.type] = live[p.type].filter((o) => o !== uses[k]); });
      e.out.forEach((p) => {
        if (!e.in.some((q) => q.name === p.name && !q.take)) live[p.type].push(`o${fresh++}`);
      });
      const values: Record<string, string> = {};
      for (const f of e.fuzz) values[f.name] = `${f.name}=${pick(VALUES[f.name] ?? ['"?"'], r)}`;
      calls.push({ ep: e.id, uses, values });
    }
    // keep programs that show more than one input or output somewhere
    if (calls.some((c) => ["snapshot", "merge"].includes(c.ep))) return calls;
  }
  return [{ ep: "new_store", uses: [] }, { ep: "snapshot", uses: ["o0"] }, { ep: "merge", uses: ["o0", "o1"] }, { ep: "roundtrip", uses: ["o0"] }];
}

/**
 * A program from `stitch exec --trace` output: its calls (the last one crashed) and the crash's
 * kind and innermost frame. `epOf` maps a stub name in the trace to an endpoint id.
 */
export function programFromTrace(trace: string, epOf: (name: string) => string | undefined) {
  const calls: Call[] = [];
  const ids = new Map<string, string>(); // stitch's object ids (o0.0) -> ours (o0, o1, ...)
  let fresh = 0;
  for (const line of trace.split("\n")) {
    const call = line.match(/^\[stitch\] #\d+ (\w+)\((.*)\)\s*$/);
    if (call) {
      const id = epOf(call[1]);
      if (!id) throw new Error(`trace: unknown stub ${call[1]}`);
      const e = ENDPOINTS.find((x) => x.id === id)!;
      const args = new Map<string, string>();
      for (const m of call[2].matchAll(/(\w+)=((?:take )?o\d+\.\d+|(?:Str|Bytes)\[[^\]]*\]"(?:[^"\\]|\\.)*"|[^,]+)/g)) args.set(m[1], m[2]);
      const uses = e.in.map((p) => {
        const ref = (args.get(p.name) ?? "").replace(/^take /, "");
        if (!ids.has(ref)) throw new Error(`trace: ${call[1]}.${p.name} refers to ${ref || "nothing"}`);
        return ids.get(ref)!;
      });
      const values: Record<string, string> = {};
      for (const f of e.fuzz) {
        const v = args.get(f.name);
        if (v !== undefined) {
          const s = v.replace(/^(?:Str|Bytes)\[[^\]]*\]/, "");
          values[f.name] = `${f.name}=${s.length > 14 ? s.slice(0, 12) + '…"' : s}`;
        }
      }
      calls.push({ ep: id, uses, values });
      continue;
    }
    const produced = line.match(/^\[stitch\]\s+-> ok produced (.*)$/);
    if (produced) for (const o of produced[1].split(/[ ,]+/).filter(Boolean)) ids.set(o, `o${fresh++}`);
  }
  if (calls.length) calls[calls.length - 1].crash = true;
  const result = trace.match(/^result: CRASH (.*?) \| (.*?)(?: \||\s+\()/m);
  return { calls, kind: result?.[1] ?? "crash", frame: result?.[2] ?? "" };
}
