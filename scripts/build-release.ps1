# Build release binaries for Windows
# Usage: .\scripts\build-release.ps1 [-Clean] [-SkipTests] [-Architecture x64|x86|arm64]

param(
    [switch]$Clean,
    [switch]$SkipTests,
    [string]$Architecture = "x64",
    [string]$BuildDir = "build-release",
    [string]$Generator = "Visual Studio 16 2019"
)

$ErrorActionPreference = "Stop"

# Script configuration
$RootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildPath = Join-Path $RootDir $BuildDir
$Version = "0.0.1"

Write-Host "Building BTOON Release Binaries" -ForegroundColor Cyan
Write-Host "===============================" -ForegroundColor Cyan
Write-Host "Architecture: $Architecture"
Write-Host "Build Directory: $BuildPath"
Write-Host "Generator: $Generator"
Write-Host ""

# Clean previous build if requested
if ($Clean) {
    Write-Host "Cleaning previous build..." -ForegroundColor Yellow
    if (Test-Path $BuildPath) {
        Remove-Item -Recurse -Force $BuildPath
    }
}

# Create build directory
if (!(Test-Path $BuildPath)) {
    New-Item -ItemType Directory -Path $BuildPath | Out-Null
}

Set-Location $BuildPath

# Configure CMake
Write-Host "Configuring CMake..." -ForegroundColor Green
$CMakeArgs = @(
    "-G", $Generator,
    "-A", $Architecture,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_TESTS=ON",
    "-DBUILD_TOOLS=ON",
    ".."
)

& cmake $CMakeArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed"
}

# Build
Write-Host ""
Write-Host "Building..." -ForegroundColor Green
& cmake --build . --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Build failed"
}

# Run tests
if (!$SkipTests) {
    Write-Host ""
    Write-Host "Running tests..." -ForegroundColor Green
    & ctest -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Some tests failed"
    }
}

# Package binaries
Write-Host ""
Write-Host "Packaging binaries..." -ForegroundColor Green

$PackageDir = Join-Path $BuildPath "package"
$BinDir = Join-Path $PackageDir "bin"
$LibDir = Join-Path $PackageDir "lib"
$IncludeDir = Join-Path $PackageDir "include"
$DocDir = Join-Path $PackageDir "doc"

# Create directories
foreach ($Dir in @($BinDir, $LibDir, $IncludeDir, $DocDir)) {
    if (!(Test-Path $Dir)) {
        New-Item -ItemType Directory -Path $Dir -Force | Out-Null
    }
}

# Copy binaries
$Binaries = @(
    "Release\btoon.exe",
    "Release\btoon-schema.exe",
    "Release\btoon-convert.exe"
)

foreach ($Binary in $Binaries) {
    $Source = Join-Path $BuildPath $Binary
    if (Test-Path $Source) {
        Copy-Item $Source $BinDir
        Write-Host "  Copied: $(Split-Path -Leaf $Source)"
    }
}

# Copy libraries
$Libraries = @(
    "Release\btoon.lib",
    "Release\btoon_core.lib",
    "Release\btoon.dll"
)

foreach ($Library in $Libraries) {
    $Source = Join-Path $BuildPath $Library
    if (Test-Path $Source) {
        Copy-Item $Source $LibDir
        Write-Host "  Copied: $(Split-Path -Leaf $Source)"
    }
}

# Copy headers
$HeaderSource = Join-Path $RootDir "include\btoon"
Copy-Item -Recurse $HeaderSource $IncludeDir
Write-Host "  Copied: headers"

# Copy documentation
$Docs = @(
    "README.md",
    "LICENSE"
)

foreach ($Doc in $Docs) {
    $Source = Join-Path $RootDir $Doc
    if (Test-Path $Source) {
        Copy-Item $Source $DocDir
    }
}

$DocsSource = Join-Path $RootDir "docs"
if (Test-Path $DocsSource) {
    Copy-Item -Recurse $DocsSource (Join-Path $DocDir "docs")
}
Write-Host "  Copied: documentation"

# Create version file
$VersionFile = Join-Path $PackageDir "VERSION.txt"
@"
BTOON Version: $Version
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss UTC" -AsUTC)
Platform: Windows-$Architecture
Compiler: $Generator
"@ | Out-File -FilePath $VersionFile -Encoding UTF8

# Create archive
$ArchiveName = "btoon-$Version-windows-$Architecture"
Write-Host ""
Write-Host "Creating archive: $ArchiveName.zip" -ForegroundColor Green

$ZipPath = Join-Path $BuildPath "$ArchiveName.zip"
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath
}

# Create ZIP archive
Add-Type -Assembly System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($PackageDir, $ZipPath)

Write-Host ""
Write-Host "Build complete!" -ForegroundColor Cyan
Write-Host "Archive: $ZipPath" -ForegroundColor Cyan
Write-Host ""
Write-Host "To use the binaries:" -ForegroundColor Yellow
Write-Host "  1. Extract the archive"
Write-Host "  2. Add the 'bin' directory to your PATH"
Write-Host "  3. Include the 'include' directory in your C++ projects"
Write-Host ""

# Return to original directory
Pop-Location
