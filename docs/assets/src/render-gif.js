// Renders stitch-anim.html to docs/assets/stitching.gif (needs Playwright and ffmpeg).
const { chromium } = require('playwright');
const { execFileSync } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');
const FPS = 20;
(async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'stitch-gif-'));
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 900, height: 400 }, deviceScaleFactor: 1 });
  await page.goto('file://' + path.join(__dirname, 'stitch-anim.html'));
  const total = await page.evaluate(() => window.TOTAL);
  const n = Math.round(total * FPS);
  for (let i = 0; i < n; i++) {
    await page.evaluate(t => window.render(t), i / FPS);
    await page.locator('canvas').screenshot({ path: path.join(dir, `f${String(i).padStart(4, '0')}.png`) });
  }
  await browser.close();
  const out = path.join(__dirname, '..', 'stitching.gif');
  execFileSync('ffmpeg', ['-y', '-loglevel', 'error', '-framerate', String(FPS), '-i', path.join(dir, 'f%04d.png'),
    '-vf', 'split[a][b];[a]palettegen=max_colors=128:stats_mode=full[p];[b][p]paletteuse=dither=none', '-loop', '0', out]);
  fs.rmSync(dir, { recursive: true });
  console.log(out, n, 'frames');
})();
