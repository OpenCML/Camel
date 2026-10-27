// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Renders the scenes the running editor has loaded, with its built-in Image sequence exporter,
// from headless Chromium: frames land in output/camel-promo/<scene>/000000.png ...
// Usage: start the editor (`npm run serve`), then `node render.cjs [url]`.
const puppeteer = require('puppeteer');

const url = process.argv[2] || 'http://localhost:9000/';

(async () => {
  const browser = await puppeteer.launch({headless: 'new', args: ['--no-sandbox', '--disable-dev-shm-usage', '--window-size=1600,1000']});
  const page = await browser.newPage();
  await page.setViewport({width: 1600, height: 1000});
  page.on('pageerror', (e) => console.log('PAGEERROR', e.message));
  page.on('console', (m) => {
    if (m.type() === 'error') console.log('CONSOLE', m.text());
  });
  await page.goto(url, {waitUntil: 'networkidle0', timeout: 120000});
  await new Promise((r) => setTimeout(r, 6000));
  // one folder of frames per scene
  const grouped = await page.evaluate(() => {
    const leaves = [...document.querySelectorAll('*')].filter((e) => e.children.length === 0 && /group by scene/i.test(e.textContent || ''));
    for (const l of leaves) {
      let p = l;
      for (let i = 0; i < 6 && p; i++, p = p.parentElement) {
        const box = p.querySelector('input[type=checkbox]');
        if (box) {
          if (!box.checked) box.click();
          return box.checked;
        }
      }
    }
    return 'no checkbox';
  });
  console.log('group by scene:', grouped);
  const clicked = await page.evaluate(() => {
    const b = [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Render');
    if (!b) return false;
    b.click();
    return true;
  });
  console.log('render started:', clicked);
  // the button reads Abort while rendering and Render again when done
  for (let i = 0; i < 600; i++) {
    await new Promise((r) => setTimeout(r, 2000));
    const state = await page.evaluate(() =>
      [...document.querySelectorAll('button')].map((b) => b.textContent.trim()).filter((x) => /^(Render|Abort)$/.test(x)).join(','),
    );
    if (state === 'Render' && i > 1) break;
  }
  console.log('done');
  await browser.close();
})();
