const { chromium } = require("playwright");
(async () => {
const times = process.argv.slice(3).map(Number), out = process.argv[2];
const b = await chromium.launch();
const p = await b.newPage({ viewport: { width: 1920, height: 1080 } });
p.on("console", (m) => console.log("console:", m.text()));
p.on("pageerror", (e) => console.log("ERR:", e.message));
await p.goto("file://" + process.cwd() + "/stitch.html#render");
await p.evaluate(() => window.ready);
for (const t of times) {
  await p.evaluate((t) => window.renderAt(t), t);
  const c = await p.$("canvas");
  await c.screenshot({ path: `${out}/f_${t.toFixed(2)}.png` });
}
await b.close();
})();
