// Render stitch.html frame by frame into an H.264 video (silent); also dump the sound cues.
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const fs = require("fs");
(async () => {
  const out = process.argv[2] || "frames.mp4", fps = +(process.argv[3] || 60);
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1920, height: 1080 } });
  p.on("pageerror", (e) => console.log("ERR:", e.message));
  await p.goto("file://" + __dirname + "/stitch.html#render");
  await p.evaluate(() => window.ready);
  const dur = await p.evaluate(() => window.DUR);
  fs.writeFileSync(out.replace(/\.mp4$/, ".events.json"), JSON.stringify(await p.evaluate(() => window.EVENTS)));
  const ff = spawn("ffmpeg", ["-loglevel", "error", "-y", "-f", "image2pipe", "-framerate", String(fps), "-i", "-",
    "-c:v", "libx264", "-preset", "slow", "-crf", "14", "-pix_fmt", "yuv420p", "-tune", "film", out], { stdio: ["pipe", "inherit", "inherit"] });
  const canvas = await p.$("canvas");
  const n = Math.round(dur * fps);
  for (let i = 0; i < n; i++) {
    await p.evaluate((t) => window.renderAt(t), i / fps);
    const buf = await canvas.screenshot({ type: "png" });
    if (!ff.stdin.write(buf)) await new Promise((r) => ff.stdin.once("drain", r));
    if (i % 120 === 0) console.log(`frame ${i}/${n}`);
  }
  ff.stdin.end();
  await new Promise((r) => ff.on("close", r));
  await b.close();
})();
