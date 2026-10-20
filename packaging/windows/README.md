# Windows installer

`tools/build_windows_installer.py` packages an already-built **Windows x64 VST3** as one offline NSIS installer. It does not build the plugin, download dependencies, modify the build toolchain, or install anything on the build machine.

The installer itself is an x64 Unicode executable. Use an NSIS distribution that includes the `amd64-unicode` stubs and plugins. NSIS 3.09-4 from Ubuntu 24.04 was used for the initial local compiler validation.

## Install or update Iris Color

1. Save your project and close FL Studio or another host that is using Iris Color.
2. Run `Iris-Color-v0.7.0-Windows-x64-Setup.exe` and approve Windows' administrator prompt.
3. Finish installation, then rescan plugins in your DAW. In FL Studio, open **Options → Manage plugins**, enable **Verify plugins** and **Rescan previously verified plugins**, then choose **Find installed plugins**.

The plugin is installed to the system's 64-bit Common Files location:

```text
C:\Program Files\Common Files\VST3\Iris Color.vst3
```

The drive and Program Files location are resolved by Windows; `C:` is only the usual example. An existing directory bundle at that exact location is updated in place. The VST3 identity is unchanged. There is no folder to drag manually, no desktop application shortcut, and no online download step inside the installer.

The installer does not discover or remove copies from custom VST3 folders. If an old manually installed copy lives elsewhere, resolve that duplicate yourself. A symlink or an old single-file `.vst3` at the intended bundle-directory path is detected and left untouched; move it before installing this directory bundle.

Uninstall using Windows **Settings → Apps → Installed apps → Iris Color**. The uninstaller removes the exact files included in its release, then removes directories only if they are empty. It preserves extra files, user presets, DAW projects, other plugins, and host settings. Factory resource filenames belong to the installation; user presets should not overwrite those filenames.

## Build from an existing bundle

From the repository root:

```sh
python3 tools/build_windows_installer.py \
  --bundle "build/windows/Iris Color.vst3" \
  --version 0.7.0 \
  --output "dist/Iris-Color-v0.7.0-Windows-x64-Setup.exe"
```

The default bundle is `build/windows/Iris Color.vst3`, produced by the existing Windows native build. The required binary is:

```text
Contents/x86_64-win/Iris Color.vst3
```

All files within the bundle, including nested fonts and license notices, are included. The packager checks the PE architecture and reads the DLL's actual `RT_VERSION` resource. Its file version must be exactly `0.7.0.0` when `--version 0.7.0` is requested. Passing an older DLL with a newer filename or version argument fails.

If NSIS is available only in a local directory:

```sh
python3 tools/build_windows_installer.py \
  --version 0.7.0 \
  --makensis /absolute/path/to/nsis/usr/bin/makensis \
  --nsisdir /absolute/path/to/nsis/usr/share/nsis \
  --output "dist/Iris-Color-v0.7.0-Windows-x64-Setup.exe"
```

`--nsisdir` sets `NSISDIR` for the child compiler only. It must contain matching `Include`, `Stubs`, and `Plugins` folders. Use compiler and common-data packages from the same distribution version. No global package installation is required.

The compiler runs with warnings treated as errors. Its output is first written as a work-directory candidate; the packager checks the x64 PE header, complete NSIS payload length, forced CRC, and source hashes before atomically replacing the final EXE. A failed or truncated compiler output cannot replace a completed installer. On success, the command produces the EXE, a `.sha256` checksum file, and a `.build-receipt.json` with installer and payload hashes. Intermediate manifests, the generated NSIS include, and `compiler.log` are retained in `build/installer/windows`, or a directory selected with `--work-dir`.

Use `--prepare-only` to validate the input and inspect the complete payload manifest without invoking NSIS. Source-bundle changes during compilation cause the command to discard that build.

The first installer release owns only the paths in its compiled manifest. If a future release retires an old resource filename, explicitly review its cleanup in that release; do not replace the exact manifest with a recursive bundle deletion.

## Installation behavior and failure handling

