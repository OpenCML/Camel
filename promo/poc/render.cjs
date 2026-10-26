// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Renders the film's frames with headless Chromium, several pages at once: a frame is a pure
// function of its time, so pages take every n-th frame. Also writes the cue sheet the score reads.
//   node render.cjs --out ../renders/poc/frames --fps 60 [--from 0] [--to 30] [--workers 4]
//   node render.cjs --out /tmp/stills --times 3.2,5.7,18.9
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer');

const args = Object.fromEntries(
    process.argv.slice(2).reduce((acc, a, i, all) => (a.startsWith('--') ? [...acc, [a.slice(2), all[i + 1]]] : acc), []),
);
const url = args.url || 'http://localhost:9100/';
const out = path.resolve(args.out || '../renders/poc/frames');
const fps = Number(args.fps || 60);
const workers = Number(args.workers || 4);
fs.mkdirSync(out, { recursive: true });

// software WebGL: Mesa llvmpipe through ANGLE's Vulkan backend was the fastest in the container
const FLAGS = ['--no-sandbox', '--disable-dev-shm-usage', '--enable-webgl', '--ignore-gpu-blocklist', '--use-gl=angle', '--use-angle=vulkan', '--enable-features=Vulkan'];

async function page(browser) {
    const p = await browser.newPage();
    await p.setViewport({ width: 1920, height: 1080 });
    p.on('pageerror', (e) => console.log('PAGEERROR', e.message));
    p.on('console', (m) => {
        if (m.type() === 'error') console.log('CONSOLE', m.text());
    });
    await p.goto(url, { waitUntil: 'networkidle0', timeout: 120000 });
    await p.waitForFunction('window.ready === true', { timeout: 120000 });
    return p;
}

async function save(p, t, file) {
    // renderAt throws when the WebGL canvas is lost or drew nothing
    const data = await p.evaluate((tt) => window.renderAt(tt), t);
    fs.writeFileSync(file, Buffer.from(data.split(',')[1], 'base64'));
}

(async () => {
    const browser = await puppeteer.launch({ headless: 'new', args: FLAGS, protocolTimeout: 600000 });
    if (args.cues) {
        const p = await page(browser);
        fs.writeFileSync(path.resolve(args.cues), JSON.stringify(await p.evaluate(() => window.cues), null, 1));
        await browser.close();
        return;
    }
    if (args.times) {
        const p = await page(browser);
        for (const t of args.times.split(',').map(Number)) await save(p, t, path.join(out, `t${t.toFixed(2)}.png`));
        await browser.close();
        return;
    }
    const from = Number(args.from || 0);
    const to = Number(args.to || 30);
    const frames = [];
    for (let i = Math.round(from * fps); i < Math.round(to * fps); i++) frames.push(i);
    const pages = await Promise.all(Array.from({ length: workers }, () => page(browser)));
    fs.writeFileSync(path.join(path.dirname(out), 'cues.json'), JSON.stringify(await pages[0].evaluate(() => window.cues), null, 1));
    const started = Date.now();
    let done = 0;
    // resumable: frames already on disk are kept; a page that dies is replaced
    await Promise.all(pages.map(async (p, w) => {
        for (let k = w; k < frames.length; k += workers) {
            const i = frames[k];
            const file = path.join(out, `${String(i).padStart(6, '0')}.png`);
            if (fs.existsSync(file) && fs.statSync(file).size > 0) {
                done++;
                continue;
            }
            for (let attempt = 0; ; attempt++) {
                try {
                    await save(p, i / fps, file);
                    break;
                } catch (e) {
                    console.log(`frame ${i}: ${e.message.split(/\r?\n/)[0]} (attempt ${attempt + 1})`);
                    if (attempt >= 2) throw e;
                    try {
                        await p.close();
                    } catch {}
                    p = await page(browser);
                }
            }
            done++;
            if (done % 60 === 0) console.log(`${done}/${frames.length}  ${((Date.now() - started) / done).toFixed(0)} ms/frame`);
        }
    }));
    await browser.close();
})();
