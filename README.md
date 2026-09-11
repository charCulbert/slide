# Slide

Slide is a stereo delay whose face is a picture of the repeats, with slide-rule
rails as the controls. The browser prototype in `prototype/` is the behavioural
spec; `docs/DESIGN.md` records the vocabulary, the modules and their seams, the
decisions, and the build order.

At this stage the plug-in is a scaffold: one stereo in, one stereo out, and the
input passed through unchanged. The engine, the parameters and the face follow.

Available as CLAP, AUv3, and WCLAP.

## Build

Requires CMake 3.24+, a C++17 compiler, and Ninja or Xcode. Dependencies are
pinned submodules under `external/`.

```sh
git clone --recurse-submodules https://github.com/charCulbert/Slide.git
cd Slide
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
`build-wclap/artifacts/Slide.wclap.tar.gz`.

## Credits

Built with these projects. Credit goes to their maintainers and contributors:

- [CLAP](https://github.com/free-audio/clap) and [clap-helpers](https://github.com/free-audio/clap-helpers) — the project maintainers and contributors.
- [clap-wrapper](https://github.com/free-audio/clap-wrapper) — the project maintainers and contributors; this project uses my fork.
- [CHOC](https://github.com/Tracktion/choc) — the project maintainers and contributors.
- [Compost](https://github.com/charCulbert/compost) and [char-clap-utils](https://github.com/charCulbert/char-clap-utils) — the project maintainers and contributors.
- [chardsp](https://github.com/charCulbert/chardsp) — the project maintainers and contributors, with [elliptic-blep](https://github.com/Signalsmith-Audio/elliptic-blep) and [Signalsmith DSP](https://github.com/Signalsmith-Audio/dsp) and their maintainers and contributors.
- [WASI SDK](https://github.com/WebAssembly/wasi-sdk), [LLVM](https://github.com/llvm/llvm-project), and [wasi-libc](https://github.com/WebAssembly/wasi-libc) — their maintainers and contributors, including the wider upstream projects they incorporate.

## License

ISC. See [LICENSE](LICENSE). Each WCLAP archive contains this license and
[third-party notices](THIRD_PARTY_NOTICES.md), with the full dependency license
texts in `THIRD_PARTY_LICENSES/`.
