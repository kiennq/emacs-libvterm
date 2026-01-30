# Resize Hang Root Cause Analysis

**Date:** January 30, 2026  
**Issue:** Emacs hangs after resizing vterm window, requires killing conpty_proxy.exe  
**Root Cause:** Synchronous ReadFile in IOCP thread blocks when control pipe client disconnects

---

## The Smoking Gun

**File:** `conpty-proxy/conpty_proxy.c`  
**Line 259:** `ReadFile(pty->ctrl_pipe, buf, sizeof(buf), &readed, NULL);`

```c
static void on_ctrl_accept(conpty_t* pty) {
    int width = 0;
    int height = 0;
    char buf[64];
    DWORD readed;
    ReadFile(pty->ctrl_pipe, buf, sizeof(buf), &readed, NULL);  // ← BLOCKS!
    if (readed <= 0) {
        return;
    }
    // ... process resize ...
}
```

---

## The Deadlock Scenario

### What Happens During Resize

**1. Emacs side (vterm.el:1956-1957):**
```elisp
BOOL success = WriteFile(pipe, msg, msg_len, &written, NULL);
CloseHandle(pipe);  // ← IMMEDIATELY closes after write
```

**2. ConPTY proxy side:**
```c
// IOCP loop (line 290-305):
GetQueuedCompletionStatus(iocp, &bytes_readed, &comp_key, &ovl, INFINITE);

if (comp_key == COMPLETION_KEY_CTRL_ACCEPT) {
    on_ctrl_accept(pty);  // ← Calls synchronous ReadFile
    async_ctrl_accept(pty);
}
```

**3. The race condition:**

**Case A: Client writes then closes quickly (current behavior):**
```
1. Emacs: CreateFileA(pipe) → success
2. Emacs: WriteFile("80 24") → success
3. Emacs: CloseHandle(pipe) → client disconnects
4. ConPTY: ConnectNamedPipe completes → IOCP signals COMPLETION_KEY_CTRL_ACCEPT
5. ConPTY: on_ctrl_accept() called
6. ConPTY: ReadFile(pipe, ..., NULL) ← SYNCHRONOUS READ
7. If client already closed: ReadFile returns 0 bytes immediately ✅ Works
8. If client hasn't closed yet: ReadFile gets data ✅ Works
```

**Case B: ReadFile called BEFORE client writes (the bug):**
```
1. ConPTY: ConnectNamedPipe completes
2. ConPTY: IOCP signals COMPLETION_KEY_CTRL_ACCEPT
3. ConPTY: on_ctrl_accept() called
4. ConPTY: ReadFile(pipe, ..., NULL) ← BLOCKS waiting for data
5. Emacs: CreateFileA(pipe) → waiting for server to accept
6. DEADLOCK: Server blocking on ReadFile, client waiting to connect
```

**Actually, looking closer at the flow:**

The issue is **NOT a deadlock** but rather:

```
1. ConnectNamedPipe completes when client connects
2. IOCP signals COMPLETION_KEY_CTRL_ACCEPT
3. on_ctrl_accept() calls SYNCHRONOUS ReadFile
4. Client is expected to write data immediately after connecting
5. BUT: If client is slow or network lag, ReadFile BLOCKS the IOCP thread
6. While IOCP thread is blocked:
   - No more ConPTY output processed (can't read from ConPTY)
   - Emacs stops receiving output
   - User sees hang
```

---

## Why This Causes Hangs

### The IOCP Thread Does Everything

The IOCP thread (line 280-309) handles:
1. **ConPTY output** → stdout (COMPLETION_KEY_IO_READ)
2. **Control pipe resize** → ConPTY API (COMPLETION_KEY_CTRL_ACCEPT)

**When line 259 blocks:**
- ✅ ReadFile waits for client to write resize data
- ❌ IOCP thread stuck, can't process other events
- ❌ ConPTY output piles up, not read
- ❌ Emacs stops receiving terminal output
- ❌ User sees freeze

**Duration of block:**
- If client writes immediately: ~1ms (fast)
- If client is slow: 10-100ms (noticeable lag)
- If client crashes/disconnects: **INFINITE** (permanent hang)

---

## The Original Design Flaw

**Commit 7b0559c** attempted to fix control pipe handling but made it worse:

**Before commit 7b0559c:**
- Control pipe used overlapped I/O
- Separate OVERLAPPED structures for accept and read
- BUT: Had infinite loop bug due to incorrect state machine

**After commit 7b0559c:**
- Fixed infinite loop
- BUT: Changed to synchronous ReadFile (line 259)
- Introduced blocking in IOCP thread

**The fix fixed one bug but introduced another!**

