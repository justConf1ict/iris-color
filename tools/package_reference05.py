#!/usr/bin/env python3
"""Package the verified Iris Color 0.5.0 Windows release; never build or run audio.

Run only after the final builds, tests, screenshots, audition and documentation:
  python3 tools/package_reference05.py --out ../iris-color-05-release

Uses the Python standard library. The ZIP contains one Iris-Color-v0.5.0 folder.
An existing final ZIP is never replaced. Publication uses a flushed, fsynced,
fully checked temporary ZIP followed by an atomic no-replace rename on Linux
or Windows. SDK checkouts, toolchains, runtimes and build trees are not bundled.
"""
from __future__ import annotations

import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
from html.parser import HTMLParser
import io
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import struct
import sys
import tempfile
from urllib.parse import unquote, urlsplit
import wave
import zipfile


ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.5.0"
PREFIX = "Iris-Color-v" + VERSION
WIN_MODULE = "build/windows/Iris Color.vst3/Contents/x86_64-win/Iris Color.vst3"
LIN_MODULE = "build/linux/Iris Color.vst3/Contents/x86_64-linux/Iris Color.so"
CURATED_TOOLS = (
    "render.cpp", "build_windows.ps1", "native_build.py", "native_build_bootstrap.py",
    "make_reference05_presets.cpp", "make_reference05_demo.py", "reference05_metrics.py",
    "verify_legacy05.py", "verify_windows05.py", "verify_renderer05_parity.py",
    "package_reference05.py",
)
WINDOWS_TESTS = {
    "host": "iris-vst3-host-test.exe", "transport": "transport04_test.exe",
    "state02": "state_compat_test.exe", "state04": "state_compat04_test.exe",
    "state05": "state_compat05_test.exe", "presets": "preset_test.exe",
}
LEGACY_TESTS = (
    "dsp_unit", "dsp_revision_visual", "dsp_color03_level", "dsp_color03_state",
    "dsp_color03_bands", "dsp_color04_features", "dsp_color04_contracts",
)
REFERENCE_RECORDS = (
    "windows05-final-receipt.json", "linux-build-final.log", "windows-build-final.log",
    "05-test-final.json", "05-test-final.log", "prism05-extremes-final.json",
    "prism05-extremes-final.log", "legacy05-validation.json", "reference05-audio.json",
    "iris-reference-presets.json", "renderer-parity05.json", "ui-review05.json",
    "reference-measurements.json", "tail-pole-summary.json", "tail-poles-adaptive.json",
    "outer-branch-evidence.json", "prism05-final-metrics.json",
    "05-tail-analysis-calibration.json", "05-test-red.json", "05-test-green-first.json",
    "05-motion-offgrid.json",
)
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".c", ".cc", ".cxx", ".inl", ".ipp",
                   ".py", ".txt", ".def", ".rc", ".md", ".json", ".cmake"}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def safe_relative(value):
    require(isinstance(value, str) and value and "\\" not in value, "Invalid relative path: " + repr(value))
    path = PurePosixPath(value)
    require(not path.is_absolute() and all(p not in (".", "..") for p in path.parts), "Unsafe relative path: " + value)
    require(not any(p.startswith(".") or p == "__pycache__" for p in path.parts), "Hidden/cache path: " + value)
    return path.as_posix()


class Inputs:
    """Snapshot exact input bytes; confirm source files still match before publication."""
    def __init__(self):
        self.files = {}

    def read(self, relative):
        relative = safe_relative(relative)
        if relative not in self.files:
            path = ROOT / relative
            require(path.is_file(), "Missing release input: " + relative)
            current = path
            while current != ROOT:
                require(not current.is_symlink(), "Symlink is not a release input: " + relative)
                current = current.parent
            self.files[relative] = path.read_bytes()
        return self.files[relative]

    def json(self, relative):
        return json.loads(self.read(relative).decode("utf-8"))

    def match(self, relative, expected):
        require(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected), "Invalid SHA-256 for " + relative)
        require(sha(self.read(relative)) == expected, "SHA-256 mismatch: " + relative)

    def sources(self, values):
        require(isinstance(values, dict) and values, "Missing source SHA-256 map")
        for relative, expected in values.items():
            self.match(relative, expected)

    def unchanged(self):
        for relative, content in self.files.items():
            path = ROOT / relative
            require(path.is_file() and not path.is_symlink() and sha(path.read_bytes()) == sha(content),
                    "Input changed during packaging: " + relative)


