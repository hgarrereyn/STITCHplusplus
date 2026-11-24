// Renders the README images from the docs site, in GitHub's light and dark page colors:
//   website/public/readme/title-{dark,light}.gif     the home page title, cycling through the languages
//   website/public/readme/languages-{dark,light}.png the language buttons, none picked
//   website/public/readme/example-{dark,light}.png   the "Programs" and "Example crash" cards
//
// They are published with the docs site, and the README links them by absolute URL
// (https://hgarrereyn.github.io/STITCHplusplus/readme/...): GitHub does not resolve relative
// <picture> srcset paths in a private repository.
//
// Needs Playwright, ffmpeg and the site running:
//   cd website && npm run build && npx astro preview
//   node docs/assets/src/render-readme.js [http://localhost:4321/STITCHplusplus/]
const { chromium } = require('playwright');
const { execFileSync } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');

const URL = process.argv[2] || 'http://localhost:4321/STITCHplusplus/';
const OUT = path.join(__dirname, '..', '..', '..', 'website', 'public', 'readme');
const GITHUB_BG = { dark: '#0d1117', light: '#ffffff' };
const FPS = 25, HOLD = 2.0, MOVE = 0.6, FADE = 0.4;

const easeOut = (x) => 1 - Math.pow(1 - Math.min(1, Math.max(0, x)), 3);

async function open(browser, theme) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 2, colorScheme: theme });
  await page.addInitScript(() => {
    localStorage.setItem('stitch-lang', 'python');
    // The home page draws random programs; seed them so both themes show the same one.
    let seed = 7;
    Math.random = () => ((seed = (seed * 16807) % 2147483647) - 1) / 2147483646;
  });
  await page.goto(URL);
  await page.addStyleTag({
    content: `html, body { background: ${GITHUB_BG[theme]} !important; }
      .rotator .word { transition: none !important; }
      .example-side { position: static !important; display: grid !important;
        grid-template-columns: 1fr 1fr; align-items: stretch; width: 1040px; padding: 2px; }
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

async function title(browser, theme) {
  const page = await open(browser, theme);
  const h1 = page.locator('.home-hero h1');
  // Clip to the text, not the heading's box: the widest of the first two lines and the languages.
  const box = await h1.boundingBox();
  const width = await h1.evaluate((el) => {
    const range = document.createRange();
    range.selectNodeContents(el.firstElementChild);
    const words = [...el.querySelectorAll('.word')].map((w) => w.getBoundingClientRect().width);
    return Math.max(range.getBoundingClientRect().width, ...words);
  });
  const clip = { x: box.x - 4, y: box.y - 4, width: Math.ceil(width) + 8, height: box.height + 12 };
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
    await page.screenshot({ path: file, clip });
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

  const out = path.join(OUT, `title-${theme}.gif`);
  execFileSync('ffmpeg', ['-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', path.join(dir, 'list.txt'),
    '-vf', 'split[a][b];[a]palettegen=stats_mode=full[p];[b][p]paletteuse=dither=none',
    '-fps_mode', 'vfr', '-loop', '0', out]);
  fs.rmSync(dir, { recursive: true });
  console.log(out, clip.width, 'x', clip.height);
}

async function languages(browser, theme) {
  const page = await open(browser, theme);
  const out = path.join(OUT, `languages-${theme}.png`);
  await page.locator('.lang-buttons').screenshot({ path: out });
  await page.close();
  console.log(out);
}

async function example(browser, theme) {
  const page = await open(browser, theme);
  await page.waitForTimeout(500); // let the first program finish drawing
  const out = path.join(OUT, `example-${theme}.png`);
  await page.locator('.example-side').screenshot({ path: out, animations: 'disabled' });
  await page.close();
  console.log(out);
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await chromium.launch();
  for (const theme of ['dark', 'light']) {
    await title(browser, theme);
    await languages(browser, theme);
    await example(browser, theme);
  }
  await browser.close();
})();
