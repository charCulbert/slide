// The face on its own in Chrome: resources/page/face.js with compost, fed the parameter table
// from Parameters.h the way the plug-in sends it. Every zone is dragged sideways and
// up and down to both ends; the run fails on a script error, on a frame that draws
// next to nothing, or on a frame slower than a budget.
//
//   node tests/FaceE2E.mjs
//   DAW_ROOT=… node tests/FaceE2E.mjs   (where playwright lives; the browser DAW)

import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {readFile} from 'node:fs/promises';
import {resolve, extname} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const dawRoot = process.env.DAW_ROOT ?? resolve(root, '../wclap-browser-daw');
const {chromium} = createRequire(`${dawRoot}/package.json`)('playwright');

// The parameter table, read from Parameters.h so the test never drifts from it.
async function table() {
  const source = await readFile(resolve(root, 'Parameters.h'), 'utf8');
  const ids = Object.fromEntries([...source.matchAll(/^\s+(\w+) = (\d+),?$/gm)].map(m => [m[1], Number(m[2])]));
  const lists = Object.fromEntries([...source.matchAll(/array<const char\*, \d+> (\w+) \{([^}]*)\}/g)]
    .map(m => [m[1], [...m[2].matchAll(/"([^"]*)"/g)].map(n => n[1])]));
  const enums = {link: lists.linkNames, sync: lists.switchNames, modType: lists.modTypeNames};
  const number = text => text.includes('/') ? text.split('/').map(Number).reduce((a, b) => a / b)
    : text === 'laws::maxRepeats' ? 1000 : Number(text);
  return [...source.matchAll(/\{ (\w+),\s+"(\w+)",\s+"([^"]+)",\s+"([^"]*)",\s+([^,]+), ([^,]+), ([^,]+), ([^,]+), ([^,]+), (\d+), (true|false) \}/g)]
    .map(([, key, identifier, name, unit, min, max, initial, step, mid, digits]) => {
      const spec = {id: ids[key], identifier, name, unit, min: number(min), max: number(max), initial: number(initial),
        step: number(step), mid: number(mid), digits: Number(digits), options: enums[key] ?? []};
      spec.curve = spec.min > 0 && spec.mid > spec.min && spec.mid < spec.max ? 'log' : 'linear';
      return spec;
    });
}

const page = specs => `<!doctype html><html><head><meta charset="utf-8"><link rel="icon" href="data:,">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&family=Barlow+Semi+Condensed:wght@500&display=swap">
<style>:root{--panel:#cdcdcd;--band:#c2c2c2;--ink:#1c1c1c;--ink2:#4f4f4f;--dim:#5d5d5d;--hair:#a9a9a9;--acc:#75540a;--lineb:#767676;--glass:rgba(255,255,255,.28)}
body{margin:0;background:var(--panel)} #host{width:765px;height:530px}</style></head>
<body><div id="host"></div><script type="module">
import './face.js';
const face = document.createElement('slide-face');
document.getElementById('host').append(face);
face.setMetadata(${JSON.stringify(specs)});
window.face = face;
</script></body></html>`;

// resources/page/ as it is staged in the plug-in: face.js and laws.js, compost beside them.
async function serve(specs) {
  const types = {'.js': 'text/javascript', '.html': 'text/html'};
  const server = createServer(async (request, response) => {
    const path = new URL(request.url, 'http://x').pathname;
    try {
      const body = path === '/' ? page(specs)
        : await readFile(resolve(root, 'resources/page', path.slice(1)));
      response.writeHead(200, {'content-type': types[extname(path)] ?? 'text/html'});
      response.end(body);
    } catch { response.writeHead(404); response.end(); }
  });
  await new Promise(done => server.listen(0, '127.0.0.1', done));
  return server;
}

const specs = await table();
assert(specs.length >= 16, `read only ${specs.length} parameters from Parameters.h`);
const server = await serve(specs);
const browser = await chromium.launch({channel: 'chrome', headless: true});
const errors = [];
try {
  const tab = await browser.newPage({viewport: {width: 765, height: 530}, deviceScaleFactor: 2});
  tab.on('pageerror', e => errors.push(e.message));
  tab.on('console', m => { if (m.type() === 'error' && !m.text().includes('fonts.g')) errors.push(m.text()); });
  await tab.goto(`http://127.0.0.1:${server.address().port}/`);
  await tab.waitForFunction(() => window.face?.zones?.length > 10);

  // What a frame looks like: how many pixels differ from the cleared corner, and how
  // long the last render took.
  await tab.evaluate(() => {
    const face = window.face, render = face.render.bind(face);
    window.renderErrors = [];
    face.render = () => {
      const t = performance.now();
      try { return render(); } catch (e) { window.renderErrors.push(e.stack); throw e; }
      finally { window.lastFrameMs = performance.now() - t; }
    };
  });
  const frame = () => tab.evaluate(() => new Promise(done => requestAnimationFrame(() => requestAnimationFrame(() => {
    const c = window.face.shadowRoot.querySelector('canvas'), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    let drawn = 0; for (let i = 0; i < d.length; i += 16) if (Math.abs(d[i] - d[0]) + Math.abs(d[i + 1] - d[1]) + Math.abs(d[i + 2] - d[2]) > 24) drawn++;
    done({drawn, ms: window.lastFrameMs ?? 0});
  }))));
  const zones = await tab.evaluate(() => window.face.zones.map(z => ({key: z.key, x: z.x, y: z.y, w: z.w, h: z.h})));
  const base = await frame();
  assert(base.drawn > 5000, `the face drew ${base.drawn} sample pixels`);

  const seen = new Set(), report = [];
  let worst = {ms: 0};
  for (const z of zones) {
    if (seen.has(z.key)) continue;
    seen.add(z.key);
    // zones are in device pixels; the page is at 2×
    const cx = (z.x + z.w / 2) / 2, cy = (z.y + z.h / 2) / 2;
    for (const [dx, dy] of [[-400, 0], [400, 0], [0, -300], [0, 300]]) {
      await tab.mouse.move(cx, cy);
      await tab.mouse.down();
      for (let k = 1; k <= 8; k++) {
        await tab.mouse.move(cx + dx * k / 8, cy + dy * k / 8);
        const f = await frame();
        if (f.ms > worst.ms) worst = {ms: f.ms, key: z.key, dx, dy};
        assert(f.drawn > base.drawn * 0.2, `dragging ${z.key} by ${dx},${dy}: the frame drew only ${f.drawn} of ${base.drawn}`);
        const thrown = await tab.evaluate(() => window.renderErrors);
        assert.equal(errors.length + thrown.length, 0, `dragging ${z.key}: ${[...errors, ...thrown].join('\n')}`);
      }
      await tab.mouse.up();
      await tab.mouse.dblclick(cx, cy); // reset what was moved
    }
    report.push(z.key);
  }
  console.log('DRAGGED', report.join(' '));
  console.log('SLOWEST FRAME', worst.ms.toFixed(1), 'ms', JSON.stringify(worst));
  assert(worst.ms < 40, `a frame took ${worst.ms.toFixed(1)} ms dragging ${worst.key}`);
  console.log('VERIFIED every zone dragged both ways, no errors, no blank frames');
} finally { await browser.close(); server.close(); }
