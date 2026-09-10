# Slide — UI prototype

A stereo delay whose face is a picture of the repeats, with slide-rule rails
as the controls. This folder is the interaction prototype for the plugin that
`../Engine.h` will eventually drive. It runs in a browser with a Web Audio
port of the engine so the picture and the sound can be judged together.

## Files

- `slide.html` — the prototype. One canvas draws the picture, the rails, the
  chips and the telemetry; a `compost-window` frames it on desktop and it
  stacks as a panel on phones. Imports compost from `../external/compost`.
- `slide-engine.js` — the engine as an AudioWorklet processor, mirroring
  `../Engine.h`: two delay lines, rotation bleed, one-pole tone and low cut,
  saturator, four-stage Schroeder allpass diffusers with the engine's stage
  times, seeded wobble, and either a feedback loop or a chain of up to 64
  stages for the finite decay shapes. `WOB` holds the medium recipes.
- `slide-standalone.html` — `slide.html` with the compost window and the
  engine inlined, built by `build.py`. Runs from a single file and is what gets
  published as a claude.ai artifact.
- `slide-doc.html` — the pitch and a parameter reference.
- `studies/` — the earlier boards this came out of, kept for the record:
  `01-rule-study.html` (the first face with presets and a node-graph engine),
  `02-faces-board.html` (eight layout ideas), `03-compost-windows.html`
  (faces in compost frames with compost knobs and sliders).

## Run

Serve the parent of `tide/` so the compost submodule resolves, then open the
page:

```sh
cd ../..            # the folder that contains tide/
python3 -m http.server 8765
# http://localhost:8765/tide/prototype/slide.html
```

Or open `slide-standalone.html` directly. MIDI needs a served page in Chrome;
audio, keys and the phrase player work either way.

To rebuild the standalone file after editing `slide.html` or the engine:

```sh
python3 build.py
```

## The face

- **Picture.** A log time axis from 1 ms to 100 s. The tall mark at the left is
  the input note. Every repeat is drawn at the moment it plays, left channel
  above the line, right below, height in dB, smeared by diffusion, greyed by
  tone, drifting with the medium. A shaded band runs from the first repeat to
  the accent marker where the tail ends. The note mark fills with input level
  and the band tints with wet output level.
- **Rails under the axis.** *Left* is the time scale. *Right* is a ratio scale
  whose index sits under the Left handle and slides with it. *Repeats* is a
  count scale whose index sits under the later of the two times, with a ∞ zone
  past 64 that engages hold. The slide-rule logic is literal: Right and Repeats
  are the slide, Left is the stock.
- **Rails beside the picture.** Sharp–Blur (diffusion, the track is drawn as
  the smear it makes), Fade–Swell (the decay envelope, continuous, flat in the
  middle), Dark–Thin (an extreme high cut below the middle, a low cut above).
- **Top band.** Link ratio / Link difference / Link off; Stereo / Ping pong /
  R is a tap / L is a tap; Sync; the medium (Tape, Oil can, Bucket, Tide,
  Clean) with a small strength rail; Mix.

## Gestures

- Drag above the line for Left, below for Right. Link decides whether one
  drags the other, and with link off the ratio snaps lightly to 1:2, 2:3, 3:4,
  1:1, 5:4, 4:3, 3:2, φ, 2:1, 3:1. Cmd or Ctrl bypasses snapping.
- Drag the marker sideways for repeats, up or down for shape. The first few
  pixels decide which, then it locks.
- Drag inside the shaded band sideways to slide both times together. Drag up
  or down in the left half for blur, in the right half for shape.
- Drag a rail handle for the same numbers. Every handle has a distinct cursor,
  lights up on hover, and while held a dashed hairline joins it to the thing it
  moves.
- Click a top-band label to cycle it. Drag the small rail after the medium
  name for its strength.

## Notes

- Fade below the midpoint of the Shape rail is a true feedback loop with the
  same envelope, so long tails and hold work. Above it the engine runs a chain
  of stages, which is what makes Flat and Swell possible.
- The bleed between lines is a rotation, not a mix, so hold keeps its energy
  with diffusion on.
- Delay reads wrap with an explicit guard for landing exactly on the buffer
  length; without it a 44.1 kHz session went NaN after three notes.
- Snapping, the medium wobble and the hiss are seeded, so the same settings
  play the same way every time.
