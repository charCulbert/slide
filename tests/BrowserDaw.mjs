// Loads the built WCLAP into the browser DAW, drives the web UI through the
// wasm module, and checks the engine's telemetry reaches the meter display.
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
  await page.locator('#plugin-file').setInputFiles(resolve(root, 'build-wclap/artifacts/Tide.wclap.tar.gz'));
  await page.locator('#plugin-inspection-status').filter({hasText: 'Added 1 plug-in'}).waitFor({timeout: 30000});
  // A synth in front of it, from the DAW's bundled demo plug-ins, so the effect
  // has something to delay. Searching opens the row's category for us.
  await page.locator('#plugin-search').fill('MNO');
  await page.locator('#plugin-tree .plugin-row').filter({hasText: 'MNO'}).first().dblclick();
  await page.locator('#plugin-search').fill('Tide');
  await page.locator('#plugin-tree .plugin-row').filter({hasText: 'Tide'}).first().dblclick();
  await page.locator('#plugin-search').fill('');

  const ui = page.frameLocator('iframe[title="Tide interface"]');
  const feedback = ui.getByRole('slider', {name: 'Feedback', exact: true});
  await feedback.waitFor({timeout: 30000});
  console.log('READOUT', await ui.locator('#readout').innerText());

  // The UI builds its controls from the plug-in's parameter metadata.
  const parameterIds = await ui.locator('compost-knob').evaluateAll(knobs =>
    knobs.map(knob => knob.getAttribute('parameter-id')));
  assert.equal(parameterIds.length, 19, 'expected nineteen knobs');
  assert.deepEqual([...parameterIds].sort((a, b) => Number(a) - Number(b)),
    [...Array(22).keys()].filter(id => ![2, 16, 21].includes(id)).map(String), 'knob parameter ids');

  // Live telemetry from the audio thread: the two delay times with the default
  // 1.5 offset ratio, refreshed while the graph runs.
  const times = async () => (await ui.locator('#readout').innerText())
    .match(/([\d.]+) \/ ([\d.]+) ms/)?.slice(1, 3).map(Number) ?? null;
  let latest = null;
  for (let attempt = 0; attempt < 60 && !latest; ++attempt) {
    await page.waitForTimeout(100);
    latest = await times();
  }
  assert(latest, 'the readout never showed delay times');
  assert(latest[0] > 300 && latest[0] < 400, `left delay ${latest[0]}`);
  assert(Math.abs(latest[1] / latest[0] - 1.5) < 0.02, `right delay ${latest[1]} is not 1.5x the left`);
  console.log('TELEMETRY', latest);

  // The visualisation is a canvas that keeps drawing.
  const painted = () => ui.locator('#field').evaluate(canvas => {
    const context = canvas.getContext('2d');
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let lit = 0;
    for (let index = 3; index < pixels.length; index += 4)
      if (pixels[index] > 8) lit += 1;
    return lit;
  });
  const lit = await painted();
  assert(lit > 500, `canvas only lit ${lit} pixels`);
  console.log('CANVAS lit pixels', lit);

  // Free and synced are exclusive, and the control that is not in charge dims.
  const sync = ui.getByRole('button', {name: /^(free|sync)$/});
  const division = ui.locator('compost-knob[parameter-id="1"]');
  assert.equal(await division.getAttribute('disabled'), '', 'division should start disabled');
  await sync.click();
  await ui.locator('#readout').filter({hasText: 'bpm'}).waitFor({timeout: 5000});
  assert.equal(await sync.getAttribute('aria-pressed'), 'true');
  assert.equal(await division.getAttribute('disabled'), null, 'division should enable when synced');
  console.log('SYNCED', await ui.locator('#readout').innerText());
  // Wait for the plug-in's echo, not just the click: the UI reflects state, it
  // does not assume it.
  await sync.click();
  await ui.locator('#sync[aria-pressed="false"]').waitFor({timeout: 5000});

  // Warp and stretch are exclusive and travel through the same parameter path.
  const mode = ui.getByRole('button', {name: /^(warp|stretch)$/});
  assert.equal(await mode.getAttribute('aria-pressed'), 'false', 'warp is the default');
  await mode.click();
  await ui.locator('#mode[aria-pressed="true"]').waitFor({timeout: 5000});
  assert.equal(await mode.innerText(), 'stretch');
  await mode.click();
  await ui.locator('#mode[aria-pressed="false"]').waitFor({timeout: 5000});
  console.log('MODE', await mode.innerText());

  // Parameter edits travel out to the host and back.
  await feedback.focus();
  await page.keyboard.press('ArrowRight');
  await ui.locator('compost-knob[parameter-id="4"][aria-valuenow="46"]').waitFor({timeout: 5000});
  console.log('FEEDBACK', await feedback.getAttribute('aria-valuenow'));

  await ui.getByRole('button', {name: 'freeze'}).click();
  await ui.locator('#freeze[aria-pressed="true"]').waitFor({timeout: 5000});
  await ui.getByRole('button', {name: 'freeze'}).click();
  await ui.locator('#freeze[aria-pressed="false"]').waitFor({timeout: 5000});
  await ui.getByRole('button', {name: 'clear'}).click();

  // Audio through the delay: a note from the synth in front has to reach the
  // effect's own meters, and the channel meter has to hear the result.
  const frame = page.frames().find(candidate => candidate.url().includes('/_wclap/resource/'));
  assert(frame, 'the UI resource frame was not created');
  await frame.evaluate(() => {
    window.tideTest = {peaks: []};
    addEventListener('message', ({data}) => {
      if (!(data instanceof ArrayBuffer)) return;
      const text = new TextDecoder().decode(data);
      if (!text.startsWith('visual:')) return;
      const fields = text.slice(7).split(',').map(Number);
      if (fields.length === 8 && fields.every(Number.isFinite))
        window.tideTest.peaks.push(Math.max(fields[5], fields[6]));
    });
  });
  await page.getByRole('button', {name: 'Keyboard', exact: true}).click();
  await page.locator('#host-keyboard').focus();
  await page.keyboard.down('a');
  await frame.waitForFunction(() => Math.max(0, ...window.tideTest.peaks.slice(-40)) > 0.05,
    null, {timeout: 15000}).catch(() => {
      throw Error('the delay never saw audio while a note was held');
    });
  await page.locator('iframe[title="Tide interface"]').screenshot({path: `${artifacts}/tide-playing.png`});
  await page.screenshot({path: `${artifacts}/daw-playing.png`});
  await page.keyboard.up('a');
  const peaks = await frame.evaluate(() => ({heard: Math.max(...window.tideTest.peaks), samples: window.tideTest.peaks.length}));
  console.log('DELAY HEARD', peaks);
  await page.waitForFunction(() => [...document.querySelectorAll('daw-session-channel')]
    .some(e => e._levels?.some(n => n > -60)), null, {timeout: 15000})
    .catch(() => { throw Error('the channel meter stayed silent with the delay inserted'); });
  console.log('METER live');

  const win = page.locator('compost-window').filter({has: page.locator('iframe[title="Tide interface"]')});
  await win.evaluate(w => w.setContentSize(600, 340));
  await page.waitForTimeout(300);
  assert(await ui.locator('main').evaluate(e => e.scrollWidth <= innerWidth && e.scrollHeight <= innerHeight),
    'compact UI overflows');
  await page.locator('iframe[title="Tide interface"]').screenshot({path: `${artifacts}/tide-compact.png`});
  await win.evaluate(w => w.setContentSize(760, 460));
  await page.waitForTimeout(400);
  await page.locator('iframe[title="Tide interface"]').screenshot({path: `${artifacts}/tide.png`});
  await page.screenshot({path: `${artifacts}/daw.png`});

  const report = {parameterIds, latest, lit, peaks,
    verified: ['wclap load', 'web UI metadata', 'live telemetry', 'canvas rendering', 'sync toggle',
      'parameter round trip', 'freeze toggle', 'clear', 'audio through the delay', 'channel meter',
      'compact layout'],
    errors};
  await writeFile(`${artifacts}/report.json`, JSON.stringify(report, null, 2));
  console.log('VERIFIED', report.verified.join(', '));
  assert.equal(errors.length, 0, errors.join('\n'));
} catch (e) {
  await page.screenshot({path: `${artifacts}/failure.png`}).catch(() => {});
  console.error('ERRORS', errors);
  console.error((await page.locator('body').innerText().catch(() => '')).slice(-4000));
  throw e;
} finally { await browser.close(); }
