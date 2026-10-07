export const minTimeMs = 10;
export const maxTimeMs = 3000;
export const openHighCutHz = 20000;
export const maxRepeats = 1000;

export const niceRatios = [
  0.25, 1 / 3, 0.5, 2 / 3, 0.75, 1, 1.25, 4 / 3, 1.5, 1.6180339887498949, 2, 3, 4
];

export const snapCapture = 0.012;
export const snapRelease = 0.025;

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

export function finiteOr(value, fallback) {
  return Number.isFinite(value) ? value : fallback;
}

const cleanRecipe = Object.freeze({
  sine: 0, sineHz: 0, rand: 0, randHz: 0, lossHz: openHighCutHz, lossTracksTime: false,
  hiss: 0, drive: 0, compand: 0
});

/** Tape (0), Oil can (1) or Bucket (2) at a Wear of 0–1. `rand` is the depth before
 * the engine's ×6, `drive` of 0 means no saturation, and `compand` is how far toward
 * 2:1 the compander works. */
export function recipeAt(medium, wear01) {
  const w = clamp(finiteOr(wear01, 0), 0, 1);
  if (!(w > 0) || medium < 0 || medium > 2) return { ...cleanRecipe };

  const amt = Math.pow(medium === 1 ? 0.7 * w : w, 1.8) * 5;
  let recipe, loss;
  if (medium === 0) {
    recipe = { ...cleanRecipe, sine: 0.0025, sineHz: 0.7, rand: 0.0012, randHz: 6, hiss: 0.0003 };
    loss = 9000;
  } else if (medium === 1) {
    recipe = { ...cleanRecipe, sine: 0.005, sineHz: 2.3, rand: 0.015, randHz: 1.4, hiss: 0.0004,
      drive: 1 + 0.5 * amt };
    loss = 2600;
  } else {
    recipe = { ...cleanRecipe, lossTracksTime: true, hiss: 0.0015, compand: Math.min(1, amt * 2) };
    loss = 8000;
  }
  recipe.sine *= amt;
  recipe.rand *= amt;
  const hissAmt = medium === 1 ? Math.pow(0.49 * w, 1.8) * 5 : amt;
  recipe.hiss *= Math.min(4, 0.9 * hissAmt);
  recipe.lossHz = loss + (openHighCutHz - loss) * (1 - Math.min(1, amt * 2));
  return recipe;
}

export const referenceBpm = 120;

/** Note lengths in beats, with their names: D dotted, T triplet. */
export const notes = [[1 / 48, '1/128T'], [1 / 32, '1/128'], [1 / 24, '1/64T'], [3 / 64, '1/128D'], [1 / 16, '1/64'],
  [1 / 12, '1/32T'], [3 / 32, '1/64D'], [1 / 8, '1/32'], [1 / 6, '1/16T'], [3 / 16, '1/32D'], [1 / 4, '1/16'],
  [1 / 3, '1/8T'], [3 / 8, '1/16D'], [1 / 2, '1/8'], [2 / 3, '1/4T'], [3 / 4, '1/8D'], [1, '1/4'],
  [4 / 3, '1/2T'], [3 / 2, '1/4D'], [2, '1/2'], [8 / 3, '1/1T'], [3, '1/2D'], [4, '1/1'], [6, '1/1D'],
  [8, '2/1'], [16, '4/1']];

/** The note nearest a length in beats, and whether it is on it. */
export function nearestNote(beats) {
  let best = notes[0];
  for (const n of notes) if (Math.abs(Math.log(n[0] / beats)) < Math.abs(Math.log(best[0] / beats))) best = n;
  return {beats: best[0], name: best[1], on: Math.abs(Math.log(best[0] / beats)) < 0.003};
}

/** The two lines' times in ms, as Laws.h's lineTimes: Left (followed from
 * referenceBpm to the tempo under Sync), and Right from it by Ratio or Difference. */
export function lineTimes(leftMs, sync, bpm, byRatio, ratio, difference) {
  const follow = sync ? referenceBpm / clamp(finiteOr(bpm, referenceBpm), 10, 999) : 1;
  const left = clamp(finiteOr(leftMs, minTimeMs) * follow, minTimeMs, maxTimeMs);
  const right = finiteOr(byRatio ? left * ratio : left + difference, left);
  return {left, right: clamp(right, minTimeMs, maxTimeMs)};
}

/** Tone's two cuts in the loop: dark (below 0) lowers a high cut from 12 kHz, thin
 * (above 0) raises a low cut from 20 Hz up to 6 kHz. */
export function toneCuts(tone11) {
  const t = clamp(finiteOr(tone11, 0), -1, 1);
  return {
    highCutHz: t < 0 ? 12000 * Math.pow(2, 7.15 * t) : openHighCutHz,
    lowCutHz: t > 0 ? Math.min(6000, 20 * Math.pow(2, 8.25 * t)) : 20
  };
}

/** The snap lock: a raw ratio captures a nice ratio within 1.2 % in log ratio and
 * only lets go past 2.5 %. `held` and the returned `newHeld` are 0 when nothing is
 * held. */
export function nearestNiceRatio(raw, held) {
  const value = finiteOr(raw, 1);
  if (!(value > 0)) return { value, newHeld: 0 };
  if (held > 0 && Math.abs(Math.log(value / held)) < snapRelease)
    return { value: held, newHeld: held };
  for (const candidate of niceRatios)
    if (Math.abs(Math.log(value / candidate)) < snapCapture)
      return { value: candidate, newHeld: candidate };
  return { value, newHeld: 0 };
}

/** The per-pass gain for Repeats n with Cross x and line times a and b; see Laws.h. */
export function passGain(repeats, cross, a, b) {
  const passes = Math.max(1, finiteOr(repeats, 1)) - 1;
  if (passes < 1e-3) return 0;
  const plain = Math.pow(10, -3 / passes), cap = 0.999;
  const x = clamp(finiteOr(cross, 0), 0, 1), s = 1 - x;
  if (x <= 0) return Math.min(plain, cap);
  const ta = Math.max(minTimeMs, finiteOr(a, minTimeMs)), tb = Math.max(minTimeMs, finiteOr(b, minTimeMs));
  const longer = Math.max(ta, tb);
  const pa = Math.pow(plain, -ta / longer), pb = Math.pow(plain, -tb / longer);
  const qa = s * pa, qb = s * pb, c = x * x * pa * pb;
  const A = qa * qb - c, B = qa + qb;
  const g = 2 / (B + Math.sqrt(Math.max(0, B * B - 4 * A)));
  return Number.isFinite(g) ? clamp(g, 0, cap) : Math.min(plain, cap);
}

/** Wobble depth is relative to the delay time, but only between 40 and 400 ms. */
export function wobbleReferenceMs(timeMs) {
  return clamp(finiteOr(timeMs, minTimeMs), 40, 400);
}
