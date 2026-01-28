# Integration test script for conpty-proxy control pipe
# Tests the control pipe functionality end-to-end

$ErrorActionPreference = "Stop"

Write-Host "=== ConPTY Control Pipe Integration Test ===" -ForegroundColor Cyan
Write-Host ""

# Configuration
$CONPTY_ID = "test-$(Get-Random -Maximum 9999)"
$CONPTY_PROXY = ".\build\conpty-proxy\conpty_proxy.exe"
$TEST_CLIENT = ".\tests\test-conpty-pipe.exe"
$WIDTH = 80
$HEIGHT = 24
$NEW_WIDTH = 120
$NEW_HEIGHT = 40

# Check if binaries exist
if (-not (Test-Path $CONPTY_PROXY)) {
    Write-Host "[ERROR] conpty_proxy.exe not found at: $CONPTY_PROXY" -ForegroundColor Red
    exit 1
}

Write-Host "[Step 1] Compiling test client..." -ForegroundColor Yellow
q:\repos\emacs-build\msys64\msys2_shell.cmd -ucrt64 -defterm -no-start -here -c "gcc -o tests/test-conpty-pipe.exe tests/test-conpty-pipe.c -lkernel32"

if (-not (Test-Path $TEST_CLIENT)) {
    Write-Host "[ERROR] Failed to compile test client" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] Test client compiled" -ForegroundColor Green
Write-Host ""

# Start conpty-proxy in background
Write-Host "[Step 2] Starting conpty-proxy with id: $CONPTY_ID" -ForegroundColor Yellow
Write-Host "Command: $CONPTY_PROXY new $CONPTY_ID $WIDTH $HEIGHT cmd.exe" -ForegroundColor Gray

$conptyProcess = Start-Process -FilePath $CONPTY_PROXY -ArgumentList "new",$CONPTY_ID,$WIDTH,$HEIGHT,"cmd.exe" -PassThru -WindowStyle Minimized

Write-Host "[OK] conpty-proxy started (PID: $($conptyProcess.Id))" -ForegroundColor Green
Write-Host ""

# Wait for pipe to be created
Write-Host "[Step 3] Waiting for control pipe to be ready..." -ForegroundColor Yellow
$pipeName = "\\.\pipe\conpty-proxy-ctrl-$CONPTY_ID"
$maxWait = 5
$waited = 0

while ($waited -lt $maxWait) {
    Start-Sleep -Milliseconds 500
    $waited += 0.5
    
    # Check if pipe exists by trying to get its properties
    try {
        $pipeExists = Test-Path $pipeName -ErrorAction SilentlyContinue
        if ($pipeExists) {
            break
        }
    } catch {
        # Pipe doesn't exist yet
    }
}

if ($waited -ge $maxWait) {
    Write-Host "[ERROR] Control pipe was not created within $maxWait seconds" -ForegroundColor Red
    Write-Host "Killing conpty-proxy process..." -ForegroundColor Yellow
    Stop-Process -Id $conptyProcess.Id -Force
    exit 1
}

Write-Host "[OK] Control pipe is ready (waited $waited seconds)" -ForegroundColor Green
Write-Host ""

# Test 1: Send initial resize
Write-Host "[Step 4] Test 1: Sending resize message ($NEW_WIDTH x $NEW_HEIGHT)" -ForegroundColor Yellow
Write-Host "Command: $TEST_CLIENT $CONPTY_ID $NEW_WIDTH $NEW_HEIGHT" -ForegroundColor Gray

$output = & $TEST_CLIENT $CONPTY_ID $NEW_WIDTH $NEW_HEIGHT 2>&1 | Out-String
Write-Host $output -ForegroundColor Gray

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Test client failed with exit code: $LASTEXITCODE" -ForegroundColor Red
    Write-Host "Killing conpty-proxy process..." -ForegroundColor Yellow
    Stop-Process -Id $conptyProcess.Id -Force
    exit 1
}

Write-Host "[OK] Test 1 passed" -ForegroundColor Green
Write-Host ""

# Wait a bit to see if conpty-proxy is still alive
Write-Host "[Step 5] Checking if conpty-proxy is still running..." -ForegroundColor Yellow
Start-Sleep -Milliseconds 500

$process = Get-Process -Id $conptyProcess.Id -ErrorAction SilentlyContinue
if (-not $process) {
    Write-Host "[ERROR] conpty-proxy process has died!" -ForegroundColor Red
    exit 1
}

# Check CPU usage
$cpuBefore = (Get-Process -Id $conptyProcess.Id).CPU
Start-Sleep -Seconds 2
$cpuAfter = (Get-Process -Id $conptyProcess.Id).CPU
$cpuUsage = $cpuAfter - $cpuBefore

Write-Host "[OK] conpty-proxy is still running (PID: $($conptyProcess.Id))" -ForegroundColor Green
Write-Host "     CPU usage in last 2 seconds: $([math]::Round($cpuUsage, 2))s" -ForegroundColor Gray

if ($cpuUsage -gt 1.0) {
    Write-Host "[WARNING] High CPU usage detected! Possible infinite loop!" -ForegroundColor Red
} else {
    Write-Host "[OK] CPU usage is normal (no infinite loop)" -ForegroundColor Green
}
Write-Host ""

# Test 2: Send multiple rapid resizes
Write-Host "[Step 6] Test 2: Sending 10 rapid resize messages" -ForegroundColor Yellow

for ($i = 0; $i -lt 10; $i++) {
    $w = 80 + $i * 5
    $h = 24 + $i * 2
    
    Write-Host "  Resize $($i+1): ${w}x${h}" -ForegroundColor Gray
    $output = & $TEST_CLIENT $CONPTY_ID $w $h 2>&1 | Out-String
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Resize $($i+1) failed!" -ForegroundColor Red
        Write-Host $output -ForegroundColor Gray
        break
    }
    
    Start-Sleep -Milliseconds 100
}

Write-Host "[OK] Test 2 completed" -ForegroundColor Green
Write-Host ""

# Final CPU check
Write-Host "[Step 7] Final CPU check..." -ForegroundColor Yellow
$cpuBefore = (Get-Process -Id $conptyProcess.Id).CPU
Start-Sleep -Seconds 2
$cpuAfter = (Get-Process -Id $conptyProcess.Id).CPU
$cpuUsage = $cpuAfter - $cpuBefore

Write-Host "     CPU usage in last 2 seconds: $([math]::Round($cpuUsage, 2))s" -ForegroundColor Gray

if ($cpuUsage -gt 1.0) {
    Write-Host "[ERROR] High CPU usage detected! Infinite loop likely present!" -ForegroundColor Red
    $testResult = "FAILED"
} else {
    Write-Host "[OK] CPU usage is normal" -ForegroundColor Green
    $testResult = "PASSED"
}
Write-Host ""

# Cleanup
Write-Host "[Step 8] Cleaning up..." -ForegroundColor Yellow
Stop-Process -Id $conptyProcess.Id -Force
Write-Host "[OK] conpty-proxy process terminated" -ForegroundColor Green
Write-Host ""

# Summary
Write-Host "=== TEST SUMMARY ===" -ForegroundColor Cyan
Write-Host "Test Result: $testResult" -ForegroundColor $(if ($testResult -eq "PASSED") { "Green" } else { "Red" })
Write-Host ""

if ($testResult -eq "FAILED") {
    exit 1
}

exit 0
