# Slide — design

Slide is a stereo delay whose face is a picture of the repeats, with slide-rule
rails as the controls. The browser prototype in `prototype/` is the behavioural
spec. The sibling plugins `../mote`, `../tapa` and `../mno` are the structural
spec: same build flow, same libraries, same file layout.

This document records the words we use, the modules and their seams, the
decisions taken, and the build order. It is kept current; nothing here is
historical.

## 1. Dictionary

Use these words exactly, in identifiers, comments, tests and the UI.

### Controls

| Term | Meaning | Range |
|---|---|---|
| **Left** | delay time of the left line | 1–2000 ms, log |
| **Right** | delay time of the right line | 1–2000 ms, log |
| **Link** | how Right follows Left: **Ratio**, **Difference**, **Off** | enum |
| **Ratio** | Right ÷ Left while Link = Ratio; snaps to the Nice ratios | 0.5–4, log |
| **Difference** | Right − Left while Link = Difference | −2000–2000 ms |
| **Nice ratios** | the snap set 1:2, 2:3, 3:4, 1:1, 5:4, 4:3, 3:2, φ, 2:1, 3:1 | constant |
| **Sync** | Left and Right follow tempo divisions | bool |
| **Division** | one of 24 grid entries, 1/128 … 1/1 plain, dotted, triplet | enum, one for Left, one for Right |
| **Repeats** | how many repeats sound | 1–64 |
| **Hold** | the ∞ zone past 64 on the Repeats rail: unity loop, filters open | bool |
| **Shape** | the decay envelope: **Fade** (−1) … **Flat** (0) … **Swell** (+1) | −1–+1 |
| **Blur** | Sharp–Blur rail: diffusion plus a roof | 0–100 % |
| **Tone** | Dark–Thin rail: high cut below centre, low cut above | −100–+100 |
| **Mix** | dry/wet, equal power | 0–100 % |
| **Mode** | **Stereo**, **Ping pong**, **Right is a tap**, **Left is a tap** | enum |
| **Medium** | **Tape**, **Oil can**, **Bucket**, **Tide**, **Digital** | enum |
| **Wear** | how much of the medium's character; 0 is clean on every medium | 0–100 % |

### Face

| Term | Meaning |
|---|---|
| **Face** | the whole canvas surface: picture, rails, chips, telemetry |
| **Picture** | the drawing of repeats on the log time axis |
| **Axis** | the log time axis, 1 ms to 100 s, five decades |
| **Note mark** | the tall mark at the left, filled by input level |
| **Repeat mark** | one stroke per repeat, left up, right down, height in dB |
| **Band** | the shaded span from the first repeat to the tail marker, tinted by wet level |
| **Tail marker** | the accent mark where the tail ends; dragging it sets Repeats and Shape |
| **Rail** | a slide-rule scale with one **handle** and an optional **index** |
| **Stock** and **slide** | Left is the stock; Right and Repeats are slides whose index sits under a stock value |
| **Chip** | a word in the top band that switches or cycles a discrete value |
| **Hairline** | the dashed line joining a held handle to what it moves |
| **Zone** | a hit-test region of the face |
| **Pending gesture** | the first pixels of a drag, before the axis is decided and locked |
| **Snap lock** | ratio snapping with hysteresis: capture at 1.2 %, release at 2.5 % in log ratio |
| **Telemetry** | what the engine reports for drawing: levels, effective times, hold |

### Engine

| Term | Meaning |
|---|---|
| **Line** | one fractional delay line (`chardsp::FractionalDelayLine`, Catmull-Rom) |
| **Stage** | one delay-and-process block: line → loop diffuser → tone → cut → loss → clip → crush → decimate |
| **Chain** | finite topology: up to 64 stages in series, no feedback; stage k feeds stage k+1 through the bleed rotation. Used when Shape ≥ −0.5 and not holding |
| **Loop** | infinite topology: one stage feeding itself through the bleed rotation with **lap gain**. Used when Shape < −0.5 or holding |
| **Lap** | one pass round the loop |
| **Gain law** | `gain[k]` per repeat from Shape and Repeats |
| **Bleed** | the 2×2 rotation between the lines: 8.6° stereo, 90° ping pong, 0° in tap modes |
| **Tap** | a second read head on one line at the other time |
| **Early diffuser** | the four-stage allpass on the input |
| **Loop diffuser** | the four-stage allpass inside each stage |
| **Roof** | the high cut Blur imposes |
| **Loss** | the medium's low-pass; Bucket's deepens with time |
| **Wobble** | the medium's time modulation |
| **Recipe** | one medium's constants: `{sine, sineHz, rand, randHz, lp, lpTime, bits, decimate, hiss}` |
| **Hiss**, **Crush**, **Decimate**, **Clip** | seeded mono noise; bit reduction; sample-and-hold rate reduction (Digital); soft clip transparent below 0.5 |
| **Tail** | how long the repeats last: `repeats × later time`, or 100 s when holding |
| **Laws** | the pure formulas shared by engine and face (§3) |