- The destination is fixed to `$COMMONFILES64\VST3\Iris Color.vst3`. An NSIS `/D` directory override is intentionally ignored.
- The uninstaller and payload manifest live separately in `$PROGRAMFILES64\Iris Color`. They are not placed in the VST3 scan folder.
- Installer and uninstaller share one named mutex and one stable 64-bit uninstall registry entry.
- Existing destination directories and files are checked for reparse points before changes. Payload symlinks, Windows-reserved filenames, and case-insensitive collisions are rejected when building.
- Every existing payload file is checked for exclusive write access before copying or removing any payload. A loaded/locked plugin produces a retry/cancel message. Neither interactive nor silent operation forcibly closes a host.
- The complete new payload is extracted before copying. Resource files are copied before the DLL. A later I/O failure reports an incomplete installation and allows retry; this lightweight installer does not promise transactional rollback after hardware, disk, or operating-system failure.
- No deletion is scheduled for a later reboot. A silent installer (`/S`) returns code `32` for a locked payload; retry dialogs default to Cancel. The uninstaller body uses the same guard. Its normal NSIS launcher starts a temporary copy, so the launcher's exit alone does not establish that removal finished; automated checks must also wait for the owned files and registry entry to disappear.
- Unknown bundle files and metadata-directory files are preserved. Only empty owned directories are removed.

## Verification

Packaging boundary checks:

```sh
python3 packaging/windows/test_installer.py
```

These exercise the actual packaging command using isolated PE resource fixtures. The fixtures are never executed or used in a distributed plugin. Tests cover the source manifest, real version parsing, non-x64 input, missing DLLs, symlinks, Windows path collisions and reserved names.

An NSIS compiler success establishes that the EXE was built. It does **not** establish that Windows UAC, a real DAW scan, or a Windows uninstall worked. The build receipt makes that limitation explicit. Record separate installation evidence for:

| Check | Required observation |
|---|---|
| Fresh installation | Every installed file matches the manifest hash at the standard x64 path |
| Upgrade from 0.6 | New payload replaces the same bundle; one uninstall entry remains |
| Reinstall same version | Same hashes, one uninstall entry |
| Locked DLL | Nonzero exit or retry/cancel; existing DLL remains unchanged |
| Uninstall | All manifest files removed; other-plugin and user-file sentinels survive |
| Real Windows 10/11 + FL Studio | UAC, scan/discovery, audio processing, project recall, and scaling checked in the target host |

Wine checks can cover installer filesystem behavior, registry behavior and lock handling. They do not replace the final real-Windows/FL-Studio checks. The current target-host acceptance remains pending until an actual Windows session verifies it.

For a synchronous uninstaller-body lock test, copy the generated uninstaller to a test directory and pass NSIS's `_?=` option last, with an unquoted path that contains no spaces. This suppresses its normal self-copy launcher. The script still resolves and checks the fixed system installation paths in `un.onInit`. Also test the normal installed uninstaller separately; do not treat this test-only invocation as the user uninstall procedure.

`hold_file_open.cpp` is a small Windows-only test helper for the lock scenario. It opens the specified file for read access without write sharing, prints `locked`, and waits up to 30 seconds for the given stop-file path. It does not write or execute the plugin. Build it with a Windows x64 compiler when running that check.

## Official references

- [Steinberg VST3 locations](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BLocations.html)
- [NSIS scripting reference](https://nsis.sourceforge.io/Docs/Chapter4.html)
- [NSIS command-line compiler](https://nsis.sourceforge.io/Docs/Chapter3.html)
- [NSIS on POSIX build systems](https://nsis.sourceforge.io/Docs/AppendixG.html)
- [Image-Line plugin installation and scanning](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm)

## Fix1: compatibility with legacy single-file VST3 installs

The original 0.7.0 NSIS file intentionally rejects a regular `.vst3` file or
symlink at the directory-bundle target, as described above. **Distribute the
Fix1 compatibility EXE** (`Iris-Color-v0.7.0-Windows-x64-Setup-Fix1.exe`)
for users upgrading from an old single-file layout. It makes a uniquely named
backup of the leaf item in `Program Files\Iris Color\Legacy-Backups` before
invoking this exact NSIS installer; no other installed plug-in is touched.
The backup is not removed by the uninstaller. Build and limitations are in
`packaging/windows/compat/README.md`.
