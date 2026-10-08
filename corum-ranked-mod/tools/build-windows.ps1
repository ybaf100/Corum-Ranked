param(
    [string]$SdkPath = (Join-Path $env:LOCALAPPDATA "CorumRankedBuild\geode-sdk"),
    [switch]$Production,
    [ValidateRange(1, 64)][int]$Jobs = 4
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($env:OS -ne "Windows_NT") { throw "Run this script on Windows x64." }

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed (exit $LASTEXITCODE)." }
}

# Enter the installed x64 VS toolchain without requiring an existing developer
# shell. LLVM/Ninja match the project's normal Windows GitHub build configuration.
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) { throw "Install Visual Studio 2022 C++ Build Tools first; see WINDOWS-BUILD.md." }
$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsInstall) { throw "Visual Studio's Desktop development with C++ workload is missing." }
$devModule = Join-Path $vsInstall "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Import-Module $devModule
Enter-VsDevShell -VsInstallPath $vsInstall -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"
foreach ($relative in @("VC\Tools\Llvm\x64\bin", "VC\Tools\Llvm\bin", "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin", "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja")) {
    $candidate = Join-Path $vsInstall $relative
    if (Test-Path $candidate) { $env:PATH = "$candidate;$env:PATH" }
}
foreach ($tool in @("git", "geode", "cmake", "ninja", "clang", "clang++")) {
    if (!(Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool. See WINDOWS-BUILD.md." }
}

$project = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content (Join-Path $project "mod.json") -Raw | ConvertFrom-Json
$SdkPath = [IO.Path]::GetFullPath($SdkPath)
$env:GEODE_SDK = $SdkPath
if (!(Test-Path (Join-Path $SdkPath "CMakeLists.txt"))) {
    # This is a build SDK checkout, separate from the Corum repository.
    if (Test-Path $SdkPath) { throw "SDK folder is incomplete: $SdkPath. Choose another -SdkPath or repair that checkout." }
    New-Item -ItemType Directory -Force (Split-Path $SdkPath -Parent) | Out-Null
    # --force permits a separate checkout when CLI already has another SDK;
    # --reinstall is deliberately absent so that checkout is never removed.
    Invoke-Checked "geode" @("sdk", "install", "--force", $SdkPath)
    if (!(Test-Path (Join-Path $SdkPath "CMakeLists.txt"))) { throw "Geode SDK installation did not complete." }
}
Invoke-Checked "geode" @("sdk", "set-path", $SdkPath)
Invoke-Checked "geode" @("sdk", "update", $manifest.geode)
Invoke-Checked "geode" @("sdk", "install-binaries", "--platform", "win", "--version", $manifest.geode)

$debugFlag = if ($Production) { "OFF" } else { "ON" }
$variant = if ($Production) { "production" } else { "debug" }
$build = Join-Path $project "build\$($manifest.version)-windows-$variant"
Invoke-Checked "cmake" @("-S", $project, "-B", $build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
    "-DGEODE_CODEGEN_CMAKE_ARGS=-DCMAKE_C_COMPILER=clang;-DCMAKE_CXX_COMPILER=clang++;-G Ninja",
    "-DGEODE_DONT_INSTALL_MODS=ON", "-DCORUM_RANKED_DEBUG_BOT_MATCH=$debugFlag", "-DCORUM_RANKED_DEBUG_SPECTATE=$debugFlag")
Invoke-Checked "cmake" @("--build", $build, "--config", "Release", "--parallel", "$Jobs")

$packages = @(Get-ChildItem $build -File -Filter "*.geode")
if ($packages.Count -ne 1) { throw "Expected one package in $build; found $($packages.Count)." }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($packages[0].FullName)
try {
    $entry = $archive.GetEntry("mod.json")
    if (!$entry) { throw "Package mod.json is missing." }
    $reader = [IO.StreamReader]::new($entry.Open())
    try { $packaged = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    if ($packaged.id -ne $manifest.id -or $packaged.version -ne $manifest.version) { throw "Package identity/version mismatch." }
    if (!($archive.Entries | Where-Object { $_.FullName -eq "$($manifest.id).dll" })) { throw "Windows DLL is missing from the package." }
} finally { $archive.Dispose() }
$dist = Join-Path $project "dist\windows-$variant"
New-Item -ItemType Directory -Force $dist | Out-Null
$output = Join-Path $dist "hwanhee1.corum_ranked.geode"
Copy-Item $packages[0].FullName $output -Force
Write-Host "Built $($manifest.version) ($variant): $output"
Write-Host "Copy this file into Geometry Dash\geode\mods while the game is closed."
