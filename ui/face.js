// <slide-face> — the whole face: picture, rails, chips, telemetry, on one canvas.
//
// Ported from prototype/slide.html, which is the behavioural spec. Every number the
// face needs about a parameter — range, step, curve, midpoint, option words —
// arrives through setMetadata (D10); nothing about the parameter set is written down
// here. Continuous parameters are compost value controls sharing the canvas as their
// event target (D11), so keyboard, ARIA and the gesture lifecycle come from compost
// and every change leaves as a normal compost parameter event. Sync and Hold are
// compost-button switches; Link, Mode and Medium are compost-button cycles, laid over
// the canvas top band and styled to look like the prototype's chips.

import './compost/components/compost-button.js';
import {createValueControl} from './compost/value-control.js';
import * as laws from './laws.js';

const MONO = '"IBM Plex Mono", ui-monospace, monospace';
const SANS = '"IBM Plex Sans", system-ui, sans-serif';

// Identifiers of the discrete parameters the top band owns as chips. `on` decides
// when the word is lit: Link is lit unless it is at its last option (Off), Mode
// unless it is at its first (Stereo), Medium never.
const CHIPS = [
  {id: 'link', mode: 'cycle', on: 'notMax', prefix: true},
  {id: 'mode', mode: 'cycle', on: 'notMin'},
  {id: 'sync', mode: 'switch', on: 'pressed'},
  {id: 'medium', mode: 'cycle', on: 'never'},
  {id: 'hold', mode: 'switch', on: 'pressed'}
];
const CHIP_IDS = CHIPS.map(chip => chip.id);

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const dB = a => 20 * Math.log10(Math.max(1e-6, a));
const fmtMs = v => v < 1000
  ? `${v < 10 ? v.toFixed(2) : v < 100 ? v.toFixed(1) : v.toFixed(0)} ms`
  : `${(v / 1000).toFixed(v < 10000 ? 2 : 1)} s`;
const rnd = i => ((i * 9301 + 49297) % 233280) / 233280 * 2 - 1;

const ratioName = r =>
  Math.abs(r - 1.6180339887) < 0.002 ? 'φ' : Math.abs(r - 2 / 3) < 0.002 ? '2:3'
  : Math.abs(r - 4 / 3) < 0.002 ? '4:3' : Math.abs(r - 1.5) < 0.002 ? '3:2'
  : Math.abs(r - 1) < 0.002 ? '1:1' : Math.abs(r - 2) < 0.002 ? '2:1'
  : Math.abs(r - 0.5) < 0.002 ? '1:2' : `${r.toFixed(2)}×`;

// The two hand-drawn cursors: a smear for Blur, a curve for Shape.
const cur = svg => `url("data:image/svg+xml,${encodeURIComponent(svg)}") 12 12, ns-resize`;
const CUR = {
  wash: cur('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><g stroke="#111" fill="none" stroke-width="1.5"><line x1="12" y1="3" x2="12" y2="21"/><path d="M8 6l4-3 4 3M8 18l4 3 4-3"/><g opacity=".35" stroke-width="3"><line x1="4" y1="9" x2="20" y2="9"/><line x1="5" y1="12" x2="19" y2="12"/><line x1="4" y1="15" x2="20" y2="15"/></g></g></svg>'),
  shape: cur('<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"><g stroke="#111" fill="none" stroke-width="1.5"><line x1="12" y1="3" x2="12" y2="21"/><path d="M8 6l4-3 4 3M8 18l4 3 4-3"/><path d="M3 17q6 0 9-9t9-2" opacity=".6"/></g></svg>')
};

const STYLE = `
:host{ position:relative; display:block; height:100%; min-width:0;
  --face-mono:${MONO}; --face-sans:${SANS}; }
canvas{ position:absolute; inset:0; width:100%; height:100%; display:block; touch-action:none }
.chips{ position:absolute; inset:0; pointer-events:none }
compost-button{ position:absolute; pointer-events:auto; font-family:var(--face-mono);
  font-weight:500; font-size:11px; line-height:22px; --compost-button-width:100%;
  --compost-button-height:22px; }
compost-button::part(button){ width:100%; height:22px; border:0; background:none;
  padding:0; color:var(--dim); cursor:pointer; font:inherit; }
compost-button::part(label){ font-size:11px; padding:0 8px; line-height:22px;
  text-decoration:none; }
compost-button:hover::part(button){ color:var(--ink) }
compost-button:hover::part(label){ text-decoration:underline;
  text-decoration-color:var(--hair); text-decoration-thickness:1px;
  text-underline-offset:4px; }
compost-button[on]::part(button){ color:var(--ink) }
compost-button[on]::part(label), compost-button[on]:hover::part(label){
  text-decoration:underline; text-decoration-color:var(--ink);
  text-decoration-thickness:2px; text-underline-offset:4px; }
compost-button::part(button):focus-visible{ outline:1px solid var(--acc); outline-offset:1px }
.controls{ position:absolute; width:1px; height:1px; overflow:hidden; clip-path:inset(50%) }
.controls>div{ width:1px; height:1px }
`;

export class SlideFace extends HTMLElement {
  constructor() {
    super();
    const root = this.attachShadow({mode: 'open'});
    root.innerHTML = `<style>${STYLE}</style><canvas></canvas>
      <div class="chips"></div><div class="controls"></div>`;
    this.canvas = root.querySelector('canvas');
    this.g = this.canvas.getContext('2d');
    this.chipHost = root.querySelector('.chips');
    this.controlHost = root.querySelector('.controls');

    this.meta = new Map();      // identifier -> parameter spec
    this.metaByID = new Map();  // id -> spec
    this.controls = new Map();  // identifier -> compost value control
    this.buttons = new Map();   // identifier -> compost-button
    this.chipBoxes = new Map(); // identifier -> {x,y,w,h} in css px
    this.zones = [];            // canvas hit rectangles, device px
    this.geo = null;
    this.rect = null;
    this.hover = null;
    this.drag = null;
    this.focusZone = null;
    this.held = 0;              // the snap lock's held ratio
    this.theme = {};
    this.themeDirty = true;
    this.dirty = true;
    this.now = 0;
    this.level = {in: 0, out: 0};
    this.frame = {inL: 0, inR: 0, wetL: 0, wetR: 0, leftMs: 0, rightMs: 0, hold: 0, bpm: 120};

    this.onPointerMove = this.onPointerMove.bind(this);
    this.onPointerDown = this.onPointerDown.bind(this);
    this.onPointerUp = this.onPointerUp.bind(this);
    this.invalidate = this.invalidate.bind(this);
    this.markTheme = () => { this.themeDirty = true; this.invalidate(); };
  }

