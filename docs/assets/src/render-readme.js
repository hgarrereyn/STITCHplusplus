// Renders the README images from the docs site (dark theme):
//   docs/assets/readme/title.png      the home page title, cycling through the languages (animated PNG)
//   docs/assets/readme/languages.png  the language buttons, none picked
//   docs/assets/readme/example.png    the "Programs" and "Example crash" cards
//
// Each image is its own dark panel on a transparent background, so one file reads on GitHub's light
// and dark pages. The README links them with a plain relative <img src>, not <picture>: a <picture>
// source follows the browser's prefers-color-scheme, which can disagree with the reader's GitHub
// theme, and relative srcset paths do not resolve in a private repository.
//
// Needs Playwright, ffmpeg and the site running:
//   cd website && npm run build && npx astro preview
//   node docs/assets/src/render-readme.js [http://localhost:4321/STITCHplusplus/]
const { chromium } = require('playwright');
const { execFileSync } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');

const URL = process.argv[2] || 'http://localhost:4321/STITCHplusplus/';
const OUT = path.join(__dirname, '..', 'readme');
const FPS = 25, HOLD = 2.0, MOVE = 0.6, FADE = 0.4;
const PAD = { x: 36, y: 28 }; // around the title, inside its panel

const easeOut = (x) => 1 - Math.pow(1 - Math.min(1, Math.max(0, x)), 3);

async function open(browser) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 2, colorScheme: 'dark' });
  await page.addInitScript(() => {
    localStorage.setItem('stitch-lang', 'python');
    // The home page draws random programs; seed them so every render shows the same one.
    let seed = 7;
    Math.random = () => ((seed = (seed * 16807) % 2147483647) - 1) / 2147483646;
  });
  await page.goto(URL);
  await page.addStyleTag({
    content: `html, body { background: transparent !important; }
      .rotator .word { transition: none !important; }
      .home-hero h1 { box-sizing: content-box; padding: ${PAD.y}px ${PAD.x}px;
        background: var(--sl-color-black); border: 1px solid var(--sl-color-hairline-light); border-radius: 16px; }
      .example-side { position: static !important; display: grid !important;
        grid-template-columns: 1fr 1fr; align-items: stretch; width: 1040px; background: transparent; }
      .lang-buttons { width: 840px; }
      .lang-buttons button, .lang-buttons button[aria-pressed="true"] {
        background: transparent !important; box-shadow: none !important; font-weight: 500 !important;
        color: var(--sl-color-gray-2) !important; }
      .lang-buttons img { opacity: 1 !important; filter: none !important; }
      .example-head p, .crash-card details, .crash-card summary { display: none !important; }`,
  });
  await page.evaluate(() => document.fonts.ready);
  return page;
}

async function title(browser) {
  const page = await open(browser);
  const h1 = page.locator('.home-hero h1');
  // Fit the panel to the text: the widest of the first two lines and the languages.
  await h1.evaluate((el) => {
    const range = document.createRange();
    range.selectNodeContents(el.firstElementChild);
    const words = [...el.querySelectorAll('.word')].map((w) => w.getBoundingClientRect().width);
    el.style.width = `${Math.ceil(Math.max(range.getBoundingClientRect().width, ...words)) + 4}px`;
  });
  const n = await page.locator('.rotator .word').count();
  const set = (states) => page.evaluate((states) => {
    document.querySelectorAll('.rotator .word').forEach((w, i) => {
      const s = states[i] || { y: -70, o: 0 };
      w.style.transform = `translateY(${s.y}%)`;
      w.style.opacity = String(s.o);
    });
  }, states);

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'stitch-title-'));
  const list = [];
  let f = 0;
  const shot = async (duration) => {
    const file = path.join(dir, `f${String(f++).padStart(4, '0')}.png`);
    await h1.screenshot({ path: file, omitBackground: true });
    list.push(`file '${file}'`, `duration ${duration}`);
  };
  for (let i = 0; i < n; i++) {
    await set({ [i]: { y: 0, o: 1 } });
    await shot(HOLD);
    const steps = Math.round(MOVE * FPS);
    for (let k = 1; k <= steps; k++) {
      const t = k / FPS;
      const p = easeOut(t / MOVE), o = Math.min(1, t / FADE);
      await set({ [i]: { y: 70 * p, o: 1 - o }, [(i + 1) % n]: { y: -70 + 70 * p, o } });
      await shot(1 / FPS);
    }
  }
  list.push(list[list.length - 2]); // the concat demuxer drops the last duration otherwise
  fs.writeFileSync(path.join(dir, 'list.txt'), list.join('\n'));
  await page.close();

  const out = path.join(OUT, 'title.png');
  execFileSync('ffmpeg', ['-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', path.join(dir, 'list.txt'),
    '-pix_fmt', 'rgba', '-fps_mode', 'vfr', '-plays', '0', '-f', 'apng', out]);
  fs.rmSync(dir, { recursive: true });
  console.log(out);
}

async function still(browser, selector, name) {
  const page = await open(browser);
  await page.waitForTimeout(500); // let the first program finish drawing
  const out = path.join(OUT, name);
  await page.locator(selector).screenshot({ path: out, omitBackground: true, animations: 'disabled' });
  await page.close();
  console.log(out);
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await chromium.launch();
  await title(browser);
  await still(browser, '.lang-buttons', 'languages.png');
  await still(browser, '.example-side', 'example.png');
  await browser.close();
})();
