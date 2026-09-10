import './compost/components/compost-knob.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const send = text => window.parent.postMessage(encoder.encode(text).buffer, '*');

const field = document.querySelector('#field');
const context = field.getContext('2d');
const wake = document.createElement('canvas');
const wakeContext = wake.getContext('2d');
const readout = document.querySelector('#readout');
const hint = document.querySelector('#hint');
const controls = document.querySelector('#controls');
const syncButton = document.querySelector('#sync');
const modeButton = document.querySelector('#mode');
const freezeButton = document.querySelector('#freeze');
const clearButton = document.querySelector('#clear');

// Presentation order only. Every control is built from the plug-in's parameter
// metadata, so ranges, units and names are defined once, in C++.
const groups = [
  { label: 'delay', identifiers: ['time', 'division', 'offset', 'spread', 'feedback', 'cross'] },
  { label: 'repeat', identifiers: ['early', 'diffuse', 'damping', 'lowcut', 'drive', 'pitch', 'stretch'] },
  { label: 'space', identifiers: ['rate', 'depth', 'shape', 'drift', 'mix', 'width'] }
];

const specs = new Map();
const specsById = new Map();
const values = new Map();
const knobs = new Map();

// Eased display state, fed by the plug-in's telemetry.
const view = { left: 350, right: 525, phase: 0, loopGain: 0.45, peakLeft: 0, peakRight: 0 };
let telemetry = { left: 350, right: 525, modulation: 0, phase: 0, loopGain: 0.45, peakLeft: 0, peakRight: 0, tempo: 120 };
let lastFrame = 0;

const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

function value(identifier) {
  return values.get(identifier) ?? specs.get(identifier)?.initial ?? 0;
}

function buildControls() {
  controls.replaceChildren();
  knobs.clear();
  for (const group of groups) {
    const row = document.createElement('section');
    row.className = 'row';
    row.style.gridTemplateColumns = `repeat(${group.identifiers.length}, minmax(0, 1fr))`;
    row.style.setProperty('--row-knobs', String(group.identifiers.length));
    const heading = document.createElement('h2');
    heading.className = 'group';
    heading.textContent = group.label;
    row.append(heading);
    for (const identifier of group.identifiers) {
      const spec = specs.get(identifier);
      if (!spec) continue;
      const knob = document.createElement('compost-knob');
      knob.setAttribute('parameter-id', spec.id);
      knob.setAttribute('label', spec.name);
      knob.setAttribute('min', spec.min);
      knob.setAttribute('max', spec.max);
      knob.setAttribute('value', value(identifier));
      knob.setAttribute('reset-value', spec.initial);
      if (spec.step > 0) knob.setAttribute('step', spec.step);
      if (spec.digits >= 0) knob.setAttribute('display-fraction-digits', spec.digits);
      if (spec.unit) knob.setAttribute('unit', spec.unit);
      if (spec.mid) {
        knob.setAttribute('mid', spec.mid);
        knob.setAttribute('curve', spec.curve);
      }
      if (spec.options) knob.setAttribute('text', spec.options);
      if (spec.description) knob.setAttribute('title', spec.description);
      knobs.set(identifier, knob);
      row.append(knob);
    }
    controls.append(row);
  }
  updateControls();
}

// Free time and host sync are two ways to set the same delay, so the control
// that is not in charge steps back instead of lying about the delay.
function updateControls() {
  const synced = value('sync') >= 0.5;
  const warp = value('stretchmode') < 0.5;
  const frozen = value('freeze') >= 0.5;
  const time = knobs.get('time');
  const division = knobs.get('division');
  if (time) time.disabled = synced;
  if (division) division.disabled = !synced;
  syncButton.setAttribute('aria-pressed', String(synced));
  syncButton.textContent = synced ? 'sync' : 'free';
  modeButton.setAttribute('aria-pressed', String(!warp));
  modeButton.textContent = warp ? 'warp' : 'stretch';
  freezeButton.setAttribute('aria-pressed', String(frozen));
}