def good_log(content, name, marker="PASS"):
    text = content.decode("utf-8", errors="replace")
    require(marker in text and not re.search(r"(?m)^FAIL\b|runtime error:|AddressSanitizer:", text),
            "Failed/incomplete validation log: " + name)
    return text


def pe64(content, name, dll=False):
    require(len(content) >= 64 and content[:2] == b"MZ", "Not a Windows executable: " + name)
    at = struct.unpack_from("<I", content, 0x3C)[0]
    require(at + 26 <= len(content) and content[at:at + 4] == b"PE\0\0", "Invalid PE header: " + name)
    require(struct.unpack_from("<H", content, at + 4)[0] == 0x8664, "Not x64: " + name)
    require(struct.unpack_from("<H", content, at + 24)[0] == 0x20B, "Not PE32+: " + name)
    if dll:
        require(struct.unpack_from("<H", content, at + 22)[0] & 0x2000, "VST3 module lacks the DLL flag")


def validate(inputs):
    records = {platform: inputs.json(f"build/{platform}/build-receipt.json") for platform in ("linux", "windows")}
    require(records["linux"]["source_sha256"] == records["windows"]["source_sha256"], "Platforms were built from different source")
    required = {"src/IrisDSP.cpp", "src/IrisDSP.h", "src/IrisPrism.h", "plugin/IrisParameters.cpp",
                "plugin/IrisParameters.h", "plugin/IrisPlugin.cpp", "plugin/IrisEditorWin.cpp", "tools/render.cpp"}
    require(required <= set(records["windows"]["source_sha256"]), "Build receipt omits core production files")
    for platform, module in (("linux", LIN_MODULE), ("windows", WIN_MODULE)):
        receipt = records[platform]
        require(receipt["binary"] == module, "Unexpected built module path")
        inputs.sources(receipt["source_sha256"])
        inputs.match(module, receipt["sha256"])
        inputs.match(f"build/{platform}/iris-render" + (".exe" if platform == "windows" else ""), receipt["renderer_sha256"])
    pe64(inputs.read(WIN_MODULE), WIN_MODULE, dll=True)
    pe64(inputs.read("build/windows/iris-render.exe"), "iris-render.exe")

    native = inputs.json("build/reference/windows05-final-receipt.json")
    require(native["passed"] is True and native["source_unchanged_through_run"] is True and
            native["module_unchanged_through_run"] is True, "Windows final batch did not pass unchanged")
    require(native["source_sha256"] == records["windows"]["source_sha256"] and
            native["module_sha256"] == records["windows"]["sha256"], "Windows tests used different production inputs")
    inputs.match("plugin/tests/host_test.cpp", native["host_source_sha256"])
    inputs.match("plugin/tests/transport04_test.cpp", native["transport_source_sha256"])
    require(len(native["commands"]) == 6 and {r["name"] for r in native["commands"]} == set(WINDOWS_TESTS), "Missing Windows test executable")
    for row in native["commands"]:
        require(row["exit_code"] == 0 and row["binary_unchanged"] is True, "Windows test failed: " + row["name"])
        require(row["log"] == f"windows-{row['name']}-final.log", "Unexpected Windows log name")
        log = "build/reference/" + row["log"]
        inputs.match(log, row["log_sha256"])
        inputs.match("build/windows/" + WINDOWS_TESTS[row["name"]], row["binary_sha256"])
        good_log(inputs.read(log), log)
    for platform in ("linux", "windows"):
        log = "build/reference/" + ("linux-build-final.log" if platform == "linux" else "windows-host-final.log")
        text = good_log(inputs.read(log), log, "All native VST3 host regression checks passed.")
        if platform == "linux":
            require("8 groups per core / 16 groups passed, 0 failures" in text and
                    "Independent 0.5 core-state review: 6 groups passed, 0 failures" in text and
                    records["linux"]["sha256"] in text, "Linux final log lacks all final host stages")
    good_log(inputs.read("build/reference/windows-transport-final.log"), "Windows transport", "8 groups per core / 16 groups passed")

    core = inputs.json("build/reference/05-test-final.json")
    require(core["passed"] is True and core["renderer_unchanged"] is True and core["passed_cases"] == 8 and
            core["failed_cases"] == 0 and len(core["cases"]) == 8 and all(c["passed"] is True for c in core["cases"]), "Core audio contracts failed")
    require(core["renderer_sha256"] == records["linux"]["renderer_sha256"], "Core audio test used a stale renderer")
    require(all(r["exit_code"] == 0 for r in core["render_calls"]), "Core fixture render failed")
    inputs.match("tests/dsp_color05_audio.py", core["test_script_sha256"])
    good_log(inputs.read("build/reference/05-test-final.log"), "Core audio")
    extremes = inputs.json("build/reference/prism05-extremes-final.json")
    require(extremes["passed"] is True and extremes["exit_code"] == 0 and extremes["source_unchanged"] is True, "DSP extremes failed")
    inputs.sources(extremes["source_sha256"])
    inputs.match("build/reference/prism05-extremes-final.log", extremes["log_sha256"])
    inputs.match("build/reference/prism05-extremes-final", extremes["binary_sha256"])
    good_log(inputs.read("build/reference/prism05-extremes-final.log"), "DSP extremes")
    for name in ("dsp_color05_mode", "dsp_color05_dive_guard"):
        good_log(inputs.read("build/review05/" + name + ".log"), name)

    legacy = inputs.json("build/reference/legacy05-validation.json")
    require(all(legacy.get(flag) is True for flag in ("pass", "source_unchanged_during_validation",
            "legacy_source_unchanged_during_validation", "input_unchanged")), "Legacy validation failed or changed")
    inputs.sources(legacy["source_sha256"])
    require(set(legacy["tests"]) == set(LEGACY_TESTS), "Missing Legacy regression executable")
    for name, test in legacy["tests"].items():
        require(test["pass"] is True and test["compile"]["exit"] == 0 and test["run"]["exit"] == 0 and not test["failing_checks"], "Legacy test failed: " + name)
    require(sum(len(t["passing_checks"]) for t in legacy["tests"].values()) == 108, "Missing 108 frozen Legacy assertions")
    require(len(legacy["audio_comparisons"]) == 2 and all(
        r["pass"] is True and r["bit_exact_float32"] is True and r["all_finite"] is True and
        r["max_absolute_error"] == 0 and r["rms_error"] == 0 for r in legacy["audio_comparisons"].values()), "Legacy sample identity failed")
    require(legacy["legacy_build_receipt"]["all_recorded_sources_match"] is True and
            legacy["legacy_build_receipt"]["renderer_matches_recorded_binary"] is True, "Legacy baseline is not the frozen release")

    parity = inputs.json("build/reference/renderer-parity05.json")
    require(parity["passed"] is True and parity["executables_unchanged"] is True and
            set(parity["profiles"]) == {"reference", "creative"}, "Renderer parity is incomplete")
    require(parity["executable_sha256"] == {p: r["renderer_sha256"] for p, r in records.items()}, "Parity used different renderers")
    for result in parity["profiles"].values():
        require(result["passed"] is True and result["all_finite"] is True and result["same_format_shape"] is True, "Renderer parity failed")
    for row in parity["commands"]:
        require(row["exit_code"] == 0, "A parity render failed")
        inputs.match(row["log"], row["log_sha256"])

    preset = inputs.json("build/reference/iris-reference-presets.json")
    require(preset["passed"] is True and len(preset["presets"]) == 7, "Seven reference presets were not validated")
    require(preset["sdk_commit"] == records["windows"]["vst3sdk_commit"], "Preset SDK differs from final build")
    inputs.match("tools/make_reference05_presets.cpp", preset["source_sha256"])
    actual = {p.name for p in (ROOT / "presets/reference05").glob("*.vstpreset")}
    require(actual == set(preset["presets"]), "Preset folder differs from its manifest")
    for name, expected in preset["presets"].items():
        require(PurePosixPath(name).name == name, "Unexpected preset path")
        inputs.match("presets/reference05/" + name, expected)
        require(inputs.read("presets/reference05/" + name)[:4] == b"VST3", "Invalid preset container")

    ui = inputs.json("build/reference/ui-review05.json")
    require(ui["passed"] is True and ui["module_sha256"] == records["windows"]["sha256"], "UI review used a different module")
    inputs.match("build/reference/windows-host-final.log", ui["windows_host_log_sha256"])
    reviewed_paths = set()
    for row in ui["screenshots"]:
        path = safe_relative(row["path"])
        require(path.startswith("docs/screenshots05/") and path not in reviewed_paths, "Unexpected/duplicate screenshot")
        inputs.match(path, row["sha256"])
        content = inputs.read(path)
        size = row.get("size", [row.get("width"), row.get("height")])
        require(content[:8] == b"\x89PNG\r\n\x1a\n" and len(content) >= 24 and
                list(struct.unpack_from(">II", content, 16)) == size, "Screenshot dimensions differ: " + path)
        reviewed_paths.add(path)
    actual = {p.relative_to(ROOT).as_posix() for p in (ROOT / "docs/screenshots05").glob("*.png")}
    require(len(reviewed_paths) == 10 and reviewed_paths == actual, "Screenshot folder differs from final review")
    eyes = {r["path"] for r in ui["screenshots"] if r.get("visually_reviewed") is True}
    require({f"docs/screenshots05/{page}-{scale}.png" for page in ("Main", "Bands", "Motion") for scale in (100, 150)} <= eyes,
            "Missing final Main/Bands/Motion visual review at 100% and 150%")

    audio = inputs.json("build/reference/reference05-audio.json")
    require(audio["passed"] is True and audio["verification"]["passed"] is True and
            audio["sources_and_renderers_unchanged_through_run"] is True, "Audition verification failed or changed")
    require(audio["renderers"]["iris05"]["sha256"] == records["linux"]["renderer_sha256"] and
            audio["renderers"]["iris04"]["sha256"] == legacy["legacy_renderer_sha256"], "Auditions used a different renderer")
    inputs.sources(audio["source_sha256"]["iris05"])
    for name, expected in audio["script_sha256"].items():
        require(PurePosixPath(name).name == name, "Unexpected audio generator name")
        inputs.match("tools/" + name, expected)
    require(len(audio["artifacts"]) == 5, "Missing five audition WAV files")
    audio_paths = set()
    for row in [*audio["artifacts"], audio["spectrum"], audio["listen_page"], audio["reproduction_instructions"]]:
        require(PurePosixPath(row["file"]).name == row["file"], "Audition artifacts must stay in their recorded directory")
        path = "audio/reference05/" + row["file"]
        require(path not in audio_paths, "Duplicate audition artifact")
        inputs.match(path, row["sha256"])
        require(len(inputs.read(path)) == row["size_bytes"], "Audition size differs: " + path)
        audio_paths.add(path)
    for row in audio["artifacts"]:
        with wave.open(io.BytesIO(inputs.read("audio/reference05/" + row["file"])), "rb") as reader:
            require((reader.getframerate(), reader.getnchannels(), reader.getsampwidth() * 8, reader.getnframes()) ==
                    (row["sample_rate"], row["channels"], row["bits_per_sample"], row["frames"]), "WAV metadata differs: " + row["file"])
            require(reader.getcomptype() == "NONE" and (row["sample_rate"], row["channels"], row["bits_per_sample"]) == (44100, 2, 24), "Unexpected audition WAV encoding")
        require(0 <= row["sample_peak"] <= .89 + 1e-7, "Audition metadata reports excess sample peak")
    actual = {p.relative_to(ROOT).as_posix() for p in (ROOT / "audio/reference05").iterdir() if p.is_file()}
    require(actual == audio_paths, "Audition directory contains unregistered or missing files")
    return {"builds": records, "ui": ui, "audio": audio, "presets": preset,
            "summary": {"core_audio_cases": 8, "legacy_assertions": 108, "legacy_bit_exact_profiles": 2,
                        "windows_executables": 6, "transport_groups_per_core": 8,
                        "screenshots": 10, "screenshots_visually_reviewed": len(eyes), "audition_waves": 5}}


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.references, self.audio_count = [], 0
        self.feed(text)

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if tag == "audio":
            self.audio_count += 1
            require("autoplay" not in attrs, "Audition HTML requests autoplay")
        for name in ("src", "href"):
            if attrs.get(name):
                self.references.append((attrs[name], tag != "a"))
        if tag == "meta" and attrs.get("http-equiv", "").lower() == "refresh":
            match = re.search(r"url\s*=\s*(.+)$", attrs.get("content", ""), re.I)
            if match:
                self.references.append((match.group(1).strip("'\" "), True))