  connectedCallback() {
    const c = this.canvas;
    c.addEventListener('pointermove', this.onPointerMove);
    c.addEventListener('pointerdown', this.onPointerDown);
    c.addEventListener('pointerup', this.onPointerUp);
    c.addEventListener('pointercancel', this.onPointerUp);
    c.addEventListener('pointerleave', () => {
      if (!this.drag && this.hover) { this.hover = null; this.invalidate(); }
    });
    this.resizeObserver = new ResizeObserver(() => { this.rect = null; this.invalidate(); });
    this.resizeObserver.observe(c);
    this.themeObserver = new MutationObserver(this.markTheme);
    this.themeObserver.observe(document.documentElement,
      {attributes: true, attributeFilter: ['data-color-scheme', 'class', 'style']});
    this.scheme = matchMedia('(prefers-color-scheme: dark)');
    this.scheme.addEventListener('change', this.markTheme);
    addEventListener('scroll', () => { this.rect = null; }, true);
    const tick = () => {
      this.raf = requestAnimationFrame(tick);
      this.now = performance.now() / 1000;
      this.easeLevels();
      if (this.dirty || this.animating()) { this.render(); this.dirty = false; }
    };
    this.raf = requestAnimationFrame(tick);
  }

  disconnectedCallback() {
    cancelAnimationFrame(this.raf);
    this.resizeObserver?.disconnect();
    this.themeObserver?.disconnect();
    this.scheme?.removeEventListener('change', this.markTheme);
    for (const control of this.controls.values()) control.dispose();
  }

  invalidate() { this.dirty = true; }

  // ---- metadata, values, telemetry -----------------------------------------

  /** The parameter table, as the plugin's handshake sent it (D10). */
  setMetadata(list) {
    this.meta.clear();
    this.metaByID.clear();
    for (const spec of list) {
      this.meta.set(spec.identifier, spec);
      this.metaByID.set(String(spec.id), spec);
    }
    this.buildControls();
    this.invalidate();
  }

  /** A value from the plugin: silent, no event back. */
  setValue(idOrIdentifier, value) {
    const spec = this.metaByID.get(String(idOrIdentifier)) || this.meta.get(idOrIdentifier);
    if (!spec || !Number.isFinite(value)) return;
    const control = this.controls.get(spec.identifier);
    if (control) control.setValue(value, false, 'bridge');
    const button = this.buttons.get(spec.identifier);
    if (button) button.setValue(value, false, 'bridge');
    this.invalidate();
  }

  /** One telemetry snapshot (D9), pulled by the bridge every animation frame. */
  setTelemetry(frame) {
    if (frame && Number.isFinite(frame.bpm)) this.frame = frame;
  }

  buildControls() {
    for (const control of this.controls.values()) control.dispose();
    this.controls.clear();
    this.buttons.clear();
    this.controlHost.replaceChildren();
    this.chipHost.replaceChildren();

    for (const spec of this.meta.values()) {
      if (CHIP_IDS.includes(spec.identifier)) continue;
      const element = document.createElement('div');
      this.controlHost.append(element);
      const control = createValueControl(element, {
        parameterID: String(spec.id),
        parameterKind: spec.step > 0 && spec.options.length ? 'discrete' : 'continuous',
        name: spec.name,
        label: spec.name,
        min: spec.min,
        max: spec.max,
        mid: spec.hasMid ? spec.mid : null,
        curve: spec.curve,
        step: spec.step,
        unit: spec.unit,
        value: spec.initial,
        resetValue: spec.initial,
        text: spec.options.length ? spec.options : '',
        displayFractionDigits: spec.digits,
        eventTarget: this.canvas,
        pointerTarget: null,
        draw: state => {
          if (state.focused) this.focusZone = spec.identifier;
          else if (this.focusZone === spec.identifier) this.focusZone = null;
          this.invalidate();
        }
      });
      this.controls.set(spec.identifier, control);
    }

    for (const chip of CHIPS) {
      const spec = this.meta.get(chip.id);
      if (!spec) continue;
      const button = document.createElement('compost-button');
      button.setAttribute('mode', chip.mode);
      button.setAttribute('parameter-id', String(spec.id));
      button.setAttribute('label', spec.name);
      button.setAttribute('aria-label', spec.name);
      if (chip.mode === 'switch') button.toggleAttribute('pressed', spec.initial >= 0.5);
      else {
        button.setAttribute('text', this.chipText(chip).join('|'));
        button.setAttribute('value', String(Math.round(spec.initial)));
      }
      button.addEventListener('change', () => this.chipChanged(chip));
      this.chipHost.append(button);
      this.buttons.set(chip.id, button);
    }
  }

  chipText(chip) {
    const spec = this.meta.get(chip.id);
    if (!spec) return [];
    // Link reads as the prototype's sentence; Medium reads "Clean" at Wear 0 (D6).
    if (chip.prefix) return spec.options.map(o => `${spec.name} ${o.toLowerCase()}`);
    if (chip.id === 'medium' && this.val('wear') === 0) return spec.options.map(() => 'Clean');
    return spec.options.slice();
  }

  // ---- reading and writing values ------------------------------------------

  val(identifier) {
    const control = this.controls.get(identifier);
    if (control) return control.value;
    const button = this.buttons.get(identifier);
    if (button) return button.value;
    return this.meta.get(identifier)?.initial ?? 0;
  }

  range(identifier) {
    const spec = this.meta.get(identifier);
    return spec ? [spec.min, spec.max] : [0, 1];
  }

  /** One complete edit: begin, value, end. */
  once(identifier, value) {
    const control = this.controls.get(identifier);
    const spec = this.meta.get(identifier);
    if (!control || !spec || !Number.isFinite(value)) return;
    const next = clamp(value, spec.min, spec.max);
    if (next === control.value) return;
    control.beginGesture('face');
    control.setValue(next, true, 'face');
    control.endGesture(false, 'face');
  }

  /** A chip that changes what another parameter means re-bases that parameter, so
   * the times on screen do not jump — the prototype's act(). */
  chipChanged(chip) {
    const {left, right} = this.lastTimes ?? this.times();
    if (chip.id === 'link') {
      this.once('ratio', left > 0 ? right / left : 1);
      this.once('difference', right - left);
    } else if (chip.id === 'sync' && this.sync()) {
      this.once('left_division', this.nearestDivision(left)?.index ?? 0);
      if (this.link() === 2) this.once('right_division', this.nearestDivision(right)?.index ?? 0);
    }
    this.invalidate();
  }

  beginEdit(identifier) { this.controls.get(identifier)?.beginGesture('face'); }
  endEdit(identifier) { this.controls.get(identifier)?.endGesture(false, 'face'); }

  setSwitch(identifier, on) {
    const button = this.buttons.get(identifier);
    if (button && button.pressed !== Boolean(on)) button.setValue(on ? 1 : 0, true, 'face');
  }

  // ---- the derived numbers the picture is drawn from -----------------------

  bpm() { return this.frame.bpm > 0 ? this.frame.bpm : 120; }

  divisions() {
    const spec = this.meta.get('left_division');
    if (!spec) return [];
    const bpm = this.bpm();
    return spec.options.map((name, index) => ({
      index,
      name,
      ms: laws.divisionMs(index, bpm),
      kind: name.endsWith('.') ? 'dot' : name.endsWith('T') ? 'trip' : 'plain'
    })).filter(d => d.ms >= laws.minTimeMs && d.ms <= this.range('left')[1]);
  }

  nearestDivision(ms) {
    const index = laws.nearestDivision(ms, this.bpm());
    const name = this.meta.get('left_division')?.options[index];
    return name == null ? null : {index, name, ms: laws.divisionMs(index, this.bpm())};
  }

