// The pure formulas of DESIGN §3, the same code as Laws.h function for function.
// Every function is total: it clamps or substitutes rather than returning a
// non-finite number, so the face can call it while a gesture is still mid-flight.
// tests/laws.test.mjs checks this file against tests/laws-fixture.json, which the
// C++ tests write from Laws.h.

export const minTimeMs = 1;
export const maxTimeMs = 2000;
export const decayRange = 6.9; // ln(1000): the last repeat is 60 dB down
export const holdTailMs = 100000;
export const openHighCutHz = 20000;

// 1:2, 2:3, 3:4, 1:1, 5:4, 4:3, 3:2, phi, 2:1, 3:1
export const niceRatios = [
  0.5, 2 / 3, 0.75, 1, 1.25, 4 / 3, 1.5, 1.6180339887498949, 2, 3
];

export const snapCapture = 0.012; // in log ratio
export const snapRelease = 0.025;

// The 24 tempo divisions of Parameters.h: eight bases, each plain, dotted and
// triplet, ordered by ascending beat length.
export const divisionBeats = [
  1 / 48, 0.03125, 1 / 24, 0.046875, 0.0625, 1 / 12, 0.09375, 0.125,
  1 / 6, 0.1875, 0.25, 1 / 3, 0.375, 0.5, 2 / 3, 0.75,
  1, 4 / 3, 1.5, 2, 8 / 3, 3, 4, 6
];

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

export function finiteOr(value, fallback) {
  return Number.isFinite(value) ? value : fallback;
}

/** Right in ms: derived from Left while Link is Ratio (0) or Difference (1), and the
 * supplied Right passed through while Link is Off (2). A derivation that cannot be
 * made falls back to Left. */
export function linkRight(link, left, ratio, difference, right) {
  const clampedLeft = clamp(finiteOr(left, minTimeMs), minTimeMs, maxTimeMs);
  let candidate = right;
  if (link === 0) candidate = clampedLeft * ratio;
  else if (link === 1) candidate = clampedLeft + difference;
  if (!Number.isFinite(candidate)) return clampedLeft;
  return clamp(candidate, minTimeMs, maxTimeMs);
}

/** The time of one division at this tempo. */
export function divisionMs(index, bpm) {
  const tempo = clamp(finiteOr(bpm, 120), 10, 999);
  const slot = clamp(Math.trunc(index), 0, divisionBeats.length - 1);
  return divisionBeats[slot] * 60000 / tempo;
}

/** The division whose time sits closest to this one, measured in log distance — the
 * prototype's `snapT`. A time that is not a positive number keeps the first slot. */
export function nearestDivision(ms, bpm) {
  const time = finiteOr(ms, 0);
  if (!(time > 0)) return 0;
  let best = 0, closest = -1;
  for (let i = 0; i < divisionBeats.length; i++) {
    const distance = Math.abs(Math.log(divisionMs(i, bpm) / time));
    if (closest < 0 || distance < closest) { closest = distance; best = i; }
  }
  return best;
}

/** The loop topology carries a Fade past half and every hold. */
export function isLoop(shape, hold) {
  return Boolean(hold) || finiteOr(shape, 0) < -0.5;
}

/** The level of repeat k, counting from 0. */
export function gainAt(shape, repeats, k) {
  const s = clamp(finiteOr(shape, 0), -1, 1);
  const count = clamp(Math.trunc(repeats), 1, 64);
  const index = clamp(Math.trunc(k), 0, count - 1);
  const u = count > 1 ? index / (count - 1) : 0;
  return s < 0 ? Math.exp(-decayRange * (-s) * u)
               : Math.exp(-decayRange * s * (1 - u));
}

/** The gain of one lap of the loop topology. */
export function lapGain(shape, repeats, hold) {
  if (hold) return 1;
  const s = clamp(finiteOr(shape, 0), -1, 1);
  const count = clamp(Math.trunc(repeats), 1, 64);
  return Math.exp(-decayRange * (-s) / Math.max(1, count - 1));
}

/** Blur and Tone together: the diffusers, the roof and the two cuts. Blur is 0–1 and
 * Tone is −1–+1, so the face divides its percentages before calling. */