def check_links(members):
    checked, audio_count = 0, 0
    for name, content in members.items():
        if not name.endswith((".html", ".md")):
            continue
        # Only delivered prose/pages, not code comments or third-party notices.
        if not (name in ("Listen.html", "README.md", "README_zh.md") or name.startswith(("docs/", "audio/reference05/"))):
            continue
        text = content.decode("utf-8")
        page = Page(text)
        references = page.references
        audio_count += page.audio_count
        if name.endswith(".md"):
            references += [(value.strip().split(' "', 1)[0], False) for value in re.findall(r"!?\[[^\]]*\]\(([^)]+)\)", text)]
        for value, resource in references:
            parsed = urlsplit(value)
            if parsed.scheme or parsed.netloc:
                require(not resource or parsed.scheme == "data", "External offline-page resource: " + value)
                continue
            if not parsed.path:
                continue
            require(not parsed.path.startswith("/"), "Absolute offline link: " + value)
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), unquote(parsed.path)))
            require(target != ".." and not target.startswith("../") and
                    (target in members or any(p.startswith(target.rstrip("/") + "/") for p in members)),
                    "Missing packaged link: " + name + " -> " + value)
            checked += 1
    require(audio_count == 5, "Offline audition pages must contain five audio players")
    return checked


def collect(inputs, verified):
    members = {}
    def add(source, destination=None):
        destination = safe_relative(destination or source)
        require(destination not in members, "Duplicate release member: " + destination)
        members[destination] = inputs.read(source)

    for folder in ("src", "plugin", "tests"):
        for path in sorted((ROOT / folder).rglob("*")):
            relative = path.relative_to(ROOT)
            if any(p.startswith(".") or p in ("__pycache__", "build", "runtime", "toolchains") for p in relative.parts):
                continue
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
                add(relative.as_posix())
    for name in CURATED_TOOLS:
        add("tools/" + name)
    for name in ("CMakeLists.txt", "README.md", "README_zh.md", "LICENSE", "requirements-dev.txt", "Listen.html"):
        add(name)
    add("docs/Prism-05-Guide_zh.md")
    for row in verified["ui"]["screenshots"]:
        add(row["path"])
    for name in ("SOURCES.json", "NOTICES.md"):
        add("third_party/" + name)
    licenses = sorted((ROOT / "third_party").glob("*LICENSE.txt"))
    require(len(licenses) >= 4, "Missing dependency license notices")
    for path in licenses:
        add(path.relative_to(ROOT).as_posix())
    bundle = ROOT / "build/windows/Iris Color.vst3"
    for path in sorted(bundle.rglob("*")):
        if path.is_file():
            add(path.relative_to(ROOT).as_posix(), "Iris Color.vst3/" + path.relative_to(bundle).as_posix())
    add("build/windows/iris-render.exe", "Tools/iris-render.exe")
    for row in [*verified["audio"]["artifacts"], verified["audio"]["spectrum"],
                verified["audio"]["listen_page"], verified["audio"]["reproduction_instructions"]]:
        add("audio/reference05/" + row["file"])
    for name in verified["presets"]["presets"]:
        add("presets/reference05/" + name)
    for platform in ("linux", "windows"):
        add(f"build/{platform}/build-receipt.json", f"Validation/{platform}-build-receipt.json")
    for name in (*REFERENCE_RECORDS, *(f"windows-{name}-final.log" for name in WINDOWS_TESTS)):
        add("build/reference/" + name, "Validation/" + name)
    for name in ("dsp_color05_mode", "dsp_color05_dive_guard"):
        add("build/review05/" + name + ".log", "Validation/" + name + ".log")
    require(len({name.casefold() for name in members}) == len(members), "Case-colliding Windows file names")
    original_hashes = {verified["audio"]["inputs"][name]["sha256"] for name in ("dry", "chroma_reference")}
    require(not any(sha(content) in original_hashes for content in members.values()), "An uploaded original was included in the release")
    return members


