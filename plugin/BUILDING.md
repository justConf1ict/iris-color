# Iris Color 0.6.0 · Build entry point

The current build, installation and validation instructions are in the [English README](../README.md) and [中文 README](../README_zh.md). Run commands from the project root.

## Windows plugin from Linux x64

```sh
python3 tools/native_build_bootstrap.py --cross-windows
python3 tools/native_build.py --target windows --test-host
```

The [bootstrap](../tools/native_build_bootstrap.py) downloads the pinned SDK and cross-compiler. The [native build](../tools/native_build.py) produces `build/windows/Iris Color.vst3`, the WAV renderer and **seven Windows verification executables**, including `state_compat06_test.exe`. The cross-build compiles these Windows tests; their runtime results are recorded separately.

For a Linux adapter build with execution of its host, transport, state and preset tests:

```sh
python3 tools/native_build_bootstrap.py
python3 tools/native_build.py --target linux --test-host
```

## Setup and release checks

- Install NSIS separately, then use [build_windows_installer.py](../tools/build_windows_installer.py) to package the Windows bundle. Commands and options are in the [installer guide](../packaging/windows/README.md).
- [verify_windows06.py](../tools/verify_windows06.py) runs the seven Windows programs and checks the actual native editor. It runs directly on Windows or accepts an optional Python Wine runner.
- [verify_renderer06_parity.py](../tools/verify_renderer06_parity.py) compares the Linux and Windows renderers on Clarity 35 and combined-effects Clarity 70 profiles.

The source archive excludes the SDK checkout, downloaded toolchains, optional Wine runner/runtime and private reference audio. Current release-validation status belongs in the root README; build success by itself does not establish target-DAW acceptance.

## Historical instructions

The previous contents of this file are preserved unchanged in [BUILDING-v0.5.0.md](../docs/BUILDING-v0.5.0.md). Its version numbers, six-program test list and historical evidence links describe the old release. Use the root README and current scripts for v0.6.
