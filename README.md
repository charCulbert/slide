# Slide

A stereo delay plug-in inspired by slide rules ([see the International Slide Rule
Museum](https://www.sliderulemuseum.com/)).

## Download

| Platform | Formats | Download |
|---|---|---|
| macOS | CLAP, VST3, AU | [Slide-macOS.zip](https://github.com/charCulbert/slide/releases/latest/download/Slide-macOS.zip) |
| Windows | CLAP, VST3 | [Slide-Windows.zip](https://github.com/charCulbert/slide/releases/latest/download/Slide-Windows.zip) |
| Linux | CLAP, VST3 | [Slide-Linux.zip](https://github.com/charCulbert/slide/releases/latest/download/Slide-Linux.zip) |
| Browser hosts | WCLAP | [Slide.wclap.tar.gz](https://github.com/charCulbert/slide/releases/latest/download/Slide.wclap.tar.gz) |

All releases are on the [releases page](https://github.com/charCulbert/slide/releases).

## Build

chardsp is a submodule, so clone with `--recursive`:

```sh
git clone --recursive https://github.com/charCulbert/slide.git
cd slide
cmake -B build
cmake --build build
```

That builds CLAP, VST3, AU (macOS) and a standalone app into `build/`. It needs
CMake 3.28 or later and a C++20 compiler; on Linux also `libasound2-dev`,
`libgtk-3-dev` and `libwebkit2gtk-4.1-dev`.

WCLAP, for browser hosts, needs the [WASI SDK](https://github.com/WebAssembly/wasi-sdk/releases):

```sh
cmake -B build-wclap -DCMAKE_TOOLCHAIN_FILE=<wasi-sdk>/share/cmake/wasi-sdk-p1.cmake
cmake --build build-wclap
```

To run the tests after building: `./build/slide-tests` and
`node --test tests/laws.test.mjs`.
