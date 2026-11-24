// The readable parts of a crash's `stitch exec --trace` output: the error message and the stacks
// (where it crashed and, for memory errors, where the memory was freed and allocated). Each frame
// is marked as library code, the spec's stub, or other (the runtime, system libraries, generated
// code), so the page can show the first two and fold the rest away.

export type Frame = { fn: string; file: string; path: string; where: "library" | "spec" | "other" };
export type Stack = { title: string; frames: Frame[] };

function frame(line: string): Frame | null {
  const m = line.match(/^\s+#\d+ 0x[0-9a-f]+\s+(?:in (.*?))?\s*(\(?\/\S*?\)?)?(?: \(BuildId: \w+\))?$/);
  if (!m) return null;
  const fn = (m[1] ?? "").trim() || "?";
  const raw = (m[2] ?? "").replace(/^\(|\)$/g, "");
  const path = raw.replace(/\+0x[0-9a-f]+$/, "");
  // file:line (no column), without directories
  const file = path.replace(/^.*\//, "").replace(/^(.+?:\d+):\d+$/, "$1");
  const binary = m[2]?.startsWith("(") || path.startsWith("/<");
  const base = path.replace(/^.*\//, "");
  const where = binary ? "other"
    : /^stubs?\.\w+(:|$)/i.test(base) ? "spec"
    : /^\/src\/|site-packages|\/gems\/|^\/demo\//.test(path) ? "library"
    : "other";
  return { fn, file, path, where };
}

/** `kind` is the crash kind from the result line, used when the output has no exception message. */
export function parseTrace(trace: string, kind: string) {
  const lines = trace.split("\n");
  let message = kind;
  const stacks: Stack[] = [];
  let cur: Stack | null = null;
  let exception = false;
  for (const line of lines) {
    const ex = line.match(/^\[stitch\] EXCEPTION in \S+: (.*)$/);
    if (ex) {
      // some runtimes repeat the message after the kind ("A: A")
      const half = ex[1].length / 2;
      message = ex[1].length % 2 === 0 && ex[1].slice(0, half - 1) === ex[1].slice(half + 1) ? ex[1].slice(half + 1) : ex[1];
      cur = { title: "Where it crashed", frames: [] };
      stacks.push(cur);
      exception = true;
      continue;
    }
    if (exception && /AddressSanitizer|^=+$/.test(line)) break; // the runtime's own abort after the exception
    const asan = line.match(/==\d+==ERROR: AddressSanitizer: ([\w-]+)/);
    if (asan) {
      // a memory error names the access on the next line ("READ of size 8 at ...")
      const access = lines[lines.indexOf(line) + 1]?.match(/^((?:READ|WRITE) of size \d+)/);
      if (access) message = `${asan[1]}: ${access[1]}`;
      cur = { title: "Where it crashed", frames: [] };
      stacks.push(cur);
      continue;
    }
    const sec = line.match(/^(freed|previously allocated) by thread \w+ here:/);
    if (sec) {
      cur = { title: sec[1] === "freed" ? "Where the memory was freed" : "Where it was allocated", frames: [] };
      stacks.push(cur);
      continue;
    }
    if (/^SUMMARY:/.test(line)) cur = null;
    const f = cur && frame(line);
    if (f && cur) cur.frames.push(f);
  }
  return { message, stacks: stacks.filter((s) => s.frames.length) };
}