function updateReadout() {
  const synced = value('sync') >= 0.5;
  const names = specs.get('division')?.options?.split('|') ?? [];
  const name = names[Math.round(value('division'))] ?? '';
  const left = Math.round(view.left);
  const right = Math.round(view.right);
  readout.textContent = synced
    ? `${name} · ${Math.round(telemetry.tempo)} bpm · ${left}/${right} ms`
    : `${left} / ${right} ms`;
}

function sendParameter(id, next) {
  send(`begin:${id}`);
  send(`value:${id}:${next}`);
  send(`end:${id}`);
}

syncButton.addEventListener('click', () => {
  sendParameter(specs.get('sync').id, value('sync') >= 0.5 ? 0 : 1);
});

modeButton.addEventListener('click', () => {
  sendParameter(specs.get('stretchmode').id, value('stretchmode') >= 0.5 ? 0 : 1);
});

freezeButton.addEventListener('click', () => {
  sendParameter(specs.get('freeze').id, value('freeze') >= 0.5 ? 0 : 1);
});

clearButton.addEventListener('click', () => send('clear'));

addEventListener('parameter-begin', ({ detail }) => send(`begin:${detail.parameterID}`));
addEventListener('parameter-edit', ({ detail }) => send(`value:${detail.parameterID}:${detail.value}`));
addEventListener('parameter-end', ({ detail }) => {
  // Escape cancels a gesture: compost resets the control and only reports it
  // here, so the value has to be resent or the plug-in keeps the dragged one.
  if (detail.cancelled) send(`value:${detail.parameterID}:${detail.value}`);
  send(`end:${detail.parameterID}`);
});

addEventListener('message', ({ data }) => {
  if (!(data instanceof ArrayBuffer) && !ArrayBuffer.isView(data)) return;
  const text = decoder.decode(data);

  if (text.startsWith('parameter\t')) {
    const [, id, identifier, name, unit, min, max, initial, step, digits, mid, curve, options, description] = text.split('\t');
    const spec = {
      id, identifier, name, unit, description,
      min: Number(min), max: Number(max), initial: Number(initial),
      step: Number(step), digits: Number(digits), mid: Number(mid), curve, options
    };
    specs.set(identifier, spec);
    specsById.set(id, spec);
    return;
  }

  if (text === 'metadata-end') {
    greeted = true;
    hint.textContent = '';
    buildControls();
    updateReadout();
    return;
  }

  if (text.startsWith('values:')) {
    for (const pair of text.slice(7).split(';')) {
      const [id, raw] = pair.split('=');
      const spec = specsById.get(id);
      const next = Number(raw);
      if (!spec || !Number.isFinite(next)) continue;
      values.set(spec.identifier, next);
      const knob = knobs.get(spec.identifier);
      if (knob && knob.value !== next) knob.value = next;
    }
    updateControls();
    updateReadout();
    return;
  }

  if (text.startsWith('visual:')) {
    const parsed = text.slice(7).split(',').map(Number);
    if (parsed.length !== 8 || !parsed.every(Number.isFinite)) return;
    telemetry = {
      left: parsed[0], right: parsed[1], modulation: parsed[2], phase: parsed[3],
      loopGain: parsed[4], peakLeft: parsed[5], peakRight: parsed[6], tempo: parsed[7]
    };
    updateReadout();
  }
});

// A host may not be ready to deliver messages from the plug-in until its GUI
// lifecycle has settled, so keep asking until the metadata arrives. Once the
// metadata is through, the controls exist and the handshake stops.
let greeted = false;
const handshake = setInterval(() => {
  if (greeted) return clearInterval(handshake);
  send('ready');
}, 150);
send('ready');
setInterval(() => { if (!document.hidden) send('visual'); }, 40);