---

## Why It Manifests After Resize

**User action:**
1. Resize Emacs window
2. vterm.el detects size change
3. Calls `vterm--conpty-proxy-debounce-resize` (debounced)
4. After 0.2s delay, calls `vterm--conpty-proxy-resize`
5. Opens pipe, writes resize message, closes pipe
6. ConPTY proxy's IOCP thread processes COMPLETION_KEY_CTRL_ACCEPT
7. Calls `on_ctrl_accept()` → **synchronous ReadFile blocks**
8. If Emacs is busy or client closed early: HANG

**Why killing conpty_proxy fixes it:**
- Killing proxy → process exits
- Emacs detects broken pipe → restarts vterm
- New proxy starts fresh

---

## Comparison: Why Keyboard Input Works

**Keyboard input path (line 311-323):**
```c
static int stdio_run(conpty_t* pty) {
    while (1) {
        // This runs in MAIN thread, not IOCP thread
        ReadFile(GetStdHandle(STD_INPUT_HANDLE), pty->std_buf, sizeof(pty->std_buf), &readed, NULL);
        WriteFile(pty->io_write, pty->std_buf, readed, &writted, NULL);
    }
}
```

**Key difference:**
- stdin ReadFile runs in **main thread** (separate from IOCP)
- If stdin blocks, IOCP thread continues processing ConPTY output
- No deadlock possible

**Control pipe ReadFile:**
- Runs in **IOCP thread** (same thread handling ConPTY output)
- If ctrl ReadFile blocks, ConPTY output stops
- Deadlock or hang

---

## The Solution

### Replace Synchronous ReadFile with Overlapped I/O

**Current (broken):**
```c
static void on_ctrl_accept(conpty_t* pty) {
    char buf[64];
    DWORD readed;
    ReadFile(pty->ctrl_pipe, buf, sizeof(buf), &readed, NULL);  // ← BLOCKS!
    // ... process resize ...
}
```

**Fixed (async):**
```c
// In conpty_t structure, add:
OVERLAPPED ctrl_read_overl;
char ctrl_read_buf[64];

static void async_ctrl_read(conpty_t* pty) {
    memset(&pty->ctrl_read_overl, 0, sizeof(OVERLAPPED));
    ReadFile(pty->ctrl_pipe, pty->ctrl_read_buf, sizeof(pty->ctrl_read_buf),
             NULL, &pty->ctrl_read_overl);
}

static void on_ctrl_accept(conpty_t* pty) {
    // Client connected, issue async read
    async_ctrl_read(pty);
}

static void on_ctrl_read(conpty_t* pty, DWORD bytes_read) {
    if (bytes_read > 0) {
        int width = 0, height = 0;
        sscanf_s(pty->ctrl_read_buf, "%d %d", &width, &height);
        
        if (width > 0 && height > 0 && 
            (pty->width != width || pty->height != height)) {
            pty->width = width;
            pty->height = height;
            COORD size = {(SHORT)width, (SHORT)height};
            g_resize_pseudo_console(pty->hpc, size);
        }
    }
    
    // Re-arm accept for next client
    async_ctrl_accept(pty);
}
```

**IOCP loop changes:**
```c
#define COMPLETION_KEY_CTRL_READ (0x03)  // New key

// In iocp_entry():
if (comp_key == COMPLETION_KEY_CTRL_ACCEPT) {
    on_ctrl_accept(pty);  // Issue async read
} else if (comp_key == COMPLETION_KEY_CTRL_READ) {
    on_ctrl_read(pty, bytes_readed);  // Process read completion
}
```

**CreateIoCompletionPort binding:**
```c
// Need to bind SAME handle with DIFFERENT key for read vs accept
// This is tricky - might need separate pipe handle or use ovl pointer to distinguish
```

---

## Alternative: Separate Control Pipe Thread

**If overlapped I/O is too complex, use dedicated thread:**

```c
static DWORD WINAPI ctrl_pipe_thread(LPVOID param) {
    conpty_t* pty = param;
    
    while (1) {
        // Wait for client connection
        if (!ConnectNamedPipe(pty->ctrl_pipe, NULL)) {
            if (GetLastError() != ERROR_PIPE_CONNECTED) {
                break;
            }
        }
        
        // Client connected, read resize message
        char buf[64];
        DWORD readed;
        if (ReadFile(pty->ctrl_pipe, buf, sizeof(buf), &readed, NULL) && readed > 0) {
            int width = 0, height = 0;
            sscanf_s(buf, "%d %d", &width, &height);
            
            if (width > 0 && height > 0) {
                // Thread-safe: ConPTY API is thread-safe
                EnterCriticalSection(&pty->resize_lock);
                pty->width = width;
                pty->height = height;
                COORD size = {(SHORT)width, (SHORT)height};
                g_resize_pseudo_console(pty->hpc, size);
                LeaveCriticalSection(&pty->resize_lock);
            }
        }
        
        // Disconnect and wait for next client
        DisconnectNamedPipe(pty->ctrl_pipe);
    }
    
    return 0;
}
```

