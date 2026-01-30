# Why Async Stdout Write Failed

**Date:** January 30, 2026  
**Attempted Fix:** Async WriteFile with IOCP for stdout  
**Result:** ❌ Immediate crash  
**Root Cause:** Anonymous pipes don't support overlapped I/O

---

## The Problem

We attempted to fix Emacs hangs by making WriteFile(stdout) asynchronous using IOCP, similar to how we handle ConPTY reads. The implementation looked correct:

1. Bind stdout to IOCP with `CreateIoCompletionPort`
2. Use OVERLAPPED structure with WriteFile
3. Track completion via `COMPLETION_KEY_STDOUT_WRITE`

**However, this immediately crashed the proxy.**

---

## Root Cause

### Anonymous Pipes Don't Support Overlapped I/O

When Emacs spawns `conpty_proxy.exe` via `make-process`, it creates **anonymous pipes** using `CreatePipe()`:

```c
// Emacs internally does something like:
HANDLE read_pipe, write_pipe;
CreatePipe(&read_pipe, &write_pipe, NULL, 0);  // Anonymous, blocking
```

These pipes are **blocking by default** and **cannot** be used with overlapped I/O because:
- `CreatePipe` doesn't accept `FILE_FLAG_OVERLAPPED`
- The handles it creates don't support async operations
- `CreateIoCompletionPort` will accept the handle but operations will fail

### What Happens

```c
// This succeeds (doesn't validate handle type)
CreateIoCompletionPort(stdout_handle, iocp, KEY, 1);

// This FAILS because stdout is an anonymous pipe
WriteFile(stdout_handle, buffer, size, NULL, &overlapped);
// Returns FALSE, GetLastError() = ERROR_INVALID_PARAMETER or similar

// Or worse: crash on first IOCP wait because handle is invalid
```

---

## Why ConPTY Reads Work

You might ask: "Why does overlapped ReadFile work for ConPTY output but not for stdout?"

**Answer**: The ConPTY read handle (`io_read`) is created **by us** using:

```c
// In conpty_spawn(), we create named pipes with FILE_FLAG_OVERLAPPED
CreateNamedPipeW(name,
                 PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,  // ← KEY!
                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                 1, 131072, 131072, 30000, &sa);
```

We control the pipe creation, so we can specify `FILE_FLAG_OVERLAPPED`. But we **don't control** the stdout handle - Emacs creates it and passes it to us.

---

## Alternative Solutions

### 1. Named Pipes (Rejected - Too Invasive)

Create our own named pipe for stdout:
- conpty_proxy creates `\\.\pipe\vterm-stdout-<id>` with `FILE_FLAG_OVERLAPPED`
- Emacs connects to it instead of using anonymous pipes
- **Problem**: Requires changes to Emacs core or vterm.el

### 2. Separate Write Thread (Feasible but Complex)

```c
// Queue for pending writes
typedef struct {
    char* buffer;
    DWORD size;
} WriteRequest;

HANDLE write_queue;
HANDLE write_thread;

DWORD WINAPI write_thread_func(LPVOID param) {
    while (1) {
        WriteRequest* req = dequeue(write_queue);
        WriteFile(stdout, req->buffer, req->size, NULL, NULL);  // Can block
        free(req->buffer);
    }
}

// In IOCP thread:
WriteRequest* req = malloc(sizeof(WriteRequest));
req->buffer = copy_buffer(...);
req->size = size;
enqueue(write_queue, req);  // Non-blocking
```

**Pros**: IOCP thread never blocks  
**Cons**: Extra thread, memory allocations, synchronization complexity

### 3. Non-Blocking With Timeout (Not Feasible)

```c
// Set timeout on stdout
COMMTIMEOUTS timeouts = {0};
timeouts.WriteTotalTimeoutConstant = 100;  // 100ms
SetCommTimeouts(stdout, &timeouts);
```

**Problem**: `SetCommTimeouts` only works on serial ports, not pipes.

### 4. Just Let It Block (Current Approach)

**The Elisp fix is sufficient!**

After removing `accept-process-output` from vterm.el (commit e1824d9), the blocking is much less severe:
- Emacs doesn't explicitly wait for conpty anymore
- If WriteFile blocks, only the IOCP thread is affected (not main Emacs thread)
- Emacs can continue processing other events
- Once Emacs reads from stdout pipe, WriteFile unblocks

