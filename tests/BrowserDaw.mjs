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

  // The chips are ordinary DOM and appear even when the canvas never draws, so the
  // picture is checked by counting pixels that differ from the cleared corner.
  const face = () => frame.evaluate(() => {
    const element = document.querySelector('slide-face');
    const canvas = element?.shadowRoot?.querySelector('canvas');
    const box = canvas?.getBoundingClientRect();
    if (!canvas?.width || !canvas?.height) return {drawn: 0, box: null};
    const data = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;
    const [r, g, b] = data;
    let drawn = 0;
    for (let i = 0; i < data.length; i += 4) {
      if (Math.abs(data[i] - r) + Math.abs(data[i + 1] - g) + Math.abs(data[i + 2] - b) > 12)
        drawn++;
    }
    return {drawn, box: {x: box.x, y: box.y, width: box.width, height: box.height}};
  });
  await frame.waitForFunction(() => {
    const canvas = document.querySelector('slide-face')?.shadowRoot?.querySelector('canvas');
    return canvas?.width > 0 && canvas.getBoundingClientRect().height > 0;
  }, null, {timeout: 15000});
  const picture = await face();
  assert(picture.box.width > 0 && picture.box.height > 0,
    `Face has no box: ${JSON.stringify(picture.box)}`);
  assert(picture.drawn > 500,
    `Face canvas drew ${picture.drawn} pixels; the picture is missing`);
  console.log('PICTURE', picture.drawn, 'pixels in',
    Math.round(picture.box.width), '×', Math.round(picture.box.height));
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

  // A drag on the picture itself: the Left bracket above the line, which is the
  // gesture the face hit-tests and hands to the Left control.
  const iframe = await page.locator('iframe[title="Slide interface"]').boundingBox();
  const grab = await frame.evaluate(() => {
    const element = document.querySelector('slide-face');
    const canvas = element.shadowRoot.querySelector('canvas');
    const box = canvas.getBoundingClientRect();
    const {XD, Y, dpr} = element.geo;
    return {x: box.x + XD(element.times().left) / dpr, y: box.y + (Y - 60) / dpr,
            perDecade: element.geo.perDec / dpr};
  });
  const before2 = await value(0);
  await page.mouse.move(iframe.x + grab.x, iframe.y + grab.y);
  await page.mouse.down();
  await page.mouse.move(iframe.x + grab.x + grab.perDecade * 0.15, iframe.y + grab.y, {steps: 8});
  await page.mouse.up();
  await frame.waitForFunction(previous => window.slideTest.values['0'] !== previous,
    before2, {timeout: 10000});
  const dragged = await value(0);
  assert(dragged > before2, `Dragging the picture did not raise Left: ${before2} -> ${dragged}`);
  console.log('DRAG Left', before2, '->', dragged);

  // Sync is a compost-button switch; the plugin must see the 1.
  await ui.getByRole('button', {name: 'Sync', exact: true}).click();
  await frame.waitForFunction(() => window.slideTest.values['5'] === 1, null, {timeout: 10000});
  assert.equal(await value(5), 1, 'Sync did not reach the plugin');

  await page.locator('iframe[title="Slide interface"]').screenshot({path: `${artifacts}/slide.png`});
  await writeFile(`${artifacts}/report.json`, JSON.stringify(
    {verified: ['face appears', 'picture draws', 'Left round-trips', 'picture drag',
                'Sync toggles'],
     pixels: picture.drawn, left: {before, after, dragged}, errors}, null, 2));
  console.log('VERIFIED face, Left', before, '->', after, ', drag ->', dragged, ', Sync on');
  assert.equal(errors.length, 0, errors.join('\n'));
} catch (e) {
  await page.screenshot({path: `${artifacts}/failure.png`}).catch(() => {});
  console.error('ERRORS', errors);
  throw e;
} finally { await browser.close(); }
