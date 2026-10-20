param([switch]$SkipDependencies)
$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot
try {
    if (-not $SkipDependencies) {
        python tools/native_build_bootstrap.py
        if ($LASTEXITCODE -ne 0) { throw "Dependency preparation failed." }
    }
    cmake -S . -B build/msvc -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed. Install Visual Studio 2022/2026 with Desktop development with C++ and CMake." }
    cmake --build build/msvc --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Native build failed." }
    Write-Host "Built: $ProjectRoot/build/msvc/Release/Iris Color.vst3"
    Write-Host "Built: $ProjectRoot/build/msvc/Release/iris-render.exe"
} finally { Pop-Location }
