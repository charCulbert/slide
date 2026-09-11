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

test('linkRight', () => {
  for (const row of fixture.linkRight)
    close(laws.linkRight(row.link, row.left, row.ratio, row.difference, row.right),
      row.out, JSON.stringify(row));
});

test('divisionMs', () => {
  for (const row of fixture.divisionMs)
    close(laws.divisionMs(row.index, row.bpm), row.out, JSON.stringify(row));
});

test('nearestDivision', () => {
  for (const row of fixture.nearestDivision)
    assert.equal(laws.nearestDivision(row.ms, row.bpm), row.out, JSON.stringify(row));
});

test('gainAt', () => {
  for (const row of fixture.gainAt)
    close(laws.gainAt(row.shape, row.repeats, row.k), row.out, JSON.stringify(row));
});

test('lapGain and isLoop', () => {
  for (const row of fixture.lapGain) {
    const what = JSON.stringify(row);
    close(laws.lapGain(row.shape, row.repeats, Boolean(row.hold)), row.lapGain, what);
    assert.equal(laws.isLoop(row.shape, Boolean(row.hold)), Boolean(row.isLoop), what);
  }
});

test('toneLaw', () => {
  for (const row of fixture.toneLaw) {
    const what = JSON.stringify(row);
    const out = laws.toneLaw(row.blur, row.tone, Boolean(row.hold));
    close(out.diffusion, row.diffusion, `diffusion ${what}`);
    close(out.early, row.early, `early ${what}`);
    close(out.highCutHz, row.highCutHz, `highCutHz ${what}`);
    close(out.lowCutHz, row.lowCutHz, `lowCutHz ${what}`);
  }
});

test('recipeAt', () => {
  for (const row of fixture.recipeAt) {
    const what = JSON.stringify(row);
    const out = laws.recipeAt(row.medium, row.wear);
    for (const key of ['sine', 'sineHz', 'rand', 'randHz', 'lossHz', 'bits', 'decimateHz', 'hiss'])
      close(out[key], row[key], `${key} ${what}`);
    assert.equal(out.lossTracksTime, Boolean(row.lossTracksTime), `lossTracksTime ${what}`);
    assert.equal(out.tidePartials, Boolean(row.tidePartials), `tidePartials ${what}`);
  }
});

test('tailMs', () => {
  for (const row of fixture.tailMs)
    close(laws.tailMs(row.mode, row.left, row.right, row.repeats, Boolean(row.hold)),
      row.out, JSON.stringify(row));
});

test('nearestNiceRatio', () => {
  for (const row of fixture.nearestNiceRatio) {
    const what = JSON.stringify(row);
    const out = laws.nearestNiceRatio(row.raw, row.held);
    close(out.value, row.out, `value ${what}`);
    close(out.newHeld, row.newHeld, `newHeld ${what}`);
  }
});

test('wobbleReferenceMs and bucketLossHz', () => {
  for (const row of fixture.wobble) {
    const what = JSON.stringify(row);
    close(laws.wobbleReferenceMs(row.timeMs), row.wobbleReferenceMs, `reference ${what}`);
    close(laws.bucketLossHz(row.lossHz, row.timeMs), row.bucketLossHz, `bucket ${what}`);
  }
});
