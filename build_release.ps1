# build_release.ps1 - Build the project as a Release .exe
# Requires: Visual Studio 2022+ with C++ Desktop Development workload, CMake 3.20+
# Run: .\build_release.ps1

$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectDir "build"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Building AutoAim (Release)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# =============================================================================
# Step 1: Setup VS Dev Environment
# =============================================================================
Write-Host "[1/4] Setting up MSVC environment..." -ForegroundColor Yellow

# Find Visual Studio using vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) {
    # Try alternate locations
    $vswhere = "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
}

# Try to find vcvarsall.bat
$vcvarsPaths = @(
    "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
)

$vcvarsall = $null
foreach ($path in $vcvarsPaths) {
    if (Test-Path $path) {
        $vcvarsall = $path
        break
    }
}

if ($vcvarsall) {
    Write-Host "  Found MSVC at: $vcvarsall" -ForegroundColor Green
    
    # Import VS environment
    $output = cmd /c "`"$vcvarsall`" x64 >nul 2>&1 && set" 2>&1
    foreach ($line in $output) {
        if ($line -match "^([^=]+)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
    Write-Host "  MSVC x64 environment loaded." -ForegroundColor Green
} else {
    Write-Host "  WARNING: Could not find MSVC. Build may fail." -ForegroundColor Red
    Write-Host "  Please install Visual Studio 2022 with C++ Desktop Development workload." -ForegroundColor Red
}

# =============================================================================
# Step 2: CMake Configure
# =============================================================================
Write-Host "[2/4] Configuring CMake..." -ForegroundColor Yellow

if (Test-Path $BuildDir) {
    Remove-Item -Path $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

Push-Location $BuildDir

# Configure
cmake .. -G "Visual Studio 18 2022" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DDEPS_DIR="$ProjectDir\deps"

if ($LASTEXITCODE -ne 0) {
    Write-Host "  CMake configuration FAILED!" -ForegroundColor Red
    Pop-Location
    exit 1
}
Write-Host "  CMake configured successfully." -ForegroundColor Green

# =============================================================================
# Step 3: Build
# =============================================================================
Write-Host "[3/4] Building Release..." -ForegroundColor Yellow

cmake --build . --config Release --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "  Build FAILED!" -ForegroundColor Red
    Pop-Location
    exit 1
}
Write-Host "  Build succeeded!" -ForegroundColor Green

# =============================================================================
# Step 4: Copy runtime files
# =============================================================================
Write-Host "[4/4] Copying runtime files..." -ForegroundColor Yellow

$exeDir = Join-Path $BuildDir "Release"
$distDir = Join-Path $ProjectDir "dist"

if (!(Test-Path $distDir)) {
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null
}

# Copy executable
$exePath = Join-Path $exeDir "AutoAim.exe"
if (Test-Path $exePath) {
    Copy-Item $exePath $distDir -Force
    Write-Host "  AutoAim.exe copied to: $distDir" -ForegroundColor Green
} else {
    Write-Host "  ERROR: AutoAim.exe not found in build output!" -ForegroundColor Red
}

# Copy IbInputSimulator.dll
$inputDll = Join-Path $ProjectDir "IbInputSimulator.dll"
if (Test-Path $inputDll) {
    Copy-Item $inputDll $distDir -Force
    Write-Host "  IbInputSimulator.dll copied." -ForegroundColor Green
} else {
    Write-Host "  WARNING: IbInputSimulator.dll not found in project root." -ForegroundColor Yellow
}

# Copy OpenCV DLLs
$opencvBinDirs = @(
    (Join-Path $ProjectDir "deps\opencv\build\x64\vc17\bin"),
    (Join-Path $ProjectDir "deps\opencv\build\x64\vc16\bin")
)
foreach ($binDir in $opencvBinDirs) {
    if (Test-Path $binDir) {
        $dlls = Get-ChildItem "$binDir\*.dll" -ErrorAction SilentlyContinue
        foreach ($dll in $dlls) {
            Copy-Item $dll.FullName $distDir -Force
        }
        Write-Host "  OpenCV DLLs copied from: $binDir" -ForegroundColor Green
        break
    }
}

# Copy ONNX Runtime DLLs
$onnxLibDir = Join-Path $ProjectDir "deps\onnxruntime\lib"
if (Test-Path $onnxLibDir) {
    $onnxDlls = Get-ChildItem "$onnxLibDir\*.dll" -ErrorAction SilentlyContinue
    foreach ($dll in $onnxDlls) {
        Copy-Item $dll.FullName $distDir -Force
    }
    Write-Host "  ONNX Runtime DLLs copied." -ForegroundColor Green
}

Pop-Location

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Build Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host " Output directory: $distDir" -ForegroundColor White
Write-Host ""
Write-Host " To run:" -ForegroundColor Yellow
Write-Host "   1. Place your .onnx model in: $distDir" -ForegroundColor White
Write-Host "   2. Run as Administrator: $distDir\AutoAim.exe" -ForegroundColor White
Write-Host ""