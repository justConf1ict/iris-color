# Third-party components

Iris Color's original DSP, wrapper and editor are released under the project's MIT license. The following independent components retain their own notices.

| Component | Use | Notice |
| --- | --- | --- |
| Steinberg VST3 SDK 3.8.1 | Plug-in interfaces and implementation helpers | `VST3-SDK-LICENSE.txt` (MIT) |
| LLVM libc++, libc++abi, libunwind and compiler runtime | Statically linked support in the supplied LLVM-MinGW Windows binaries | `LLVM-RUNTIME-LICENSE.txt` (Apache 2.0 with LLVM exceptions) |
| MinGW-w64 runtime | Windows startup and C runtime support in the supplied cross-compiled binaries | `MINGW-RUNTIME-LICENSE.txt` |
| MinGW-w64 winpthreads | Thread-support library notice, retained with the Windows build | `WINPTHREADS-LICENSE.txt` |
| DM Sans Regular and Medium | Unmodified fonts bundled with the native editor | `assets/fonts/OFL.txt` in source; `Contents/Resources/fonts/OFL.txt` in the installed bundle (SIL Open Font License 1.1) |

The pinned source repositories, exact commits and compiler archive checksum are in `SOURCES.json`. `tools/native_build_bootstrap.py` downloads build dependencies from their official repositories/releases; the compiler and the full SDK checkout are not included in the product download.

The native editor uses Windows drawing APIs and a private collection of the bundled DM Sans fonts. It does not install fonts into Windows. The font source revision and checksums are recorded in `assets/fonts/README.md`. The plugin contains no external artwork, sound samples or proprietary audio algorithms. Iris Color is an independent effect, with no affiliation to any similarly featured commercial plug-in. Its v0.6 editor takes visual inspiration from Autochroma's warm neutral palette and restrained controls; its own interface is drawn in code.

Private audition files distributed separately may contain the user's supplied bass recording and Iris-processed comparisons. Those recordings retain their original rights and are not covered by the project's MIT source-code license. They are excluded from the source archive and installer; the processor does not load or embed these recordings.
