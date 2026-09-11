// The bridge (DESIGN §4). Text over postMessage in both directions; the face never
// talks to the plugin itself, and the plugin never knows how the face is drawn.
//
//   UI  -> plugin : ready | begin:<id> | value:<id>:<v> | end:<id> | visual
//   plugin -> UI  : parameter<TAB>… lines, metadata-end, values:<id>=<v>;…,
//                   visual:<inL>,<inR>,<wetL>,<wetR>,<leftMs>,<rightMs>,<hold>,<bpm>

import './compost/components/compost-window.js';
import './face.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const send = text => parent.postMessage(encoder.encode(text).buffer, '*');

const face = document.createElement('slide-face');
const specs = [];
// mote's data-editing idea: a value arriving from the plugin must not fight the
// gesture the user is in the middle of.
const editing = new Set();

// The prototype's mobile branch: a compost-window frames the face on desktop, and it
// stacks as a plain panel on phones.
const MOBILE = innerWidth < 700 || (matchMedia('(pointer:coarse)').matches && innerWidth < 900);
const host = document.querySelector('#face-host');

if (MOBILE) {
  const panel = document.createElement('section');
  panel.className = 'mobile-panel';
  const body = document.createElement('div');
  body.className = 'body';
  body.style.setProperty('--h', `${Math.round(innerHeight * 0.78)}px`);
  body.append(face);
  panel.append(body);
  host.append(panel);
} else {
  const frame = document.createElement('compost-window');
  frame.setAttribute('heading', 'Slide');
  frame.setAttribute('static', '');
  frame.setAttribute('open', '');
  const holder = document.createElement('div');
  holder.className = 'full';
  holder.append(face);
  frame.append(holder);
  host.append(frame);
  const fit = () => {
    const pad = 20, top = 16;
    frame.setAttribute('width', String(Math.max(320, innerWidth - pad * 2)));
    frame.setAttribute('height', String(Math.max(240, innerHeight - top * 2 - 26)));
    frame.moveTo?.(pad, top);
  };
  fit();
  addEventListener('resize', fit);
}

// ---- UI -> plugin -----------------------------------------------------------

addEventListener('parameter-begin', ({detail}) => {
  editing.add(String(detail.parameterID));
  send(`begin:${detail.parameterID}`);
});
addEventListener('parameter-edit', ({detail}) => {
  send(`value:${detail.parameterID}:${detail.value}`);
});
addEventListener('parameter-end', ({detail}) => {
  // Escape cancels a gesture: compost resets the control and reports the start
  // value only here, so resend it or the plugin keeps the last dragged value.
  if (detail.cancelled) send(`value:${detail.parameterID}:${detail.value}`);
  editing.delete(String(detail.parameterID));
  send(`end:${detail.parameterID}`);
});

// ---- plugin -> UI -----------------------------------------------------------

addEventListener('message', ({data, source}) => {
  // The native CHOC bridge dispatches a MessageEvent without a source window.
  if (source !== parent && !(source === null && parent === window)) return;
  if (!(data instanceof ArrayBuffer) && !ArrayBuffer.isView(data)) return;
  const text = decoder.decode(data);

  if (text.startsWith('parameter\t')) {
    const [, id, identifier, name, unit, min, max, initial, step, digits, mid, curve, options]
      = text.split('\t');
    const numbers = {min: Number(min), max: Number(max), initial: Number(initial),
      step: Number(step), mid: Number(mid), digits: Number(digits)};
    specs.push({
      id: Number(id), identifier, name, unit, curve,
      options: options ? options.split('|') : [],
      hasMid: numbers.mid > numbers.min && numbers.mid < numbers.max,
      ...numbers
    });
    return;
  }
  if (text === 'metadata-end') { face.setMetadata(specs); return; }
  if (text.startsWith('values:')) {
    for (const pair of text.slice(7).split(';')) {
      if (!pair) continue;
      const [id, value] = pair.split('=');
      if (editing.has(id)) continue;
      face.setValue(id, Number(value));
    }
    return;
  }
  if (text.startsWith('visual:')) {
    const [inL, inR, wetL, wetR, leftMs, rightMs, hold, bpm] =
      text.slice(7).split(',').map(Number);
    face.setTelemetry({inL, inR, wetL, wetR, leftMs, rightMs, hold, bpm});
  }
});

// D9: telemetry is pulled on the animation clock, and only while the face is visible.
const pull = () => {
  requestAnimationFrame(pull);
  if (!document.hidden) send('visual');
};
requestAnimationFrame(pull);

send('ready');
