
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

import * as laws from '../resources/page/laws.js';

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
  const snap = (raw, held, value, newHeld) => {
    const out = laws.nearestNiceRatio(raw, held);
    close(out.value, value, `value ${raw} held ${held}`);
    close(out.newHeld, newHeld, `newHeld ${raw} held ${held}`);
  };
  snap(1.5, 0, 1.5, 1.5);
  snap(1.5 * Math.exp(0.02), 1.5, 1.5, 1.5);
  snap(1.5 * Math.exp(0.03), 1.5, 1.5 * Math.exp(0.03), 0);
  snap(1.5 * Math.exp(0.02), 0, 1.5 * Math.exp(0.02), 0);
  snap(1.5 * Math.exp(0.01), 0, 1.5, 1.5);
  snap(2.5, 0, 2.5, 0);
  snap(0.252, 0, 0.25, 0.25);
  snap(1 / 3 * 1.005, 0, 1 / 3, 1 / 3);
  snap(3.98, 0, 4, 4);
  snap(1.02, 1, 1, 1);
  for (let i = 1; i < laws.niceRatios.length; ++i) assert.ok(laws.niceRatios[i] > laws.niceRatios[i - 1]);
});

test('wobbleReferenceMs', () => {
  for (const row of fixture.wobble)
    close(laws.wobbleReferenceMs(row.timeMs), row.wobbleReferenceMs, JSON.stringify(row));
});

test('lineTimes', () => {
  for (const row of fixture.lineTimes) {
    const out = laws.lineTimes(row.leftMs, Boolean(row.sync), row.bpm, Boolean(row.byRatio), row.relation, row.relation);
    close(out.left, row.left, `left ${JSON.stringify(row)}`);
    close(out.right, row.right, `right ${JSON.stringify(row)}`);
  }
});

test('notes', () => {
  assert.equal(laws.notes.length, fixture.notes.length);
  fixture.notes.forEach((row, i) => close(laws.notes[i][0], row.beats, `note ${i}`));
  for (const row of fixture.nearestNote) {
    const n = laws.nearestNote(row.in);
    close(n.beats, row.beats, JSON.stringify(row));
    assert.equal(n.on, Boolean(row.on), JSON.stringify(row));
  }
  assert.deepEqual(['1/8', '1/8D', '1/8T'].map(name => laws.notes.find(n => n[1] === name)[0]), [0.5, 0.75, 1 / 3]);
});

test('constants', () => {
  for (const [key, value] of Object.entries(fixture.constants[0])) assert.equal(laws[key], value, key);
});

test('passGain', () => {
  for (const row of fixture.passGain)
    close(laws.passGain(row.repeats, row.cross, row.a, row.b), row.out, JSON.stringify(row));
});