function shapeValue(shape, phase) {
  if (shape === 1) return 1 - 4 * Math.abs(phase - 0.5);
  if (shape === 2) return 1 - 2 * phase;
  if (shape === 3) {
    const position = phase * 12;
    const index = Math.floor(position);
    const blend = 0.5 - 0.5 * Math.cos(Math.PI * (position - index));
    const from = randomStep(index);
    const to = randomStep(index + 1);
    return from + (to - from) * blend;
  }
  return Math.sin(phase * Math.PI * 2);
}

// The random shape is smooth sample and hold; the curve shows its character
// rather than the exact sequence the audio rate generator is walking.
function randomStep(index) {
  const noise = Math.sin(index * 12.9898 + 0.31) * 43758.5453;
  return (noise - Math.floor(noise)) * 2 - 1;
}

// The same soft clip the engine puts in the feedback path, so a driven delay
// draws a flattened repeat envelope instead of pretending it is clean.
function softClip(value, gain) {
  const scaled = value * gain;
  const magnitude = Math.abs(scaled);
  const clipped = magnitude <= 1 ? scaled : Math.sign(scaled) * (1 + Math.tanh(magnitude - 1));
  return clipped / gain;
}

function layout(width, height) {
  const margin = 16;
  const combTop = 6;
  const combBottom = Math.round(height * 0.72);
  const waterline = Math.round(combTop + (combBottom - combTop) * 0.44);
  const legend = height - 12;
  // Repeats are drawn inside the band, so their length follows the space the
  // window actually gives them. The lower half keeps room for the markers.
  const reachUp = Math.max(6, waterline - combTop - 4);
  const reachDown = Math.max(6, combBottom - waterline - 13);
  const left = Math.max(1, view.left);
  const right = Math.max(1, view.right);
  const start = Math.min(left, right) * 0.7;
  const end = Math.max(left, right) * 18;
  const span = Math.log(end / start);
  const x = time => margin + (width - 2 * margin) * clamp(Math.log(Math.max(time, start) / start) / span, 0, 1);
  const timeAt = position => start * Math.exp(span * clamp((position - margin) / (width - 2 * margin), 0, 1));
  return { width, margin, combTop, combBottom, waterline, legend, x, timeAt, left, right, reachUp, reachDown, start, end };
}

// The waterline is the modulation: each point sits where the LFO was when the
// material from that delay time entered the line, so the repeats ride on the
// shape that moved them there.
function waterlineAt(geometry, position) {
  const depth = clamp(value('depth') / 100, 0, 1);
  if (depth <= 0.001) return geometry.waterline;
  const rate = Math.max(0.01, value('rate'));
  const cyclesBack = geometry.timeAt(position) * 0.001 * rate;
  // Stay inside the band, so the curve never collides with the rules.
  const reach = Math.max(4, Math.min(geometry.waterline - geometry.combTop,
    geometry.combBottom - geometry.waterline) - 5);
  return geometry.waterline
    - shapeValue(Math.round(value('shape')), view.phase - cyclesBack) * depth * reach * 0.9;
}

