#!/usr/bin/env node
"use strict";
// Entry points: the engine's worker, and the reproducer CLI.
//
//   node main.js [--trace] [--plan FILE] [--manifest] FILE.stp...      run programs (reproducer)
//   node main.js --each [--timeout S] [--files-from LIST] FILE.stp...   run all, keep going (stitch cov)
//   node main.js worker                                                 engine worker (fds 3 and 4)
//
// The harness is every .js / .cjs / .mjs file under <project>/harness (except harness/data).
const fs = require("fs");
const path = require("path");
const instrument = require("./instrument");
const { REG, RT } = require("./runtime");
const { Source } = require("./source");

async function loadHarness(project, instr) {
  project = path.resolve(project);
  process.env.STITCH_PROJECT = process.env.STITCH_PROJECT || project;
  if (instr) instrument.install(project);
  else instrument.registerLoader(null);  // module resolution for .mjs harness files only
  const hdir = path.join(project, "harness");
  const files = [];
  const walk = (d) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) {
        if (e.name !== "data" && e.name !== "node_modules") walk(p);
      } else if (/\.(c|m)?js$/.test(e.name)) files.push(p);
    }
  };
  if (fs.existsSync(hdir)) walk(hdir);
  if (!files.length) throw new Error(`no harness files (${hdir}/**/*.js)`);
  for (const f of files) {
    if (f.endsWith(".mjs")) await import(require("url").pathToFileURL(f).href);
    else require(f);
  }
  if (!REG.endpoints.length) throw new Error("the harness defines no stubs (stitch.endpoint)");
  for (const fn of REG.inits) await fn();
  RT.snapshotBuiltins();
}

async function runProgram(data, trace = false, cmplog = false) {
  instrument.setCmplog(cmplog);
  return RT.run(data, trace);
}

// ---------------------------------------------------------------- worker
function readExact(fd, n) {
  const b = Buffer.alloc(n);
  let off = 0;
  while (off < n) {
    let k;
    try {
      k = fs.readSync(fd, b, off, n - off, null);
    } catch (e) {
      if (e.code === "EAGAIN") continue;
      throw e;
    }
    if (k === 0) throw new Error("EOF");
    off += k;
  }
  return b;
}

function send(tag, payload = Buffer.alloc(0)) {
  const h = Buffer.alloc(5);
  h.write(tag, 0, "latin1");
  h.writeUInt32LE(payload.length, 1);
  const m = Buffer.concat([h, payload]);
  let off = 0;
  while (off < m.length) off += fs.writeSync(4, m, off);
}

async function worker(project) {
  await loadHarness(project, process.env.STITCH_FUZZING === "1");
  const hello = Buffer.alloc(4);
  hello.writeUInt32LE(instrument.MAP_SIZE, 0);
  send("H", Buffer.concat([hello, Buffer.from(REG.manifest())]));
  const dts = REG.dtList();
  for (;;) {
    let h, p;
    try {
      h = readExact(3, 5);
      p = readExact(3, h.readUInt32LE(1));
    } catch (e) {
      return;
    }
    const tag = String.fromCharCode(h[0]);
    if (tag === "X") {
      const flags = p.readUInt32LE(0);
      const r = await runProgram(p.subarray(4), !!(flags & 1), !!(flags & 2));
      send(r === "finding" ? "C" : "O", RT.resultsBytes());
    } else if (tag === "D") {
      const dt = dts[p.readUInt32LE(0)];
      const cap = p.readUInt32LE(4);
      const ch = p.subarray(8);
      send("R", dt && dt.decoder ? RT.runDecoder(dt, new Source(ch, { cap: Math.max(ch.length, cap) }), cap) : ch.subarray(0, cap));
    } else if (tag === "G") {
      const di = p.readUInt32LE(0);
      const dt = dts[di];
      const seed = p.readBigUInt64LE(4);
      const cap = p.readUInt32LE(12);
      let out = Buffer.alloc(0);
      if (dt && dt.decoder) {
        const src = new Source(p.subarray(16, 16 + cap), { cap, gen: true, seed, dict: REG.dicts.get(di) || [] });
        RT.runDecoder(dt, src);
        out = src.buffer().subarray(0, cap);
      }
      send("R", out);
    } else if (tag === "S") {
      const dt = dts[p.readUInt32LE(0)];
      const spans = [];
      if (dt && dt.decoder) RT.runDecoder(dt, new Source(p.subarray(4), { cap: p.length - 4, spans }));
      const b = Buffer.alloc(4 * spans.length);
      spans.forEach((v, i) => b.writeUInt32LE(v, 4 * i));
      send("R", b);
    } else if (tag === "I") {
      const dt = dts[p.readUInt32LE(0)];
      const items = [];
      if (dt && dt.decoder) {
        const src = new Source(p.subarray(4), { cap: p.length - 4, items });
        RT.runDecoder(dt, src);
        src._finishItems();
      }
      const b = Buffer.alloc(4 * items.length);
      items.forEach((v, i) => b.writeUInt32LE(v >>> 0, 4 * i));
      send("R", b);
    } else if (tag === "T") {
      const di = p.readUInt32LE(0);
      const toks = REG.dicts.get(di) || [];
      const t = Buffer.from(p.subarray(4));
      if (toks.length < 4096 && !toks.some((x) => x.equals(t))) toks.push(t);
      REG.dicts.set(di, toks);
    } else if (tag === "P") {
      if (RT.plan !== null) fs.closeSync(RT.plan);
      const f = p.toString();
      RT.plan = f ? fs.openSync(f, "w") : null;
    } else if (tag === "Q") {
      if (RT.plan !== null) fs.closeSync(RT.plan);
      return;
    }
  }
}