  sync() { return this.val('sync') >= 0.5; }
  hold() { return this.val('hold') >= 0.5; }
  link() { return Math.round(this.val('link')); }
  mode() { return Math.round(this.val('mode')); }
  medium() { return Math.round(this.val('medium')); }
  repeats() { return Math.round(this.val('repeats')); }
  shape() { return this.val('shape'); }
  blur01() { const [, max] = this.range('blur'); return this.val('blur') / max; }
  tone11() { const [, max] = this.range('tone'); return this.val('tone') / max; }
  mix01() { const [, max] = this.range('mix'); return this.val('mix') / max; }
  wear01() { const [, max] = this.range('wear'); return this.val('wear') / max; }

  /** Left and Right in ms, after Sync and Link. */
  times() {
    const [lo, hi] = this.range('left');
    const sync = this.sync();
    const bpm = this.bpm();
    const left = sync
      ? laws.divisionMs(Math.round(this.val('left_division')), bpm)
      : clamp(this.val('left'), lo, hi);
    const linked = laws.linkRight(this.link(), left, this.val('ratio'),
      this.val('difference'), this.val('right'));
    let right = linked;
    if (sync) {
      right = this.link() === 2
        ? laws.divisionMs(Math.round(this.val('right_division')), bpm)
        : (this.nearestDivision(linked)?.ms ?? linked);
    }
    return {left: clamp(left, lo, hi), right: clamp(right, lo, hi)};
  }

  tail() {
    const {left, right} = this.times();
    return laws.tailMs(this.mode(), left, right, this.repeats(), this.hold());
  }

  gains() {
    const n = this.repeats();
    const out = [];
    for (let k = 0; k < n; k++) out.push(laws.gainAt(this.shape(), n, k));
    return out;
  }

  /** Fraction of a decade of accumulated timing wobble at this instant. Wear 0 is
   * still on every medium (D6) and Digital never wobbles. */
  drift(n, side) {
    const wear = this.wear01();
    const amt = Math.pow(wear, 1.8) * 5;
    const medium = this.medium();
    if (amt <= 0 || medium === 4 || this.drag) return 0;
    const t = this.now, ph = side ? 1.57 : 0;
    let w = 0;
    if (medium === 0) w = 0.35 * Math.sin(2 * Math.PI * 0.7 * t + ph) + 0.15 * Math.sin(2 * Math.PI * 6.1 * t + n + ph);
    else if (medium === 1) w = 0.6 * Math.sin(2 * Math.PI * 2.3 * t + ph) + 0.9 * Math.sin(2 * Math.PI * 0.33 * t + rnd(n) * 3 + ph) * rnd(n * 7 + side);
    else if (medium === 2) w = 0.25 * rnd(Math.floor(t * 20) + n * 13 + side);
    else if (medium === 3) w = 0.5 * (Math.sin(2 * Math.PI * 0.3 * t + ph) + 0.6 * Math.sin(2 * Math.PI * 0.311 * t + 0.4) + 0.35 * Math.sin(2 * Math.PI * 0.155 * t + 1.9)) / 1.95;
    return w * amt * 0.035 * Math.sqrt(n);
  }

  /** Every repeat that sounds, as {t, side, a, n}. */
  repeatList() {
    const out = [];
    const G = this.gains(), N = G.length;
    const mode = this.mode(), hold = this.hold();
    const pp = mode === 1, tap = mode === 2 ? 'R' : mode === 3 ? 'L' : false;
    const {left: TL, right: TR} = this.times();
    const th = Math.asin(pp ? 1 : 0.15), sk = Math.cos(th), lk = Math.sin(th);
    const LIMIT = laws.holdTailMs;

    if (hold) {
      for (let k = 0; k < 400; k++) {
        const t = tap === 'L' ? k * TR + TL : (k + 1) * TL;
        if (t <= LIMIT) out.push({t, side: 0, a: Math.SQRT1_2, n: k + 1});
        const t2 = tap === 'R' ? k * TL + TR : (k + 1) * TR;
        if (t2 <= LIMIT) out.push({t: t2, side: 1, a: Math.SQRT1_2, n: k + 1});
        if (t > LIMIT && t2 > LIMIT) break;
      }
      return out;
    }
    if (tap === 'R') {
      for (let k = 0; k < N; k++) {
        out.push({t: (k + 1) * TL, side: 0, a: G[k], n: k + 1});
        out.push({t: k * TL + TR, side: 1, a: G[k], n: k + 1});
      }
      return out;
    }
    if (tap === 'L') {
      for (let k = 0; k < N; k++) {
        out.push({t: k * TR + TL, side: 0, a: G[k], n: k + 1});
        out.push({t: (k + 1) * TR, side: 1, a: G[k], n: k + 1});
      }
      return out;
    }
    const heap = [], key = new Map();
    const K = e => `${e.side}:${Math.round(Math.log10(e.t) * 300)}:${e.n}`;
    const push = e => {
      const k = K(e), ex = key.get(k);
      if (ex) { ex.a = Math.hypot(ex.a, e.a); return; }
      key.set(k, e); heap.push(e);
      let i = heap.length - 1;
      while (i > 0) {
        const p = (i - 1) >> 1;
        if (heap[p].t <= heap[i].t) break;
        [heap[p], heap[i]] = [heap[i], heap[p]]; i = p;
      }
    };
    const pop = () => {
      const top = heap[0], last = heap.pop();
      if (heap.length) {
        heap[0] = last;
        let i = 0;
        for (;;) {
          const l = 2 * i + 1, r = l + 1; let m = i;
          if (l < heap.length && heap[l].t < heap[m].t) m = l;
          if (r < heap.length && heap[r].t < heap[m].t) m = r;
          if (m === i) break;
          [heap[m], heap[i]] = [heap[i], heap[m]]; i = m;
        }
      }
      key.delete(K(top));
      return top;
    };
    push({t: TL, side: 0, a: Math.SQRT1_2, n: 1});
    push({t: TR, side: 1, a: Math.SQRT1_2, n: 1});
    while (heap.length && out.length < 4000) {
      const e = pop();
      if (e.t > LIMIT || e.n > N) continue;
      out.push({t: e.t, side: e.side, a: e.a * G[e.n - 1], n: e.n});
      if (e.n >= N) continue;
      push({t: e.t + [TL, TR][e.side], side: e.side, a: e.a * sk, n: e.n + 1});
      push({t: e.t + [TL, TR][1 - e.side], side: 1 - e.side, a: e.a * lk, n: e.n + 1});
    }
    return out;
  }

  easeLevels() {
    const f = this.frame;
    const i = Math.max(f.inL || 0, f.inR || 0), o = Math.max(f.wetL || 0, f.wetR || 0);
    const L = this.level;
    L.in = i > L.in ? i : L.in * 0.85;
    L.out = o > L.out ? o : L.out * 0.93;
    if (L.in < 0.001) L.in = 0;
    if (L.out < 0.001) L.out = 0;
  }

  animating() {
    return (this.wear01() > 0 && this.medium() !== 4) || this.level.in > 0 || this.level.out > 0;
  }

  // ---- layout ---------------------------------------------------------------

