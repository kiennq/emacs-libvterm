# ConPTY Control Pipe Tests

This directory contains integration tests for the conpty-proxy control pipe functionality.

## Test Files

- **test-conpty-pipe.c** - Simple C client to test control pipe communication
- **test-integration.ps1** - Automated PowerShell test script

## Running Tests

### Prerequisites

- Windows 10 1809+ (ConPTY support)
- MSYS2 UCRT64 environment
- Built `conpty_proxy.exe` in `build/conpty-proxy/`

### Quick Test

```powershell
cd C:\Users\kingu\.cache\quelpa\build\vterm
.\tests\test-integration.ps1
```

This will:
1. Compile the test client
2. Start conpty-proxy with a random ID
3. Send resize messages via control pipe
4. Monitor CPU usage for infinite loops
5. Report test results

### Manual Testing

1. **Compile test client**:
   ```bash
   gcc -o test-conpty-pipe.exe tests/test-conpty-pipe.c
   ```

2. **Start conpty-proxy** (in terminal 1):
   ```bash
   ./build/conpty-proxy/conpty_proxy.exe new test-123 80 24 cmd.exe
   ```

3. **Send resize message** (in terminal 2):
   ```bash
   ./test-conpty-pipe.exe test-123 100 30
   ```

4. **Expected output**:
   ```
   [1] Connecting to pipe: \\.\pipe\conpty-proxy-ctrl-test-123
   [2] Successfully connected to pipe!
   [3] Sending resize message: '100 30' (6 bytes)
   [4] Successfully wrote 6 bytes
   [5] Successfully flushed pipe
   [6] Closed pipe
   
   [SUCCESS] Resize message sent successfully!
   ```

## Troubleshooting

### Error: Pipe does not exist
- Verify conpty-proxy is running
- Check the conpty-id matches

### Error: Pipe is busy
- Another client is already connected
- Only one client can connect at a time

### High CPU usage after resize
- Indicates infinite loop bug
- Check conpty-proxy logs
- Use Process Explorer to see stuck threads

## Test Results Interpretation

**PASSED**: 
- All resize messages sent successfully
- CPU usage < 1s over 2 second period
- No infinite loops detected

**FAILED**:
- Client couldn't connect to pipe
- WriteFile failed
- CPU usage > 1s (infinite loop detected)
- conpty-proxy process crashed
