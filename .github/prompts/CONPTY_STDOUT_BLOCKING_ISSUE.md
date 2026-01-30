# ConPTY Proxy Stdout Blocking Issue

**Date:** January 29, 2026  
**Issue:** Emacs still hangs periodically, requires killing conpty_proxy.exe  
**Root Cause:** Synchronous WriteFile to stdout blocks when Emacs isn't reading

---

## Problem Analysis

### Current Architecture

**conpty_proxy.c** has two threads:

1. **IOCP Thread** (line 280-308):
   - Reads from ConPTY **asynchronously** (overlapped I/O)
   - Writes to stdout **SYNCHRONOUSLY** (line 301)
   ```c
   WriteFile(pty->std_out, pty->io_buf[current_buf], bytes_readed, &writted, NULL);
   ```

2. **Main Thread** (stdio_run, line 311-322):
   - Reads from stdin **SYNCHRONOUSLY** (line 315)
   ```c
   ReadFile(GetStdHandle(STD_INPUT_HANDLE), pty->std_buf, sizeof(pty->std_buf), &readed, NULL);
   ```
   - Writes to ConPTY **SYNCHRONOUSLY** (line 319)
   ```c
   WriteFile(pty->io_write, pty->std_buf, readed, &writted, NULL);
   ```

### Why This Causes Hangs

**Scenario 1: Stdout blocks**
1. ConPTY generates lots of output
2. IOCP thread reads from ConPTY (async) → success
3. IOCP thread calls `WriteFile(stdout, ...)` → **BLOCKS** if Emacs isn't reading
4. If Emacs is busy (GC, redisplay, other events), it won't read from stdout pipe
5. Pipe buffer fills up (~4KB default)
6. WriteFile blocks indefinitely
7. IOCP thread stuck → no more ConPTY output processed
8. **User sees hang**, must kill conpty_proxy.exe

**Scenario 2: Stdin blocks**
1. User types in Emacs
2. Emacs calls `process-send-string` → writes to stdin pipe
3. Main thread's `ReadFile(stdin, ...)` reads it → success
4. But if ConPTY is busy or backpressured, `WriteFile(pty->io_write, ...)` blocks
5. Main thread stuck → no more keyboard input processed
6. **User sees hang**

### Why Emacs Might Not Read

Emacs process filter (`vterm--filter`) is called from Emacs event loop. If Emacs is:
- Running garbage collection
- Processing other events (timers, etc.)
- Redisplaying (slow on Windows)
- Blocked on something else

Then it won't call `read()` on the stdout pipe, causing WriteFile to block.

---

## Solution Options

### Option 1: Make Stdout Write Asynchronous (Recommended)

Change line 301 to use overlapped I/O:

```c
// BEFORE (blocking):
WriteFile(pty->std_out, pty->io_buf[current_buf], bytes_readed, &writted, NULL);

// AFTER (async):
OVERLAPPED stdout_overl = {0};
WriteFile(pty->std_out, pty->io_buf[current_buf], bytes_readed, NULL, &stdout_overl);
// Don't wait for completion - let OS buffer it
```

**Benefits**:
- IOCP thread never blocks on stdout
- OS handles buffering and backpressure
- Continues reading from ConPTY even if Emacs is slow

**Challenges**:
- Need to manage OVERLAPPED structures for each write
- Need to track completion to know when buffer can be reused
- More complex than synchronous approach

---

### Option 2: Use Separate Thread for Stdout Writes

```c
// Queue writes to separate thread
typedef struct {
    char* buffer;
    DWORD size;
} WriteRequest;

HANDLE write_queue;
HANDLE write_thread;

DWORD WINAPI write_thread_func(LPVOID param) {
    while (1) {
        WriteRequest* req = dequeue(write_queue);
        WriteFile(pty->std_out, req->buffer, req->size, NULL, NULL);
        free(req->buffer);
        free(req);
    }
}

// In IOCP thread:
WriteRequest* req = malloc(...);
req->buffer = copy_buffer(pty->io_buf[current_buf], bytes_readed);
req->size = bytes_readed;
enqueue(write_queue, req);
```

**Benefits**:
- IOCP thread never blocks
- Simple queue-based design
- Write thread can block without affecting reads

**Challenges**:
- Need thread synchronization (mutex/semaphore)
- Extra memory allocation per write
- More threads = more complexity

---

### Option 3: Add Timeout to WriteFile

```c
// Set timeout on stdout handle
COMMTIMEOUTS timeouts = {0};
timeouts.WriteTotalTimeoutConstant = 100;  // 100ms timeout
SetCommTimeouts(pty->std_out, &timeouts);

// WriteFile will now timeout instead of blocking forever
if (!WriteFile(pty->std_out, pty->io_buf[current_buf], bytes_readed, &writted, NULL)) {
    if (GetLastError() == ERROR_TIMEOUT) {
        // Handle timeout - maybe queue the data?
    }
}
```

