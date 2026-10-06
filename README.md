# Slide Lab

A stereo delay with two lines, left and right, whose interface is a web page:
a picture of the repeats.

- `entry.cpp`: the CLAP entry point, which makes the plugin and lists its
  factory presets.
- `plugin.cpp`: the whole CLAP plugin: stereo in and out, parameters, state,
  presets, and the `clap.gui` and `clap.webview` extensions. `plugin.h` is what
  it gives `entry.cpp` and the tests.
- `dsp/`: the sound. `Engine.h` with `Diffuser.h` and `Laws.h`, built on
  chardsp's delay line and one-pole (`libs/chardsp`, a git submodule).
  `Parameters.h`: the parameter table. `Presets.h`: the factory presets.
- `resources/`: the files the plugin ships. Its page is in `resources/page/`
  (`index.html`, `main.js`, the face in `face.js` and `laws.js`); it talks to
  the plugin with `page/lib/messages.js`, which the build adds from
  `libs/webview/js/`, and draws with compost's controls in `page/compost/`.
- Hosts that support `clap.webview` (like WCLAP browser hosts) show the page
  themselves; elsewhere `libs/webview` shows it in a WebView (CHOC) inside the
  host's window. On iOS there is no interface yet.

## Build

chardsp is a submodule, so clone with `--recursive` (or run
`git submodule update --init` in a clone you already have):

```sh
cmake -B build && cmake --build build
```

That builds CLAP, VST3, AU (macOS) and a standalone app into `build/`. It needs CMake 3.28+ and a C++20 compiler (on Linux also `libgtk-3-dev` and `libwebkit2gtk-4.1-dev`).

**WCLAP** (for browser hosts) needs the [WASI SDK](https://github.com/WebAssembly/wasi-sdk/releases):

```sh
cmake -B build-wclap -DCMAKE_TOOLCHAIN_FILE=<wasi-sdk>/share/cmake/wasi-sdk-p1.cmake
cmake --build build-wclap        # makes build-wclap/SlideLab.wclap.tar.gz
```

**More formats**, each with one extra flag:

| Format | Configure with |
|---|---|
| AUv3 (macOS) | `cmake -B build-xcode -G Xcode` |
| iOS app with AUv3 | `cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS` (signing for a device needs your team in Xcode) |
| AAX | `cmake -B build-aax -DAAX_SDK_ROOT=<your AAX SDK>` (the SDK comes from Avid; shipping needs PACE signing) |

## Test

```sh
./build/slide-tests                 # the plugin, engine and laws (ctest --test-dir build runs it too)
node --test tests/laws.test.mjs     # resources/page/laws.js against tests/laws-fixture.json
```

`./build/slide-tests --write-fixture tests/laws-fixture.json` rewrites the
fixture from `Laws.h`. `tests/FaceE2E.mjs` and `tests/BrowserDaw.mjs` drive the
face in Chrome with Playwright (from the wclap-browser-daw checkout).

The workflow in `.github/workflows/` builds every push on macOS, Windows and
Linux, checks the plugins with clap-validator, pluginval and (macOS) auval,
and uploads them only if they pass.

`libs/` holds the reusable parts; see the comment at the top of each `.cmake`
file there.
