# Windows build and regression checks

Requirements: Visual Studio C++ tools, CMake, Python 3, and a Windows SDK. The verified build used MSVC 19.41.34120 and SDK 10.0.22621.0. Build only in a local source directory; these commands do not install the plugin.

For a Git checkout or GitHub-generated source archive, fetch the pinned official SDK first:

```powershell
python tools/native_build_bootstrap.py
```

The separately attached **CrashFix-Source.zip includes SDK source without Git metadata**. Skip that bootstrap in the attached archive and configure directly:

```powershell
cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64 "-DIRIS_BUILD_REGRESSION_TESTS=ON"
cmake --build build/msvc --config RelWithDebInfo --parallel 4
ctest --test-dir build/msvc -C RelWithDebInfo --output-on-failure --parallel 2
```

If needed, select an installed SDK with `-DCMAKE_SYSTEM_VERSION=10.0.22621.0`. Runtime output is under `build/msvc/RelWithDebInfo/Iris Color.vst3/Contents/x86_64-win/`.

Check that both TTF files named in `assets/fonts/README.md` are present. The fonts are private resources and are not installed into Windows.

## Dynamic native host tests

```powershell
$tests = "build/msvc/RelWithDebInfo"
$plugin = "$tests/Iris Color.vst3/Contents/x86_64-win/Iris Color.vst3"
& "$tests/gui_lifecycle_test.exe" $plugin 600 25
& "$tests/gui_controls_test.exe" $plugin
& "$tests/stability_test.exe" $plugin matrix
& "$tests/stability_test.exe" $plugin audio 60
& "$tests/stability_test.exe" $plugin combined 600
& "$tests/host_contract_test.exe" $plugin
& "$tests/transport04_test.exe" $plugin
```

Each must return exit code 0. These tests use their own offscreen windows. The older `host_test` GUI/menu checks use process-global SendInput; do not run them while working in another application. The new `host_contract_test` reuses its processor/automation/state checks without those GUI input checks.

`dsp_color04_neutral` requires a separate v0.3 reference build and is not included in the 17 CTest cases. No result for it is claimed by this release.

For MSVC ASan, configure a separate directory with `-DIRIS_ENABLE_ASAN=ON`, use RelWithDebInfo, and put the installed MSVC Hostx64/x64 directory on that test process's PATH. Do not distribute the instrumented plugin as the normal product. Windows ASan does not replace UBSan/TSan or complete leak checking.

## Installer generation

The current pre-release ships a ZIP. With an official NSIS compiler supporting `amd64-unicode` already available, generate an EXE using the existing packager:

```powershell
python tools/build_windows_installer.py --bundle "build/msvc/RelWithDebInfo/Iris Color.vst3" --version 0.7.0 --output "dist/Iris-Color-v0.7.0-CrashFix-Windows-x64-Setup.exe" --makensis "C:/path/to/NSIS/makensis.exe" --nsisdir "C:/path/to/NSIS"
```

Add `--prepare-only` to validate the bundle and prepare inputs without compiling or installing anything. Never relabel an old installer as a crash fix. Close all plugin hosts before installing.

The committed verification summary describes tests actually run on the supplied DLL, not every future build from these sources. Real FL Studio acceptance is pending.
