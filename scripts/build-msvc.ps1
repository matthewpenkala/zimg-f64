param(
    [string]$BuildDirectory = "build-msvc",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Build = Join-Path $Root $BuildDirectory

cmake -S $Root -B $Build -A x64 `
    -DZIMG_F64_BUILD_MPFR_ORACLE=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

cmake --build $Build --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

