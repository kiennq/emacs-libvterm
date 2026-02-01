#!/usr/bin/env pwsh
# run-benchmark.ps1 - Helper script to run vterm benchmarks and capture profiling output

param(
    [string]$EmacsPath = "C:\Program Files\Emacs\bin\runemacs.exe",
    [string]$VtermPath = "C:\Users\$env:USERNAME\.cache\quelpa\build\vterm",
    [string]$OutputDir = ".\benchmark-results",
    [switch]$WithProfile
)

# Create output directory
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$resultFile = Join-Path $OutputDir "benchmark-$timestamp.txt"
$profileFile = Join-Path $OutputDir "profile-$timestamp.txt"

Write-Host "=== Vterm Benchmark Runner ===" -ForegroundColor Cyan
Write-Host "Emacs: $EmacsPath"
Write-Host "Vterm: $VtermPath"
Write-Host "Output: $OutputDir"
Write-Host ""

# Check if Emacs exists
if (-not (Test-Path $EmacsPath)) {
    Write-Host "ERROR: Emacs not found at $EmacsPath" -ForegroundColor Red
    Write-Host "Please specify correct path with -EmacsPath parameter" -ForegroundColor Yellow
    exit 1
}

# Check if benchmark script exists
$benchmarkScript = Join-Path $VtermPath "benchmark\benchmark.el"
if (-not (Test-Path $benchmarkScript)) {
    Write-Host "ERROR: Benchmark script not found at $benchmarkScript" -ForegroundColor Red
    exit 1
}

Write-Host "Running benchmarks..." -ForegroundColor Green
Write-Host "This will take about 30-60 seconds..." -ForegroundColor Yellow
Write-Host ""

# Build Emacs command
$emacsCmd = @(
    "-Q",  # No init file
    "-L", $VtermPath,  # Load path
    "--eval", "(require 'vterm)",
    "--eval", "(load-file `"$benchmarkScript`")",
    "--eval", "(vterm-benchmark-run-all)",
    "--eval", "(kill-emacs)"
)

# Run Emacs and capture output
if ($WithProfile) {
    Write-Host "Profiling enabled - capturing stderr..."
    $profileFile = Join-Path $OutputDir "profile.txt"
    $stdoutFile = Join-Path $OutputDir "stdout.txt"
    $process = Start-Process -FilePath $EmacsPath `
                             -ArgumentList $emacsCmd `
                             -NoNewWindow `
                             -Wait `
                             -PassThru `
                             -RedirectStandardError $profileFile `
                             -RedirectStandardOutput $stdoutFile
} else {
    $process = Start-Process -FilePath $EmacsPath `
                             -ArgumentList $emacsCmd `
                             -NoNewWindow `
                             -Wait `
                             -PassThru
}

# Check if benchmark results were created
$benchResultFile = Join-Path $VtermPath "benchmark-results.txt"
if (Test-Path $benchResultFile) {
    Copy-Item $benchResultFile $resultFile -Force
    Write-Host ""
    Write-Host "=== Benchmark Results ===" -ForegroundColor Green
    Get-Content $resultFile | Write-Host
    
    Write-Host ""
    Write-Host "Results saved to: $resultFile" -ForegroundColor Cyan
} else {
    Write-Host "WARNING: Benchmark results file not found" -ForegroundColor Yellow
}

# Show profiling results if enabled
if ($WithProfile -and (Test-Path $profileFile)) {
    $profileContent = Get-Content $profileFile
    if ($profileContent.Count -gt 0) {
        Write-Host ""
        Write-Host "=== Profiling Results ===" -ForegroundColor Green
        $profileContent | Write-Host
        Write-Host ""
        Write-Host "Profile saved to: $profileFile" -ForegroundColor Cyan
    } else {
        Write-Host ""
        Write-Host "WARNING: No profiling output captured" -ForegroundColor Yellow
        Write-Host "Make sure vterm-module.dll was built with -DENABLE_PROFILING=ON" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Done!" -ForegroundColor Green