### Plumbing

| Term | Meaning |
|---|---|
| **Parameter table** | the constexpr table: id, identifier, name, unit, range, default, curve, options |
| **Values** | one `double` per parameter id; the state blob |
| **Edit** | one UI-originated change or gesture edge, queued to the audio thread |
| **Bridge** | the text protocol between face and plugin (§4) |
| **Metadata handshake** | the plugin sends the parameter table on `ready`; the face hard-codes nothing |
| **Frame** | one telemetry snapshot, pulled by the face every animation frame |

## 2. Modules and seams

```
┌──────────────┐   Values / Edits    ┌──────────────┐   Bridge (text)   ┌──────────────┐
│    Engine    │ ◄───────────────── │    Plugin    │ ◄──────────────► │     Face     │
│  (Engine.h)  │ ────────────────►  │ (Plugin.cpp) │                   │ (ui/face.js) │
│              │   Telemetry         │              │                   │              │
└──────┬───────┘                     └──────────────┘                   └──────┬───────┘
       │ Laws.h                                                                │ laws.js
       └──────────────── laws-fixture.json (generated by the C++ tests) ───────┘
```

**Engine** (`Engine.h`, namespace `slide`): deep module. Interface: `prepare(sampleRate)`,
`reset()`, `set(Parameter, value)`, `setTempo(bpm)`, `process(in, out, frames)`,
`telemetry()`. Everything else is implementation, built on chardsp's
`FractionalDelayLine`, `AllpassDiffuser` and `OnePole`. Tests drive it through this interface.

**Laws** (`Laws.h`, `ui/laws.js`): pure functions, identical in both languages, pinned by
`tests/laws-fixture.json` which the C++ test writes and the JS test checks:
`linkRight`, `divisionMs`, `gainLaw` / `lapGain` / `isLoop`, `toneLaw`, `recipeAt`, `tailMs`,
`nearestNiceRatio`.

**Parameters** (`Parameters.h`): the table and helpers. Ids append-only; state indexed by id.

**Plugin** (`Plugin.h/.cpp`): the CLAP adapter, mote's shape. `clap::helpers::Plugin`, params,
state, preset load and discovery, `char_clap::WebUI`, `processEventChunks`, a
`ParamQueue<Edit,128>` for UI edits, transport tempo per block. No DSP, no drawing.

**Face** (`ui/face.js` defines `<slide-face>`; `ui/main.js` is the bridge adapter): one canvas.
Every continuous control is a compost `createValueControl` sharing the canvas as event
target with `pointerTarget: null`; the face hit-tests, decides the axis, then calls
`startPointerDrag`. Each control has its own small semantic element, so keyboard and
ARIA come from compost. Sync and Hold are `compost-button mode="switch"`; Link, Mode and
Medium are `compost-button mode="cycle"`. Hit regions, cursors, hairlines, snap lock, tick generation and
the link rules stay in the face.

## 3. Laws

From the prototype; the spec until a listening test says otherwise.

```
gain[k]  = exp(−6.9·(−shape)·u)        shape < 0,  u = k/(repeats−1)
         = exp(−6.9·shape·(1−u))       shape ≥ 0
isLoop   = shape < −0.5 or hold
lapGain  = exp(−6.9·(−shape)/max(1, repeats−1));  1 when holding

roof     = 9000·2^(−3.3·blur)
toneLp   = tone < 0 ? 12000·2^(5.5·tone) : 20000
cutHz    = tone > 0 ? 20·2^(7·tone) : 20        clamped 20–2500
highCut  = clamp(min(roof, toneLp), 200, 20000);  20000 when holding
diffusion = min(1, 1.15·blur)·0.62
early     = max(0, (blur−0.3)/0.7)^1.3·0.62

amt = wear^1.8·5
tape    sine .0025@0.7Hz  rand .0012@6Hz   lp 9000            hiss .0003
oil     sine .005 @2.3Hz  rand .015 @1.4Hz lp 2600  bits 10   hiss .0004
bucket                    rand .0008@20Hz  lp 8000 (×√(60/T)) hiss .0005
tide    sine .006 @0.3Hz, partials 1 / 1.0355 / 0.518, ÷1.95
digital bits 16→5 with wear, decimate 48 kHz→2.5 kHz with wear, no wobble, no hiss
sine·amt, rand·amt·6, hiss·min(4, 0.9·amt); lp full at amt ≥ 0.5; crush at amt > 0.1
wobble reference time = clamp(T, 40, 400) ms

early diffuser  L {2.3, 5.1, 9.7, 15.3}  R {2.9, 6.1, 11.3, 17.9} ms
loop diffuser   L {4.7, 7.9, 13.1, 19.7} R {5.3, 8.9, 14.9, 22.3} ms
clip            |v| ≤ 0.5 pass; else sign·(0.5 + 0.5·tanh(2(|v|−0.5)))
wet clip        |v| ≤ 0.9 pass; else sign·(0.9 + 0.1·tanh((|v|−0.9)/0.1))
mix             dry·cos(mix·π/2) + wet·sin(mix·π/2)
smoothing       20 ms on times, lap gain, tone, mix, input gates
```

