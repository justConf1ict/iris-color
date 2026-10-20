# Iris Color v0.7.0 Fix1 — legacy single-file VST3 migration

## Cause

The v0.7.0 NSIS installer expects `Iris Color.vst3` in the system VST3
folder to be a **directory**. Older installations may have a **single file**
(or an explicit reparse-point link) at that exact path. The unmodified installer
stops with `This location is a link or a file, not a normal plugin directory`.

## What Fix1 does

`tools/build_legacy_compat_setup.py` builds a native x64, no-CRT EXE that
embeds the original, CRC-verified v0.7.0 NSIS installer. It needs Clang and
`lld-link` on Linux. It does not change the plug-in DSP, identity or version.

On launch it elevates through Windows UAC, resolves Windows system folders,
then examines exactly one path:

    C:\Program Files\Common Files\VST3\Iris Color.vst3

- Missing: launch the unmodified installer.
- Normal directory: launch the unmodified installer (the ordinary upgrade).
- Ordinary file or reparse point: try to open it exclusively, and then rename
  the **item itself**, not the reparse target, to a vacant numbered backup in
  `C:\Program Files\Iris Color\Legacy-Backups`. No destination backup is
  replaced; only one exact VST3 path is migrated.
- Launch the original NSIS setup with the same elevated token and wait for it.
- Check that the new directory and its x64 VST3 DLL exist.
- On failure, restore the legacy item only if the destination path is absent;
  otherwise keep the backup safe for manual recovery and report the problem.

Original NSIS checks, including file locks and uninstall metadata, remain
active. The backup directory is **not** owned by its uninstaller and remains
on disk until the user deliberately removes it. This is intentional.

**Limitations:** The EXE is unsigned. Wine and native Windows/FL Studio runtime
validation are not available in this build environment. The wrapper is verified
by structural PE/import tests, payload CRC32 + SHA-256 checks, static code
inspection and reproducible build; the Windows user must verify final install.

## Rebuild

```sh
python3 tools/build_legacy_compat_setup.py \
  --original Iris-Color-v0.7.0-Windows-x64-Setup.exe \
  --output Iris-Color-v0.7.0-Windows-x64-Setup-Fix1.exe
```

No NSIS rebuild is required for this hotfix. The original NSIS installer can
be recovered from Fix1 by parsing its final 16-byte `IRIS07F1` footer:
`[8-byte magic][4-byte little-endian payload size][4-byte CRC32]`.
The payload is immediately before the footer. Validate its CRC, and use the
original NSIS `validate_nsis_installer` routine before repacking.