  readTheme() {
    if (!this.themeDirty) return;
    const s = getComputedStyle(this);
    for (const k of ['panel', 'band', 'ink', 'ink2', 'dim', 'hair', 'track', 'acc', 'bg'])
      this.theme[k] = s.getPropertyValue(`--${k}`).trim() || '#888';
    this.themeDirty = false;
  }

  layout() {
    const c = this.canvas;
    const r = this.rect || (this.rect = c.getBoundingClientRect());
    const dpr = Math.min(2, devicePixelRatio || 1);
    const W = Math.max(320, Math.round(r.width * dpr)), H = Math.max(240, Math.round(r.height * dpr));
    if (c.width !== W || c.height !== H) { c.width = W; c.height = H; }
    const narrow = W / dpr < 720;
    const u = dpr;
    const top = (narrow ? 92 : 60) * u;
    const railH = 26 * u, railGap = 4 * u, rails = 3;
    const bottom = H - (rails * (railH + railGap)) - 12 * u;
    const axisY = bottom - 4 * u;
    const X0 = (narrow ? 52 : 68) * u, X1 = W - (narrow ? 116 : 140) * u;
    const Y = top + (axisY - top) / 2 - 6 * u;
    const HMAX = (axisY - top) / 2 - 30 * u;
    return {
      W, H, dpr: u, narrow, top, axisY, X0, X1, Y, HMAX, railH, railGap,
      railY: k => bottom + 8 * u + k * (railH + railGap),
      vx: {wash: X1 + (narrow ? 26 : 34) * u, shape: X1 + (narrow ? 62 : 74) * u,
           tone: X1 + (narrow ? 98 : 114) * u},
      vTop: Y - HMAX, vBot: Y + HMAX,
      XD: v => X0 + Math.log10(v) / 5 * (X1 - X0),
      perDec: (X1 - X0) / 5
    };
  }

  /** The chip row: the same measured-word layout the prototype drew, but the words
   * are compost buttons laid over the canvas. */
  layoutChips() {
    const {dpr, narrow, W} = this.geo;
    const g = this.g;
    g.font = `500 ${Math.round(11 * dpr)}px ${MONO}`;
    const widthOf = label => g.measureText(label).width + 16 * dpr;
    const label = chip => {
      const button = this.buttons.get(chip.id);
      if (!button) return '';
      if (chip.mode === 'switch') return this.meta.get(chip.id)?.name ?? '';
      const text = this.chipText(chip);
      return text[clamp(Math.round(this.val(chip.id)), 0, text.length - 1)] ?? '';
    };
    const h = 22 * dpr;
    const yc = 11 * dpr, y2 = narrow ? 40 * dpr : yc;
    const boxes = {};
    let X = 92 * dpr;
    boxes.link = {x: X, y: yc, w: widthOf(label(CHIPS[0])), h};
    X = boxes.link.x + boxes.link.w + 6 * dpr;
    boxes.mode = {x: X, y: yc, w: widthOf(label(CHIPS[1])), h};
    X = narrow ? 16 * dpr : boxes.mode.x + boxes.mode.w + 22 * dpr;
    boxes.sync = {x: X, y: y2, w: widthOf(label(CHIPS[2])), h};
    X = boxes.sync.x + boxes.sync.w + 6 * dpr;
    boxes.medium = {x: X, y: y2, w: widthOf(label(CHIPS[3])), h};

    const xr = W - 16 * dpr;
    const holdW = widthOf(label(CHIPS[4]));
    boxes.hold = {x: xr - holdW, y: y2, w: holdW, h};

    // the Wear rail after the medium word, and the Mix rail left of Hold
    const bw = (narrow ? 56 : 80) * dpr;
    boxes.wearRail = {x0: boxes.medium.x + boxes.medium.w + 6 * dpr, w: bw,
      y: y2 + h / 2 + 1};
    const mw = (narrow ? 80 : 120) * dpr;
    boxes.mixRail = {x0: boxes.hold.x - 24 * dpr - mw, w: mw, y: y2 + 11 * dpr};
    return boxes;
  }

  placeChips(boxes) {
    const {dpr} = this.geo;
    for (const chip of CHIPS) {
      const button = this.buttons.get(chip.id);
      const box = boxes[chip.id];
      if (!button || !box) continue;
      if (chip.mode === 'cycle') {
        const text = this.chipText(chip).join('|');
        if (button.getAttribute('text') !== text) button.setAttribute('text', text);
      }
      const value = this.val(chip.id);
      const spec = this.meta.get(chip.id);
      const on = chip.on === 'pressed' ? value >= 0.5
        : chip.on === 'notMax' ? value !== spec.max
        : chip.on === 'notMin' ? value !== spec.min : false;
      button.toggleAttribute('on', on);
      button.style.left = `${box.x / dpr}px`;
      button.style.top = `${box.y / dpr}px`;
      button.style.width = `${box.w / dpr}px`;
      button.style.height = `${box.h / dpr}px`;
    }
  }

  // ---- drawing --------------------------------------------------------------

  txt(s, x, y, size, col, align = 'center', base = 'alphabetic', font = MONO, weight = 400) {
    const g = this.g;
    g.fillStyle = col; g.font = `${weight} ${size}px ${font}`;
    g.textAlign = align; g.textBaseline = base; g.fillText(s, x, y);
  }