## 4. Bridge

Text over `postMessage`, as the family does. UI → plugin: `ready`, `begin:<id>`,
`value:<id>:<v>`, `end:<id>`, `preset:<key>`, `visual`. Plugin → UI: the parameter table as
tab-separated `parameter` lines then `metadata-end`; `values:<id>=<v>;…`; and the Frame
`visual:<inL>,<inR>,<wetL>,<wetR>,<leftMs>,<rightMs>,<hold>,<bpm>`. The face sends `visual`
once per animation frame while visible; the plugin answers with the newest of a
`ParamQueue<Frame,4>` filled every ~4 ms of audio. Values are engineering units; curves are
presentation.

## 5. Decisions

- **D1** Project `slide`, namespace `slide`, id `com.charlieculbert.slide`, presets
  `com.charlieculbert.slide.presets`, AUv2 `ChAr`/`Slid`/`aufx`, state magic `SLID` v1.
- **D2** Parameters: left, right, link, ratio, difference, sync, left_division,
  right_division, repeats, hold, shape, blur, tone, mix, mode, medium, wear. Feedback is
  derived, never a parameter. Right is a real parameter so Link = Off automates; while
  linked the engine derives it.
- **D3** Link is enforced in the engine so host automation of Left drags Right along.
- **D4** Sync uses division parameters, so tempo changes are exact.
- **D5** Chain memory is the prototype's: 64 stages × 2.1 s per channel, ~52 MB at 48 kHz.
  WCLAP gets a memory hint. No budget, no cap.
- **D6** Wear 0 is clean on every medium; the chip reads "Clean" there. Digital is the fifth
  medium, degrading by crush and decimation.
- **D7** No auto-hold. Hold is the ∞ zone of the Repeats rail and a bool parameter.
- **D8** Cmd/Ctrl bypasses snapping.
- **D9** Telemetry is pulled on the animation clock; drawing runs at display rate.
- **D10** Metadata handshake; the face hard-codes no ranges.
- **D11** The face is one canvas built on compost value controls (pinned `7b75bb3`).
- **D12** Delay primitives live in chardsp with runtime capacity: `FractionalDelayLine`
  (Catmull-Rom), `AllpassDiffuser`, `OnePole`. chardsp's linear `DelayLine` is untouched.
- **D13** A safety clip on the wet sum, after the stages and before the mix: 64 repeats
  can add up past full scale, so the wet path is a wire below 0.9 and a tanh knee above
  it, approaching but never passing 1. The dry path is untouched, so Mix 0 is still the
  input sample for sample.

## 6. Risks

- **R1** 128 diffusers and 384 filter states per sample. Measure in WCLAP at A4.
- **R2** The chain↔loop switch at Shape −0.5 needs a short crossfade. Test for clicks.
- **R3** Two implementations of the Laws; the fixture test is the guard.
- **R4** Crush, decimation and hiss thresholds are the prototype's; confirm by ear.

## 7. Plan

- **A1** Scaffold: submodules as mote plus chardsp, CMake, presets, Entry, licences, README
  skeleton. An empty effect builds natively and as WCLAP.
- **A2** Parameters, Laws, fixture, text round-trip tests.
- **A3** Engine, loop topology, with tests (integer and fractional delay, lap gain decay,
  ping pong, tap modes, hold, denormals, extremes, 44.1 kHz wrap).
- **A4** Engine, chain topology: gain law, topology switch, WCLAP CPU measurement.
- **A5** Plugin: params, state, presets, webview, event chunks, tempo, tail, Frame queue.
- **A6** Face: port `render()`, `pick()`, `move()` onto `<slide-face>` with value controls;
  `laws.js` and fixture test; bridge; Playwright test against the WCLAP DAW.
- **A7** Presets (ten, covering every medium, Mode, Link and corner of Shape), README,
  screenshot, notices.
