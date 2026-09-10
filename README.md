# Tide

A stereo delay that moves. Two delay lines, each with its own time, share a
feedback loop that can be damped, saturated, diffused, crossed into the other
channel and transposed. The repeats drift and swell with the modulation, so the
echoes sound like water rather than a metronome.

What it does with a repeat:

- **The two sides are independent.** Left and right have their own time.
  **Offset** sets the right one as a multiple of the left, and **Spread** moves
  them symmetrically apart around the set time without either side bouncing.
- **Cross** rotates the feedback between the channels. At 0% the sides stay
  separate, at 100% the repeats alternate channels. Rotation is what keeps the
  image: a plain mix would collapse to mono at half way, this stays wide and
  the crossed repeats come back inverted.
- **Feedback** up to 100% sustains. **Freeze** mutes the input and closes the
  loop so the tail holds, **Clear** flushes the lines.
- **Diffusion, twice.** **Early** diffuses on the way in, so the very first
  repeat already arrives smeared. **Diffuse** diffuses inside the loop, so every
  repeat smears a little further than the one before.
- **Pitch** transposes the repeats by up to an octave either way, inside the
  loop, so with feedback up they climb one interval per pass.
- **Stretch** scales the time between repeats. **Warp** links pitch to it, so
  stretching them out drops them as well, the way a transport does when it runs
  slow. **Stretch** holds the pitch and only moves the time.
- **Modulation** on the delay time (sine, triangle, ramp or smooth random) plus
  **Drift**, both interpolated, with the channels a quarter cycle apart. The
  modulation is audible as movement and visible as the shape the repeats ride
  on.

Controls, in signal order:

- **Time** — delay time, 1–2000 ms, used when Free.
- **Division** — 1/1 … 1/32 with dotted and triplet values, used when Synced.
- **Offset** — right channel time as a multiple of the left (0.25–4×).
- **Spread** — moves left and right apart around the set time, up to 1.6× and
  0.4× of it. Wide, still on the grid, and the repeats stay on their own side.
- **Feedback** — how much of each repeat returns, 0–100% where 100% sustains.
- **Cross** — 0–100% of the feedback rotated into the other channel.
- **Early** — input diffusion. The first repeat arrives smeared instead of sharp.
- **Diffuse** — loop diffusion. Each repeat comes back smeared a little further.
- **Damping** — high cut in the feedback path, so repeats darken as they go.
- **Low Cut** — low cut in the feedback path, so repeats thin out as they go.
- **Drive** — saturation in the feedback path. Transparent below full scale and
  soft above it, so the loop cannot run away.
- **Pitch** — −12 to +12 semitones on the repeats.
- **Stretch** — 0.5× to 1.5× the time between repeats, with the Warp / Stretch
  switch deciding whether the pitch follows.
- **Rate**, **Depth**, **Shape**, **Drift** — the movement of the delay time.
- **Mix** — dry to wet.
- **Width** — stereo width of the wet signal only. The dry image stays put.

Freeze, Sync and the Warp / Stretch switch sit next to the readout, which shows
the two delay times the engine is actually running.

Factory presets: Slap, Eighth Bounce, Quarter Drift, Underwater, Chorus Line,
Ramp Wash, Sixteenth Smear, Shimmer, Slow Lens.

## The field

The display is one picture of the whole loop rather than an ornament:

- Each **repeat is a tick**, left ones rising and right ones falling, placed on
  a log time axis. Height and brightness follow the feedback, soft clipped by
  **Drive** exactly as the audio is, so a driven delay draws a flattened tail.
- **Diffusion** widens each tick into a smear, and later repeats carry more of
  it, which is what cumulative diffusion looks like.
- **Cross** splits each repeat's energy between its own side and the other one,
  so a ping pong reads as the ticks flipping sides.
- The **waterline is the modulation**: each point sits where the LFO was when
  the material from that delay time entered, so the repeats ride the shape that
  moved them. The trails behind them are the modulation's motion.
- **Mix** is the balance between the dry tick at the left edge and the combs.
- **Tone** dims the field, **Stretch** moves the combs apart, and **Freeze**
  closes the frame.

![Tide running in the browser DAW](screenshot.png)

## Build

Requires CMake 3.24+, a C++17 compiler, and Ninja or Xcode. Dependencies are
pinned submodules under `external/`.

```sh
git clone --recurse-submodules https://github.com/charCulbert/Tide.git
cd Tide
cmake --preset native
cmake --build --preset native
ctest --preset native
```

For WCLAP, install WASI SDK 33.0 with pthread support:

```sh
export WASI_SDK_ROOT=/path/to/wasi-sdk
cmake --preset wclap
cmake --build --preset wclap
```

For macOS AUv3:

```sh
cmake --preset xcode
cmake --build --preset xcode --config Release
```

Outputs are written to `build-*/artifacts/`. The WCLAP archive is
`build-wclap/artifacts/Tide.wclap.tar.gz`.

## Tests

`ctest --preset native` runs the engine tests: delay timing to the sample,
fractional interpolation level, feedback decay, ping pong alternation, stereo
image at every cross setting, spread, early diffusion, granular pitch, warp
against stretch, freeze, denormal flushing, modulation range, tempo sync, width,
dry path, stability at extreme settings, and the parameter, state, preset and
audio port behaviour through the CLAP entry point.

`clap-validator` passes on the built CLAP: 19 passed, 0 failed, 2 skipped (the
note port tests, which do not apply to an effect).

`tests/BrowserDaw.mjs` loads the WCLAP into the browser DAW, plays a note through
it and checks the web UI: parameter metadata, live telemetry from the audio
thread, canvas rendering, the sync and mode switches, parameter round trips and
the compact window layout. Run it with the DAW's dev server up:

```sh
(cd ../../wclap-browser-daw && npm run dev -- --port 8470) &
node tests/BrowserDaw.mjs
```

## Credits

Built with these projects. Credit goes to their maintainers and contributors:

- [CLAP](https://github.com/free-audio/clap) and [clap-helpers](https://github.com/free-audio/clap-helpers) — the project maintainers and contributors.
- [clap-wrapper](https://github.com/free-audio/clap-wrapper) — the project maintainers and contributors; this project uses my fork.
- [CHOC](https://github.com/Tracktion/choc) — the project maintainers and contributors.
- [Compost](https://github.com/charCulbert/compost) and [char-clap-utils](https://github.com/charCulbert/char-clap-utils) — the project maintainers and contributors.
- [WASI SDK](https://github.com/WebAssembly/wasi-sdk), [LLVM](https://github.com/llvm/llvm-project), and [wasi-libc](https://github.com/WebAssembly/wasi-libc) — their maintainers and contributors, including the wider upstream projects they incorporate.

## License

ISC. See [LICENSE](LICENSE). Each WCLAP archive contains this license and
[third-party notices](THIRD_PARTY_NOTICES.md), with the full dependency license
texts in `THIRD_PARTY_LICENSES/`.
