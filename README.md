# Iris Color · v0.7.0 CrashFix

Windows x64 VST3 musical color effect with Prism/Legacy processing and Glass, Metal, Wood and String Material Body models.

**This is a pre-release pending real FL Studio acceptance.** Real FL64 dumps led to a native GUI crash reproduced and fixed in an isolated Windows VST3 host. Isolated-host success is not complete DAW validation.

[中文说明](README_zh.md) · [Pre-release](https://github.com/justConf1ict/iris-color/releases/tag/v0.7.0-crashfix.1) · [Investigation](reports/iris-color-fl-crash.md) · [Build and test](BUILDING.md)

## Downloads and installation

- [Windows VST3 ZIP](https://github.com/justConf1ict/iris-color/releases/download/v0.7.0-crashfix.1/Iris-Color-v0.7.0-CrashFix-Windows-x64.zip)
- [Complete source ZIP with pinned SDK](https://github.com/justConf1ict/iris-color/releases/download/v0.7.0-crashfix.1/Iris-Color-v0.7.0-CrashFix-Source.zip)
- [SHA-256 checksums](https://github.com/justConf1ict/iris-color/releases/download/v0.7.0-crashfix.1/SHA256SUMS.txt)

The [historical v0.5.0 release](https://github.com/justConf1ict/iris-color/releases/tag/v0.5.0) remains available. Its release classification does not establish real FL Studio acceptance. GitHub's automatically generated source archives contain neither plugin binaries nor the SDK checkout; the attached source ZIP supports an offline build.

Save your project and fully close all hosts using Iris. Back up the existing `Iris Color.vst3`, then copy the ZIP's entire same-named directory into `C:\Program Files\Common Files\VST3\`, keeping Contents/resources/fonts. Administrator permission may be required. Never overwrite a loaded DLL or delete other plugins.

If the old target is a single file, move that exact item to a backup location first. This pre-release has no Setup EXE; the build guide provides the existing NSIS generation command. The earlier Fix1 installer only migrated legacy paths and did not repair this GUI crash. Verify/rescan Iris in FL and test an empty project before important sessions.

## Fix and validation

MinGW's GDI+ GenericTypographic wrapper cache could outlive the GDI+ heap when the last editor closed. The original DLL failed on its second editor opening at `+0x3A8F6`, matching the FL dumps, without audio processing. Text drawing now owns its StringFormat within the paint callback. Body and the full GUI remain enabled; DSP, parameter IDs, state format and VST3 class IDs are unchanged. Windows SDK macro conflicts were resolved and missing DM Sans source assets restored.

Verified: 17 CTest cases, 180 audio configurations, 16 transport groups, ASan, two simultaneous editors and separate 10-minute GUI-only/combined tests with complete private-font resources. See the [verification record](reports/validation/crashfix/verification.json) and [test output](reports/validation/crashfix).

Known limits: real FL acceptance is pending; strict text pixel equivalence failed because of small margin/subpixel differences; ASan processed 60 seconds of audio in about 268 seconds and is not a realtime-performance pass. The embedded version remains `0.7.0`; identify this build by tag and DLL hash.

## Use and source

Choose Root/Scale, keep Color above 0%, then gradually raise Body Amount. Body defaults to 0%, including older states without Body records. The materials use 24 fixed modal slots and are physically inspired approximations, not measured materials or full physical simulations.

[DSP](src) · [VST3/native editor](plugin) · [Tests](tests) · [Build instructions](BUILDING.md). Original code is [MIT licensed](LICENSE); fonts are SIL OFL and third-party components retain their [notices](third_party/NOTICES.md).