// Repeats: one comb per channel, rising for the left and falling for the right.
// Height and brightness carry the feedback, the smearing carries diffusion, and
// the side a repeat is drawn on carries the cross feed.
function drawRepeats(target, geometry) {
  const { x, width } = geometry;
  const gain = clamp(view.loopGain, 0, 1);
  const drive = 1 + value('drive') * 0.04;
  const tone = (1 - value('damping') / 100 * 0.35) * (1 - value('lowcut') / 100 * 0.2);
  const early = value('early') / 100;
  const perRepeat = value('diffuse') / 100;
  const cross = clamp(value('cross') / 100, 0, 1);
  const mix = clamp(value('mix') / 100, 0, 1);
  const widthScale = value('width') / 100;

  const channels = [
    { time: geometry.left, energy: clamp(0.34 + view.peakLeft * 2.2, 0, 1), direction: -1, reach: geometry.reachUp * (0.6 + 0.4 * widthScale) },
    { time: geometry.right, energy: clamp(0.34 + view.peakRight * 2.2, 0, 1), direction: 1, reach: geometry.reachDown * (0.6 + 0.4 * widthScale) }
  ];

  target.lineWidth = 1;
  for (let channel = 0; channel < channels.length; ++channel) {
    const own = channels[channel];
    const other = channels[1 - channel];
    for (let repeat = 1; repeat <= 28; ++repeat) {
      const level = repeat === 1 ? 1 : Math.pow(gain, repeat - 1);
      if (level < 0.015) break;
      const position = x(repeat * own.time);
      if (position > width - geometry.margin) break;

      // Cross rotates the two channels, so a repeat's energy is split between
      // its own side and the other one: at a full cross the repeats alternate.
      const ownWeight = Math.pow(Math.cos(cross * Math.PI * 0.5), 2 * (repeat - 1));
      const spread = Math.min(1.6, early + perRepeat * (repeat - 1));
      const drawn = softClip(level, drive);
      const base = geometry.waterline + 0;
      for (const [side, weight] of [[own, ownWeight], [other, 1 - ownWeight]]) {
        if (weight < 0.02) continue;
        const energy = 0.42 + 0.58 * side.energy;
        const length = side.reach * (0.3 + 0.7 * Math.sqrt(drawn)) * energy;
        const alpha = (0.3 + 0.7 * Math.pow(drawn, 0.45)) * (0.38 + 0.62 * side.energy) * 0.9
          * weight * tone * (0.5 + 0.5 * mix);
        if (alpha < 0.012) continue;
        const pixel = Math.round(position) + 0.5;
        // Diffusion smears a repeat across the time axis, and later repeats
        // carry more of it.
        const blur = Math.round(spread * 7);
        target.strokeStyle = `rgba(233,233,228,${(alpha / (1 + blur * 0.5)).toFixed(3)})`;
        for (let offset = -blur; offset <= blur; ++offset) {
          if (blur > 0 && Math.abs(offset) > 1 && Math.abs(offset) % 2 !== 0) continue;
          const px = pixel + offset;
          const top = waterlineAt(geometry, px);
          target.beginPath();
          target.moveTo(px, top);
          target.lineTo(px, top + side.direction * length);
          target.stroke();
        }
      }
      void base;
    }
  }

  // The dry signal sits at the left edge: its brightness is what is left of the
  // mix, so the balance between it and the combs reads as dry to wet.
  if (mix < 0.999) {
    const dryAlpha = (1 - mix) * 0.7;
    const px = Math.round(geometry.margin) + 0.5;
    target.strokeStyle = `rgba(233,233,228,${dryAlpha.toFixed(3)})`;
    target.beginPath();
    target.moveTo(px, geometry.waterline - geometry.reachUp * 0.6);
    target.lineTo(px, geometry.waterline + geometry.reachDown * 0.6);
    target.stroke();
  }
}

