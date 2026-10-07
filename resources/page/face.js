// <slide-face> — the whole face on one canvas: the picture of the echoes in the
// middle, every parameter on a rail around it, and most of them draggable in the
// picture too, each gesture level with its rail.
//
// The picture: two lines, left (L) and right (R), each a row of echoes. L is the
// stock and R the slide, R's time held to L's by a ratio or a difference. The
// band from the first echo to the glass is the loop; before it is the input's (Pre
// blur), after the glass the output's (Post blur). The glass sits on the tail:
// sideways is Repeats, up and down is Tone.
//
// Every number the face needs about a parameter — range, step, curve, midpoint,
// option words — arrives through setMetadata; nothing about the parameter set is
// written down here except which identifier each gesture moves. Continuous
// parameters are compost value controls sharing the canvas as their event target,
// so keyboard, ARIA and the gesture lifecycle come from compost and every change
// leaves as a normal compost parameter event. Link, Sync and the Mod type are compost
// buttons laid over the canvas where their chips are drawn.

import './compost/components/compost-button.js';
import {createValueControl} from './compost/value-control.js';
import * as laws from './laws.js';

const MONO = '"IBM Plex Mono", ui-monospace, monospace';

// The discrete parameters. Each is drawn on the canvas, with its compost button laid
// over the drawing, invisible, for the keyboard and screen readers.
const CHIPS = [
  {id: 'mod_type', mode: 'cycle'},
  {id: 'sync', mode: 'switch'},
  {id: 'link', mode: 'cycle'}
];
const CHIP_IDS = CHIPS.map(chip => chip.id);

// Note values in beats, for Sync's snapping and names.
const NOTES = [[1 / 48, '1/128T'], [1 / 32, '1/128'], [1 / 24, '1/64T'], [3 / 64, '1/128D'], [1 / 16, '1/64'],
  [1 / 12, '1/32T'], [3 / 32, '1/64D'], [1 / 8, '1/32'], [1 / 6, '1/16T'], [3 / 16, '1/32D'], [1 / 4, '1/16'],
  [1 / 3, '1/8T'], [3 / 8, '1/16D'], [1 / 2, '1/8'], [2 / 3, '1/4T'], [3 / 4, '1/8D'], [1, '1/4'],
  [4 / 3, '1/2T'], [3 / 2, '1/4D'], [2, '1/2'], [8 / 3, '1/1T'], [3, '1/2D'], [4, '1/1'], [6, '1/1D'],
  [8, '2/1'], [16, '4/1']];

const RATIO_NAMES = ['1:4', '1:3', '1:2', '2:3', '3:4', '1:1', '5:4', '4:3', '3:2', 'φ', '2:1', '3:1', '4:1'];
const NICE = laws.niceRatios.map((r, i) => [r, RATIO_NAMES[i]]);
// The ratios that claim label space first on R's rail, most common first.
const RAIL_RATIOS = ['1:1', '2:1', '1:2', '3:2', '2:3', '3:1', '4:3', '3:4', 'φ', '1:3', '4:1', '1:4', '5:4'];

// Names are set in a condensed grotesque, like a rule's engraving; numbers and
// readouts are mono.
const NAMES = '"Barlow Semi Condensed", "IBM Plex Sans Condensed", system-ui, sans-serif';
const MOD_AMOUNTS = ['mod_a', 'mod_b', 'mod_c'];

// The axis runs to two minutes; a longer tail pins the glass to the edge.
const AXIS_MAX = 120000;

// Which picture key each parameter identifier lights, for keyboard focus.
const FOCUS = {left_time: 'L', left_beats: 'L', ratio: 'R', difference: 'R', repeats: 'repeats',
  pre_blur: 'pre', loop_blur: 'loop', post_blur: 'post', tone: 'tone', mix: 'mix', mod_a: 'mod', mod_b: 'mod',
  mod_c: 'mod', cross: 'cross', feed: 'feed'};

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const ratioName = r => (NICE.find(([v]) => Math.abs(Math.log(r / v)) < 0.004) || [0, r.toFixed(2)])[1];
const fmt = ms => !Number.isFinite(ms) || ms > 3.6e6 ? '∞' : ms >= 60000 ? `${(ms / 60000).toFixed(1)} min`
  : ms >= 1000 ? `${(ms / 1000).toFixed(ms >= 10000 ? 1 : 2)} s` : `${ms < 10 ? ms.toFixed(1) : Math.round(ms)} ms`;
const fmtCount = n => n < 10 ? n.toFixed(1) : `${Math.round(n)}`;

// The time axis: log from 10 ms to two minutes across the whole width, one scale
// throughout, so echoes close up evenly however long the tail runs.
function timeAxis(x0, x1) {
  const lo = laws.minTimeMs, dec = Math.log10(AXIS_MAX / lo);
  const X = t => x0 + Math.log10(clamp(t, lo, AXIS_MAX) / lo) / dec * (x1 - x0);
  X.inv = x => lo * Math.pow(10, (x - x0) / (x1 - x0) * dec);
  return X;
}