export function toneLaw(blur01, tone11, hold) {
  const b = clamp(finiteOr(blur01, 0), 0, 1);
  const t = clamp(finiteOr(tone11, 0), -1, 1);
  const roof = 9000 * Math.pow(2, -3.3 * b);
  const toneLp = t < 0 ? 12000 * Math.pow(2, 5.5 * t) : openHighCutHz;
  const lowCutHz = clamp(t > 0 ? 20 * Math.pow(2, 7 * t) : 20, 20, 2500);
  const highCutHz = hold ? openHighCutHz
                         : clamp(Math.min(roof, toneLp), 200, openHighCutHz);
  const early = Math.pow(Math.max(0, (b - 0.3) / 0.7), 1.3) * 0.62;
  return { diffusion: Math.min(1, 1.15 * b) * 0.62, early, highCutHz, lowCutHz };
}

export const cleanRecipe = {
  sine: 0, sineHz: 0, rand: 0, randHz: 0, lossHz: openHighCutHz,
  lossTracksTime: false, bits: 0, decimateHz: 0, hiss: 0, tidePartials: false
};

/** One medium's constants at this Wear. `rand` is the depth before the engine's ×6,
 * and `decimateHz` of 0 means no sample-and-hold at all. */
export function recipeAt(medium, wear01) {
  const w = clamp(finiteOr(wear01, 0), 0, 1);
  if (!(w > 0) || medium < 0 || medium > 4) return { ...cleanRecipe };

  const amt = Math.pow(w, 1.8) * 5;
  if (medium === 4) {
    // Digital wears by crushing and decimating: bits fall linearly to 8, the
    // sample rate falls by ratio from 48 kHz to 8 kHz. Its clock drifts slowly
    // and jitters a little; no hiss, no loss.
    return {
      ...cleanRecipe,
      sine: 0.0008 * amt, sineHz: 0.4, rand: 0.0002 * amt, randHz: 12,
      bits: Math.round(16 - 8 * w),
      decimateHz: 48000 * Math.pow(8000 / 48000, w)
    };
  }

  let recipe = { ...cleanRecipe };
  let loss = openHighCutHz;
  if (medium === 0) { // Tape
    recipe = { sine: 0.0025, sineHz: 0.7, rand: 0.0012, randHz: 6, lossHz: 0,
      lossTracksTime: false, bits: 0, decimateHz: 0, hiss: 0.0003, tidePartials: false };
    loss = 9000;
  } else if (medium === 1) { // Oil can
    recipe = { sine: 0.005, sineHz: 2.3, rand: 0.015, randHz: 1.4, lossHz: 0,
      lossTracksTime: false, bits: 10, decimateHz: 0, hiss: 0.0004, tidePartials: false };
    loss = 2600;
  } else if (medium === 2) { // Bucket
    recipe = { sine: 0, sineHz: 0, rand: 0.0008, randHz: 20, lossHz: 0,
      lossTracksTime: true, bits: 0, decimateHz: 0, hiss: 0.0005, tidePartials: false };
    loss = 8000;
  } else { // Tide
    recipe = { sine: 0.006, sineHz: 0.3, rand: 0, randHz: 0, lossHz: 0,
      lossTracksTime: false, bits: 0, decimateHz: 0, hiss: 0, tidePartials: true };
    loss = openHighCutHz;
  }
  recipe.sine *= amt;
  recipe.rand *= amt;
  recipe.hiss *= Math.min(4, 0.9 * amt);
  recipe.bits = amt > 0.1 ? recipe.bits : 0;
  recipe.lossHz = loss + (openHighCutHz - loss) * (1 - Math.min(1, amt * 2));
  return recipe;
}

/** How long the repeats last, in ms. A tap mode adds the tap's own time to the last
 * repeat of the line it reads. */
export function tailMs(mode, left, right, repeats, hold) {
  if (hold) return holdTailMs;
  const l = clamp(finiteOr(left, minTimeMs), minTimeMs, maxTimeMs);
  const r = clamp(finiteOr(right, minTimeMs), minTimeMs, maxTimeMs);
  const count = clamp(Math.trunc(repeats), 1, 64);
  if (mode === 2) return count * l + r; // Right is a tap on the left line
  if (mode === 3) return count * r + l; // Left is a tap on the right line
  return count * Math.max(l, r);
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

/** Wobble depth is relative to the delay time, but only between 40 and 400 ms. */
export function wobbleReferenceMs(timeMs) {
  return clamp(finiteOr(timeMs, minTimeMs), 40, 400);
}

/** Bucket loses more of the top the longer the line is. */
export function bucketLossHz(lossHz, timeMs) {
  const loss = finiteOr(lossHz, openHighCutHz);
  const time = Math.max(minTimeMs, finiteOr(timeMs, minTimeMs));
  // The floor never lifts the cut above the medium's own loss.
  return Math.min(loss, Math.max(1200, loss * Math.sqrt(60 / time)));
}