// ---------------------------------------------------------------- reproducer
async function repro(argv) {
  const trace = argv.includes("--trace");
  const each = argv.includes("--each");
  let files = [], plan = null, timeout = 10, project = process.cwd();
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (["--plan", "--timeout", "--project", "--files-from", "--coverage"].includes(a)) {
      const v = argv[++i];
      if (a === "--plan") plan = v;
      else if (a === "--timeout") timeout = +v;
      else if (a === "--project") project = v;
      else if (a === "--files-from") files.push(...fs.readFileSync(v, "utf8").split("\n").filter(Boolean));
    } else if (!a.startsWith("-")) files.push(a);
  }
  const ci = argv.indexOf("--coverage");
  if (ci >= 0 && !process.env.NODE_V8_COVERAGE) return coverage(argv, argv[ci + 1], project);
  if (argv.includes("--manifest")) {
    await loadHarness(project, false);
    process.stdout.write(REG.manifest() + "\n");
    return 0;
  }
  await loadHarness(project, false);
  if (plan) RT.plan = fs.openSync(plan, "w");
  if (each) {
    const stats = { programs: 0, ok: 0, crashed: 0, timeouts: 0, invalid: 0 };
    for (const f of files) {
      stats.programs++;
      let timer;
      const r = await Promise.race([
        runProgram(fs.readFileSync(f)),
        new Promise((res) => { timer = setTimeout(() => res("timeout"), timeout * 1000); }),
      ]);
      clearTimeout(timer);
      stats[{ ok: "ok", finding: "crashed", invalid: "invalid", timeout: "timeouts" }[r]]++;
    }
    process.stdout.write(JSON.stringify(stats) + "\n");
    return 0;
  }
  for (const f of files) {
    if (trace) process.stderr.write(`[stitch] running ${f}\n`);
    let data;
    try {
      data = fs.readFileSync(f);
    } catch (e) {
      process.stderr.write(`[stitch] cannot open ${f}: ${e.message}\n`);
      return 2;
    }
    const r = await runProgram(data, trace);
    if (r === "finding") return 1;
    if (r === "invalid") return 2;
  }
  if (RT.plan !== null) fs.closeSync(RT.plan);
  return 0;
}

// `--coverage DIR`: run again under V8 coverage, then convert it with c8 (lcov.info, html/)
function coverage(argv, dir, project) {
  const cp = require("child_process");
  const raw = path.join(dir, "v8");
  fs.mkdirSync(raw, { recursive: true });
  const args = argv.filter((a, i) => a !== "--coverage" && argv[i - 1] !== "--coverage");
  const r = cp.spawnSync(process.execPath, [__filename, ...args], {
    stdio: "inherit", env: { ...process.env, NODE_V8_COVERAGE: raw },
  });
  const inc = [];
  for (const name of (process.env.STITCH_JS_INCLUDE || "").split(",").map((x) => x.trim()).filter(Boolean)) {
    try {
      const pj = require.resolve(`${name}/package.json`, { paths: [project, "/opt", "/src"] });
      inc.push(`${fs.realpathSync(path.dirname(pj))}/**`);
    } catch (e) {
      if (name.startsWith("/")) inc.push(`${name}/**`);
    }
  }
  inc.push(`${path.resolve(project, "harness")}/**`);
  const c8 = path.join(__dirname, "node_modules", "c8", "bin", "c8.js");
  const common = ["--temp-directory", raw, "--allowExternal", "--exclude-node-modules=false",
    ...inc.flatMap((g) => ["--include", g]), "--exclude", `${__dirname}/**`];
  const rep = cp.spawnSync(process.execPath, [c8, "report", ...common, "--reports-dir", dir, "--reporter", "lcovonly",
    "--reporter", "text"], { encoding: "utf8", cwd: "/" });
  fs.writeFileSync(path.join(dir, "report.txt"), (rep.stdout || "") + (rep.stderr || ""));
  const lcov = path.join(dir, "lcov.info");
  if (fs.existsSync(lcov)) {
    // c8 writes paths relative to its working directory (/)
    fs.writeFileSync(lcov, fs.readFileSync(lcov, "utf8").replace(/^SF:(?!\/)/gm, "SF:/"));
  }
  cp.spawnSync(process.execPath, [c8, "report", ...common, "--reports-dir", path.join(dir, "html"), "--reporter", "html"],
    { encoding: "utf8", cwd: "/" });
  fs.rmSync(raw, { recursive: true, force: true });
  return r.status === null ? 1 : r.status;
}

async function main(argv) {
  if (argv[0] === "worker") {
    const i = argv.indexOf("--project");
    await worker(i >= 0 ? argv[i + 1] : process.cwd());
    return 0;
  }
  if (!argv.length || argv[0] === "-h" || argv[0] === "--help") {
    process.stdout.write("usage: stitch-js [--trace] [--plan FILE] [--manifest] [--each] FILE.stp...\n");
    return 0;
  }
  return repro(argv);
}

main(process.argv.slice(2)).then((code) => {
  process.exitCode = code;
  // a library may keep timers or sockets open: do not wait for them
  setImmediate(() => process.exit(code));
}, (e) => {
  process.stderr.write(`[stitch] error: ${e && e.stack ? e.stack : e}\n`);
  process.exit(1);
});