**Benefits**:
- Simple change
- Prevents indefinite blocking

**Challenges**:
- `SetCommTimeouts` only works on COM ports, not pipes
- Would need to use `CancelSynchronousIo` on another thread (complex)

---

### Option 4: Use Named Pipes with Asynchronous I/O

Instead of anonymous pipes (from `make-process`), use named pipes:

```c
// Create named pipe with async support
HANDLE stdout_pipe = CreateNamedPipe(
    "\\\\.\\pipe\\vterm-stdout",
    PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,  // Async!
    PIPE_TYPE_BYTE | PIPE_WAIT,
    1, 65536, 65536, 0, NULL);

// Write asynchronously
OVERLAPPED ovl = {0};
WriteFile(stdout_pipe, buffer, size, NULL, &ovl);
// Check completion via GetOverlappedResult or IOCP
```

**Benefits**:
- True async I/O
- Can integrate with existing IOCP architecture

**Challenges**:
- Requires changing Emacs side to connect to named pipe (major change)
- Would break compatibility with current `make-process` approach

---

## Recommended Solution

**Option 1: Async Stdout Write with OVERLAPPED**

This is the cleanest solution that fits the existing architecture:

1. Add `OVERLAPPED stdout_overl` to `conpty_t` structure
2. Make stdout handle async: `FILE_FLAG_OVERLAPPED` (may require recreating handle)
3. Change line 301 to async WriteFile
4. Track completion via IOCP (add `COMPLETION_KEY_STDOUT_WRITE`)
5. Only reuse buffer after write completes

### Implementation Sketch

```c
// In conpty_t structure:
typedef struct {
    ...
    OVERLAPPED stdout_overl;
    BOOL stdout_write_pending;
    char* pending_stdout_buf;
    DWORD pending_stdout_size;
} conpty_t;

// In IOCP thread:
if (comp_key == COMPLETION_KEY_IO_READ) {
    int current_buf = pty->io_buf_active;
    pty->io_buf_active = 1 - current_buf;
    async_io_read(pty);
    
    // Queue async write to stdout
    if (!pty->stdout_write_pending) {
        memset(&pty->stdout_overl, 0, sizeof(OVERLAPPED));
        BOOL success = WriteFile(pty->std_out, pty->io_buf[current_buf], 
                                  bytes_readed, NULL, &pty->stdout_overl);
        if (!success && GetLastError() == ERROR_IO_PENDING) {
            pty->stdout_write_pending = TRUE;
        }
    } else {
        // Previous write still pending - buffer this data
        // (Or drop it, or implement queue)
    }
} else if (comp_key == COMPLETION_KEY_STDOUT_WRITE) {
    pty->stdout_write_pending = FALSE;
    // Write completed, can reuse buffer
}
```

---

## Testing Strategy

1. **Reproduce hang**:
   ```elisp
   M-x vterm
   ;; Generate lots of output
   find /c/Windows/System32
   ;; While output is printing, do:
   M-x garbage-collect  ; Force GC to delay reading
   ;; Check if proxy hangs
   ```

2. **Check pipe buffer fullness**:
   - Use Process Explorer to monitor handle states
   - Check if WriteFile is blocking (thread state)

3. **Verify fix**:
   - Output should continue even during Emacs GC
   - No hangs even with rapid output + Emacs delays
   - CPU usage should stay low

---

## Alternative Quick Fix

If async I/O is too complex, we could:

**Make stdout write non-blocking by increasing pipe buffer size**:

In Emacs `make-process`, there's no direct way to control pipe buffer size, but we could:
1. Have Emacs create the pipes explicitly with larger buffers
2. Pass pipe handles to conpty_proxy.exe
3. Or: Have proxy recreate stdout handle with larger buffer (hacky)

This won't eliminate blocking, but will make it less likely by allowing more data to buffer.

---

## Related Issues

- Original `accept-process-output` removal fixed Elisp-side blocking
- This issue is C-side blocking in the proxy
- Both stem from synchronous I/O expectations

---

## Decision Needed

Which approach should we take?
1. ✅ **Async stdout write** (Option 1) - Best long-term solution
2. ⏸️ **Separate write thread** (Option 2) - Simpler but more threads
3. ❌ **Timeout** (Option 3) - Not feasible on pipes
4. ❌ **Named pipes** (Option 4) - Too invasive

**Recommendation**: Implement Option 1 (async stdout write with OVERLAPPED).

---

**Status**: Issue diagnosed, solution proposed  
**Next Step**: Implement async WriteFile with OVERLAPPED  
