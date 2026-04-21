# setup_deps.ps1 - Download and set up all dependencies for building
# Requires: PowerShell 5.1+, ~2GB free disk space, internet access
# Run: .\setup_deps.ps1
#
# If downloads fail (network/firewall), see the manual instructions printed below.

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$DepsDir = Join-Path $ProjectDir "deps"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " AutoAim Dependency Setup Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if (!(Test-Path $DepsDir)) {
    New-Item -ItemType Directory -Path $DepsDir -Force | Out-Null
}

# =============================================================================
# OpenCV
# =============================================================================
$OpenCVVersion = "4.10.0"
$OpenCVDir = Join-Path $DepsDir "opencv"

Write-Host "[1/2] OpenCV $OpenCVVersion" -ForegroundColor Yellow

if (!(Test-Path "$OpenCVDir\build\include\opencv2\opencv.hpp")) {

    $OpenCVUrl = "https://github.com/opencv/opencv/releases/download/$OpenCVVersion/opencv-$($OpenCVVersion -replace '\.','')-windows.exe"
    $OpenCVInstaller = Join-Path $DepsDir "opencv-setup.exe"

    Write-Host "  Downloading OpenCV $OpenCVVersion..."
    Write-Host "  URL: $OpenCVUrl"

    $downloaded = $false
    try {
        & curl.exe -L -o $OpenCVInstaller $OpenCVUrl --connect-timeout 30 --max-time 600 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $fsize = (Get-Item $OpenCVInstaller).Length
            if ($fsize -gt 100000000) {  # > 100MB, likely complete
                $downloaded = $true
            }
        }
    } catch { }

    if ($downloaded) {
        Write-Host "  Extracting OpenCV..."
        Start-Process -FilePath $OpenCVInstaller -ArgumentList "-o`"$OpenCVDir`" -y" -Wait
        $ExtractedDir = Join-Path $OpenCVDir "opencv"
        if (Test-Path $ExtractedDir) {
            Get-ChildItem $ExtractedDir | Move-Item -Destination $OpenCVDir -Force
            Remove-Item $ExtractedDir -Force -Recurse -ErrorAction SilentlyContinue
        }
        Remove-Item $OpenCVInstaller -Force -ErrorAction SilentlyContinue
        Write-Host "  OpenCV installed!" -ForegroundColor Green
    } else {
        Remove-Item $OpenCVInstaller -Force -ErrorAction SilentlyContinue
        Write-Host ""
        Write-Host "  AUTOMATIC DOWNLOAD FAILED." -ForegroundColor Red
        Write-Host "  Please download OpenCV manually:" -ForegroundColor Yellow
        Write-Host "  1. Go to https://opencv.org/releases/" -ForegroundColor White
        Write-Host "  2. Download opencv-$($OpenCVVersion -replace '\.','')-windows.exe" -ForegroundColor White
        Write-Host "  3. Run the exe to extract" -ForegroundColor White
        Write-Host "  4. Move extracted 'opencv' folder to: $OpenCVDir" -ForegroundColor White
        Write-Host "     Expected: $OpenCVDir\build\include\opencv2\opencv.hpp" -ForegroundColor Gray
        Write-Host ""
    }
} else {
    Write-Host "  OpenCV already installed." -ForegroundColor Green
}

# =============================================================================
# ONNX Runtime
# =============================================================================
$OnnxVersion = "1.19.2"
$OnnxRuntimeDir = Join-Path $DepsDir "onnxruntime"

Write-Host "[2/2] ONNX Runtime $OnnxVersion (GPU)" -ForegroundColor Yellow

if (!(Test-Path "$OnnxRuntimeDir\include\onnxruntime_cxx_api.h")) {
    $urls = @(
        @("GPU (CUDA/TensorRT)", "https://github.com/microsoft/onnxruntime/releases/download/v$OnnxVersion/onnxruntime-win-x64-gpu-$OnnxVersion.zip"),
        @("CPU-only", "https://github.com/microsoft/onnxruntime/releases/download/v$OnnxVersion/onnxruntime-win-x64-$OnnxVersion.zip")
    )

    $downloaded = $false
    foreach ($urlInfo in $urls) {
        $label = $urlInfo[0]; $url = $urlInfo[1]
        Write-Host "  Trying $label..."
        $zipFile = Join-Path $DepsDir "onnxruntime.zip"
        try {
            & curl.exe -L -o $zipFile $url --connect-timeout 30 --max-time 600 2>&1 | Out-Null
            if ($LASTEXITCODE -eq 0) {
                $fsize = (Get-Item $zipFile -ErrorAction SilentlyContinue).Length
                if ($fsize -gt 5000000) {  # > 5MB, likely complete
                    $downloaded = $true
                    Write-Host "  Extracting ONNX Runtime..."
                    $TempExtract = Join-Path $DepsDir "onnxruntime-temp"
                    Expand-Archive -Path $zipFile -DestinationPath $TempExtract -Force
                    $ContentDir = (Get-ChildItem $TempExtract -Directory | Select-Object -First 1).FullName
                    if (!(Test-Path $OnnxRuntimeDir)) { New-Item -ItemType Directory -Path $OnnxRuntimeDir -Force | Out-Null }
                    Copy-Item "$ContentDir\include" -Destination "$OnnxRuntimeDir\include" -Recurse -Force
                    Copy-Item "$ContentDir\lib" -Destination "$OnnxRuntimeDir\lib" -Recurse -Force
                    Remove-Item $zipFile -Force -ErrorAction SilentlyContinue
                    Remove-Item $TempExtract -Force -Recurse -ErrorAction SilentlyContinue
                    Write-Host "  ONNX Runtime $label installed!" -ForegroundColor Green
                    break
                }
            }
        } catch { }
        Remove-Item $zipFile -Force -ErrorAction SilentlyContinue
    }

    if (!$downloaded) {
        Write-Host ""
        Write-Host "  AUTOMATIC DOWNLOAD FAILED." -ForegroundColor Red
        Write-Host "  Please download ONNX Runtime manually:" -ForegroundColor Yellow
        Write-Host "  1. Go to https://github.com/microsoft/onnxruntime/releases/tag/v$OnnxVersion" -ForegroundColor White
        Write-Host "  2. Download onnxruntime-win-x64-gpu-$OnnxVersion.zip" -ForegroundColor White
        Write-Host "  3. Extract and copy 'include' and 'lib' to: $OnnxRuntimeDir\" -ForegroundColor White
        Write-Host "     Expected: $OnnxRuntimeDir\include\onnxruntime_cxx_api.h" -ForegroundColor Gray
        Write-Host ""
    }
} else {
    Write-Host "  ONNX Runtime already installed." -ForegroundColor Green
}

# =============================================================================
# Verify
# =============================================================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
$cvOk = Test-Path "$OpenCVDir\build\include\opencv2\opencv.hpp"
$ortOk = Test-Path "$OnnxRuntimeDir\include\onnxruntime_cxx_api.h"
Write-Host " OpenCV:        $(if($cvOk){'FOUND'}else{'NOT FOUND'})" -ForegroundColor $(if($cvOk){'Green'}else{'Red'})
Write-Host " ONNX Runtime:  $(if($ortOk){'FOUND'}else{'NOT FOUND'})" -ForegroundColor $(if($ortOk){'Green'}else{'Red'})
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
if ($cvOk -and $ortOk) {
    Write-Host " All dependencies ready! Run: .\build_release.ps1" -ForegroundColor Green
} else {
    Write-Host " Some dependencies missing. Follow manual instructions above." -ForegroundColor Red
}