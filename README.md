# Slide Lab

A stereo delay plug-in inspired by slide rules ([see the International Slide Rule
Museum](https://www.sliderulemuseum.com/)). Two delay lines, left and right, are read
off one log time scale like a rule's: the right line is set from the left by a ratio
or a difference, and the face draws every repeat where it lands.

## Download

| Platform | Formats | Download |
|---|---|---|
| macOS | CLAP, VST3, AU | [Slide-Lab-macOS.zip](https://github.com/charCulbert/slide/releases/latest/download/Slide-Lab-macOS.zip) |
| Windows | CLAP, VST3 | [Slide-Lab-Windows.zip](https://github.com/charCulbert/slide/releases/latest/download/Slide-Lab-Windows.zip) |
| Linux | CLAP, VST3 | [Slide-Lab-Linux.zip](https://github.com/charCulbert/slide/releases/latest/download/Slide-Lab-Linux.zip) |
| Browser hosts | WCLAP | [SlideLab.wclap.tar.gz](https://github.com/charCulbert/slide/releases/latest/download/SlideLab.wclap.tar.gz) |

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
