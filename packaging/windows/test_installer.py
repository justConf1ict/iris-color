#!/usr/bin/env python3
"""Checks the real packaging entry point's payload and path boundaries."""

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import runpy
import zlib


ROOT = Path(__file__).resolve().parents[2]
BUILD_TOOL = ROOT / "tools/build_windows_installer.py"


def versioned_pe(version=(0, 6, 0, 0), machine=0x8664):
    """Minimal PE resource fixture; never executed or used as a release plugin."""
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, machine, 1, 0, 0, 0, 240, 0x2022)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    struct.pack_into("<II", data, 0x98 + 128, 0x1000, 512)
    data[0x188:0x190] = b".rsrc\0\0\0"
    struct.pack_into("<IIII", data, 0x190, 512, 0x1000, 512, 0x200)
    for offset, name, target in ((0, 16, 0x80000020), (0x20, 1, 0x80000040), (0x40, 1033, 0x60)):
        struct.pack_into("<HHII", data, 0x200 + offset + 12, 0, 1, name, target)
    key = "VS_VERSION_INFO\0".encode("utf-16-le")
    struct.pack_into("<HHH", data, 0x280, 92, 52, 0)
    data[0x286:0x286 + len(key)] = key
    major, minor, patch, revision = version
    struct.pack_into("<IIII", data, 0x2A8, 0xFEEF04BD, 0x10000, (major << 16) | minor, (patch << 16) | revision)
    struct.pack_into("<IIII", data, 0x260, 0x1080, 92, 1200, 0)
    return bytes(data)


class InstallerPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="iris-installer-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bundle = self.root / "Iris Color.vst3"
        self.binary = self.bundle / "Contents/x86_64-win/Iris Color.vst3"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(versioned_pe())
        self.resources = self.bundle / "Contents/Resources"
        self.resources.mkdir()
        (self.resources / "LICENSE.txt").write_text("Fixture license\n")
        self.work = self.root / "prepared"

    def prepare(self):
        return subprocess.run(
            [sys.executable, str(BUILD_TOOL), "--bundle", str(self.bundle), "--version", "0.6.0",
             "--work-dir", str(self.work), "--output", str(self.root / "fixture-setup.exe"), "--prepare-only"],
            text=True, capture_output=True, check=False,
        )

    def test_prepared_manifest_covers_binary_and_nested_unicode_resource(self):
        font = self.resources / "Fonts" / "字体$font.otf"
        font.parent.mkdir()
        font.write_bytes(b"font fixture")
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((self.work / "payload-manifest.json").read_text(encoding="utf-8"))
        actual = {item["path"]: item for item in manifest["files"]}
        expected = {p.relative_to(self.bundle).as_posix(): p for p in self.bundle.rglob("*") if p.is_file()}
        self.assertEqual(actual.keys(), expected.keys())
        for relative, path in expected.items():
            self.assertEqual(actual[relative]["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(manifest["binary_version"], "0.6.0.0")
        include = (self.work / "payload.nsh").read_text(encoding="utf-8")
        self.assertIn("字体$$font.otf", include)
        self.assertNotIn("RMDir /r", include)
        self.assertIn('Delete "$BundleDir\\Contents\\Resources\\Fonts\\字体$$font.otf"', include)
        self.assertFalse((self.root / "fixture-setup.exe").exists())

    def test_missing_plugin_binary_is_rejected(self):
        self.binary.unlink()
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Required plugin binary is missing", result.stderr)

    def test_bundle_root_document_is_packaged(self):
        (self.bundle / "desktop.ini").write_text("fixture root resource\n")
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((self.work / "payload-manifest.json").read_text(encoding="utf-8"))
        self.assertIn("desktop.ini", {item["path"] for item in manifest["files"]})

    def test_old_binary_cannot_be_relabelled_as_new_release(self):
        self.binary.write_bytes(versioned_pe((0, 5, 0, 0)))
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("binary version 0.5.0.0 does not match", result.stderr)

    def test_non_x64_binary_is_rejected(self):
        self.binary.write_bytes(versioned_pe(machine=0x014C))
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Windows x64", result.stderr)

    def test_case_colliding_resource_names_are_rejected(self):
        (self.resources / "license.TXT").write_text("collides on Windows")
        if len([p for p in self.resources.iterdir() if p.name.lower()=="license.txt"])<2:
            self.skipTest("Filesystem cannot create two case-colliding fixture files")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("case-insensitive", result.stderr)

    def test_symlink_cannot_escape_payload(self):
        outside = self.root / "outside-secret.txt"
        outside.write_text("must not be packaged")
        try:
            (self.resources / "linked.txt").symlink_to(outside)
        except OSError as error:
            if getattr(error,"winerror",None)==1314:
                self.skipTest("Windows symlink privilege unavailable; system settings unchanged")
            raise
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Symlink", result.stderr)

    def test_windows_reserved_resource_name_is_rejected(self):
        (self.resources / "CON.txt").write_text("not a valid Windows payload")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Windows path", result.stderr)

    def test_truncated_or_changed_installer_fails_integrity_validation(self):
        namespace = runpy.run_path(str(BUILD_TOOL))
        self.assertTrue("validate_nsis_installer" in namespace, "The packager must validate the produced installer's integrity")
        verify = namespace["validate_nsis_installer"]
        data = bytearray(versioned_pe())
        struct.pack_into("<H", data, 0x96, 0x22)  # executable, not a DLL
        body = b"NSIS integrity-check fixture, never executed"
        data += struct.pack("<7I", 8, 0xDEADBEEF, 0x6C6C754E, 0x74666F73, 0x74736E49, 32, 28 + len(body) + 4)
        data += body
        data += struct.pack("<I", zlib.crc32(data[512:]))
        path = self.root / "integrity-fixture.exe"
        path.write_bytes(data)
        verify(path)
        path.write_bytes(data[:-8])
        with self.assertRaisesRegex(ValueError, "truncated"):
            verify(path)
        data[-7] ^= 0x40
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "CRC"):
            verify(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
