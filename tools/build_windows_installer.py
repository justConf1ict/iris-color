#!/usr/bin/env python3
"""Package an already-built Windows x64 Iris Color VST3 with local NSIS."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib


ROOT = Path(__file__).resolve().parents[1]
BINARY_PATH = "Contents/x86_64-win/Iris Color.vst3"
RESERVED_NAMES = {"CON", "PRN", "AUX", "NUL", *(f"COM{x}" for x in range(1, 10)), *(f"LPT{x}" for x in range(1, 10))}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def windows_relative_path(relative):
    path = PurePosixPath(relative)
    if path.is_absolute() or not path.parts or any(part in (".", "..") for part in path.parts):
        raise ValueError(f"Invalid Windows path: {relative!r}")
    for part in path.parts:
        if (part[-1:] in (" ", ".") or part.split(".", 1)[0].upper() in RESERVED_NAMES
                or any(ord(c) < 32 or c in '<>:"|?*\\' for c in part)):
            raise ValueError(f"Invalid Windows path: {relative!r}")
    return str(path).replace("/", "\\")


def nsis_string(text):
    return str(text).replace("$", "$$").replace('"', '$\\"')


def nsis_compile_string(text):
    # File/OutFile/include paths are compiler inputs, not runtime NSIS strings.
    # In those inputs $$ is literally two dollar characters, so do not apply
    # the runtime escaping above. Reject syntax that could expand a macro.
    text = str(text)
    if any(c in text for c in ('"', "\r", "\n", "${", "$%")):
        raise ValueError("Build paths cannot contain quotes, newlines or NSIS macro syntax")
    return text


def read_pe_version(path):
    """Read RT_VERSION from the PE resource tree, without executing the plugin."""
    data = path.read_bytes()

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError("Truncated Windows PE binary")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("Plugin is not a Windows x64 PE DLL")
    pe = unpack("<I", 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Plugin is not a Windows x64 PE DLL")
    machine, sections, _, _, _, optional_size, flags = unpack("<HHIIIHH", pe + 4)
    optional = pe + 24
    if machine != 0x8664 or unpack("<H", optional)[0] != 0x20B or not flags & 0x2000:
        raise ValueError("Plugin must be a Windows x64 PE32+ DLL")
    if optional_size < 136 or unpack("<I", optional + 108)[0] < 3:
        raise ValueError("Plugin has no Windows version resource")
    resource_rva, resource_size = unpack("<II", optional + 128)

    def rva_to_offset(rva, size):
        for index in range(sections):
            section = optional + optional_size + index * 40
            _, address, raw_size, raw_offset = unpack("<IIII", section + 8)
            delta = rva - address
            if 0 <= delta and delta + size <= raw_size:
                offset = raw_offset + delta
                if offset + size <= len(data):
                    return offset
        raise ValueError("Invalid Windows PE resource address")

    if not resource_rva or not resource_size:
        raise ValueError("Plugin has no Windows version resource")
    resource_base = rva_to_offset(resource_rva, min(resource_size, 16))

    def entries(directory):
        if directory + 16 > resource_size:
            raise ValueError("Invalid Windows PE resource directory")
        named, numbered = unpack("<HH", resource_base + directory + 12)
        count = named + numbered
        if count > 4096 or directory + 16 + count * 8 > resource_size:
            raise ValueError("Invalid Windows PE resource entries")
        return [unpack("<II", resource_base + directory + 16 + index * 8) for index in range(count)]

    type_entry = next((target for name, target in entries(0) if name == 16), None)
    if type_entry is None or not type_entry & 0x80000000:
        raise ValueError("Plugin has no Windows RT_VERSION resource")
    versions = []
    for _, name_entry in entries(type_entry & 0x7FFFFFFF):
        if not name_entry & 0x80000000:
            raise ValueError("Invalid Windows version resource name")
        for _, language_entry in entries(name_entry & 0x7FFFFFFF):
            if language_entry & 0x80000000 or language_entry + 16 > resource_size:
                raise ValueError("Invalid Windows version resource language")
            rva, size, _, _ = unpack("<IIII", resource_base + language_entry)
            start = rva_to_offset(rva, size)
            length, value_length, value_type = unpack("<HHH", start)
            key = "VS_VERSION_INFO\0".encode("utf-16-le")
            if (length > size or value_length < 52 or value_type != 0
                    or data[start + 6:start + 6 + len(key)] != key):
                raise ValueError("Invalid Windows VS_VERSION_INFO resource")
            value = start + ((6 + len(key) + 3) & ~3)
            if value + 52 > start + length:
                raise ValueError("Truncated Windows fixed version information")
            signature, revision, ms, ls = unpack("<IIII", value)
            if signature != 0xFEEF04BD or revision != 0x10000:
                raise ValueError("Invalid Windows fixed version information")
            versions.append((ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF))
    if not versions or len(set(versions)) != 1:
        raise ValueError("Missing or conflicting Windows binary versions")
    return versions[0]


def validate_nsis_installer(path):
    """Check the unsigned NSIS file's x64 PE header, length and forced CRC.

    NSIS' firstheader is at a 512-byte boundary. Its length includes that
    header and the trailing CRC. The normal NSIS CRC excludes the first 512
    PE-header bytes; see Source/exehead/fileform.c in the official NSIS source.
    """
    data = path.read_bytes()
    if len(data) < 512 or data[:2] != b"MZ":
        raise ValueError("NSIS did not produce a Windows installer executable")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if (pe + 26 > len(data) or data[pe:pe + 4] != b"PE\0\0"
            or struct.unpack_from("<H", data, pe + 4)[0] != 0x8664
            or struct.unpack_from("<H", data, pe + 24)[0] != 0x20B):
        raise ValueError("NSIS installer must be a Windows x64 PE32+ executable")
    magic = (0xDEADBEEF, 0x6C6C754E, 0x74666F73, 0x74736E49)
    for offset in range(512, len(data) - 27, 512):
        header = struct.unpack_from("<7I", data, offset)
        if header[1:5] != magic:
            continue
        flags, length = header[0], header[6]
        if not flags & 8 or flags & 4:
            raise ValueError("NSIS installer must retain forced CRC checking")
        if length < 32 or offset + length != len(data):
            raise ValueError("NSIS installer is truncated or has an invalid payload length")
        expected = struct.unpack_from("<I", data, len(data) - 4)[0]
        actual = zlib.crc32(data[512:-4])
        if expected != actual:
            raise ValueError("NSIS installer CRC mismatch")
        return f"{actual:08x}"
    raise ValueError("NSIS installer is truncated or has no valid payload header")


def payload_manifest(bundle, version):
    if bundle.is_symlink():
        raise ValueError("Symlink bundle roots are not allowed")
    binary = bundle / BINARY_PATH
    if not binary.is_file():
        raise ValueError(f"Required plugin binary is missing: {BINARY_PATH}")
    seen = {}
    files = []
    for path in sorted(bundle.rglob("*")):
        relative = path.relative_to(bundle).as_posix()
        windows_relative_path(relative)
        folded = relative.casefold()
        if folded in seen:
            raise ValueError(f"Windows case-insensitive path collision: {seen[folded]!r} and {relative!r}")
        seen[folded] = relative
        if path.is_symlink():
            raise ValueError(f"Symlink payload entries are not allowed: {relative}")
        if path.is_file():
            files.append({"path": relative, "bytes": path.stat().st_size, "sha256": digest(path)})
        elif not path.is_dir():
            raise ValueError(f"Unsupported payload entry: {relative}")
    binary_version = read_pe_version(binary)
    actual_version = ".".join(str(part) for part in binary_version)
    if binary_version != (*version, 0):
        raise ValueError(f"Plugin binary version {actual_version} does not match requested release {'.'.join(map(str, version))}.0")
    return {"product": "Iris Color", "version": ".".join(map(str, version)), "binary_version": actual_version,
            "architecture": "Windows x64", "bundle": "Iris Color.vst3", "files": files}


def payload_include(bundle, manifest):
    files = manifest["files"]
    directories = {""}
    for item in files:
        parent = PurePosixPath(item["path"]).parent
        while parent != PurePosixPath("."):
            directories.add(parent.as_posix())
            parent = parent.parent
    directories = sorted(directories, key=lambda path: (path.count("/"), path))

    def relative(path):
        return nsis_string(windows_relative_path(path)) if path else ""

    def destination(path):
        return "$BundleDir" + ("\\" + relative(path) if path else "")

    lines = ["; Generated from validated payload paths. Do not edit.", "!macro IrisStagePayload"]
    for item in files:
        path = PurePosixPath(item["path"])
        parent = relative(path.parent.as_posix()) if path.parent != PurePosixPath(".") else ""
        lines += [f'  SetOutPath "$PLUGINSDIR\\payload\\{parent}"',
                  f'  File "/oname={nsis_string(path.name)}" "{nsis_compile_string(bundle / item["path"])}"']
    lines += ["!macroend", "", "!macro IrisPreflight PREFIX"]
    for directory in directories:
        lines += [f'  StrCpy $CheckPath "{destination(directory)}"', '  Call "${PREFIX}CheckDirectory"']
    for item in files:
        lines += [f'  StrCpy $CheckPath "{destination(item["path"])}"', '  Call "${PREFIX}CheckFile"']
    lines += ["!macroend", "", "!macro IrisInstallPayload"]
    for directory in directories:
        lines += [f'  StrCpy $CheckPath "{destination(directory)}"', '  Call EnsureDirectory']
    # Copy the DLL last. A resource-copy failure therefore leaves the old DLL intact.
    for item in sorted(files, key=lambda item: item["path"] == BINARY_PATH):
        lines += [f'  StrCpy $CopySource "$PLUGINSDIR\\payload\\{relative(item["path"])}"',
                  f'  StrCpy $CheckPath "{destination(item["path"])}"', '  Call InstallFile']
    lines += ["!macroend", "", "!macro IrisRemovePayload"]
    for item in files:
        path = destination(item["path"])
        lines += ['  ClearErrors', f'  Delete "{path}"', '  ${If} ${Errors}',
                  f'    StrCpy $CheckPath "{path}"', '    Call un.RetryDelete', '  ${EndIf}']
    for directory in reversed(directories):
        lines += [f'  RMDir "{destination(directory)}"']
    lines += ["  ClearErrors", "!macroend", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, default=ROOT / "build/windows/Iris Color.vst3")
    parser.add_argument("--version", default="0.7.0", help="Must match the DLL's file version (with a final .0).")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--work-dir", type=Path, default=ROOT / "build/installer/windows")
    parser.add_argument("--makensis", default="makensis", help="Path to the NSIS compiler; no automatic downloads.")
    parser.add_argument("--nsisdir", type=Path, help="Local NSIS data folder containing Include, Stubs and Plugins.")
    parser.add_argument("--prepare-only", action="store_true", help="Validate the bundle and generate build inputs without compiling.")
    args = parser.parse_args()
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", args.version):
        raise ValueError("Version must contain three nonnegative decimal integers")
    version = tuple(int(part) for part in args.version.split("."))
    if any(part > 65535 for part in version):
        raise ValueError("Windows version components must not exceed 65535")
    # Check before resolve(), which would hide a symlink bundle root.
    bundle_input = args.bundle.absolute()
    if bundle_input.is_symlink():
        raise ValueError("Symlink bundle roots are not allowed")
    bundle = bundle_input.resolve()
    manifest = payload_manifest(bundle, version)
    work = args.work_dir.resolve()
    output = (args.output or ROOT / f"dist/Iris-Color-v{args.version}-Windows-x64-Setup.exe").resolve()
    if work == bundle or bundle in work.parents or output == bundle or bundle in output.parents:
        raise ValueError("Installer work and output paths must be outside the source bundle")
    if output.suffix.lower() != ".exe":
        raise ValueError("Installer output must use the .exe extension")
    work.mkdir(parents=True, exist_ok=True)
    candidate = work / "installer-candidate.exe"
    manifest_path = work / "payload-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    include = work / "payload.nsh"
    include.write_text(payload_include(bundle, manifest), encoding="utf-8")
    script = work / "build-installer.nsi"
    defines = {"IRIS_VERSION": args.version, "IRIS_VERSION_QUAD": args.version + ".0", "IRIS_OUTPUT": str(candidate),
               "IRIS_PAYLOAD_NSH": str(include), "IRIS_PAYLOAD_MANIFEST": str(manifest_path),
               "IRIS_INSTALL_KB": str((sum(item["bytes"] for item in manifest["files"]) + 1023) // 1024)}
    script.write_text("\n".join(f'!define {key} "{nsis_compile_string(value)}"' for key, value in defines.items())
                      + f'\n!include "{nsis_compile_string(ROOT / "packaging/windows/IrisColor.nsi")}"\n', encoding="utf-8")
    if args.prepare_only:
        print(f"Prepared {len(manifest['files'])} payload files: {manifest_path}")
        return
    compiler = shutil.which(args.makensis)
    if compiler is None:
        raise ValueError("NSIS compiler not found; set --makensis and optionally --nsisdir")
    env = os.environ.copy()
    if args.nsisdir:
        env["NSISDIR"] = str(args.nsisdir.resolve())
    prefix = "/" if os.name == "nt" else "-"
    compiler_version = subprocess.run([compiler, prefix + "VERSION"], env=env, capture_output=True, text=True, check=True).stdout.strip()
    output.parent.mkdir(parents=True, exist_ok=True)
    candidate.unlink(missing_ok=True)
    command = [compiler, prefix + "WX", prefix + "NOCONFIG", prefix + "INPUTCHARSET", "UTF8", str(script)]
    result = subprocess.run(command, env=env, capture_output=True, text=True, check=False)
    log = work / "compiler.log"
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise ValueError(f"NSIS compilation failed (exit {result.returncode}); see {log}\n{result.stdout}{result.stderr}")
    if manifest != payload_manifest(bundle, version):
        candidate.unlink(missing_ok=True)
        raise ValueError("Source bundle changed during packaging; discarded installer. Rebuild from a stable bundle.")
    nsis_crc = validate_nsis_installer(candidate)
    # A failed compiler may leave a truncated stub. Never let it replace a
    # completed output. Commit only the verified candidate, atomically, on the
    # output directory's filesystem (which may differ from the work directory).
    staged = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".iris-installer-", suffix=".exe", dir=output.parent, delete=False) as destination:
            staged = Path(destination.name)
            with candidate.open("rb") as source:
                shutil.copyfileobj(source, destination)
            destination.flush()
            os.fsync(destination.fileno())
        validate_nsis_installer(staged)
        staged.replace(output)
    finally:
        if staged is not None:
            staged.unlink(missing_ok=True)
    receipt = {"installer": output.name, "version": args.version, "bytes": output.stat().st_size,
               "sha256": digest(output), "compiler": compiler_version, "compiler_sha256": digest(Path(compiler)),
               "nsis_crc32": nsis_crc, "payload": manifest, "command": command,
               "packager_sha256": digest(Path(__file__)), "installer_script_sha256": digest(ROOT / "packaging/windows/IrisColor.nsi"),
               "windows_runtime_validation": "Not performed by this build command; run installation/upgrade/uninstall checks on Windows."}
    receipt_path = output.with_suffix(".build-receipt.json")
    receipt_path.write_text(json.dumps(receipt, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    output.with_suffix(".sha256").write_text(f"{receipt['sha256']}  {output.name}\n", encoding="utf-8")
    print(f"Built: {output}\nPayload files: {len(manifest['files'])}\nSHA-256: {receipt['sha256']}\nReceipt: {receipt_path}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Installer build failed: {error}", file=sys.stderr)
        raise SystemExit(1)
