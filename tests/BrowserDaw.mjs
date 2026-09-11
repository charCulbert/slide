// Slide in the wclap-browser-daw, after ../mote/tests/BrowserDaw.mjs: load the
// WCLAP, confirm the face appears, drag Left and watch the value come back through
// the bridge, toggle Sync, and keep a screenshot.
//
//   node tests/BrowserDaw.mjs            (the DAW served at 127.0.0.1:8470)
//   DAW_ROOT=… DAW_URL=… node tests/BrowserDaw.mjs

import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {mkdir, writeFile} from 'node:fs/promises';
import {resolve} from 'node:path';

const root = fileURLToPath(new URL('..', import.meta.url));
const dawRoot = process.env.DAW_ROOT ?? resolve(root, '../../wclap-browser-daw');
const {chromium} = createRequire(`${dawRoot}/package.json`)('playwright');
const artifacts = resolve(root, 'build-native/browser-test');
await mkdir(artifacts, {recursive: true});
const browser = await chromium.launch({channel: 'chrome', headless: true,
  args: ['--autoplay-policy=no-user-gesture-required']});
const page = await browser.newPage({viewport: {width: 1440, height: 960}});
const errors = [];
page.on('pageerror', e => errors.push(e.message));
try {
  await page.goto(process.env.DAW_URL ?? 'http://127.0.0.1:8470/');
  await page.locator('html[data-boot="ready"]').waitFor();
  await page.getByRole('button', {name: /^Start audio/}).click();
  await page.locator('body[data-engine="ready"]').waitFor({timeout: 30000});
  await page.getByRole('button', {name: 'Plugins', exact: true}).click();
  await page.locator('#plugin-file')
    .setInputFiles(resolve(root, 'build-wclap/artifacts/Slide.wclap.tar.gz'));
  await page.locator('#plugin-inspection-status').filter({hasText: 'Added 1 plug-in'})
    .waitFor({timeout: 30000});
  await page.locator('#plugin-tree .plugin-row').filter({hasText: 'Slide'}).dblclick();

  const ui = page.frameLocator('iframe[title="Slide interface"]');
  // The face is one canvas plus a semantic element per parameter; the metadata
  // handshake has landed once Left exists with compost's slider role.
  await ui.getByRole('slider', {name: 'Left', exact: true}).waitFor({timeout: 30000});
  await ui.getByRole('button', {name: 'Sync', exact: true}).waitFor();
  await page.screenshot({path: `${artifacts}/loaded.png`});

  const frame = page.frames().find(f => f.url().includes('/_wclap/resource/'));
  assert(frame, 'Slide resource frame was not created');
  await frame.evaluate(() => {
    window.slideTest = {values: {}, sent: []};
    addEventListener('message', ({data}) => {
      if (!(data instanceof ArrayBuffer)) return;
      const text = new TextDecoder().decode(data);
      if (!text.startsWith('values:')) return;
      for (const pair of text.slice(7).split(';')) {
        if (!pair) continue;
        const [id, value] = pair.split('=');
        window.slideTest.values[id] = Number(value);
      }
    });
  });
  const value = id => frame.evaluate(i => window.slideTest.values[i], String(id));

  // Drag Left: the keyboard drives the same compost gesture the pointer does, and
  // the plugin must answer with the new value.
  const left = ui.getByRole('slider', {name: 'Left', exact: true});
  await left.focus();
  const before = Number(await left.getAttribute('aria-valuenow'));
  await page.keyboard.press('ArrowRight');
  await page.keyboard.press('ArrowRight');
  const after = Number(await left.getAttribute('aria-valuenow'));
  assert(after > before, `Left did not move: ${before} -> ${after}`);
  await frame.waitForFunction(
    ([id, expected]) => Math.abs((window.slideTest.values[id] ?? -1) - expected) < 0.01,
    ['0', after], {timeout: 10000});
  assert.equal(await value(0), after, 'Left did not round-trip through the bridge');

  // Sync is a compost-button switch; the plugin must see the 1.
  await ui.getByRole('button', {name: 'Sync', exact: true}).click();
  await frame.waitForFunction(() => window.slideTest.values['5'] === 1, null, {timeout: 10000});
  assert.equal(await value(5), 1, 'Sync did not reach the plugin');

  await page.locator('iframe[title="Slide interface"]').screenshot({path: `${artifacts}/slide.png`});
  await writeFile(`${artifacts}/report.json`, JSON.stringify(
    {verified: ['face appears', 'Left round-trips', 'Sync toggles'],
     left: {before, after}, errors}, null, 2));
  console.log('VERIFIED face, Left', before, '->', after, ', Sync on');
  assert.equal(errors.length, 0, errors.join('\n'));
} catch (e) {
  await page.screenshot({path: `${artifacts}/failure.png`}).catch(() => {});
  console.error('ERRORS', errors);
  throw e;
} finally { await browser.close(); }