  line(x1, y1, x2, y2, col, w = 1, a = 1) {
    const g = this.g;
    g.strokeStyle = col; g.lineWidth = w; g.globalAlpha = a;
    g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y2); g.stroke(); g.globalAlpha = 1;
  }

  rail(y, name, ticks, hx, handleCol, active, label, idx) {
    const {X0, X1, dpr, railH} = this.geo, T = this.theme;
    const a = active ? 1 : 0.55;
    this.line(X0, y + railH * 0.55, X1, y + railH * 0.55, T.hair, 1, a);
    for (const t of ticks) {
      if (t.x < X0 - 1 || t.x > X1 + 1) continue;
      this.line(t.x, y + railH * 0.55, t.x,
        y + railH * 0.55 - (t.major ? 9 : t.mid ? 6 : 3.5) * dpr,
        t.major ? T.ink2 : T.dim, t.major ? 1 : 0.7, a * (t.major ? 1 : 0.7));
      if (t.label != null)
        this.txt(t.label, t.x, y + railH * 0.55 - 11 * dpr, Math.round(8.5 * dpr), T.dim, 'center', 'bottom');
    }
    this.txt(name, X0 - 8 * dpr, y + railH * 0.55, Math.round(9 * dpr),
      active ? T.ink : T.dim, 'right', 'middle');
    if (idx != null) {
      this.line(idx, y + railH * 0.55 - 12 * dpr, idx, y + railH * 0.55 + 3 * dpr, T.dim, 1, a * 0.8);
      this.txt('1', idx, y + railH * 0.55 + 5 * dpr, Math.round(8 * dpr), T.dim, 'center', 'top');
    }
    const g = this.g;
    this.line(hx, y + railH * 0.55 - 11 * dpr, hx, y + railH - 1 * dpr, handleCol, active ? 2 : 1.2, 1);
    g.fillStyle = handleCol; g.beginPath();
    g.moveTo(hx - 5.5 * dpr, y + railH - 1 * dpr);
    g.lineTo(hx + 5.5 * dpr, y + railH - 1 * dpr);
    g.lineTo(hx, y + railH - 7 * dpr); g.closePath(); g.fill();
    if (label)
      this.txt(label, X1 + 8 * dpr, y + railH * 0.55, Math.round(9.5 * dpr),
        active ? T.ink : T.dim, 'left', 'middle');
  }

  logTicks(X, lo, hi, labels = true) {
    const out = [];
    for (let d = Math.floor(Math.log10(lo)); d <= Math.ceil(Math.log10(hi)); d++) {
      const b = Math.pow(10, d);
      for (let m = 1; m < 10; m++) {
        const v = b * m;
        if (v < lo * 0.999 || v > hi * 1.001) continue;
        const major = m === 1, mid = m === 2 || m === 5;
        out.push({x: X(v), major, mid,
          label: (labels && (major || (!this.geo.narrow && mid && v >= 10)))
            ? (v < 1000 ? v : `${v / 1000}s`) : null});
        const steps = m < 3 ? 10 : 5;
        for (let s = 1; s < steps; s++) {
          const vv = v + b * s / steps;
          if (vv > hi) break;
          out.push({x: X(vv)});
        }
      }
    }
    return out;
  }

  nameT(ms) {
    if (!this.sync()) return fmtMs(ms);
    const d = this.divisions().find(d => Math.abs(d.ms - ms) < 0.01);
    return d ? d.name : fmtMs(ms);
  }

  render() {
    if (!this.meta.size) return;
    this.geo = this.layout();
    this.readTheme();
    const g = this.g, T = this.theme;
    const {W, H, dpr, top, axisY, X0, X1, Y, HMAX, XD, narrow} = this.geo;
    this.zones = [];
    g.clearRect(0, 0, W, H);

    const {left: TL, right: TR} = this.times();
    this.lastTimes = {left: TL, right: TR};
    const hold = this.hold(), sync = this.sync(), link = this.link(), mode = this.mode();
    const tap = mode === 2 ? 'R' : mode === 3 ? 'L' : false;
    const count = this.repeats(), shape = this.shape();
    const blur = this.blur01(), tone = this.tone11(), wear = this.wear01(), mix = this.mix01();
    const D = laws.toneLaw(blur, tone, hold);
    const tail = this.tail(), xTail = XD(Math.min(tail, laws.holdTailMs));
    const which = this.drag ? this.drag.which : (this.hover ?? this.focusKey());
    const nMax = this.range('repeats')[1];

    const boxes = this.layoutChips();
    this.placeChips(boxes);

    // ---- top band
    this.txt('S L I D E', 16 * dpr, 22 * dpr, Math.round(12 * dpr), T.ink, 'left', 'middle', MONO, 500);
    { // the Wear rail after the medium word
      const {x0: bx, w: bw, y: by} = boxes.wearRail;
      const on = which === 'wearRail';
      this.line(bx, by, bx + bw, by, T.hair, 1, on ? 1 : 0.55);
      for (let i = 0; i <= 8; i++)
        this.line(bx + bw * i / 8, by, bx + bw * i / 8, by - (i % 4 ? 2.5 : 5) * dpr, T.dim, 0.8, 0.7);
      const hx = bx + bw * wear;
      this.line(hx, by - 7 * dpr, hx, by + 5 * dpr, wear === 0 ? T.dim : T.ink, on ? 2.2 : 1.4);
      if (on) this.txt(`${Math.round(wear * 100)}`, bx + bw + 6 * dpr, by, Math.round(9 * dpr), T.dim, 'left', 'middle');
      this.zones.push({x: bx - 6 * dpr, y: by - 12 * dpr, w: bw + 12 * dpr, h: 24 * dpr,
        key: 'wearRail', rail: {x0: bx, w: bw}});
    }
    { // Mix: a tiny rail in the band, left of Hold
      const {x0: mx, w: mw, y: my} = boxes.mixRail;
      this.line(mx, my, mx + mw, my, T.hair, 1, which === 'mix' ? 1 : 0.55);
      for (let i = 0; i <= 10; i++)
        this.line(mx + mw * i / 10, my, mx + mw * i / 10, my - (i % 5 ? 2.5 : 5) * dpr, T.dim, 0.8, 0.7);
      const hx = mx + mw * mix;
      this.line(hx, my - 7 * dpr, hx, my + 5 * dpr, T.ink, which === 'mix' ? 2.2 : 1.4);
      this.txt(this.meta.get('mix').name, mx - 6 * dpr, my, Math.round(9 * dpr), T.dim, 'right', 'middle');
      this.zones.push({x: mx - 8 * dpr, y: my - 12 * dpr, w: mw + 16 * dpr, h: 24 * dpr,
        key: 'mix', rail: {x0: mx, w: mw}});
    }

    // ---- axis
    this.line(X0, axisY, X1, axisY, T.hair);
    if (sync) {
      let plainIndex = -1;
      for (const d of this.divisions()) {
        const x = XD(d.ms), plain = d.kind === 'plain';
        if (plain) plainIndex++;
        const show = narrow ? (plain && plainIndex % 2 === 1) : true;
        this.line(x, axisY, x, axisY + (plain ? 7 : 4) * dpr, plain ? T.ink2 : T.dim, 1, plain ? 1 : 0.6);
        this.line(x, top + 6 * dpr, x, axisY, T.hair, 1, plain ? 0.5 : 0.2);
        if (show) this.txt(d.name, x, axisY + (plain ? 8 : 20) * dpr,
          Math.round((plain ? 10 : 8.5) * dpr), plain ? T.dim : T.hair, 'center', 'top');
      }
      for (const v of [5000, 10000, 20000, 50000, 100000]) {
        const x = XD(v);
        this.line(x, axisY, x, axisY + 5 * dpr, T.dim, 1, 0.6);
        this.txt(`${v / 1000} s`, x, axisY + 8 * dpr, Math.round(10 * dpr), T.dim, 'center', 'top');
      }
    } else {
      for (const t of this.logTicks(XD, 1, laws.holdTailMs)) {
        if (t.major || t.mid)
          this.line(t.x, axisY, t.x, axisY + (t.major ? 7 : 4) * dpr, t.major ? T.ink2 : T.dim, 1, t.major ? 1 : 0.6);
        else this.line(t.x, axisY, t.x, axisY + 2 * dpr, T.dim, 0.7, 0.4);
        if (t.label != null)
          this.txt(String(t.label).replace('s', ' s') + (t.label === 1 ? ' ms' : ''),
            t.x, axisY + 8 * dpr, Math.round(10 * dpr), T.dim, 'center', 'top');
      }
    }

    // ---- picture
    this.line(X0, Y, X1, Y, T.hair);
    const xFirst = XD(Math.min(TL, TR));
    g.fillStyle = T.band; g.globalAlpha = 0.5;
    g.fillRect(xFirst, Y - HMAX, Math.max(0, xTail - xFirst), 2 * HMAX);
    g.globalAlpha = 1;
    if (which === 'field' || which === 'all') {
      g.globalAlpha = 0.1; g.fillStyle = T.acc;
      g.fillRect(xFirst, Y - HMAX, Math.max(0, xTail - xFirst), 2 * HMAX);
      g.globalAlpha = 1;
    }
    const toneK = Math.pow(clamp((Math.log10(D.highCutHz) - Math.log10(200)) / 2, 0, 1), 0.6);
    const thin = tone > 0 ? 1 - tone * 0.6 : 1;
    const early = D.early / 0.62, diff = D.diffusion / 0.62;
    for (const q of this.repeatList()) {
      const x = XD(q.t) + this.drift(q.n, q.side) * this.geo.perDec;
      if (x > X1) continue;
      const frac = clamp((dB(q.a) + 66) / 66, 0, 1);
      if (frac <= 0) continue;
      const h = HMAX * frac, lv = Math.pow(frac, 0.7);
      const fade = (1 - (1 - toneK) * Math.min(1, q.n / 6)) * lv;
      const smear = (early * 18 + diff * (3 + q.n * 2.2)) * dpr * 0.5;
      for (let s = -smear; s <= smear; s += Math.max(1, smear / 6)) {
        const a = (smear > 0 ? (1 - Math.abs(s) / (smear + 1)) / (1 + smear / 8) : 1) * (0.25 + 0.75 * fade);
        this.line(x + s, Y, x + s, q.side === 0 ? Y - h : Y + h, T.ink, 1.5 * dpr * thin, a);
        if (smear === 0) break;
      }
    }
    const nh = HMAX * clamp((dB(Math.SQRT1_2) + 66) / 66, 0, 1);
    this.line(X0 + 2, Y - nh, X0 + 2, Y + nh, T.ink, 2.5 * dpr);
    if (this.level.in > 0.001) {
      const lh = HMAX * clamp((dB(this.level.in) + 60) / 60, 0, 1);
      this.line(X0 + 2, Y - lh, X0 + 2, Y + lh, T.acc, 4 * dpr, 0.9);
    }
    if (this.level.out > 0.001) {
      g.globalAlpha = clamp((dB(this.level.out) + 60) / 60, 0, 1) * 0.22;
      g.fillStyle = T.acc;
      g.fillRect(xFirst, Y - HMAX, Math.max(0, xTail - xFirst), 2 * HMAX);
      g.globalAlpha = 1;
    }

    const hot = k => which === k;
    const bracket = (x, up, key, label) => {
      const on = hot(key), y = up ? Y - HMAX - 2 : Y + HMAX + 2;
      if (on) {
        g.globalAlpha = 0.16; g.fillStyle = T.acc;
        g.fillRect(x - 14 * dpr, up ? top : Y, 28 * dpr, up ? Y - top : axisY - Y);
        g.globalAlpha = 1;
      }
      this.line(x - 5 * dpr, y, x, up ? y - 5 * dpr : y + 5 * dpr, on ? T.acc : T.ink, (on ? 2 : 1.2) * dpr);
      this.line(x, up ? y - 5 * dpr : y + 5 * dpr, x + 5 * dpr, y, on ? T.acc : T.ink, (on ? 2 : 1.2) * dpr);
      if (label && (on || this.drag))
        this.txt(label, x, up ? y - 8 * dpr : y + 8 * dpr, Math.round(11 * dpr), T.ink,
          'center', up ? 'bottom' : 'top', SANS, 500);
    };
    const rl = sync ? this.nameT(TR)
      : link === 0 ? ratioName(TR / TL)
      : link === 1 ? `${TR - TL >= 0 ? '+' : '−'}${fmtMs(Math.abs(TR - TL))}`
      : fmtMs(TR);
    bracket(XD(TL), true, 'time', (tap === 'L' ? 'tap ' : '') + this.nameT(TL));
    bracket(XD(TR), false, 'tr', (tap === 'R' ? 'tap ' : '') + rl);
    {
      const on = hot('rep');
      if (on) {
        g.globalAlpha = 0.16; g.fillStyle = T.acc;
        g.fillRect(xTail - 14 * dpr, Y - HMAX - 2, 28 * dpr, axisY - (Y - HMAX - 2));
        g.globalAlpha = 1;
      }
      this.line(xTail, Y - HMAX - 2, xTail, axisY, T.acc, (on ? 2.5 : 1) * dpr);
      g.fillStyle = T.acc; g.beginPath();
      g.moveTo(xTail - 5 * dpr, Y - HMAX - 2);
      g.lineTo(xTail + 5 * dpr, Y - HMAX - 2);
      g.lineTo(xTail, Y - HMAX + 5 * dpr); g.closePath(); g.fill();
      if (on || this.drag)
        this.txt(hold ? 'holding' : `${count} × · ${fmtMs(tail)}`,
          xTail + (xTail > W * 0.8 ? -6 : 6) * dpr, Y + HMAX + 2, Math.round(11 * dpr),
          T.acc, xTail > W * 0.8 ? 'right' : 'left', 'top', SANS, 500);
    }
    if (which === 'wash' || which === 'shapeM') {
      const mid = (xFirst + xTail) / 2;
      g.globalAlpha = 0.08; g.fillStyle = T.acc;
      if (which === 'wash') g.fillRect(X0, Y - HMAX, mid - X0, 2 * HMAX);
      else g.fillRect(mid, Y - HMAX, X1 - mid, 2 * HMAX);
      g.globalAlpha = 1;
      this.txt(which === 'wash' ? `${this.meta.get('blur').name} ${Math.round(blur * 100)} · ↕`
        : `${this.meta.get('shape').name} · ↕`,
        which === 'wash' ? (X0 + mid) / 2 : (mid + X1) / 2, Y - HMAX - 4 * dpr,
        Math.round(11 * dpr), T.dim, 'center', 'bottom');
    }

    // ---- rails under the axis, slide-rule style
    const rL = this.geo.railY(0), rR = this.geo.railY(1), rN = this.geo.railY(2);
    const perDec = this.geo.perDec;
    this.rail(rL, this.meta.get('left').name, this.logTicks(XD, 1, this.range('left')[1], false),
      XD(TL), T.ink, hot('time') || hot('all'), this.nameT(TL));
    { // Right: a ratio scale whose index sits under Left, and slides with it
      const XR = r => XD(TL) + Math.log10(r) * perDec;
      const [rMin, rMax] = this.range('ratio');
      const majors = new Set([0.5, 1, 1.5, 2, 3]);
      const labels = new Map([[0.5, '½'], [1.5, '3:2'], [2, '2'], [3, '3'], [rMax, String(rMax)]]);
      const ticks = [];
      for (const r of [...laws.niceRatios, rMax])
        ticks.push({x: XR(r), major: majors.has(r), mid: !majors.has(r), label: labels.get(r) ?? null});
      for (let r = rMin; r <= rMax; r *= 1.0594) ticks.push({x: XR(r)});
      this.rail(rR, this.meta.get('right').name, ticks, XD(TR), T.ink,
        hot('tr') || hot('all'), rl, XR(1));
    }
    { // Repeats: a count scale whose index sits under the later of Left and Right
      const base = tap === 'R' ? TL : tap === 'L' ? TR : Math.max(TL, TR);
      const XN = n => XD(base * n);
      const ticks = [];
      const majors = [1, 2, 4, 8, 16, 32, 64], mids = [3, 6, 12, 24, 48, 96];
      const shown = narrow ? majors : [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96];
      for (let n = 1; n <= nMax; n++)
        ticks.push({x: XN(n), major: majors.includes(n), mid: mids.includes(n),
          label: shown.includes(n) ? String(n) : null});
      const xInf = Math.min(X1 - 4 * dpr, XN(nMax) + 16 * dpr);
      this.txt('∞', xInf, rN + this.geo.railH * 0.55 - 11 * dpr, Math.round(9 * dpr),
        hold ? T.acc : T.dim, 'center', 'bottom');
      this.rail(rN, this.meta.get('repeats').name, ticks, hold ? xInf : XN(count), T.acc,
        hot('rep'), hold ? this.meta.get('hold').name : `${count} ×`, null);
    }

    // ---- vertical rails: Blur as a smear strip, Shape and Tone as scales
    const {vx, vTop, vBot} = this.geo, vh = vBot - vTop;
    {
      const x = vx.wash, on = hot('wash'), medium = this.medium();
      for (let i = 0; i <= 24; i++) {
        const u = i / 24, y = vBot - u * vh, sm = u * u * 10 * dpr;
        const n = 1 + Math.round(sm / 1.2);
        for (let s = 0; s < n; s++) {
          let off = n > 1 ? (s / (n - 1) - 0.5) * sm * 2 : 0;
          let x0 = x - 9 * dpr, x1 = x + 9 * dpr;
          let al = (on ? 1 : 0.55) * (0.9 / n + 0.1);
          if (medium === 1) { off += rnd(i * 31 + s) * sm * 0.8; x0 += rnd(i * 17 + s) * 3 * dpr * u; x1 += rnd(i * 23 + s) * 3 * dpr * u; }
          else if (medium === 2) { off = Math.round(off / (2.5 * dpr)) * (2.5 * dpr); al *= 0.85; }
          else if (medium === 3) { off += Math.sin(u * 9 + s) * sm * 0.3; }
          else if (medium === 0) { x0 -= u * 2 * dpr; x1 += u * 2 * dpr; }
          this.line(x0, y + off, x1, y + off, T.ink2, 0.9, al);
        }
      }
      const hy = vBot - blur * vh;
      this.line(x - 13 * dpr, hy, x + 13 * dpr, hy, T.ink, on ? 2.2 : 1.4);
      this.txt('Sharp', x, vBot + 8 * dpr, Math.round(8.5 * dpr), on ? T.ink : T.dim, 'center', 'top');
      this.txt('Blur', x, vTop - 6 * dpr, Math.round(8.5 * dpr), on ? T.ink : T.dim, 'center', 'bottom');
      if (on || this.drag)
        this.txt(String(Math.round(blur * 100)), x + 14 * dpr, hy, Math.round(9 * dpr), T.ink, 'left', 'middle');
      this.zones.push({x: x - 17 * dpr, y: vTop - 4 * dpr, w: 34 * dpr, h: vh + 8 * dpr, key: 'wash'});
    }
    for (const [key, x, value, col, hiLabel, loLabel, midLabel] of [
      ['shape', vx.shape, shape, T.acc, 'Swell', 'Fade', 'Flat'],
      ['tone', vx.tone, tone, T.ink, 'Thin', 'Dark', 'Full']]) {
      const on = hot(key);
      this.line(x, vTop, x, vBot, T.hair, 1, on ? 1 : 0.55);
      for (let i = 0; i <= 20; i++) {
        const y = vBot - i / 20 * vh, maj = i % 10 === 0, mid = i % 5 === 0;
        this.line(x - (maj ? 7 : mid ? 5 : 3) * dpr, y, x + (maj ? 7 : mid ? 5 : 3) * dpr, y,
          T.dim, maj ? 1 : 0.7, (on ? 1 : 0.55) * (maj ? 1 : 0.7));
      }
      this.txt(hiLabel, x, vTop - 6 * dpr, Math.round(8.5 * dpr), on ? T.ink : T.dim, 'center', 'bottom');
      this.txt(loLabel, x, vBot + 8 * dpr, Math.round(8.5 * dpr), on ? T.ink : T.dim, 'center', 'top');
      if (on) this.txt(midLabel, x + 11 * dpr, (vTop + vBot) / 2, Math.round(8.5 * dpr), T.dim, 'left', 'middle');
      const hy = vBot - (value + 1) / 2 * vh;
      this.line(x - 11 * dpr, hy, x + 11 * dpr, hy, col, on ? 2.2 : 1.4);
      this.zones.push({x: x - 17 * dpr, y: vTop - 4 * dpr, w: 34 * dpr, h: vh + 8 * dpr, key});
    }

    // ---- the hairline joining the handle in use to the thing it moves
    if (this.drag) {
      const w = this.drag.which;
      g.setLineDash([3 * dpr, 3 * dpr]);
      if (w === 'time' || w === 'all') this.line(XD(TL), top, XD(TL), rL + this.geo.railH, T.acc, 1, 0.7);
      if (w === 'tr' || w === 'all') this.line(XD(TR), top, XD(TR), rR + this.geo.railH, T.acc, 1, 0.7);
      if (w === 'rep') this.line(xTail, top, xTail, rN + this.geo.railH, T.acc, 1, 0.7);
      if (w === 'wash') { const hy = vBot - blur * vh; this.line(xFirst, hy, vx.wash - 13 * dpr, hy, T.acc, 1, 0.7); }
      if (w === 'shape' || w === 'shapeM') { const hy = vBot - (shape + 1) / 2 * vh; this.line(xTail, hy, vx.shape - 11 * dpr, hy, T.acc, 1, 0.7); }
      if (w === 'tone') { const hy = vBot - (tone + 1) / 2 * vh; this.line(xTail, hy, vx.tone - 11 * dpr, hy, T.acc, 1, 0.7); }
      g.setLineDash([]);
    }
  }

  /** The zone a keyboard focus lights up, so the invisible semantic elements still
   * show where they are. */
  focusKey() {
    return {left: 'time', right: 'tr', ratio: 'tr', difference: 'tr',
      left_division: 'time', right_division: 'tr', repeats: 'rep', shape: 'shape',
      blur: 'wash', tone: 'tone', mix: 'mix', wear: 'wearRail'}[this.focusZone] ?? null;
  }

  // ---- hit testing and gestures ---------------------------------------------

  point(e) {
    const r = this.rect || (this.rect = this.canvas.getBoundingClientRect());
    return {x: (e.clientX - r.left) / r.width * this.canvas.width,
            y: (e.clientY - r.top) / r.height * this.canvas.height};
  }

  zoneAt(p) {
    const hit = this.zones.find(h => p.x >= h.x && p.x <= h.x + h.w && p.y >= h.y && p.y <= h.y + h.h);
    if (hit) return hit.key;
    const {X0, X1, Y, HMAX, top, axisY, XD, dpr, railH} = this.geo;
    for (const [k, i] of [['time', 0], ['tr', 1], ['rep', 2]]) {
      const y = this.geo.railY(i);
      if (p.y >= y && p.y <= y + railH && p.x >= X0 - 30 * dpr && p.x <= X1 + 60 * dpr) return k;
    }
    if (p.y < top || p.y > axisY || p.x < X0 - 10 * dpr || p.x > X1 + 10 * dpr) return null;
    const {left: TL, right: TR} = this.times();
    const tail = Math.min(this.tail(), laws.holdTailMs);
    if (Math.abs(p.x - XD(tail)) < 14 * dpr) return 'rep';
    const nearL = Math.abs(p.x - XD(TL)) < 24 * dpr, nearR = Math.abs(p.x - XD(TR)) < 24 * dpr;
    const inBand = p.x > XD(Math.min(TL, TR)) && p.x < XD(tail) && Math.abs(p.y - Y) < HMAX;
    if (!nearL && !nearR && inBand) return 'field';
    if (!nearL && !nearR)
      return p.x < (XD(Math.min(TL, TR)) + XD(tail)) / 2 ? 'wash' : 'shapeM';
    return p.y < Y ? 'time' : 'tr';
  }

  onPointerMove(e) {
    if (!this.geo) return;
    const p = this.point(e);
    if (this.drag) { this.move(p, e); return; }
    const h = this.zoneAt(p);
    if (h === this.hover) return;
    this.hover = h;
    this.invalidate();
    this.canvas.style.cursor =
      h === 'wash' ? CUR.wash : h === 'shape' || h === 'shapeM' ? CUR.shape
      : h === 'tone' ? 'ns-resize' : h === 'rep' ? 'col-resize' : h === 'field' ? 'move'
      : h === 'wearRail' ? 'ew-resize' : h ? 'ew-resize' : 'default';
  }

  onPointerDown(e) {
    if (e.button !== 0 || !this.geo) return;
    const p = this.point(e);
    const k = this.zoneAt(p);
    if (!k) return;

    // The three side rails are plain relative drags, which is exactly what compost
    // does; hand the gesture straight to the control.
    if (k === 'wash' || k === 'shape' || k === 'tone') {
      const id = k === 'wash' ? 'blur' : k;
      const control = this.controls.get(id);
      if (!control) return;
      control.configure({drag: {axis: 'y', mode: 'relative',
        distance: 2 * this.geo.HMAX / this.geo.dpr}});
      this.drag = {which: k};
      this.invalidate();
      control.startPointerDrag(e);
      return;
    }

    try { this.canvas.setPointerCapture(e.pointerId); } catch { /* no live pointer */ }
    const {left: TL, right: TR} = this.times();
    const tail = Math.min(this.tail(), laws.holdTailMs);
    const half = k === 'field'
      ? (p.x < (this.geo.XD(Math.min(TL, TR)) + this.geo.XD(tail)) / 2 ? 'wash' : 'shapeM')
      : null;
    const hit = this.zones.find(h => p.x >= h.x && p.x <= h.x + h.w && p.y >= h.y && p.y <= h.y + h.h);
    this.drag = {
      which: k, pending: k === 'field' || k === 'rep', half,
      x0: p.x, y0: p.y, tl0: TL, tr0: TR, c0: this.repeats(), s0: this.shape(),
      b0: this.val('blur'),
      rail: hit?.rail, started: new Set()
    };
    this.held = 0;
    if (k === 'mix' || k === 'wearRail') this.move(p, e);
    this.invalidate();
  }

  onPointerUp() {
    if (!this.drag) return;
    for (const id of this.drag.started ?? []) this.endEdit(id);
    this.drag = null;
    this.invalidate();
  }

  start(id) {
    if (!this.drag.started.has(id)) { this.drag.started.add(id); this.beginEdit(id); }
  }

  write(id, value) {
    this.start(id);
    const control = this.controls.get(id);
    if (control) control.setValue(value, true, 'face');
  }

  /** Left in ms, written through whichever parameter owns it right now. */
  writeLeft(ms) {
    const [lo, hi] = this.range('left');
    if (this.sync()) {
      const d = this.nearestDivision(clamp(ms, lo, hi));
      if (d) this.write('left_division', d.index);
    } else this.write('left', clamp(ms, lo, hi));
  }

  writeRight(ms, bypassSnap) {
    const [lo, hi] = this.range('left');
    const raw = clamp(ms, lo, hi);
    const link = this.link();
    if (link === 0) { this.writeLeft(raw / Math.max(1e-9, this.val('ratio'))); return; }
    if (link === 1) { this.writeLeft(raw - this.val('difference')); return; }
    if (this.sync()) {
      const d = this.nearestDivision(raw);
      if (d) this.write('right_division', d.index);
      return;
    }
    const {left} = this.times();
    if (bypassSnap) { this.held = 0; this.write('right', raw); return; }
    const snapped = laws.nearestNiceRatio(raw / left, this.held);
    this.held = snapped.newHeld;
    this.write('right', clamp(left * snapped.value, lo, hi));
  }

  move(p, e) {
    const {dpr, HMAX, perDec} = this.geo;
    const d = this.drag;
    if (d.pending) {
      const dx = Math.abs(p.x - d.x0), dy = Math.abs(p.y - d.y0);
      if (Math.max(dx, dy) < 4 * dpr) return;
      if (d.which === 'rep') d.which = dx >= dy ? 'rep' : 'shapeM';
      else if (d.which === 'field') d.which = dx >= dy ? 'all' : d.half;
      d.pending = false;
    }
    const bypass = e?.metaKey || e?.ctrlKey;
    const w = d.which;

    if (w === 'wearRail' || w === 'mix') {
      const id = w === 'mix' ? 'mix' : 'wear';
      const [, max] = this.range(id);
      const spec = this.meta.get(id);
      const raw = clamp((p.x - d.rail.x0) / d.rail.w, 0, 1) * max;
      this.write(id, spec.step > 0 ? Math.round(raw / spec.step) * spec.step : raw);
      this.invalidate();
      return;
    }

    const k = Math.pow(10, (p.x - d.x0) / perDec);
    const [lo, hi] = this.range('left');
    if (w === 'all') {
      const kk = clamp(k, lo / Math.min(d.tl0, d.tr0), hi / Math.max(d.tl0, d.tr0));
      this.writeLeft(d.tl0 * kk);
      if (this.link() === 2 && !this.sync()) this.write('right', clamp(d.tr0 * kk, lo, hi));
      else if (this.link() === 2) {
        const div = this.nearestDivision(clamp(d.tr0 * kk, lo, hi));
        if (div) this.write('right_division', div.index);
      }
    } else if (w === 'time') this.writeLeft(d.tl0 * k);
    else if (w === 'tr') this.writeRight(d.tr0 * k, bypass);
    else if (w === 'rep') {
      const nMax = this.range('repeats')[1];
      const raw = d.c0 * k;
      if (raw > nMax * 1.6) { this.write('repeats', nMax); this.setSwitch('hold', true); }
      else { this.setSwitch('hold', false); this.write('repeats', clamp(Math.round(raw), 1, nMax)); }
    } else if (w === 'shapeM') {
      const [sLo, sHi] = this.range('shape');
      this.write('shape', clamp(d.s0 - (p.y - d.y0) / (HMAX * 1.2), sLo, sHi));
    } else if (w === 'wash') {
      // the left half of the band, once the pending gesture has locked to vertical
      const [bLo, bHi] = this.range('blur');
      this.write('blur', clamp(d.b0 + (d.y0 - p.y) / (HMAX * 2) * (bHi - bLo), bLo, bHi));
    }
    this.invalidate();
  }
}

customElements.define('slide-face', SlideFace);
