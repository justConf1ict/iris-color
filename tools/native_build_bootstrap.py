#!/usr/bin/env python3
"""Fetch pinned native build dependencies locally. Never installs system packages."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SOURCES = json.loads((ROOT / "third_party/SOURCES.json").read_text())

def run(*args):
    subprocess.run([str(x) for x in args], check=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross-windows", action="store_true", help="Also download portable Linux x64 -> Windows LLVM-MinGW.")
    args = parser.parse_args()
    spec = SOURCES["vst3sdk"]
    sdk = ROOT / "third_party/vst3sdk"
    if not sdk.exists():
        run("git", "clone", "--no-checkout", "--filter=blob:none", spec["url"], sdk)
        run("git", "-C", sdk, "checkout", "--detach", spec["commit"])
    actual = subprocess.check_output(["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True).strip()
    if actual != spec["commit"]:
        raise SystemExit(f"SDK is at {actual}; expected {spec['commit']}. Use a fresh source folder or select the pinned commit deliberately.")
    run("git", "-C", sdk, "submodule", "update", "--init", "--depth", "1", *spec["submodules"])
    for name, commit in spec["submodules"].items():
        actual = subprocess.check_output(["git", "-C", str(sdk / name), "rev-parse", "HEAD"], text=True).strip()
        if actual != commit:
            raise SystemExit(f"Unexpected {name} commit: {actual}")
    if args.cross_windows:
        spec = SOURCES["llvm_mingw_cross_compiler"]
        folder = ROOT / "tools/.toolchains"
        folder.mkdir(parents=True, exist_ok=True)
        archive = folder / "llvm-mingw.tar.xz"
        if not archive.exists():
            print("Downloading portable LLVM-MinGW from its official GitHub release...", flush=True)
            urllib.request.urlretrieve(spec["url"], archive)
        digest = hashlib.file_digest(archive.open("rb"), "sha256").hexdigest()
        if digest != spec["sha256"]:
            raise SystemExit("Compiler archive SHA-256 does not match the pinned release. Nothing was extracted.")
        if not (folder / spec["directory"]).exists():
            with tarfile.open(archive) as tar:
                if hasattr(tarfile, "data_filter"):
                    tar.extractall(folder, filter="data")
                else:
                    raise SystemExit("Python with tarfile.data_filter is required for secure compiler extraction (Python 3.12 recommended).")
        print("Compiler: " + str(folder / spec["directory"] / "bin/x86_64-w64-mingw32-clang++"))
    print("Pinned native build dependencies are ready.")

if __name__ == "__main__":
    main()