**Pros:**
- Simple blocking I/O
- No IOCP thread blocking
- Dedicated thread can block without affecting output

**Cons:**
- Extra thread
- Need critical section for thread safety

---

## Recommended Fix

**Use overlapped I/O for control pipe read** (similar to commit 7b0559c but done correctly):

1. Keep separate OVERLAPPED structures:
   - `ctrl_accept_overl` for ConnectNamedPipe
   - `ctrl_read_overl` for ReadFile

2. Add `COMPLETION_KEY_CTRL_READ` key

3. State machine:
   ```
   [ACCEPTING] → ConnectNamedPipe (async)
               → IOCP signals COMPLETION_KEY_CTRL_ACCEPT
               → Issue ReadFile (async)
               → [READING]
   
   [READING] → ReadFile completes
             → IOCP signals COMPLETION_KEY_CTRL_READ
             → Process resize
             → DisconnectNamedPipe
             → [ACCEPTING]
   ```

4. Distinguish completion keys by:
   - Option A: Different comp_key values
   - Option B: Compare ovl pointer with &pty->ctrl_accept_overl vs &pty->ctrl_read_overl

---

## Files to Modify

**`conpty-proxy/conpty_proxy.c`:**
- Line 90: Add `OVERLAPPED ctrl_read_overl;`
- Line 91: Add `char ctrl_read_buf[64];`
- Line 23: Add `#define COMPLETION_KEY_CTRL_READ (0x03)`
- Line 254-278: Rewrite `on_ctrl_accept()` and add `on_ctrl_read()`
- Line 302-305: Update IOCP loop to handle COMPLETION_KEY_CTRL_READ

**Total changes:** ~30 lines modified, ~20 lines added

---

## Testing Strategy

### Reproduce the Hang

```elisp
M-x vterm

;; Rapidly resize window
(dotimes (i 100)
  (set-frame-width (selected-frame) (+ 80 (random 40)))
  (sit-for 0.05))

;; Check if Emacs hangs (no output, must kill conpty_proxy)
```

### Verify Fix

```elisp
;; After fix, same test should work smoothly
(dotimes (i 100)
  (set-frame-width (selected-frame) (+ 80 (random 40)))
  (sit-for 0.05))

;; Should not hang, terminal should resize correctly
```

### Stress Test

```bash
# In vterm, generate continuous output while resizing
while true; do echo "test $(date)"; sleep 0.1; done

# Manually resize window rapidly
# Should not hang or lose output
```

---

## Related Issues

**Previous commits that touched this code:**
- `7b0559c` - "fix infinite loop in control pipe handling"
  - Fixed infinite loop but introduced synchronous ReadFile
- `2917604` - "use synchronous read to avoid pipe busy error"
  - Attempted fix for ERROR_PIPE_BUSY
- `ae62ce7` - "move control pipe to separate thread"
  - Moved to separate thread but only for "resize" command, not main proxy

**This is the THIRD iteration** of control pipe handling, each fixing one bug but introducing another.

---

## Lessons Learned

1. **Never do synchronous I/O in an IOCP thread**
   - IOCP threads should only do async operations
   - Use `GetQueuedCompletionStatus` + overlapped I/O

2. **Separate threads for separate concerns**
   - IOCP thread: ConPTY output (must never block)
   - Main thread: stdin input (can block)
   - Control thread: Resize operations (can block OR use async)

3. **Test the unhappy path**
   - What if client disconnects early?
   - What if client is slow to write?
   - What if multiple clients connect rapidly?

4. **Keep it simple**
   - Original design with helper process worked (commit before 7c85bfa)
   - Optimization to avoid process spawning introduced complexity
   - Sometimes simple is better than fast

---

## Conclusion

The hang is caused by **synchronous ReadFile in IOCP thread** (line 259). When the control pipe client (Emacs) writes resize data and disconnects quickly, there's a race condition where ReadFile might block waiting for data that will never arrive or has already been flushed.

**Fix:** Use overlapped I/O for control pipe read, similar to how ConPTY output is handled.

**Alternative:** Move control pipe handling to a separate dedicated thread (simpler but more threads).

**Status:** Bug identified, fix designed, ready to implement.

---

**Next Step:** Implement overlapped I/O for control pipe read operation.