const STYLE = `
:host{ position:relative; display:block; height:100%; min-width:0; }
canvas{ position:absolute; inset:0; width:100%; height:100%; display:block; touch-action:none }
.chips{ position:absolute; inset:0; pointer-events:none }
compost-button{ position:absolute; pointer-events:none; opacity:0; --compost-button-width:100%; --compost-button-height:100%; }
compost-button:focus-within{ opacity:1 }
compost-button::part(button){ width:100%; height:100%; border:0; background:none; color:transparent; padding:0;
  outline:1px solid var(--acc); outline-offset:1px; }
:host(:focus){ outline:none }
.entry{ position:absolute; z-index:2; width:96px; padding:2px 5px; font:12px ${MONO}; color:var(--ink);
  background:var(--panel); border:1px solid var(--acc); border-radius:2px; outline:none }
.entry.wrong{ border-color:var(--ink) }
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
    this.zones = [];            // hit rectangles, device px, back to front
    this.rect = null;
    this.hover = null;
    this.drag = null;
    this.focusZone = null;
    this.held = 0;              // the snap lock's held ratio
    this.theme = {};
    this.themeDirty = true;
    this.dirty = true;
    this.now = 0;
    this.frame = {bpm: 120};

    this.invalidate = this.invalidate.bind(this);
    this.markTheme = () => { this.themeDirty = true; this.invalidate(); };

    const c = this.canvas;
    c.addEventListener('pointermove', e => this.onPointerMove(e));
    c.addEventListener('pointerdown', e => this.onPointerDown(e));
    c.addEventListener('pointerup', e => this.onPointerUp(e));
    c.addEventListener('pointercancel', e => this.onPointerUp(e));
    c.addEventListener('dblclick', e => this.onDoubleClick(e));
    this.addEventListener('keydown', e => this.onKey(e));
    c.addEventListener('pointerleave', () => {
      if (!this.drag && this.hover) { this.hover = null; this.invalidate(); }
    });
    addEventListener('scroll', () => { this.rect = null; }, true);
  }

  // Moving the face from one parent to another disconnects and reconnects it. Both
  // callbacks must therefore be reversible: they start and stop the clock and the
  // observers, and never touch the value controls, whose ARIA and gesture state have
  // to survive the move. The canvas's own listeners are wired once, in the constructor.
  connectedCallback() {
    if (!this.hasAttribute('tabindex')) this.tabIndex = 0; // so a clicked control can be typed into
    this.resizeObserver = new ResizeObserver(() => { this.rect = null; this.invalidate(); });
    this.resizeObserver.observe(this.canvas);
    this.scheme = matchMedia('(prefers-color-scheme: dark)');
    this.scheme.addEventListener('change', this.markTheme);
    document.fonts?.ready.then(this.invalidate);
    this.rect = null;
    this.themeDirty = true;
    this.invalidate();
    const tick = () => {
      this.raf = requestAnimationFrame(tick);
      this.now = performance.now() / 1000;
      // render() reports false while the view has no box; stay dirty until it does.
      if (this.dirty || this.moving()) this.dirty = !this.render();
    };
    this.raf = requestAnimationFrame(tick);
  }

  disconnectedCallback() {
    cancelAnimationFrame(this.raf);
    this.resizeObserver?.disconnect();
    this.scheme?.removeEventListener('change', this.markTheme);
  }

  invalidate() { this.dirty = true; }

  // ---- metadata, values, telemetry -----------------------------------------

  /** The parameter table, as the plugin's handshake sent it. */
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
  setValue(id, value) {
    const spec = this.metaByID.get(String(id));
    if (!spec || !Number.isFinite(value)) return;
    this.controls.get(spec.identifier)?.setValue(value, false, 'bridge');
    this.buttons.get(spec.identifier)?.setValue(value, false, 'bridge');
    this.invalidate();
  }

  /** The host's tempo, for Sync, and each line's wobble as the engine has it. */
  setTelemetry(frame) {
    if (!frame || !Number.isFinite(frame.bpm)) return;
    const wobble = Array.isArray(frame.wobble) && frame.wobble.length === 2 && frame.wobble.every(Number.isFinite) ? frame.wobble : null;
    if (frame.bpm === this.frame.bpm && String(wobble) === String(this.frame.wobble)) return;
    this.frame = {bpm: frame.bpm, wobble};
    this.invalidate();
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
        parameterKind: 'continuous',
        name: spec.name,
        label: spec.name,
        min: spec.min,
        max: spec.max,
        mid: spec.mid,
        curve: spec.curve,
        step: spec.step,
        unit: spec.unit,
        value: spec.initial,
        resetValue: spec.initial,
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
      button.setAttribute('aria-label', spec.name);
      if (chip.mode === 'switch') button.toggleAttribute('pressed', spec.initial >= 0.5);
      else {
        button.setAttribute('text', spec.options.join('|'));
        button.setAttribute('value', String(Math.round(spec.initial)));
      }
      button.addEventListener('change', () => this.chipChanged(chip.id));
      this.chipHost.append(button);
      this.buttons.set(chip.id, button);
    }
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

  initial(identifier) { return this.meta.get(identifier)?.initial ?? 0; }

  /** Inside a drag: the first write to a parameter opens its gesture, and the drag's
   * end closes every gesture it opened. Outside one, a write is a complete edit:
   * begin, value, end, and nothing at all when the value is unchanged. */
  write(identifier, value) {
    const control = this.controls.get(identifier);
    if (!control) return;
    control.setValue(value, true, 'face'); // compost clamps, snaps, skips no-ops and opens the gesture
    if (this.drag) this.drag.started.add(identifier); else control.endGesture(false, 'face');
  }

  /** A chip that changes what the times mean re-bases them first, so nothing on
   * screen or in the sound moves: Link and Sync both keep both lines put. */
  chipChanged(id) {
    const [l, r] = this.lastTimes ?? this.times();
    if (id === 'link') { this.write('ratio', r / l); this.write('difference', r - l); }
    else if (id === 'sync') this.writeLeft(l, true);
    this.invalidate();
  }

  // ---- the parameters in the picture's units -------------------------------

  bpm() { return this.frame.bpm > 0 ? this.frame.bpm : 120; }
  beatMs() { return 60000 / this.bpm(); }
  sync() { return this.val('sync') >= 0.5; }
  ratioMode() { return Math.round(this.val('link')) === 0; }
  modType() { return clamp(Math.round(this.val('mod_type')), 0, 2); }
  modAmount(type = this.modType()) { return this.val(MOD_AMOUNTS[type]); }
  repeats() { return Math.max(1, this.val('repeats')); }
  percent(id) { return this.val(id) / 100; }

  times() {
    const l = clamp(this.sync() ? this.val('left_beats') * this.beatMs() : this.val('left_time'),
      laws.minTimeMs, laws.maxTimeMs);
    const r = this.ratioMode() ? l * this.val('ratio') : l + this.val('difference');
    return [l, clamp(Number.isFinite(r) ? r : l, laws.minTimeMs, laws.maxTimeMs)];
  }

  tail() {
    return Math.max(...this.times()) * this.repeats();
  }

  nearestNote(ms) {
    const beats = ms / this.beatMs();
    let best = NOTES[0];
    for (const n of NOTES) if (Math.abs(Math.log(n[0] / beats)) < Math.abs(Math.log(best[0] / beats))) best = n;
    return {beats: best[0], name: best[1], ms: best[0] * this.beatMs()};
  }

  /** A time as the reader wants it: under Sync, its nearest note, with a ~ when it
   * is not on it (Sync keeps the setting rather than snapping it). */
  nameT(ms) {
    if (!this.sync()) return fmt(ms);
    const n = this.nearestNote(ms);
    return Math.abs(Math.log(n.ms / ms)) < 0.003 ? n.name : `~${n.name}`;
  }

  /** What R reads as: its note under Sync with Ratio, otherwise its relation to L
   * (under Sync with Diff, L is on the grid and R a fixed offset from it). */
  relWord() {
    const [l, r] = this.times();
    if (this.sync() && this.ratioMode()) return this.nameT(r);
    return this.ratioMode() ? `L × ${ratioName(r / l)}` : `L ${r - l >= 0 ? '+' : '−'} ${fmt(Math.abs(r - l))}`;
  }

  // ---- moving the times -----------------------------------------------------

  /** Writes L's own time in the units Sync stores; under Sync it snaps to the
   * nearest note unless `free`. */
  writeLeft(ms, free) {
    const t = clamp(ms, laws.minTimeMs, laws.maxTimeMs);
    if (this.sync()) this.write('left_beats', free ? t / this.beatMs() : this.nearestNote(t).beats);
    else this.write('left_time', t);
  }

  snapRatio(r, free) {
    if (free) { this.held = 0; return r; }
    const s = laws.nearestNiceRatio(r, this.held);
    this.held = s.newHeld;
    return s.value;
  }

  /** Moves L alone: R stays where it was at the start of the drag, and the relation
   * follows. Under Sync L snaps to notes; otherwise the ratio snaps to nice ones. */
  moveL(ms, rFixed, free) {
    const [rMin, rMax] = this.range('ratio');
    let l = clamp(ms, laws.minTimeMs, laws.maxTimeMs);
    if (this.ratioMode()) {
      l = clamp(l, rFixed / rMax, rFixed / rMin);
      if (this.sync() && !free) l = this.nearestNote(l).ms;
      else l = rFixed / this.snapRatio(rFixed / l, free);
      this.writeLeft(l, true);
      this.write('ratio', clamp(rFixed / l, rMin, rMax));
    } else {
      if (this.sync() && !free) l = this.nearestNote(l).ms;
      this.writeLeft(l, true);
      this.write('difference', rFixed - l);
    }
  }

  /** Moves R alone: the relation changes, L stays. */
  moveR(ms, free) {
    const l = this.times()[0], r = clamp(ms, laws.minTimeMs, laws.maxTimeMs);
    const [rMin, rMax] = this.range('ratio');
    if (this.ratioMode()) // under Sync, R snaps to notes; otherwise to nice ratios
      this.write('ratio', clamp(this.sync() ? (free ? r : this.nearestNote(r).ms) / l : this.snapRatio(r / l, free), rMin, rMax));
    else this.write('difference', r - l);
  }

  /** Moves both: L goes, and R keeps its ratio or difference to it. */
  moveBoth(ms, free) {
    const ratio = this.val('ratio'), diff = this.val('difference');
    const lo = this.ratioMode() ? Math.max(laws.minTimeMs, laws.minTimeMs / ratio) : Math.max(laws.minTimeMs, laws.minTimeMs - diff);
    const hi = this.ratioMode() ? Math.min(laws.maxTimeMs, laws.maxTimeMs / ratio) : Math.min(laws.maxTimeMs, laws.maxTimeMs - diff);
    this.writeLeft(clamp(ms, lo, hi), free);
  }

  resetL() {
    const r = this.times()[1];
    const l = this.sync() ? this.initial('left_beats') * this.beatMs() : this.initial('left_time');
    this.writeLeft(l, true);
    this.write('ratio', r / l);
    this.write('difference', r - l);
  }

  // R's reset puts it on L: a 1:1 ratio, no difference
  resetR() { this.write('ratio', 1); this.write('difference', 0); }

  // ---- the picture's model --------------------------------------------------

  /** Every echo, as the engine makes them: routes through L (i passes) and R (j
   * passes) land at i·L + j·R. */
  echoes() {
    const [TA, TB] = this.times(), x = this.percent('cross'), f = this.percent('feed');
    const g = laws.passGain(this.repeats(), x, TA, TB);
    const out = [], heap = [], key = new Map(), FLOOR = Math.pow(10, -66 / 20);
    // each route is its own echo, even where routes land together at a nice ratio, so
    // the picture draws a ratio and its near neighbours the same way and nothing pops
    // as a drag snaps on or off one (each pixel column shows its loudest echo)
    const K = e => `${e.line}:${e.i}:${e.j}`;
    const up = i => { while (i > 0) { const p = (i - 1) >> 1; if (heap[p].t <= heap[i].t) break; [heap[p], heap[i]] = [heap[i], heap[p]]; i = p; } };
    const down = i => { for (;;) { const l = 2 * i + 1, r = l + 1; let m = i;
      if (l < heap.length && heap[l].t < heap[m].t) m = l; if (r < heap.length && heap[r].t < heap[m].t) m = r;
      if (m === i) return; [heap[m], heap[i]] = [heap[i], heap[m]]; i = m; } };
    const push = e => { if (e.a < FLOOR || heap.length > 10000) return; const k = K(e), ex = key.get(k);
      if (ex) { ex.a += e.a; ex.aL += e.aL; if (e.hop && !ex.hop) ex.hop = e.hop; return; }
      key.set(k, e); heap.push(e); up(heap.length - 1); };
    const a0 = 0.7 * Math.min(1, 1 - f), b0 = 0.7 * Math.min(1, 1 + f);
    push({t: TA, line: 0, i: 1, j: 0, a: a0, aL: a0, n: 1});
    push({t: TB, line: 1, i: 0, j: 1, a: b0, aL: 0, n: 1});
    while (heap.length && out.length < 5000) {
      const top = heap[0], last = heap.pop(); if (heap.length) { heap[0] = last; down(0); }
      key.delete(K(top)); out.push(top);
      for (const to of [top.line, 1 - top.line]) { const i = top.i + (to === 0), j = top.j + (to === 1);
        const k = g * (to === top.line ? 1 - x : x);
        push({t: i * TA + j * TB, line: to, i, j, n: top.n + 1, a: top.a * k, aL: top.aL * k,
          hop: to !== top.line ? {t: top.t, line: top.line} : null}); }
    }
    out.g = g;
    return out;
  }

  recipe() { return laws.recipeAt(this.modType(), this.modAmount() / 100); }

  /** True while the Mod is moving the echoes, so the picture keeps redrawing. */
  moving() { return this.meta.size > 0 && this.recipe().sine > 0; }

  /** How far the Mod's wobble has each line's time now, as a fraction of it: the
   * engine's own, random part and all, when it sends it; otherwise (no plug-in, as
   * in the tests) the slow sine alone, by the same law on the face's clock. */
  wobbleNow() {
    if (this.frame.wobble) return this.frame.wobble;
    const rec = this.recipe(), times = this.times();
    return [0, 1].map(line => rec.sine * Math.sin(2 * Math.PI * (rec.sineHz * this.now + line * 0.25))
      * laws.wobbleReferenceMs(times[line]) / times[line]);
  }

  /** How an echo looks after its passes through the loop's filters, in either
   * theme: losing highs (the Mod's loss, Tone's dark side) makes it darker and a
   * little heavier, losing lows (Tone's thin side) brighter and finer. Both grow over
   * the first few passes. Returns a width factor and a tint from -1 (towards black)
   * to +1 (towards white). */
  shadeOf(e) {
    const cuts = laws.toneCuts(this.val('tone') / 100);
    const hc = Math.min(this.recipe().lossHz, cuts.highCutHz);
    const dark = 1 - Math.pow(clamp((Math.log10(hc) - Math.log10(200)) / 2, 0, 1), 0.6);
    const thin = clamp(Math.log10(cuts.lowCutHz / 20) / Math.log10(2500 / 20), 0, 1);
    const p = Math.min(1, e.n / 6);
    return {width: Math.max(0.4, 1 + 0.8 * dark * p - 0.55 * thin * p), tint: (thin - dark) * p};
  }

  /** How wide an echo's smear is drawn: Pre and Post evenly on every echo, Loop
   * growing pass by pass. */
  smearOf(e) {
    const pre = 0.7 * this.percent('pre_blur'), loop = 0.7 * this.percent('loop_blur'), post = 0.7 * this.percent('post_blur');
    return 7 * pre + 9 * post + 4 * loop * Math.sqrt(e.n) * (1 + loop);
  }

  // ---- drawing --------------------------------------------------------------

  readTheme() {
    if (!this.themeDirty) return;
    const s = getComputedStyle(this);
    for (const k of ['panel', 'band', 'ink', 'ink2', 'dim', 'hair', 'acc', 'lineb', 'glass'])
      this.theme[k] = s.getPropertyValue(`--${k}`).trim() || '#888';
    this.themeDirty = false;
  }

  /** The parameters the zone under the pointer (or in hand) moves, so their rails
   * light up; keyboard focus lights its own. */
  hotParams() {
    const z = this.drag ? this.drag.z : this.zones.find(q => q.key === this.hover);
    if (z) return new Set(this.drag && z.byAxis && this.drag.axis ? [z.byAxis[this.drag.axis]] : z.params || []);
    if (this.selected) return new Set([this.selected.key]);
    const focus = FOCUS[this.focusZone];
    return new Set(focus ? [focus] : []);
  }

  /** Draws the face, and reports whether it could. A plug-in window starts with no
   * size at all, so a view with no box is not drawn: the bitmap would not match what
   * is on screen, and the ResizeObserver asks again the moment there is a box. */
  render() {
    if (!this.meta.size) return false;
    const box = this.rect || (this.rect = this.canvas.getBoundingClientRect());
    if (!(box.width > 0 && box.height > 0)) return false;
    const c = this.canvas, dpr = Math.min(2, devicePixelRatio || 1);
    const W = Math.round(box.width * dpr), H = Math.round(box.height * dpr);
    if (c.width !== W || c.height !== H) { c.width = W; c.height = H; }
    this.dpr = dpr;
    this.readTheme();
    const g = this.g, T = this.theme;
    g.clearRect(0, 0, W, H);
    // what is lit comes from the zones as last drawn, before they are drawn again
    this.lit = this.hotParams();
    this.zones = [];
    this.drawFace(W, H, dpr, g, T);
    return true;
  }

  drawFace(W, H, dpr, g, T) {
    const zone = z => this.zones.push(z);
    const ln = (x1, y1, x2, y2, col, w = 1, a = 1) => { g.strokeStyle = col; g.lineWidth = w * dpr; g.globalAlpha = a;
      g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y2); g.stroke(); g.globalAlpha = 1; };
    const tx = (s, x, y, col, size = 10, align = 'center', base = 'middle', font = MONO, weight = 400) => {
      g.fillStyle = col; g.font = `${weight} ${Math.round(size * dpr)}px ${font}`; g.textAlign = align; g.textBaseline = base; g.fillText(s, x, y); };
    const tri = (x, y, s, up, col) => { g.fillStyle = col; g.beginPath(); g.moveTo(x - s, y); g.lineTo(x + s, y);
      g.lineTo(x, up ? y - s * 1.3 : y + s * 1.3); g.closePath(); g.fill(); };
    const rect = (x, y, w, h, col, a = 1) => { g.fillStyle = col; g.globalAlpha = a; g.fillRect(x, y, w, h); g.globalAlpha = 1; };
    // Names start with a capital and are set in NAMES; the scale letters L and R are
    // mono capitals, as on a rule.
    const name = (s, x, y, col, size = 9.5, align = 'center', base = 'middle') => {
      if (/^[LR]$/.test(s)) tx(s, x, y, col, size + 0.5, align, base, MONO, 500);
      else tx(s, x, y, col, size + 0.5, align, base, NAMES, 500);
    };
    // A compost button laid over a drawn control, at its box in device pixels.
    const overlay = (id, x, y, w, h) => {
      const b = this.buttons.get(id);
      if (b) Object.assign(b.style, {left: `${x / dpr}px`, top: `${y / dpr}px`, width: `${w / dpr}px`, height: `${h / dpr}px`});
      return b;
    };
    // A label on a scale: it is left out rather than drawn over another label of the
    // same row, so a crowded scale thins its numbers instead of smudging them.
    const taken = new Map();
    const label = (s, x, y, col, size, align = 'center', base = 'bottom', gap = 4) => {
      g.font = `400 ${Math.round(size * dpr)}px ${MONO}`;
      const w = g.measureText(s).width, x0 = align === 'center' ? x - w / 2 : align === 'right' ? x - w : x;
      const row = taken.get(y) ?? [];
      if (row.some(([a, b]) => x0 < b + gap * dpr && x0 + w > a - gap * dpr)) return false;
      row.push([x0, x0 + w]); taken.set(y, row);
      tx(s, x, y, col, size, align, base);
      return true;
    };
    const dash = (x1, y1, x2, y2) => { g.setLineDash([3 * dpr, 3 * dpr]); ln(x1, y1, x2, y2, T.acc, 1, 0.75); g.setLineDash([]); };

    // An engraved rail: fine graduations, 0/50/100 at the majors, and a handle.
    const hRail = (x0, x1, y, value01, label, readout, on, ticks = 10, ends) => {
      ln(x0, y, x1, y, T.hair, 1, on ? 1 : 0.7);
      const n = ticks * 5;
      for (let i = 0; n && i <= n; i++) { const x = x0 + (x1 - x0) * i / n, major = i % (n / 2) === 0, mid = i % 5 === 0;
        ln(x, y, x, y - (major ? 7 : mid ? 4.5 : 2.2) * dpr, T.dim, major ? 0.9 : 0.6, major ? 0.9 : 0.6);
        if (major) tx(ends && i === 0 ? ends[0] : ends && i === n ? ends[1] : `${Math.round(i / n * 100)}`, x, y - 9 * dpr, T.dim, 8.5, 'center', 'bottom'); }
      const hx = x0 + (x1 - x0) * clamp(value01, 0, 1);
      ln(hx, y - 8 * dpr, hx, y + 5 * dpr, on ? T.acc : T.ink, on ? 2.4 : 1.6);
      if (label) name(label, x0 - 7 * dpr, y, T.dim, 9.5, 'right');
      if (readout) tx(readout, x1 + 7 * dpr, y, on ? T.ink : T.ink2, 10, 'left');
    };
    const vRail = (x, y0, y1, value01, top, bottom, on, label) => {
      ln(x, y0, x, y1, T.hair, 1, on ? 1 : 0.7);
      for (let i = 0; i <= 50; i++) { const y = y1 - (y1 - y0) * i / 50, major = i % 25 === 0, mid = i % 5 === 0, s = (major ? 7 : mid ? 4.5 : 2.2) * dpr;
        ln(x - s, y, x + s, y, T.dim, major ? 0.9 : 0.6, major ? 0.9 : 0.55); }
      const hy = y1 - (y1 - y0) * clamp(value01, 0, 1);
      ln(x - 11 * dpr, hy, x + 11 * dpr, hy, on ? T.acc : T.ink, on ? 2.4 : 1.6);
      name(top, x, y0 - 8 * dpr, T.dim, 9, 'center', 'bottom'); name(bottom, x, y1 + 8 * dpr, T.dim, 9, 'center', 'top');
      if (label) tx(label, x + 14 * dpr, hy, T.ink, 10, 'left');
    };
    // A blur rail: its graduations smear more the higher they sit, sharp at the foot
    // and blurred at the head, with the same handle as every other rail.
    const blurRail = (x, y0, y1, value01, title, on) => {
      for (let i = 0; i <= 24; i++) {
        const u = i / 24, y = y1 - u * (y1 - y0), sm = u * u * 10 * dpr, n = 1 + Math.round(sm / (1.2 * dpr));
        for (let k = 0; k < n; k++) {
          const off = n > 1 ? (k / (n - 1) - 0.5) * sm * 2 : 0, x0 = x - (9 + u * 2) * dpr, x1 = x + (9 + u * 2) * dpr;
          ln(x0, y + off, x1, y + off, T.ink2, 0.8, (on ? 0.85 : 0.45) * (0.8 / n + 0.08));
        }
      }
      const hy = y1 - (y1 - y0) * clamp(value01, 0, 1);
      ln(x - 13 * dpr, hy, x + 13 * dpr, hy, on ? T.acc : T.ink, on ? 2.4 : 1.6);
      name(title, x, y0 - 8 * dpr, T.dim, 9, 'center', 'bottom');
      name('off', x, y1 + 8 * dpr, T.dim, 9, 'center', 'top');
    };

    // ---- geometry: the picture keeps its width margins and stretches with height
    const X0 = 156 * dpr, X1 = W - 196 * dpr, top = 70 * dpr, bot = H - 160 * dpr, axisY = bot + 12 * dpr;
    const X = timeAxis(X0, X1), [TA, TB] = this.times(), mid = (top + bot) / 2, longer = Math.max(TA, TB);
    const Xm = X(10000); // the L and R rails run to 10 s, past the longest time
    this.lastTimes = [TA, TB];
    const cross = this.percent('cross'), feed = this.percent('feed'), tone = this.val('tone') / 100;
    const pre = this.percent('pre_blur'), loop = this.percent('loop_blur'), post = this.percent('post_blur');
    const repeats = this.repeats(), tail = this.tail();
    const S = (bot - top) * 0.25, rows = [mid - S, mid + S];
    const half = (bot - top) * 0.17, list = this.echoes(), xFirst = X(Math.min(TA, TB));
    const xTail = Math.min(X1, X(tail));
    const lit = this.lit, on = k => lit.has(k);
    const vy = v => bot - (bot - top) * clamp(v, 0, 1); // the shared vertical scale
    const held = this.drag ? this.drag.z.key : this.hover || '';
    // while a rail is held or hovered, its gesture in the picture shows itself
    const fromRail = held.endsWith('Rail');
    const arrow = (x, y, vertical) => { if (!fromRail) return; const s = 9 * dpr;
      g.strokeStyle = T.acc; g.fillStyle = T.acc; g.lineWidth = 1.6 * dpr; g.beginPath();
      if (vertical) { g.moveTo(x, y - s); g.lineTo(x, y + s); } else { g.moveTo(x - s, y); g.lineTo(x + s, y); } g.stroke();
      for (const k of [-1, 1]) { g.beginPath();
        if (vertical) { g.moveTo(x, y + k * (s + 4 * dpr)); g.lineTo(x - 4 * dpr, y + k * s); g.lineTo(x + 4 * dpr, y + k * s); }
        else { g.moveTo(x + k * (s + 4 * dpr), y); g.lineTo(x + k * s, y - 4 * dpr); g.lineTo(x + k * s, y + 4 * dpr); }
        g.closePath(); g.fill(); } };
    const cw = 30 * dpr, gTop = top - 8 * dpr, gBot = axisY, afterX = Math.min(X1, xTail + cw / 2);
    // Cross reads 0–100 up to the middle, where each echo splits equally between the
    // lines, then on to 200, swap, where every echo changes line.
    const crossWord = () => cross > 0.995 ? 'swap' : `${Math.round(cross * 200)}`;
    const toneWord = () => Math.abs(tone) < 0.01 ? 'full' : `${tone < 0 ? 'dark' : 'thin'} ${Math.round(Math.abs(tone) * 100)}`;
    const feedWords = () => Math.abs(feed) < 0.005 ? 'into L and R' : feed <= -0.995 ? 'into L only' : feed >= 0.995 ? 'into R only'
      : feed < 0 ? `L 100 · R ${Math.round((1 + feed) * 100)}` : `L ${Math.round((1 - feed) * 100)} · R 100`;

    // ---- top band: the name, Mod, Mix
    g.letterSpacing = `${(4 * dpr).toFixed(1)}px`;
    tx('Slide', 16 * dpr, 24 * dpr, T.ink, 13, 'left', 'middle', NAMES, 500);
    g.letterSpacing = '0px';
    const mix = this.percent('mix'), type = this.modType();
    // Mod: one thin amount scale per type, stacked like a rule's A, B, C scales. Each
    // keeps its own amount; the chosen one is engraved and carries the cursor, the
    // others show where theirs was left. A row's letter picks it.
    g.font = `500 ${Math.round(10 * dpr)}px ${NAMES}`;
    name('Mod', 104 * dpr, 30 * dpr, T.dim, 9.5, 'left');
    const r0 = 104 * dpr + g.measureText('Mod').width + 34 * dpr, r1 = r0 + 140 * dpr, ys = [17, 30, 43].map(v => v * dpr);
    const modButton = overlay('mod_type', 104 * dpr, 6 * dpr, r0 - 104 * dpr - 4 * dpr, 44 * dpr);
    ['A', 'B', 'C'].forEach((w, i) => {
      const y = ys[i], chosen = i === type, amount = this.modAmount(i) / 100, hx = r0 + (r1 - r0) * amount;
      tx(w, r0 - 8 * dpr, y, chosen ? T.ink : T.dim, 10, 'right', 'middle', NAMES, 500);
      ln(r0, y, r1, y, T.hair, 1, chosen ? 1 : 0.45);
      if (chosen) for (let k = 0; k <= 20; k++) { const x = r0 + (r1 - r0) * k / 20;
        ln(x, y, x, y - (k % 10 === 0 ? 5 : k % 2 === 0 ? 3 : 1.6) * dpr, T.dim, 0.7, 0.8); }
      ln(hx, y - (chosen ? 6 : 3) * dpr, hx, y + (chosen ? 5 : 3) * dpr, chosen ? (on('mod') ? T.acc : T.ink) : T.dim,
        chosen ? (on('mod') ? 2.4 : 1.6) : 1, chosen ? 1 : 0.6);
      if (chosen) tx(amount < 0.005 ? 'clean' : `${Math.round(amount * 100)}`, r1 + 7 * dpr, y, on('mod') ? T.ink : T.ink2, 10, 'left');
      const pick = (p, d, dr) => { if (dr.done) return; dr.done = true; if (i !== type) modButton?.setValue(i, true, 'face'); };
      if (chosen) zone({x: r0 - 6 * dpr, y: y - 7 * dpr, w: r1 - r0 + 12 * dpr, h: 14 * dpr, key: 'modRail', params: ['mod'], cursor: 'ew-resize',
        move: p => this.write(MOD_AMOUNTS[i], clamp((p.x - r0) / (r1 - r0), 0, 1) * 100),
        dbl: () => this.write(MOD_AMOUNTS[i], this.initial(MOD_AMOUNTS[i]))});
      else zone({x: r0 - 6 * dpr, y: y - 7 * dpr, w: r1 - r0 + 12 * dpr, h: 14 * dpr, key: 'modPick', cursor: 'pointer', move: pick});
      zone({x: r0 - 22 * dpr, y: y - 7 * dpr, w: 18 * dpr, h: 14 * dpr, key: 'modPick', cursor: 'pointer', move: pick});
    });
    const mr = [W - 170 * dpr, W - 50 * dpr];
    hRail(mr[0], mr[1], 30 * dpr, mix, 'Mix', `${Math.round(mix * 100)}`, on('mix'), 10, ['dry', 'wet']);

    // ---- picture: before the echoes is the input's (Pre), the band is the loop's
    // (Loop), after the glass is the output's (Post)
    if (on('pre')) rect(X0, top, Math.max(0, xFirst - X0), bot - top, T.acc, 0.07);
    rect(xFirst, top, Math.max(0, Math.min(xTail, X1) - xFirst), bot - top, T.band, on('loop') || (on('L') && on('R')) ? 0.95 : 0.55);
    if (on('post')) rect(afterX, top, Math.max(0, X1 - afterX), bot - top, T.acc, 0.07);
    for (const r of rows) ln(X0, r, X1, r, T.hair, 1, 0.6);
    this.drawEchoes(list, X, rows, half, X1, dpr, ln);

    // axis: log from 10 ms to two minutes
    ln(X0, axisY, X1, axisY, T.hair);
    for (let d = 10; d < AXIS_MAX; d *= 10) for (let m = 1; m < 10; m++) { const v = d * m; if (v > AXIS_MAX) break;
      ln(X(v), axisY, X(v), axisY + (m === 1 ? 6 : m === 5 ? 3.5 : 2) * dpr, m === 1 ? T.ink2 : T.dim, m === 1 ? 1 : 0.6, m === 1 ? 1 : 0.6); }
    ln(X1, axisY, X1, axisY + 6 * dpr, T.ink2, 1);
    // labels in order of importance: the ends and decades first, then what fits
    for (const [t, s] of [[10, '10 ms'], [AXIS_MAX, '2 min'], [100, '100'], [1000, '1 s'], [10000, '10 s'], [60000, '1 min'],
      [50, '50'], [500, '500'], [5000, '5 s']])
      label(s, X(t), axisY + 9 * dpr, T.dim, 9.5, 'center', 'top');

    // pointers on the first echoes: sideways is time, up/down pulls the rows together
    for (const [line, t] of [[0, TA], [1, TB]]) {
      const x = X(t), k = line ? 'Rp' : 'Lp', hot = held === k || on(line ? 'R' : 'L') || on('cross');
      tri(x, line ? rows[1] + half + 4 * dpr : rows[0] - half - 4 * dpr, (hot ? 6.5 : 5) * dpr, line === 1, line ? T.lineb : T.ink);
      if (hot) tx(this.drag?.axis === 'y' ? `Cross ${crossWord()}` : line ? `R = ${this.relWord()}` : `L ${this.nameT(t)}`, x + 10 * dpr,
        line ? rows[1] + half + 16 * dpr : rows[0] - half - 14 * dpr, line ? T.lineb : T.ink, 11, 'left', 'middle', NAMES, 500);
    }

    // the glass: a flat cursor window whose hairline is the tail (Repeats)
    const gx = Math.min(xTail, X1 - cw / 2), glassHot = on('repeats') || on('tone');
    rect(gx - cw / 2, gTop, cw, gBot - gTop, T.glass);
    g.strokeStyle = glassHot ? T.acc : T.ink2; g.lineWidth = (glassHot ? 1.4 : 1) * dpr; g.globalAlpha = 0.8;
    g.strokeRect(gx - cw / 2 + 0.5, gTop + 0.5, cw - 1, gBot - gTop - 1); g.globalAlpha = 1;
    rect(gx - cw / 2 - 2 * dpr, gTop - 5 * dpr, cw + 4 * dpr, 5 * dpr, T.ink2, 0.8);
    rect(gx - cw / 2 - 2 * dpr, gBot, cw + 4 * dpr, 5 * dpr, T.ink2, 0.8);
    ln(gx, gTop + 3 * dpr, gx, gBot - 3 * dpr, T.acc, on('repeats') ? 2.4 : 1.3);
    // Tone: a level line inside the glass, level with its rail
    ln(gx - cw / 2, vy((tone + 1) / 2), gx + cw / 2, vy((tone + 1) / 2), on('tone') ? T.acc : T.ink2, on('tone') ? 2.2 : 1.2);
    // Post: a level line across the space after the glass, the output's side
    if (afterX < X1) ln(afterX + 4 * dpr, vy(post), X1, vy(post), on('post') ? T.acc : T.ink2, on('post') ? 2 : 1, on('post') ? 1 : 0.5);
    if (on('repeats')) tx(`${fmtCount(repeats)} repeats${Number.isFinite(tail) ? ` · ${fmt(tail)}` : ''}`,
      gx - cw / 2 - 8 * dpr, gTop + 12 * dpr, T.acc, 11, 'right', 'middle', NAMES, 500);

    // gesture hints for whichever rail is in hand
    if (on('pre')) arrow((X0 + xFirst) / 2, vy(pre), true);
    if (on('loop')) arrow((xFirst + Math.min(xTail, X1)) / 2, vy(loop), true);
    if (on('post') && afterX < X1) arrow((afterX + X1) / 2, vy(post), true);
    if (on('tone')) arrow(gx - cw / 2 - 16 * dpr, vy((tone + 1) / 2), true);
    if (on('L')) arrow(X(TA), rows[0] - half - 22 * dpr, false);
    if (on('R')) arrow(X(TB), rows[1] + half + 30 * dpr, false);
    if (on('cross')) { arrow(X(TA) - 18 * dpr, rows[0] - half - 4 * dpr, true); arrow(X(TB) - 18 * dpr, rows[1] + half + 4 * dpr, true); }
    if (on('repeats')) arrow(gx, gTop - 12 * dpr, false);
    if (on('feed')) arrow(X0 - 70 * dpr, (rows[0] + rows[1]) / 2, true);

    // ---- left: Pre blur, level with the space before the echoes, and the in line
    const px = X0 - 100 * dpr, fx = X0 - 44 * dpr;
    blurRail(px, top, bot, pre, 'Pre-blur', on('pre'));
    if (on('pre')) tx(`${Math.round(pre * 100)}`, px, top - 22 * dpr, T.acc, 10);
    if (on('pre')) dash(px + 11 * dpr, vy(pre), Math.max(px + 12 * dpr, xFirst - 4 * dpr), vy(pre));
    ln(fx, rows[0], fx, rows[1], T.hair, 1, on('feed') ? 1 : 0.7);
    name('L', fx, rows[0] - 10 * dpr, T.ink, 10, 'center', 'bottom');
    name('R', fx, rows[1] + 10 * dpr, T.lineb, 10, 'center', 'top');
    const fy = rows[0] + (rows[1] - rows[0]) * (feed + 1) / 2;
    ln(fx - 10 * dpr, fy, fx + 10 * dpr, fy, on('feed') ? T.acc : T.ink, on('feed') ? 3 : 2);
    name('In', fx + 14 * dpr, fy, T.dim, 9.5, 'left');
    if (on('feed')) tx(feedWords(), fx + 30 * dpr, fy, T.ink, 11, 'left', 'middle', NAMES, 500);

    // ---- right: Blur, Post-blur, Cross and Tone, each level with its gesture
    const loopX = X1 + 34 * dpr, postX = X1 + 74 * dpr, crossX = X1 + 114 * dpr, toneX = X1 + 154 * dpr;
    blurRail(loopX, top, bot, loop, 'Blur', on('loop'));
    if (on('loop')) { dash(Math.max(xFirst, X0), vy(loop), loopX - 11 * dpr, vy(loop)); tx(`${Math.round(loop * 100)}`, loopX, top - 22 * dpr, T.acc, 10); }
    blurRail(postX, top, bot, post, 'Post-blur', on('post'));
    if (on('post')) { dash(X1, vy(post), postX - 11 * dpr, vy(post)); tx(`${Math.round(post * 100)}`, postX, top - 22 * dpr, T.acc, 10); }
    // Cross: a plain rail, 0 at the foot, an equal split at the middle, swap at the
    // head. Its gesture is up and down on either line's pointer; the picture shows it
    // as the rows' colours mixing and the ties between them.
    vRail(crossX, top, bot, cross, 'Cross', 'off', on('cross'), '');
    if (on('cross')) dash(X(TB) + 8 * dpr, vy(cross), crossX - 11 * dpr, vy(cross));
    if (on('cross')) tx(crossWord(), crossX, top - 22 * dpr, T.acc, 10);
    vRail(toneX, top, bot, (tone + 1) / 2, 'Thin', 'Dark', on('tone'), '');
    if (on('tone')) { const y = vy((tone + 1) / 2); dash(gx + cw / 2, y, toneX - 11 * dpr, y); tx(toneWord(), toneX, top - 22 * dpr, T.acc, 10); }

    // ---- bottom rails: L time (with Sync), R relation (with Link), Repeats
    const rY = k => axisY + (46 + k * 38) * dpr;
    hRail(X0, Xm, rY(0), (X(TA) - X0) / (Xm - X0), 'L', this.nameT(TA), on('L'), 0);
    // Sync and Link sit just after their readouts, beside what they change: Sync a
    // toggle, Link a Ratio | Diff switch.
    {
      const sync = this.sync(), sx = Xm + 76 * dpr, h = 18 * dpr;
      g.font = `500 ${Math.round(10.5 * dpr)}px ${NAMES}`; const w = g.measureText('Sync').width + 14 * dpr;
      if (sync) rect(sx, rY(0) - h / 2, w, h, T.ink);
      else { g.strokeStyle = held === 'syncBtn' ? T.ink : T.hair; g.lineWidth = dpr; g.strokeRect(sx + 0.5, rY(0) - h / 2 + 0.5, w - 1, h - 1); }
      tx('Sync', sx + w / 2, rY(0), sync ? T.panel : T.ink, 10.5, 'center', 'middle', NAMES, 500);
      const b = overlay('sync', sx, rY(0) - h / 2, w, h);
      zone({x: sx, y: rY(0) - h / 2, w, h, key: 'syncBtn', cursor: 'pointer',
        move: (p, d, dr) => { if (dr.done) return; dr.done = true; b?.setValue(sync ? 0 : 1, true, 'face'); }});
    }
    // R's scale, on the time axis so its handle sits under R's first echo: a tick at
    // every nice ratio (or a spread of differences) from L. Labels go on a lower tier,
    // then an upper one, the common ratios first; one that fits on neither is left
    // as a tick, and the picture names the ratio while R moves.
    const rx = X(TB);
    // Under Sync with Ratio the scale is note values instead: plain notes first,
    // dotted and triplet after.
    const plain = name => !name.endsWith('D') && !name.endsWith('T');
    const relTicks = this.sync() && this.ratioMode() ? NOTES.map(([beats, n]) => [beats * this.beatMs(), n]).filter(([ms]) => ms >= laws.minTimeMs && ms <= laws.maxTimeMs)
      .map(([ms, n]) => [X(ms), n, plain(n)])
      : this.ratioMode() ? NICE.filter(([r]) => TA * r >= laws.minTimeMs && TA * r <= laws.maxTimeMs)
      .map(([r, n]) => [X(TA * r), n, true]).sort((a, b) => RAIL_RATIOS.indexOf(a[1]) - RAIL_RATIOS.indexOf(b[1]))
      : [-1000, -500, -200, -100, -50, -20, 0, 20, 50, 100, 200, 500, 1000].filter(d => TA + d >= laws.minTimeMs && TA + d <= laws.maxTimeMs)
        .map(d => [X(TA + d), d ? `${d > 0 ? '+' : '−'}${Math.abs(d)}` : '0', [0, -200, 200, -1000, 1000, -50, 50].includes(d)]);
    ln(X0, rY(1), Xm, rY(1), T.hair, 1, 0.7);
    for (const first of [true, false])
      for (const [x, name, primary] of relTicks) {
        if (primary !== first) continue;
        if (label(name, x, rY(1) - 9 * dpr, T.dim, 9, 'center', 'bottom', 3) || label(name, x, rY(1) - 20 * dpr, T.dim, 9, 'center', 'bottom', 3))
          ln(x, rY(1), x, rY(1) - 6 * dpr, T.dim, 0.9, primary ? 1 : 0.7);
        else ln(x, rY(1), x, rY(1) - 3 * dpr, T.dim, 0.9);
      }
    ln(rx, rY(1) - 8 * dpr, rx, rY(1) + 5 * dpr, on('R') ? T.acc : T.lineb, 2.2);
    name('R', X0 - 7 * dpr, rY(1), T.lineb, 9.5, 'right'); tx(this.relWord(), Xm + 7 * dpr, rY(1), on('R') ? T.ink : T.ink2, 10, 'left');
    {
      const ratioOn = this.ratioMode(), h = 18 * dpr;
      let lx = Xm + 76 * dpr;
      g.font = `500 ${Math.round(10.5 * dpr)}px ${NAMES}`;
      const cells = [['Ratio', 0], ['Diff', 1]].map(([w, v]) => ({w, v, width: g.measureText(w).width + 14 * dpr}));
      const total = cells.reduce((a, c) => a + c.width, 0), b = overlay('link', lx, rY(1) - h / 2, total, h);
      g.strokeStyle = T.hair; g.lineWidth = dpr; g.strokeRect(lx + 0.5, rY(1) - h / 2 + 0.5, total - 1, h - 1);
      for (const c of cells) {
        const chosen = (c.v === 0) === ratioOn;
        if (chosen) rect(lx, rY(1) - h / 2, c.width, h, T.ink);
        tx(c.w, lx + c.width / 2, rY(1), chosen ? T.panel : T.dim, 10.5, 'center', 'middle', NAMES, 500);
        zone({x: lx, y: rY(1) - h / 2, w: c.width, h, key: 'linkBtn', cursor: 'pointer', move: () => { if (!chosen) b?.setValue(c.v, true, 'face'); }});
        lx += c.width;
      }
    }
    // Repeats' rail ends where its top (1000) puts the tail, on the time axis
    const xRepMax = X(longer * laws.maxRepeats);
    ln(X0, rY(2), xRepMax, rY(2), T.hair, 1, on('repeats') ? 1 : 0.7);
    label('1000', xRepMax, rY(2) - 8 * dpr, T.dim, 9);
    ln(xRepMax, rY(2), xRepMax, rY(2) - 5 * dpr, T.dim, 0.9);
    for (const n of [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]) { const x = X(longer * n); if (x < X0 || x > xRepMax - 6 * dpr) continue;
      ln(x, rY(2), x, rY(2) - 5 * dpr, T.dim, 0.9); label(`${n}`, x, rY(2) - 8 * dpr, T.dim, 9); }
    ln(xTail, rY(2) - 8 * dpr, xTail, rY(2) + 5 * dpr, on('repeats') ? T.acc : T.ink, on('repeats') ? 2.4 : 1.6);
    name('Repeats', X0 - 7 * dpr, rY(2), T.dim, 9.5, 'right'); tx(`${fmtCount(repeats)} ×`, X1 + 7 * dpr, rY(2), on('repeats') ? T.ink : T.ink2, 10, 'left');

    // ---- zones, back to front
    const level = p => clamp((bot - p.y) / (bot - top), 0, 1);
    const toneAt = v => { const t = v * 2 - 1; return Math.abs(t) < 0.03 ? 0 : t * 100; };
    // the count whose tail lands at x; the rail's end always means the top, even
    // when that tail runs past the axis
    const repeatsAt = x => x >= xRepMax - dpr ? laws.maxRepeats : clamp(X.inv(x) / Math.max(...this.times()), 1, laws.maxRepeats);
    const railX = (x0, x1, id) => ({cursor: 'ew-resize', move: p => this.write(id, clamp((p.x - x0) / (x1 - x0), 0, 1) * 100),
      dbl: () => this.write(id, this.initial(id))});
    zone({x: mr[0] - 6 * dpr, y: 14 * dpr, w: mr[1] - mr[0] + 12 * dpr, h: 26 * dpr, key: 'mixRail', params: ['mix'], ...railX(mr[0], mr[1], 'mix')});
    zone({x: xFirst, y: top, w: Math.max(0, Math.min(xTail, X1) - xFirst), h: bot - top, key: 'band', lock: 'xy', cursor: 'move',
      params: ['L', 'R', 'loop'], byAxis: {x: 'L', y: 'loop'},
      move: (p, d, dr) => dr.axis === 'x' ? this.moveBoth(X.inv(X(dr.snap.L) + d.dx), d.free) : this.write('loop_blur', level(p) * 100),
      dbl: () => this.write('loop_blur', this.initial('loop_blur'))});
    zone({x: X0, y: top, w: Math.max(0, xFirst - X0 - 14 * dpr), h: bot - top, key: 'preZone', params: ['pre'], cursor: 'ns-resize',
      move: p => this.write('pre_blur', level(p) * 100), dbl: () => this.write('pre_blur', this.initial('pre_blur'))});
    if (afterX < X1) zone({x: afterX, y: top, w: X1 - afterX, h: bot - top, key: 'postZone', params: ['post'], cursor: 'ns-resize',
      move: p => this.write('post_blur', level(p) * 100), dbl: () => this.write('post_blur', this.initial('post_blur'))});
    // up and down on a pointer is Cross: towards the other line raises it
    const crossBy = (sign, dr, d) => this.write('cross', clamp(dr.snap.cross + sign * d.dy / (bot - top), 0, 1) * 100);
    zone({x: X(TA) - 14 * dpr, y: top, w: 28 * dpr, h: rows[0] + half + 14 * dpr - top, key: 'Lp', lock: 'xy', cursor: 'move',
      params: ['L', 'cross'], byAxis: {x: 'L', y: 'cross'},
      move: (p, d, dr) => dr.axis === 'x' ? this.moveL(X.inv(X(dr.snap.L) + d.dx), dr.snap.R, d.free) : crossBy(1, dr, d),
      dbl: () => this.resetL()});
    zone({x: X(TB) - 14 * dpr, y: rows[1] - half - 4 * dpr, w: 28 * dpr, h: bot - rows[1] + half + 4 * dpr, key: 'Rp', lock: 'xy', cursor: 'move',
      params: ['R', 'cross'], byAxis: {x: 'R', y: 'cross'},
      move: (p, d, dr) => dr.axis === 'x' ? this.moveR(X.inv(X(dr.snap.R) + d.dx), d.free) : crossBy(-1, dr, d),
      dbl: () => this.resetR()});
    zone({x: gx - cw / 2, y: gTop, w: cw, h: gBot - gTop, key: 'glass', lock: 'xy', cursor: 'move',
      params: ['repeats', 'tone'], byAxis: {x: 'repeats', y: 'tone'},
      move: (p, d, dr) => dr.axis === 'x'
        ? this.write('repeats', repeatsAt(X(Math.max(...this.times()) * dr.snap.repeats) + d.dx))
        : this.write('tone', toneAt(level(p))),
      dbl: () => { this.write('repeats', this.initial('repeats')); this.write('tone', 0); }});
    zone({x: fx - 18 * dpr, y: rows[0] - 10 * dpr, w: 50 * dpr, h: rows[1] - rows[0] + 20 * dpr, key: 'in', params: ['feed'], cursor: 'ns-resize',
      move: p => { const f = clamp(((p.y - rows[0]) / (rows[1] - rows[0])) * 2 - 1, -1, 1); this.write('feed', Math.abs(f) < 0.04 ? 0 : f * 100); },
      dbl: () => this.write('feed', 0)});
    // the rails themselves
    const vZone = (x, key, id, set) => zone({x: x - 16 * dpr, y: top - 6 * dpr, w: 32 * dpr, h: bot - top + 12 * dpr, key: `${key}Rail`,
      params: [key], cursor: 'ns-resize', move: p => set(level(p)), dbl: () => this.write(id, this.initial(id))});
    vZone(px, 'pre', 'pre_blur', v => this.write('pre_blur', v * 100));
    vZone(loopX, 'loop', 'loop_blur', v => this.write('loop_blur', v * 100));
    vZone(postX, 'post', 'post_blur', v => this.write('post_blur', v * 100));
    vZone(toneX, 'tone', 'tone', v => this.write('tone', toneAt(v)));
    vZone(crossX, 'cross', 'cross', v => this.write('cross', v * 100));
    zone({x: X0 - 4 * dpr, y: rY(0) - 13 * dpr, w: Xm - X0 + 8 * dpr, h: 26 * dpr, key: 'LRail', params: ['L'], cursor: 'ew-resize',
      move: (p, d, dr) => this.moveL(X.inv(X(dr.snap.L) + d.dx), dr.snap.R, d.free), dbl: () => this.resetL()});
    zone({x: X0 - 4 * dpr, y: rY(1) - 13 * dpr, w: Xm - X0 + 8 * dpr, h: 26 * dpr, key: 'RRail', params: ['R'], cursor: 'ew-resize',
      move: (p, d, dr) => this.moveR(X.inv(X(dr.snap.R) + d.dx), d.free),
      dbl: () => this.resetR()});
    zone({x: X0 - 4 * dpr, y: rY(2) - 13 * dpr, w: xRepMax - X0 + 8 * dpr, h: 26 * dpr, key: 'repeatsRail', params: ['repeats'], cursor: 'ew-resize',
      move: p => this.write('repeats', repeatsAt(p.x)), dbl: () => this.write('repeats', this.initial('repeats'))});
  }

  /** Echoes as strokes around a row, the loudest per pixel column; a smear is drawn
   * as several faint copies side by side. The model lists only so many echoes, so
   * past the last one each line carries on as an envelope, so a long tail never just
   * stops. The envelope starts at the level the
   * listed echoes end on, falls at the rate they were falling, and is drawn the way
   * they are, so the join does not show. */
  drawEchoes(list, X, rows, half, xMax, dpr, ln) {
    const T = this.theme;
    // An echo keeps the colour of the line it started on, mixed by how much of it
    // came from each, so Cross shows as the rows mixing; Tone then tints it.
    const colourOf = (e, shade) => {
      const s = clamp(e.aL / Math.max(e.a, 1e-12), 0, 1), k = 0.7 * Math.abs(shade.tint);
      return `color-mix(in srgb, color-mix(in srgb, ${T.ink} ${s * 100}%, ${T.lineb}) ${(1 - k) * 100}%, ${shade.tint > 0 ? 'white' : 'black'})`; };
    const ref = Math.max(...list.map(e => e.a), 1e-9), cols = new Map(), wob = this.wobbleNow();
    // one echo's stroke: its height and strength from its level, shaded by the loop's
    // filters, smeared by the blurs as faint copies side by side
    const stroke = (e, x) => {
      const frac = clamp((20 * Math.log10(e.a / ref) + 60) / 60, 0, 1); if (frac <= 0) return false;
      const shade = this.shadeOf(e), h = half * frac, col = colourOf(e, shade), sm = this.smearOf(e) * dpr;
      const alpha = 0.25 + 0.75 * Math.pow(frac, 0.7);
      if (sm <= 0.5) { ln(x, rows[e.line] - h, x, rows[e.line] + h, col, 1.5 * shade.width, alpha); return true; }
      const steps = Math.max(2, Math.round(sm / 2));
      for (let s = -steps; s <= steps; s++) ln(x + s * sm / steps, rows[e.line] - h, x + s * sm / steps, rows[e.line] + h, col, 1.2 * shade.width,
        alpha * (1 - Math.abs(s) / (steps + 1)) / (1 + steps / 6));
      return true;
    };
    for (const e of list) { const x = X(e.t * (1 + wob[e.line])); if (x > xMax) continue;
      const k = `${e.line}:${Math.round(x / (1.5 * dpr))}`, ex = cols.get(k); if (!ex || e.a > ex.e.a) cols.set(k, {e, x}); }
    const times = this.times();
    for (const line of [0, 1]) {
      const mine = list.filter(e => e.line === line); if (!mine.length || !(list.g > 0)) continue;
      const last = mine[mine.length - 1], T0 = times[line], longer = Math.max(...times);
      // Where the listed echoes end, the tail goes on at the rate Repeats sets: 60 dB
      // over Repeats times the longer line, the loop's slowest decay (uncrossed, each
      // line simply falls by its own pass gain), drawn exactly as they are.
      const per = this.percent('cross') > 0 ? -3 / (longer * Math.max(1, this.repeats() - 1)) : Math.log10(list.g) / T0;
      // the outline the eye reads is the taller echoes, so start from the 90th
      // percentile of the last listed ones, each carried on to the last one's time at
      // that rate, so a stretch that was still falling doesn't start it high again
      const tailEnd = mine.slice(-48), levels = tailEnd.map(e => e.a * Math.pow(10, per * (last.t - e.t))).sort((p, q) => p - q);
      const end = {a: levels[Math.min(levels.length - 1, Math.floor(levels.length * 0.9))], t: last.t};
      const mixL = tailEnd.reduce((acc, e) => acc + e.aL, 0) / Math.max(1e-12, tailEnd.reduce((acc, e) => acc + e.a, 0));
      const shift = 1 + wob[line], other = times[1 - line], crossed = this.percent('cross') > 0;
      // strokes where echoes arrive: every pass of this line, and with Cross up every
      // pass of the other line too, as the listed echoes do; where they crowd closer
      // than a column, the column keeps the first
      // how densely the listed echoes filled the pixel columns just before the join;
      // where they filled them all, the continuation fills every column too, so the
      // texture carries on instead of opening into stripes
      const col = 1.5 * dpr, endX = X(last.t * shift), seen = new Set();
      for (const e of mine) { const ex = X(e.t * shift); if (ex > endX - 40 * dpr) seen.add(Math.round(ex / col)); }
      const filled = seen.size / Math.max(1, Math.round(40 * dpr / col));
      let lastX = endX, own = last.t + T0, across = last.t + other;
      for (let k = 0; k < 40000; k++) {
        let t, fromAcross = false;
        if (filled > 0.6) { // dense: one stroke per column
          t = X.inv(lastX + col) / shift; if (!Number.isFinite(t)) break;
          fromAcross = crossed && ((t - last.t) / other) % 1 < ((t - last.t) / T0) % 1;
        } else {
          t = crossed ? Math.min(own, across) : own; fromAcross = t !== own;
          if (fromAcross) across += other; else own += T0;
        }
        const x = X(t * shift); if (!Number.isFinite(x) || x > xMax) break;
        if (x - lastX < col * 0.99) continue;
        lastX = x;
        const a = end.a * Math.pow(10, per * (t - end.t));
        if (!stroke({line, n: last.n + (t - last.t) / T0, a, aL: a * mixL}, x)) break;
        // an echo that came across gets its tie, as the listed ones do
        const tie = fromAcross ? 0.8 * this.percent('cross') * clamp((20 * Math.log10(a / ref) + 60) / 60, 0, 1) : 0;
        if (tie >= 0.01) {
          const from = 1 - line, hx = X((t - T0) * (1 + wob[from]));
          ln(hx, rows[from] + (line > from ? half : -half) * 0.4, x, rows[line] + (line > from ? -half : half) * 0.4, T.acc, 1, tie);
        }
      }
    }
    { // ties: a diagonal from an echo to the one it seeds on the other row, as strong
      // as the share Cross sends across times how loud the echo is
      const share = this.percent('cross');
      if (share > 0) for (const {e, x} of cols.values()) {
        if (!e.hop) continue;
        const frac = clamp((20 * Math.log10(e.a / ref) + 60) / 60, 0, 1), alpha = 0.8 * share * frac;
        if (alpha < 0.01) continue;
        const px = X(e.hop.t * (1 + wob[e.hop.line])), y0 = rows[e.hop.line] + (e.line > e.hop.line ? half : -half) * 0.4,
          y1 = rows[e.line] + (e.line > e.hop.line ? -half : half) * 0.4;
        ln(px, y0, x, y1, T.acc, 1, alpha);
      }
    }
    for (const {e, x} of cols.values()) stroke(e, x);
  }

  // ---- hit testing and gestures ---------------------------------------------

  point(e) {
    const r = this.rect || (this.rect = this.canvas.getBoundingClientRect());
    return {x: (e.clientX - r.left) / r.width * this.canvas.width,
            y: (e.clientY - r.top) / r.height * this.canvas.height};
  }

  hit(p) {
    for (let i = this.zones.length - 1; i >= 0; i--) {
      const z = this.zones[i];
      if (p.x >= z.x && p.x <= z.x + z.w && p.y >= z.y && p.y <= z.y + z.h) return z;
    }
    return null;
  }

  onPointerMove(e) {
    const p = this.point(e);
    const drag = this.drag;
    if (drag) {
      const d = {dx: p.x - drag.p0.x, dy: p.y - drag.p0.y, free: e.metaKey || e.ctrlKey || e.shiftKey};
      // a two-way zone picks its axis from the first few pixels of movement
      if (drag.z.lock === 'xy' && !drag.axis) {
        if (Math.max(Math.abs(d.dx), Math.abs(d.dy)) < 4 * this.dpr) return;
        drag.axis = Math.abs(d.dx) >= Math.abs(d.dy) ? 'x' : 'y';
      }
      drag.z.move?.(p, d, drag);
      this.invalidate();
      return;
    }
    const z = this.hit(p), k = z?.key ?? null;
    this.canvas.style.cursor = z?.cursor ?? 'default';
    if (k !== this.hover) { this.hover = k; this.invalidate(); }
  }

  onPointerDown(e) {
    if (e.button !== 0) return;
    const p = this.point(e), z = this.hit(p);
    this.focus({preventScroll: true});
    this.closeEntry();
    this.selected = null;
    if (!z) { this.invalidate(); return; }
    try { this.canvas.setPointerCapture(e.pointerId); } catch { /* no live pointer */ }
    const [L, R] = this.times();
    this.held = 0;
    this.drag = {z, p0: p, axis: z.lock === 'xy' ? null : z.lock, started: new Set(),
      snap: {L, R, repeats: this.repeats(), cross: this.percent('cross')}};
    // a one-way rail jumps to the pointer at once; a two-way zone waits for its axis
    if (!z.lock) z.move?.(p, {dx: 0, dy: 0, free: e.metaKey || e.ctrlKey || e.shiftKey}, this.drag);
    this.invalidate();
  }

  onPointerUp(e) {
    if (!this.drag) return;
    for (const id of this.drag.started) this.controls.get(id)?.endGesture(false, 'face');
    // the control just used is the one typing goes to: on a two-way zone, the way it moved
    const z = this.drag.z, key = z.byAxis && this.drag.axis ? z.byAxis[this.drag.axis] : z.params?.[0];
    if (key && e) { const r = this.canvas.getBoundingClientRect(); this.selected = {key, x: e.clientX - r.left, y: e.clientY - r.top}; }
    this.drag = null;
    this.invalidate();
  }

  // ---- typing a value into the control last used ---------------------------

  onKey(e) {
    if (!this.selected || this.entry || e.metaKey || e.ctrlKey || e.altKey) return;
    if (e.key === 'Escape') { this.selected = null; this.invalidate(); return; }
    if (!/^[0-9.+-]$/.test(e.key)) return;
    e.preventDefault();
    const box = document.createElement('input');
    box.className = 'entry'; box.value = e.key; box.spellcheck = false;
    const r = this.canvas.getBoundingClientRect();
    Object.assign(box.style, {left: `${clamp(this.selected.x + 8, 0, r.width - 110)}px`, top: `${clamp(this.selected.y - 26, 0, r.height - 24)}px`});
    box.addEventListener('keydown', k => {
      k.stopPropagation();
      if (k.key === 'Escape') { this.closeEntry(); this.focus({preventScroll: true}); }
      else if (k.key === 'Enter') {
        if (this.enter(this.selected.key, box.value)) { this.closeEntry(); this.focus({preventScroll: true}); }
        else { box.classList.add('wrong'); box.select(); }
      } else box.classList.remove('wrong');
    });
    // numbers only, with what the readouts themselves use: a ratio's colon, a
    // note's slash and its D or T
    box.addEventListener('input', () => { box.value = box.value.replace(/[^0-9.+\-:/dt]/gi, ''); });
    box.addEventListener('blur', () => this.closeEntry());
    this.shadowRoot.append(box);
    this.entry = box;
    box.focus();
  }

  closeEntry() { const box = this.entry; this.entry = null; box?.remove(); }

  /** Writes what was typed for a control, in the numbers the face shows it in, and
   * reports whether it made sense: times in ms or as a note, R as a ratio (3:2 or
   * 1.5) or under Diff an offset in ms, Cross on its readout's scale (100 is even,
   * 200 swap), Tone below 0 dark and above thin. */
  enter(key, text) {
    const t = text.trim().toLowerCase().replace(/\s+/g, ' ');
    const ok = Number.isFinite;
    const number = v => /^[+-]?(\d+\.?\d*|\.\d+)$/.test(v) ? Number(v) : NaN;
    const note = v => { const n = NOTES.find(([, name]) => name.toLowerCase() === v.replace(/\.$/, 'd')); return n ? n[0] * this.beatMs() : NaN; };
    const time = v => ok(number(v)) ? number(v) : note(v); // ms, or a note
    const set = (id, v) => { if (!ok(v)) return false; this.write(id, v); return true; };
    switch (key) {
      case 'L': { const ms = time(t); if (!ok(ms)) return false; this.writeLeft(ms, true); return true; }
      case 'R': {
        if (this.ratioMode()) {
          const m = t.match(/^([\d.]+):([\d.]+)$/), r = m ? number(m[1]) / number(m[2]) : number(t);
          if (ok(r) && r > 0) return set('ratio', r);
          const ms = note(t); if (!ok(ms)) return false; this.moveR(ms, true); return true;
        }
        const ms = note(t); if (ok(ms)) { this.moveR(ms, true); return true; }
        return set('difference', time(t));
      }
      case 'cross': return set('cross', number(t) / 2);
      case 'tone': return set('tone', number(t));
      case 'mod': return set(MOD_AMOUNTS[this.modType()], number(t));
      default: {
        const id = {repeats: 'repeats', pre: 'pre_blur', loop: 'loop_blur', post: 'post_blur', mix: 'mix', feed: 'feed'}[key];
        return id ? set(id, number(t)) : false;
      }
    }
  }

  /** Double-click puts back the default of whatever is under the pointer. */
  onDoubleClick(e) {
    const z = this.hit(this.point(e));
    this.onPointerUp();
    z?.dbl?.();
    this.invalidate();
  }
}

customElements.define('slide-face', SlideFace);
