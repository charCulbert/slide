// R3: two implementations of the Laws. This test pins ui/laws.js to the fixture
// tests/laws-fixture.json, which the C++ tests write straight out of Laws.h.
//
//   node --test tests/laws.test.mjs

import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

import * as laws from '../ui/laws.js';

const fixture = JSON.parse(
  readFileSync(fileURLToPath(new URL('laws-fixture.json', import.meta.url)), 'utf8'));

const tolerance = 1e-9;
const close = (actual, expected, what) =>
  assert.ok(Math.abs(actual - expected) <= tolerance,
    `${what}: ${actual} != ${expected} (Δ ${Math.abs(actual - expected)})`);

test('recipeAt', () => {
  for (const row of fixture.recipeAt) {
    const what = JSON.stringify(row);
    const out = laws.recipeAt(row.medium, row.wear);
    for (const key of ['sine', 'sineHz', 'rand', 'randHz', 'lossHz', 'hiss', 'drive', 'compand'])
      close(out[key], row[key], `${key} ${what}`);
    assert.equal(out.lossTracksTime, Boolean(row.lossTracksTime), `lossTracksTime ${what}`);
  }
});

test('toneCuts', () => {
  for (const row of fixture.toneCuts) {
    const what = JSON.stringify(row);
    const out = laws.toneCuts(row.tone);
    close(out.highCutHz, row.highCutHz, `highCutHz ${what}`);
    close(out.lowCutHz, row.lowCutHz, `lowCutHz ${what}`);
  }
});

test('nearestNiceRatio', () => {
  for (const row of fixture.nearestNiceRatio) {
    const what = JSON.stringify(row);
    const out = laws.nearestNiceRatio(row.raw, row.held);
    close(out.value, row.out, `value ${what}`);
    close(out.newHeld, row.newHeld, `newHeld ${what}`);
  }
});

test('wobbleReferenceMs', () => {
  for (const row of fixture.wobble)
    close(laws.wobbleReferenceMs(row.timeMs), row.wobbleReferenceMs, JSON.stringify(row));
});

test('passGain', () => {
  for (const row of fixture.passGain)
    close(laws.passGain(row.repeats, row.cross, row.a, row.b), row.out, JSON.stringify(row));
});
