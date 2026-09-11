// The fake plug-in behind ui/index.html: the metadata handshake, the value echo,
// the telemetry frames, and prototype/slide-engine.js in an AudioWorklet so the
// face can be judged against prototype/slide.html with sound.

import {metadataLines, initialValues, byIdentifier} from './parameters.js';
import {ENGINE_SRC} from '/prototype/slide-engine.js';
import * as laws from '/ui/laws.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const toUI = text => postMessage(encoder.encode(text).buffer, '*');
const values = initialValues();
const id = name => byIdentifier[name].id;
const val = name => values[id(name)];

// ---- mount the real page ----------------------------------------------------

const html = await (await fetch('/ui/index.html')).text();
const parsed = new DOMParser().parseFromString(html, 'text/html');
for (const node of parsed.head.querySelectorAll('link[rel="stylesheet"], style'))
  document.head.append(node.cloneNode(true));
for (const node of parsed.body.children) {
  if (node.tagName === 'SCRIPT') continue;
  document.body.insertBefore(node.cloneNode(true), document.getElementById('dev-bar'));
}

addEventListener('message', ({data}) => {
  if (!(data instanceof ArrayBuffer)) return;
  const text = decoder.decode(data);
  if (text === 'ready') {
    for (const line of metadataLines()) toUI(line);
    toUI('metadata-end');
    sendValues();
    return;
  }
  if (text === 'visual') {
    const {left, right} = times();
    toUI(`visual:${level('in')},${level('in')},${level('out')},${level('out')},`
      + `${left},${right},${val('hold')},120`);
    return;
  }
  const edit = /^value:(\d+):(-?[\d.eE+]+)$/.exec(text);
  if (edit) {
    values[Number(edit[1])] = Number(edit[2]);
    applyAudio();
    return;
  }
  if (text.startsWith('end:')) sendValues();
});

const sendValues = () =>
  toUI(`values:${Object.entries(values).map(([k, v]) => `${k}=${v}`).join(';')};`);

// The plug-in derives Right and the synced times; the harness does the same so the
// echoed values are what the face would read back from a real host.
function times() {
  const bpm = 120;
  const sync = val('sync') >= 0.5;
  const left = sync ? laws.divisionMs(val('left_division'), bpm) : val('left');
  const linked = laws.linkRight(val('link'), left, val('ratio'), val('difference'), val('right'));
  const right = sync
    ? (val('link') === 2 ? laws.divisionMs(val('right_division'), bpm) : nearest(linked, bpm))
    : linked;
  return {left, right};
}
function nearest(ms, bpm) {
  let best = null;
  for (let i = 0; i < 24; i++) {
    const t = laws.divisionMs(i, bpm);
    const e = Math.abs(Math.log(t / ms));
    if (!best || e < best.e) best = {e, t};
  }
  return best.t;
}

// ---- audio ------------------------------------------------------------------

let A = null, building = null;
const level = which => {
  if (!A) return 0;
  const node = which === 'in' ? A.anIn : A.anOut;
  node.getFloatTimeDomainData(A.buf);
  let m = 0;
  for (const v of A.buf) if (Math.abs(v) > m) m = Math.abs(v);
  return Number(m.toFixed(4));
};

function buildAudio() {
  if (building) return building;
  building = (async () => {
    const ctx = new AudioContext();
    await ctx.audioWorklet.addModule(
      URL.createObjectURL(new Blob([ENGINE_SRC], {type: 'application/javascript'})));
    const src = ctx.createGain();
    const merge = ctx.createChannelMerger(2);
    src.connect(merge, 0, 0); src.connect(merge, 0, 1);
    const eng = new AudioWorkletNode(ctx, 'tide-engine', {numberOfInputs: 1,
      numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2,
      channelCountMode: 'explicit', channelInterpretation: 'discrete'});
    const lim = ctx.createDynamicsCompressor();
    lim.threshold.value = -6; lim.knee.value = 6; lim.ratio.value = 20;
    lim.attack.value = 0.002; lim.release.value = 0.15;
    merge.connect(eng).connect(lim).connect(ctx.destination);
    const anIn = ctx.createAnalyser(), anOut = ctx.createAnalyser();
    anIn.fftSize = anOut.fftSize = 1024;
    src.connect(anIn); eng.connect(anOut);
    A = {ctx, src, eng, anIn, anOut, buf: new Float32Array(1024)};
    applyAudio();
  })();
  return building;
}