def rename_no_replace(source, destination):
    """Atomically publish a complete file without a check/replace overwrite race."""
    if os.name == "nt":
        os.rename(source, destination)  # Windows rename fails if the target exists.
        return
    require(sys.platform.startswith("linux"), "Atomic release publication is supported on Linux and Windows")
    libc = ctypes.CDLL(None, use_errno=True)
    rename = getattr(libc, "renameat2", None)
    require(rename is not None, "renameat2 is required for atomic no-replace publication")
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(source), -100, os.fsencode(destination), 1) != 0:
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(destination))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT.parent / "iris-color-05-release")
    args = parser.parse_args()
    output = args.out.resolve()
    archive = output / (PREFIX + "-Windows.zip")
    require(not archive.exists(), "Refusing to overwrite the final ZIP: " + str(archive))
    inputs = Inputs()
    verified = validate(inputs)
    members = collect(inputs, verified)
    links = check_links(members)
    inventory = {name: {"sha256": sha(content), "size_bytes": len(content)} for name, content in sorted(members.items())}
    manifest = {
        "version": VERSION, "root_directory": PREFIX, "created_utc": datetime.now(timezone.utc).isoformat(),
        "passed": True, "validation": verified["summary"], "checked_local_links": links,
        "windows_module_sha256": verified["builds"]["windows"]["sha256"],
        "linux_module_sha256": verified["builds"]["linux"]["sha256"],
        "production_source_sha256": verified["builds"]["windows"]["source_sha256"],
        "package_script_sha256": sha(inputs.read("tools/package_reference05.py")),
        "audio_provenance": verified["audio"]["provenance"],
        "scope": "Verified Windows x64 VST3 under Wine; no FL Studio or physical Windows-hardware verification claim. Auditions use the user-provided source and reference, as recorded in Validation/reference05-audio.json.",
        "inventory_scope": "Payload files below; SHA256SUMS also covers this manifest and excludes only SHA256SUMS itself.",
        "files": inventory,
    }
    members["release-manifest.json"] = json_bytes(manifest)
    members["SHA256SUMS"] = "".join(f"{sha(content)}  {name}\n" for name, content in sorted(members.items())).encode("utf-8")
    inputs.unchanged()
    output.mkdir(parents=True, exist_ok=True)
    pending = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".iris05-", suffix=".zip.pending", dir=output, delete=False) as stream:
            pending = Path(stream.name)
            with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
                for name, content in sorted(members.items()):
                    zipped.writestr(PREFIX + "/" + name, content)
            stream.flush()
            os.fsync(stream.fileno())
        with zipfile.ZipFile(pending) as zipped:
            require(zipped.testzip() is None, "ZIP CRC verification failed")
            require(set(zipped.namelist()) == {PREFIX + "/" + name for name in members}, "ZIP inventory differs from verified payload")
            require(len(zipped.namelist()) == len(members), "Duplicate ZIP members")
            for name, content in members.items():
                require(sha(zipped.read(PREFIX + "/" + name)) == sha(content), "ZIP file differs from validated bytes: " + name)
        inputs.unchanged()
        require(not archive.exists(), "A final ZIP appeared during packaging")
        rename_no_replace(pending, archive)
        pending = None
        if os.name == "posix":
            descriptor = os.open(output, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
    finally:
        if pending is not None:
            pending.unlink(missing_ok=True)
    print(json.dumps({"passed": True, "archive": str(archive), "sha256": sha(archive.read_bytes()),
                      "size_bytes": archive.stat().st_size, "files": len(members), "zip_crc_passed": True,
                      "all_member_hashes_match": True, "validation": verified["summary"]}, indent=2))


if __name__ == "__main__":
    main()
