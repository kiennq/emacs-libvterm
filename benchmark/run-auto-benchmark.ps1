# Automated Vterm Performance Benchmark
# Runs vterm with profiling and compares baseline vs optimized

param(
    [string]$EmacsPath = "c:/ProgramData/scoop/apps/emacs-k/current/bin/emacs.exe",
    [string]$VtermPath = "C:/Users/kienn/.cache/quelpa/build/vterm",
    [string]$OutputDir = "C:/Users/kienn/.cache/vterm",
    [int]$Iterations = 5
)

Write-Host "=== Vterm Automated Performance Benchmark ===" -ForegroundColor Cyan
Write-Host "Emacs: $EmacsPath"
Write-Host "Vterm: $VtermPath"
Write-Host "Output: $OutputDir"
Write-Host "Iterations: $Iterations"
Write-Host ""

# Check if Emacs exists
if (-not (Test-Path $EmacsPath)) {
    Write-Host "ERROR: Emacs not found at $EmacsPath" -ForegroundColor Red
    exit 1
}

# Check if vterm module exists
$vtermModule = Join-Path $OutputDir "vterm-module.dll"
if (-not (Test-Path $vtermModule)) {
    Write-Host "ERROR: vterm-module.dll not found at $vtermModule" -ForegroundColor Red
    exit 1
}

Write-Host "Running benchmark (this may take 30-60 seconds)..." -ForegroundColor Yellow
Write-Host ""

# Build Emacs command
$vtermLoadPath = $VtermPath -replace '\\', '/'
$testScript = "$VtermPath/benchmark/vterm-perf-test.el" -replace '\\', '/'
$vtermEl = "$VtermPath/vterm.el" -replace '\\', '/'

$emacsArgs = @(
    "-Q"
    "--batch"
    "--eval"
    "(setq vterm-module-cmake-args `"-DUSE_SYSTEM_LIBVTERM=OFF`")"
    "--eval"
    "(setq vterm-always-compile-module nil)"
    "--eval"
    "(add-to-list 'load-path \`"$vtermLoadPath\`")"
    "--eval"
    "(require 'vterm)"
    "--eval"
    "(setq vterm-perf-test-iterations $Iterations)"
    "--load"
    "$testScript"
)

Write-Host "Command: $EmacsPath $($emacsArgs -join ' ')" -ForegroundColor DarkGray
Write-Host ""

# Run benchmark
$outputFile = Join-Path $OutputDir "benchmark-output.txt"
$errorFile = Join-Path $OutputDir "benchmark-errors.txt"

try {
    & $EmacsPath $emacsArgs 2>&1 | Tee-Object -FilePath $outputFile | Write-Host
    
    # Check if profiling data was generated
    $profileFile = Join-Path $OutputDir "vterm-profile.txt"
    if (Test-Path $profileFile) {
        Write-Host ""
        Write-Host "=== Profiling Results ===" -ForegroundColor Green
        Get-Content $profileFile | Write-Host
        
        # Also save with timestamp
        $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $archiveFile = Join-Path $OutputDir "profile-$timestamp.txt"
        Copy-Item $profileFile $archiveFile
        Write-Host ""
        Write-Host "Results archived to: $archiveFile" -ForegroundColor Cyan
    } else {
        Write-Host ""
        Write-Host "WARNING: No profiling data generated" -ForegroundColor Yellow
        Write-Host "Make sure vterm-module.dll was built with -DENABLE_PROFILING=ON" -ForegroundColor Yellow
    }
    
    # Check for errors
    if (Test-Path $outputFile) {
        $output = Get-Content $outputFile -Raw
        if ($output -match "error|Error|ERROR") {
            Write-Host ""
            Write-Host "WARNING: Errors detected in output" -ForegroundColor Yellow
        }
    }
    
} catch {
    Write-Host "ERROR: Benchmark failed: $_" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== Benchmark Complete ===" -ForegroundColor Green
Write-Host "Full output: $outputFile"
Write-Host "Profile data: $profileFile"
