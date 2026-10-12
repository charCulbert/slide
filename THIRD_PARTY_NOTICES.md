# Third-party software

This plugin is built with, and its binaries include, the following. CMake downloads them: CMakeLists.txt pins a version of char-clap-template, and that version pins the rest; keep their notices with anything you distribute. Every build ships their full licence texts in THIRD_PARTY_LICENSES/.

- char-clap-template (its libs/core and libs/webview): MIT. https://github.com/charCulbert/char-clap-template
- CLAP SDK: MIT. https://github.com/free-audio/clap
- clap-wrapper, and the VST3 and AudioUnit SDKs it downloads: MIT (see each SDK's own licence). https://github.com/free-audio/clap-wrapper
- CHOC: ISC. https://github.com/Tracktion/choc
- chardsp (`libs/chardsp`, a git submodule; the fractional delay line and one-pole are used): MIT. https://github.com/charCulbert/chardsp
- compost (the files in `resources/page/compost/`, with its LICENSE there): MIT. https://github.com/charCulbert/compost
