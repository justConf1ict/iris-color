#!/usr/bin/env python3
"""Build Iris Color with g++ or LLVM-MinGW; no CMake or global installation required."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = json.loads((ROOT / "third_party/SOURCES.json").read_text())

def run(command):
    result = subprocess.run([str(x) for x in command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise RuntimeError("Command failed: " + " ".join(str(x) for x in command) + "\n" + result.stdout)
    return result.stdout

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("linux", "windows"), default="windows")
    parser.add_argument("--compiler", help="C++ compiler path; g++ for Linux, x86_64-w64-mingw32-clang++ for Windows")
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument("--test-host", action="store_true", help="Build the dynamic VST3 host regression executable.")
    args = parser.parse_args()
    windows = args.target == "windows"
    cross = ROOT / "tools/.toolchains" / SOURCES["llvm_mingw_cross_compiler"]["directory"] / "bin"
    compiler = args.compiler or (str(cross / "x86_64-w64-mingw32-clang++") if windows else "g++")
    if not shutil.which(compiler):
        raise SystemExit("Compiler not found. Run: python3 tools/native_build_bootstrap.py --cross-windows")
    sdk = ROOT / "third_party/vst3sdk"
    if not (sdk / "public.sdk/source/vst/vstaudioeffect.cpp").exists():
        raise SystemExit("SDK missing. Run: python3 tools/native_build_bootstrap.py")
    sdk_sources = [sdk / line for line in (ROOT / "plugin/sdk_sources.txt").read_text().splitlines() if line]
    platform_main = sdk / "public.sdk/source/main" / ("dllmain.cpp" if windows else "linuxmain.cpp")
    local_sources = [ROOT / "plugin" / name for name in ("IrisParameters.cpp", "IrisPlugin.cpp", "IrisEditorWin.cpp", "IrisEntry.cpp")]
    local_sources.append(ROOT / "src/IrisDSP.cpp")
    build = ROOT / "build" / args.target
    objdir = build / "objects"
    objdir.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c++17", "-O2", "-DNDEBUG", "-DRELEASE=1", "-I"+str(ROOT), "-I"+str(ROOT / "src"), "-I"+str(sdk), "-fvisibility=hidden"]
    if windows:
        flags += ["-DUNICODE", "-D_UNICODE", "-DNOMINMAX", "-D_WIN32_WINNT=0x0A00", "-Wno-pragma-pack"]
    else:
        flags += ["-fPIC", "-pthread"]
    all_sources = sdk_sources + [platform_main] + local_sources
    command_record = []
    latest_header = max(p.stat().st_mtime for path in (ROOT / "src", ROOT / "plugin") for p in path.glob("*.h"))
    compiler_version = run([compiler, "--version"]).splitlines()[0]
    config = json.dumps({"compiler": compiler, "version": compiler_version, "flags": flags}, sort_keys=True)
    config_path = build / "compiler-config.json"
    rebuild = not config_path.exists() or config_path.read_text() != config
    config_path.write_text(config)
    def compile_source(source):
        relative = source.relative_to(ROOT)
        obj = objdir / (str(relative).replace("/", "__") + ".o")
        command = [compiler, *flags, "-c", source, "-o", obj]
        if rebuild or not obj.exists() or obj.stat().st_mtime < max(source.stat().st_mtime, latest_header):
            output = run(command)
            obj.with_suffix(".log").write_text(output)
            if output.strip(): print("Compiler notes: " + str(relative) + " (see object .log)", flush=True)
        return source, obj, [str(x) for x in command]
    objects = {}
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(compile_source, source) for source in all_sources]
        failures = []
        for f in as_completed(futures):
            try:
                source, obj, command = f.result()
                objects[source] = obj; command_record.append(command)
            except Exception as error:
                failures.append(str(error))
        if failures: raise RuntimeError("\n\n".join(failures))
    bundle = build / "Iris Color.vst3"
    binary_dir = bundle / "Contents" / ("x86_64-win" if windows else "x86_64-linux")
    binary_dir.mkdir(parents=True, exist_ok=True)
    binary = binary_dir / ("Iris Color.vst3" if windows else "Iris Color.so")
    libraries = ["-static", "-lole32", "-luuid", "-luser32", "-lgdi32", "-lgdiplus", "-lshell32", "-lwinmm", "-Wl,--no-insert-timestamp"] if windows else ["-pthread", "-ldl", "-Wl,-z,defs"]
    extra = []
    if windows:
        resource = objdir / "version.res"
        resource_compiler = Path(compiler).parent / "x86_64-w64-mingw32-windres"
        if resource_compiler.exists():
            run([resource_compiler, "-i", ROOT / "plugin/IrisVersion.rc", "-o", resource])
            extra.append(resource)
        extra.append(ROOT / "plugin/IrisColor.def")
    link_command = [compiler, "-shared", "-o", binary, *[objects[s] for s in all_sources], *extra, *libraries]
    run(link_command); command_record.append([str(x) for x in link_command])
    resources = bundle / "Contents/Resources"
    resources.mkdir(parents=True, exist_ok=True)
    # Private editor fonts travel with the bundle; they are not installed into Windows.
    fonts = ROOT / "assets/fonts"
    if not (fonts / "DMSans-Regular.ttf").is_file() or not (fonts / "DMSans-Medium.ttf").is_file():
        raise RuntimeError("Bundled DM Sans fonts are missing from assets/fonts.")
    shutil.copytree(fonts, resources / "fonts", dirs_exist_ok=True)
    shutil.copy2(sdk / "LICENSE.txt", resources / "VST3-SDK-LICENSE.txt")
    for notice in (ROOT / "third_party").glob("*LICENSE.txt"):
        shutil.copy2(notice, resources / notice.name)
    if (ROOT / "LICENSE").exists(): shutil.copy2(ROOT / "LICENSE", resources / "IRIS-COLOR-LICENSE.txt")
    elif (ROOT / "LICENSE.txt").exists(): shutil.copy2(ROOT / "LICENSE.txt", resources / "IRIS-COLOR-LICENSE.txt")
    renderer = build / ("iris-render.exe" if windows else "iris-render")
    renderer_command = [compiler, *flags, ROOT / "tools/render.cpp", objects[ROOT / "src/IrisDSP.cpp"], "-o", renderer]
    if windows: renderer_command += ["-static", "-Wl,--no-insert-timestamp"]
    run(renderer_command); command_record.append([str(x) for x in renderer_command])
    if args.test_host:
        host = build / ("iris-vst3-host-test.exe" if windows else "iris-vst3-host-test")
        host_support = [sdk / "public.sdk/source/vst" / name for name in
            ("hosting/hostclasses.cpp", "hosting/pluginterfacesupport.cpp", "utility/stringconvert.cpp")]
        host_support.append(sdk / "public.sdk/source/common/commonstringconvert.cpp")
        host_command = [compiler, *flags, ROOT / "plugin/tests/host_test.cpp", *host_support, objects[ROOT / "plugin/IrisParameters.cpp"], *[objects[s] for s in sdk_sources if s.name not in ("moduleinit.cpp",)], "-o", host, *libraries]
        run(host_command); command_record.append([str(x) for x in host_command])
        print("Host regression executable: " + str(host))
        if not windows:
            print(run([host, binary]), end="")
        transport_test = build / ("transport04_test.exe" if windows else "transport04_test")
        transport_command = [str(x) for x in host_command]
        transport_command[transport_command.index(str(ROOT / "plugin/tests/host_test.cpp"))] = str(ROOT / "plugin/tests/transport04_test.cpp")
        transport_command[transport_command.index(str(host))] = str(transport_test)
        run(transport_command); command_record.append(transport_command)
        if not windows: print(run([transport_test, binary]), end="")
        # These use the actual current serializer/Controller, not a copy of
        # their implementation. Literal old chunk bytes and IDs remain frozen.
        for test_name in ("state_compat_test", "state_compat04_test", "state_compat05_test", "state_compat06_test", "state_compat07_test", "preset_test"):
            test_binary = build / (test_name + (".exe" if windows else ""))
            test_objects = [objects[ROOT / "plugin/IrisParameters.cpp"]]
            if test_name == "preset_test":
                test_objects += [objects[ROOT / "plugin/IrisPlugin.cpp"],
                    objects[ROOT / "plugin/IrisEditorWin.cpp"], objects[ROOT / "src/IrisDSP.cpp"]]
            test_command = [compiler, *flags, ROOT / "plugin/tests" / (test_name + ".cpp"),
                *test_objects, *[objects[s] for s in sdk_sources if s.name != "moduleinit.cpp"],
                "-o", test_binary, *libraries]
            run(test_command); command_record.append([str(x) for x in test_command])
            if not windows: print(run([test_binary]), end="")
    receipt = {
        "target": "Windows x64 VST3 / UCRT" if windows else "Linux x64 headless validation build",
        "compiler": compiler_version,
        "vst3sdk_commit": SOURCES["vst3sdk"]["commit"],
        "binary": str(binary.relative_to(ROOT)),
        "sha256": hashlib.file_digest(binary.open("rb"), "sha256").hexdigest(),
        "renderer_sha256": hashlib.file_digest(renderer.open("rb"), "sha256").hexdigest(),
        "resource_sha256": {str(path.relative_to(bundle)): hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
            for path in sorted(resources.rglob("*")) if path.is_file()},
        "source_sha256": {str(path.relative_to(ROOT)): hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
            for path in sorted(set(local_sources + [ROOT / "tools/render.cpp"] +
                [p for directory in (ROOT / "src", ROOT / "plugin") for p in directory.glob("*.h")]))},
        "commands": command_record,
    }
    (build / "build-receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print("Built: " + str(binary))
    print("Built: " + str(renderer))
    print("SHA-256: " + receipt["sha256"])

if __name__ == "__main__":
    main()
