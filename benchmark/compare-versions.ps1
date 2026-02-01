# Compare Baseline vs Optimized Performance
# This script runs the same test on both versions

param(
    [string]$VtermDir = "C:\Users\kienn\.cache\vterm",
    [string]$OutputDir = "C:\Users\kienn\.cache\vterm"
)

Write-Host "=== Vterm Performance Comparison ===" -ForegroundColor Cyan
Write-Host ""

$baseline = Join-Path $VtermDir "vterm-module-baseline.dll"
$optimized = Join-Path $VtermDir "vterm-module-optimized.dll"
$active = Join-Path $VtermDir "vterm-module.dll"

if (-not (Test-Path $baseline)) {
    Write-Host "ERROR: Baseline not found at $baseline" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $optimized)) {
    Write-Host "ERROR: Optimized not found at $optimized" -ForegroundColor Red
    exit 1
}

Write-Host "Instructions:" -ForegroundColor Yellow
Write-Host "1. I will deploy BASELINE version" -ForegroundColor Yellow
Write-Host "2. You restart Emacs and run: M-x load-file benchmark/simple-test.el" -ForegroundColor Yellow
Write-Host "3. Press Enter when done" -ForegroundColor Yellow
Write-Host "4. I will deploy OPTIMIZED version" -ForegroundColor Yellow
Write-Host "5. You restart Emacs and run the same test" -ForegroundColor Yellow
Write-Host "6. Press Enter when done" -ForegroundColor Yellow
Write-Host "7. I will compare the results" -ForegroundColor Yellow
Write-Host ""

# Deploy baseline
Write-Host "Deploying BASELINE version..." -ForegroundColor Green
Copy-Item $baseline $active -Force
Write-Host "BASELINE deployed. Please restart Emacs and run the test." -ForegroundColor Green
Write-Host "Press Enter when done..."
Read-Host

$baselineResults = Join-Path $OutputDir "vterm-profile.txt"
if (Test-Path $baselineResults) {
    $baselineArchive = Join-Path $OutputDir "profile-baseline.txt"
    Copy-Item $baselineResults $baselineArchive -Force
    Write-Host "Baseline results saved to: $baselineArchive" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "Deploying OPTIMIZED version..." -ForegroundColor Green
Copy-Item $optimized $active -Force
Write-Host "OPTIMIZED deployed. Please restart Emacs and run the same test." -ForegroundColor Green
Write-Host "Press Enter when done..."
Read-Host

$optimizedResults = Join-Path $OutputDir "vterm-profile.txt"
if (Test-Path $optimizedResults) {
    $optimizedArchive = Join-Path $OutputDir "profile-optimized.txt"
    Copy-Item $optimizedResults $optimizedArchive -Force
    Write-Host "Optimized results saved to: $optimizedArchive" -ForegroundColor Cyan
}

# Compare results
Write-Host ""
Write-Host "=== COMPARISON ===" -ForegroundColor Cyan
Write-Host ""

if ((Test-Path $baselineArchive) -and (Test-Path $optimizedArchive)) {
    Write-Host "BASELINE:" -ForegroundColor Yellow
    Get-Content $baselineArchive | Write-Host
    Write-Host ""
    Write-Host "OPTIMIZED:" -ForegroundColor Yellow
    Get-Content $optimizedArchive | Write-Host
    Write-Host ""
    Write-Host "Compare the 'Avg (ms)' column for refresh_lines and term_redraw" -ForegroundColor Cyan
} else {
    Write-Host "ERROR: Could not find result files" -ForegroundColor Red
}

Write-Host ""
Write-Host "Done! Optimized version is currently active." -ForegroundColor Green