// The new parameter set mapped onto the worklet's message shape, exactly as
// prototype/slide.html's applyAudio does, but through ui/laws.js.
function applyAudio() {
  if (!A) return;
  const {left, right} = times();
  const hold = val('hold') >= 0.5;
  const repeats = Math.round(val('repeats'));
  const shape = val('shape');
  const tone = laws.toneLaw(val('blur') / 100, val('tone') / 100, hold);
  const recipe = laws.recipeAt(Math.round(val('medium')), val('wear') / 100);
  const mode = Math.round(val('mode'));
  const gains = [];
  for (let k = 0; k < repeats; k++) gains.push(laws.gainAt(shape, repeats, k));
  A.eng.port.postMessage({
    tl: left, tr: right,
    fb: laws.isLoop(shape, hold) ? Math.min(0.99, laws.lapGain(shape, repeats, hold)) : 0.5,
    bleed: mode === 1 ? 1 : 0.15,
    inL: 1, inR: mode === 1 ? 0 : 1,
    tone: tone.highCutHz, hpf: tone.lowCutHz,
    early: tone.early / 0.62, diff: tone.diffusion / 0.62,
    mix: val('mix') / 100,
    shape: laws.isLoop(shape, hold) ? 'exp' : 'flat',
    gains,
    tap: mode === 2 ? 'R' : mode === 3 ? 'L' : false,
    hold, autoHold: false,
    wob: {sine: recipe.sine, sineHz: recipe.sineHz, rand: recipe.rand,
      randHz: recipe.randHz, tide: recipe.tidePartials, lp: recipe.lossHz,
      lpTime: recipe.lossTracksTime, bits: recipe.bits, hiss: recipe.hiss}
  });
}

async function note(freq = 220, vel = 0.8) {
  if (!A) await buildAudio();
  if (A.ctx.state !== 'running') await A.ctx.resume();
  const ctx = A.ctx, t = ctx.currentTime;
  const o = ctx.createOscillator(), o2 = ctx.createOscillator();
  o.type = o2.type = 'sawtooth';
  o.frequency.value = freq; o2.frequency.value = freq * 1.004;
  const f = ctx.createBiquadFilter(); f.type = 'lowpass';
  const e = ctx.createGain();
  o.connect(f); o2.connect(f); f.connect(e).connect(A.src);
  f.frequency.setValueAtTime(freq * (4 + 12 * vel), t);
  f.frequency.exponentialRampToValueAtTime(freq * 1.5, t + 0.12);
  e.gain.setValueAtTime(0.0001, t);
  e.gain.exponentialRampToValueAtTime(0.05 + 0.6 * vel * vel, t + 0.004);
  e.gain.exponentialRampToValueAtTime(0.0001, t + 0.16);
  o.start(t); o2.start(t); o.stop(t + 0.2); o2.stop(t + 0.2);
}

// ---- dev bar ----------------------------------------------------------------

const root = document.documentElement;
const scheme = document.getElementById('dev-scheme');
const setScheme = dark => {
  root.dataset.colorScheme = dark ? 'dark' : 'light';
  scheme.textContent = dark ? 'light mode' : 'dark mode';
};
setScheme(matchMedia('(prefers-color-scheme: dark)').matches);
scheme.addEventListener('click', () => setScheme(root.dataset.colorScheme !== 'dark'));
document.getElementById('dev-play').addEventListener('click', () => note());
let phrase = null;
document.getElementById('dev-phrase').addEventListener('click', async () => {
  if (phrase) { clearInterval(phrase); phrase = null; return; }
  await buildAudio();
  const seq = [220, 0, 220, 330, 0, 247, 220, 196, 0, 247, 330, 0];
  let i = 0;
  phrase = setInterval(() => { const f = seq[i++ % seq.length]; if (f) note(f, 0.8); }, 250);
});

// The real page's own module last, so the import map and the bridge are in place.
const module = document.createElement('script');
module.type = 'module';
module.src = '/ui/main.js';
document.body.append(module);
