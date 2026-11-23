// The shape of an endpoint: a rounded block with its name, object inputs on top (circles, colored by
// type) and object outputs on the bottom; wires between programs' calls join these. Fuzzed inputs
// are not objects: they come in from the right (orange diamonds), outside the graph. A borrowed
// object comes back out; a taken one (hollow circle) does not. Used for the harness gutter, the
// programs and the crash. Returns SVG markup (no <svg> wrapper) and the port positions.

export type ObjType = "Store" | "Snapshot";
export type Port = { type: ObjType; name: string; take?: boolean };
export type Fuzz = { name: string; decoder?: string };
export type Shape = { in: Port[]; out: Port[]; fuzz: Fuzz[]; check?: boolean };
export type Placed = { x: number; y: number; type?: ObjType };
export type Glyph = { svg: string; w: number; h: number; extent: number; ins: Placed[]; outs: Placed[] };

export const BOX_H = 30;
export const PORT_R = 4.5;
const CHAR_W = 6.9; // the name's font: 11.5px mono
const PORT_GAP = 30;
const TYPE_CLASS: Record<ObjType, string> = { Store: "p-store", Snapshot: "p-snap" };

const esc = (s: string) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");

export function glyphWidth(shape: Shape, name: string) {
  const ports = Math.max(shape.in.length, shape.out.length);
  return Math.max(name.length * CHAR_W + 28 + (shape.check ? 16 : 0), ports * PORT_GAP + 12, 64);
}

const FUZZ_GAP = 12;     // between stacked fuzzed inputs
const LEAD = 9;          // the orange line that brings a fuzzed input in from the right
const SMALL_W = 5.8;     // label and value font

/** How far right of the block the fuzzed inputs' labels reach. */
export function fuzzExtent(shape: Shape, texts: string[]) {
  if (!shape.fuzz.length) return 0;
  return LEAD + 6 + Math.max(...texts.map((t) => t.length)) * SMALL_W;
}

/**
 * A glyph with its top-left corner at (x, y). `labels` puts the port names next to the ports.
 * `values` (fuzzed values, by input name) are drawn to the right of the block.
 */
export function glyph(shape: Shape, name: string, x: number, y: number,
  opts: { labels?: boolean; values?: Record<string, string>; cls?: string; w?: number } = {}): Glyph {
  const w = opts.w ?? glyphWidth(shape, name), h = BOX_H;
  const parts: string[] = [];
  parts.push(`<rect class="g-box" x="${x}" y="${y}" width="${w}" height="${h}" rx="8" />`);
  parts.push(`<text class="g-name" x="${x + (shape.check ? w / 2 - 7 : w / 2)}" y="${y + h / 2 + 4}" text-anchor="middle">${esc(name)}</text>`);
  if (shape.check) {
    const cx = x + w - 15, cy = y + h / 2;
    parts.push(`<circle class="g-check" cx="${cx}" cy="${cy}" r="6.5" /><path class="g-check-mark" d="M${cx - 3} ${cy}l2 2.2 4-4.4" />`);
  }

  // top: object inputs
  const ins: Placed[] = [];
  shape.in.forEach((p, i) => {
    const px = x + (w * (i + 1)) / (shape.in.length + 1), py = y;
    ins.push({ x: px, y: py, type: p.type });
    parts.push(`<circle class="g-port ${TYPE_CLASS[p.type]}${p.take ? " take" : ""}" cx="${px}" cy="${py}" r="${PORT_R}" data-port="in" data-name="${esc(p.name)}" />`);
    if (opts.labels) parts.push(`<text class="g-label ${TYPE_CLASS[p.type]}" x="${px}" y="${py - 8}" text-anchor="middle">${esc(p.name)}</text>`);
  });

  // right: fuzzed inputs, each led in from outside the graph, with its name or its value
  const texts = shape.fuzz.map((f) => opts.values?.[f.name] ?? (opts.labels ? f.name : ""));
  shape.fuzz.forEach((f, i) => {
    const px = x + w, py = y + h / 2 + (i - (shape.fuzz.length - 1) / 2) * FUZZ_GAP, d = 4.6;
    parts.push(`<path class="g-lead" d="M${px + LEAD} ${py}H${px}" />`);
    parts.push(`<path class="g-port p-fuzz${f.decoder ? " decoded" : ""}" d="M${px} ${py - d}L${px + d} ${py}L${px} ${py + d}L${px - d} ${py}Z" data-port="fuzz" data-name="${esc(f.name)}" />`);
    if (texts[i]) parts.push(`<text class="${opts.values ? "g-value" : "g-label p-fuzz"}" x="${px + LEAD + 4}" y="${py + 3.5}">${esc(texts[i])}</text>`);
  });

  // bottom: object outputs
  const outs: Placed[] = [];
  shape.out.forEach((p, i) => {
    const px = x + (w * (i + 1)) / (shape.out.length + 1), py = y + h;
    outs.push({ x: px, y: py, type: p.type });
    parts.push(`<circle class="g-port ${TYPE_CLASS[p.type]}" cx="${px}" cy="${py}" r="${PORT_R}" data-port="out" data-name="${esc(p.name)}" />`);
    if (opts.labels) parts.push(`<text class="g-label ${TYPE_CLASS[p.type]}" x="${px}" y="${py + 15}" text-anchor="middle">${esc(p.name)}</text>`);
  });

  const extent = w + fuzzExtent(shape, texts.filter(Boolean).length ? texts : [""]);
  return { svg: `<g class="glyph${opts.cls ? " " + opts.cls : ""}">${parts.join("")}</g>`, w, h, extent, ins, outs };
}

/** A wire from an output port down to an input port. Skipping calls routes it out to `lane`. */
export function wire(a: Placed, b: Placed, lane: number | null) {
  if (lane === null) {
    const m = (a.y + b.y) / 2;
    return `M${a.x} ${a.y} C${a.x} ${m} ${b.x} ${m} ${b.x} ${b.y}`;
  }
  const r = 10;
  const dir = Math.sign(lane - a.x) || 1, dirB = Math.sign(lane - b.x) || 1;
  return [
    `M${a.x} ${a.y}`,
    `L${a.x} ${a.y + 6}`,
    `Q${a.x} ${a.y + 6 + r} ${a.x + dir * r} ${a.y + 6 + r}`,
    `L${lane - dir * r} ${a.y + 6 + r}`,
    `Q${lane} ${a.y + 6 + r} ${lane} ${a.y + 6 + 2 * r}`,
    `L${lane} ${b.y - 6 - 2 * r}`,
    `Q${lane} ${b.y - 6 - r} ${lane - dirB * r} ${b.y - 6 - r}`,
    `L${b.x + dirB * r} ${b.y - 6 - r}`,
    `Q${b.x} ${b.y - 6 - r} ${b.x} ${b.y - 6}`,
    `L${b.x} ${b.y}`,
  ].join(" ");
}

export const typeClass = (t: ObjType) => TYPE_CLASS[t];