function drawChrome(target, geometry) {
  const { width, margin, combTop, combBottom, waterline, legend, x } = geometry;
  const frozen = value('freeze') >= 0.5;

  target.lineWidth = 1;
  target.strokeStyle = frozen ? 'rgba(255,255,255,0.30)' : 'rgba(255,255,255,0.09)';
  hairline(target, margin, combTop, width - margin, combTop);
  hairline(target, margin, combBottom, width - margin, combBottom);

  // The modulation curve itself, drawn under the repeats.
  target.strokeStyle = 'rgba(255,255,255,0.22)';
  target.beginPath();
  for (let px = margin; px <= width - margin; ++px) {
    const y = waterlineAt(geometry, px);
    if (px === margin) target.moveTo(px, y);
    else target.lineTo(px, y);
  }
  target.stroke();

  target.font = '8px ui-monospace, SFMono-Regular, Menlo, monospace';
  target.fillStyle = 'rgba(255,255,255,0.30)';
  target.textAlign = 'center';
  // Where each channel's first repeat lands.
  for (const [name, time] of [['L', geometry.left], ['R', geometry.right]]) {
    const position = Math.round(x(time)) + 0.5;
    target.strokeStyle = 'rgba(255,255,255,0.14)';
    target.beginPath();
    target.moveTo(position, combBottom - 10);
    target.lineTo(position, combBottom);
    target.stroke();
    target.fillText(name, position, legend - 1);
  }

  // Legend: what the movement is doing on the left, what the delay is set to on
  // the right.
  const names = ['sine', 'triangle', 'ramp', 'random'];
  const shape = Math.round(value('shape'));
  const rate = value('rate');
  const depth = Math.round(value('depth'));
  target.fillStyle = 'rgba(255,255,255,0.34)';
  target.textAlign = 'left';
  const hold = value('stretch') < 0.995 || value('stretch') > 1.005
    ? ` · ${value('stretch').toFixed(2)}x ${value('stretchmode') >= 0.5 ? 'stretch' : 'warp'}`
    : '';
  const pitch = Math.round(value('pitch'));
  const shift = pitch === 0 ? '' : ` · ${pitch > 0 ? '+' : ''}${pitch} st`;
  target.fillText(`${names[shape] ?? ''} ${depth}% · ${rate.toFixed(2)} Hz${shift}${hold}`, margin, legend - 1);

  const divisions = specs.get('division')?.options?.split('|') ?? [];
  const label = value('sync') >= 0.5
    ? `${divisions[Math.round(value('division'))] ?? ''} · ${Math.round(telemetry.tempo)} bpm`
    : `${Math.round(value('time') * 10) / 10} ms`;
  target.textAlign = 'right';
  target.fillText(label, width - margin, legend - 1);
}

function hairline(target, x0, y0, x1, y1) {
  target.beginPath();
  target.moveTo(x0, y0 + 0.5);
  target.lineTo(x1, y1 + 0.5);
  target.stroke();
}

function frame(time) {
  requestAnimationFrame(frame);
  const width = Math.max(1, Math.round(field.clientWidth));
  const height = Math.max(1, Math.round(field.clientHeight));
  const ratio = Math.min(2, window.devicePixelRatio || 1);
  if (field.width !== Math.round(width * ratio) || field.height !== Math.round(height * ratio)) {
    field.width = wake.width = Math.round(width * ratio);
    field.height = wake.height = Math.round(height * ratio);
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    wakeContext.setTransform(ratio, 0, 0, ratio, 0, 0);
  }

  const elapsed = lastFrame ? clamp((time - lastFrame) / 1000, 0, 0.1) : 0;
  lastFrame = time;
  const easing = 1 - Math.exp(-elapsed * 16);
  view.left += (telemetry.left - view.left) * easing;
  view.right += (telemetry.right - view.right) * easing;
  view.loopGain += (telemetry.loopGain - view.loopGain) * easing;
  view.peakLeft += (Math.min(1, telemetry.peakLeft) - view.peakLeft) * easing;
  view.peakRight += (Math.min(1, telemetry.peakRight) - view.peakRight) * easing;
  let delta = telemetry.phase - view.phase;
  if (delta < -0.5) delta += 1;
  else if (delta > 0.5) delta -= 1;
  view.phase = (view.phase + delta * Math.min(1, elapsed * 12) + 1) % 1;

  const geometry = layout(width, height);
  // The wake layer fades a little every frame, so repeats that move with the
  // modulation trail off like ripples. Chrome is drawn crisp on top of it.
  wakeContext.globalCompositeOperation = 'destination-out';
  wakeContext.fillStyle = 'rgba(0, 0, 0, 0.42)';
  wakeContext.fillRect(0, 0, width, height);
  wakeContext.globalCompositeOperation = 'source-over';
  wakeContext.globalAlpha = 0.5;
  drawRepeats(wakeContext, geometry);
  wakeContext.globalAlpha = 1;

  context.clearRect(0, 0, width, height);
  context.drawImage(wake, 0, 0, width, height);
  drawChrome(context, geometry);
}

requestAnimationFrame(frame);