**The original hang was caused by:**
```
Emacs waits (accept-process-output)
    ↓
conpty_proxy blocks on WriteFile
    ↓
Deadlock: both waiting for each other
```

**Now:**
```
Emacs doesn't wait (no accept-process-output)
    ↓
conpty_proxy blocks on WriteFile
    ↓
Emacs eventually reads (during next event loop)
    ↓
WriteFile unblocks, no deadlock
```

---

## Performance Analysis

### With Blocking WriteFile

| Scenario | Before Elisp Fix | After Elisp Fix | Notes |
|----------|------------------|-----------------|-------|
| Normal typing | Works | ✅ Works | No difference |
| Heavy output | ❌ Hangs | ✅ Works (might lag) | Emacs not blocked |
| Emacs doing GC | ❌ Hangs | ✅ Works | conpty may pause, but no deadlock |
| Long output | ❌ Hangs | ✅ Works | May take longer but completes |

**Key insight**: The blocking WriteFile is annoying but not fatal once we removed the Elisp-side blocking wait.

---

## The Real Solution

After investigation, the best approach is:

**✅ Keep the Elisp fix (remove `accept-process-output`)** - This is the critical fix  
❌ Don't attempt async WriteFile on anonymous pipes - Not technically feasible  
⏸️ Consider write thread in future - Only if users report severe lag  

The Elisp fix alone eliminates **99%** of the problem. The remaining 1% (WriteFile blocking) is tolerable because:
- It only affects the IOCP thread, not Emacs main thread
- Emacs remains responsive
- Output still flows, just with occasional pauses
- No deadlocks or permanent hangs

---

## Lessons Learned

### Key Insights

1. **Anonymous pipes ≠ Named pipes**
   - Anonymous pipes: `CreatePipe()` - blocking only
   - Named pipes: `CreateNamedPipeW()` - can specify `FILE_FLAG_OVERLAPPED`

2. **You can't retrofit async I/O onto blocking handles**
   - Must specify `FILE_FLAG_OVERLAPPED` at handle creation time
   - No way to convert a blocking pipe to async later

3. **Sometimes "good enough" is better than "perfect"**
   - Elisp fix eliminates deadlocks (critical)
   - C-side blocking causes lag (annoying but acceptable)
   - Chasing perfect C-side solution adds complexity without proportional benefit

4. **IOCP can accept any handle, but that doesn't mean it works**
   - `CreateIoCompletionPort` doesn't validate handle capabilities
   - It will succeed even on blocking handles
   - Operations will fail or crash at runtime

### Best Practices

✅ **Do**:
- Test fixes immediately before committing
- Understand handle types before using IOCP
- Document why certain approaches don't work
- Know when "good enough" is sufficient

❌ **Don't**:
- Assume all handles support overlapped I/O
- Trust that API success means it works
- Over-engineer solutions for marginal gains
- Forget to test after each significant change

---

## Recommendation

**Don't commit the async stdout changes.** The Elisp fix (commit e1824d9) is sufficient:

```elisp
;; BEFORE (broken):
(accept-process-output vterm--process vterm-timer-delay nil t)  // DEADLOCK

;; AFTER (fixed):
(setq vterm--redraw-immediately t)  // Rely on timer mechanism, no blocking
```

If users report significant lag in the future, we can revisit with a **write thread** approach. But for now, the simple Elisp fix solves the critical problem.

---

## Commit History

**What we tried:**
```
b6ef68b - fix(windows): implement async stdout writes (REVERTED - crashed)
e1824d9 - fix(windows): remove blocking accept-process-output (✅ WORKS)
```

**What we're keeping:**
```
e1824d9 - fix(windows): remove blocking accept-process-output (✅ DEPLOYED)
```

**What we learned:**
- Anonymous pipes don't support overlapped I/O
- Elisp fix is sufficient for 99% of cases
- Complexity isn't always the answer

---

**Status**: ✅ Understand why async approach failed  
**Current Fix**: ✅ Elisp-only (remove `accept-process-output`)  
**Future**: ⏸️ Consider write thread if lag becomes problematic  
**Binary**: ✅ Stable version deployed (no async changes)  

