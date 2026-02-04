# AGENTS.md - emacs-libvterm (Windows Fork)

This document provides guidelines for AI coding agents working on this codebase.

## Project Overview

emacs-libvterm is a terminal emulator for Emacs using libvterm as a dynamic module.
This fork adds Windows support via ConPTY.

**Architecture**: `vterm.el` -> `vterm-module.dll` -> ConPTY -> Shell

## Build Commands

### Prerequisites (MSYS2 UCRT64)
```bash
pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc make
```

### Quick Build (incremental)
```bash
export PATH=/q/repos/emacs-build/msys64/ucrt64/bin:$PATH
cd /c/Users/$USER/.cache/quelpa/build/vterm
cmake --build build --target vterm-module -j8
```

### Full Build (clean)
```bash
rm -rf build && mkdir build && cd build
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DUSE_SYSTEM_LIBVTERM=OFF
cd .. && cmake --build build -j8
```

### Build Single File (syntax check)
```bash
gcc -c vterm-module.c -I. -Ibuild/libvterm-prefix/src/libvterm/include \
    -Ibuild/libtermiwin-prefix/src/libtermiwin/include -Wall -Wextra
```

### Deploy
```bash
cp vterm-module.dll /c/Users/$USER/.cache/vterm/
```

**If DLL is locked by Emacs**: Rename old file first, then copy new one:
```bash
DEPLOY_DIR=/c/Users/$USER/.cache/vterm
mv $DEPLOY_DIR/vterm-module.dll $DEPLOY_DIR/vterm-module.dll.old 2>/dev/null
cp vterm-module.dll $DEPLOY_DIR/
```
Windows allows renaming in-use files. The old DLL stays loaded until Emacs restarts.

## Testing

Manual testing (no automated test suite):
1. After deploy, restart Emacs to load new DLL
2. `M-x vterm` to launch terminal
3. Test: typing, scrolling, resize, shell exit

## Code Style

### C Code (vterm-module.c, arena.c)

**Formatting**:
- 2-space indentation
- Opening brace on same line: `if (cond) {`
- Max ~100 chars per line
- Single blank line between functions

**Naming**:
- Functions: `snake_case` for static, `Fvterm_*` for Emacs-exposed
- Types: `PascalCase` (e.g., `Term`, `ConPTYState`, `ScrollbackLine`)
- Macros: `UPPER_SNAKE_CASE` (e.g., `VTERM_INLINE`, `SB_MAX`)
- Struct members: `snake_case`

**Includes** (order):
```c
#include "vterm-module.h"  // Own header first
#include "elisp.h"         // Project headers
#include <assert.h>        // Standard library
#include <windows.h>       // Platform-specific (guarded)
```

**Error Handling**:
- Return `Qnil` on failure from Emacs functions
- Check all allocation results (malloc, VirtualAlloc)
- Use early returns for error cases
- Clean up resources on failure paths

**Platform Guards**:
```c
#ifdef _WIN32
// Windows-specific code
#endif
```

**Performance Hints**:
```c
VTERM_INLINE static inline ...     // Hot path functions
VTERM_LIKELY(x) / VTERM_UNLIKELY(x) // Branch prediction
VTERM_HOT                          // Hot functions
```

### Emacs Lisp (vterm.el)

**Formatting**:
- 2-space indentation
- `lexical-binding: t` required
- Docstrings for all public functions

**Naming**:
- Public: `vterm-*` (e.g., `vterm-mode`, `vterm-send-key`)
- Private: `vterm--*` (e.g., `vterm--process`, `vterm--invalidate`)
- Buffer-local vars: `vterm--*` with `defvar-local`

**Conventions**:
```elisp
(defvar-local vterm--process nil "The shell process.")
(defun vterm--internal-fn ()
  "Private helper function."
  ...)
```

## Key Files

| File | Purpose |
|------|---------|
| `vterm.el` | Emacs Lisp frontend, user interaction |
| `vterm-module.c` | C module, libvterm bridge, rendering, in-process ConPTY |
| `vterm-module.h` | Type definitions, function declarations |
| `elisp.c` | Emacs symbol/function caching |
| `conpty-proxy/arena.c` | Arena allocator for O(1) memory (used by vterm-module.c) |

## Architecture Notes

### Windows ConPTY Flow (In-Process)
```
User Input -> vterm.el -> vterm-module.dll -> ConPTY pipe -> Shell
Shell Output <- ConPTY pipe <- background thread <- vterm-module.dll <- display
```

### Memory Management (Windows)
- `persistent_arena`: Long-lived data (LineInfo, directories)
- `temp_arena`: Per-frame render buffers (reset after redraw)
- Double buffering: 128KB x 2 for async I/O

### Critical Paths (Performance-Sensitive)
- `refresh_lines()` - Main rendering loop
- `term_redraw()` - Full redraw entry point
- `conpty_output_thread()` - Async I/O reader

## Common Tasks

### Adding a New Emacs Function
1. Declare in `vterm-module.h`: `emacs_value Fvterm_new_fn(...);`
2. Implement in `vterm-module.c`
3. Register in `emacs_module_init()`: `bind_function(env, "vterm--new-fn", Fvterm_new_fn)`
4. Expose in `vterm.el` if needed

### Modifying ConPTY Behavior
Edit ConPTY section in `vterm-module.c` (search for `#ifdef _WIN32` and ConPTY functions).

## Don'ts

- Don't buffer stdin (breaks keyboard responsiveness)
- Don't block Emacs main thread (use async/threadpool)
- Don't force `(redisplay)` for non-interactive operations
- Don't use `accept-process-output` with blocking waits

## Checked-in Files

- `emacs-module.h`: Emacs module API header (from Emacs 31). This file IS checked in
  to ensure consistent builds. Update from Emacs source when targeting new Emacs versions.

## Line Endings

Keep original line endings. Convert after edits:
```bash
unix2dos vterm.el  # If file uses CRLF
dos2unix file.c    # If file uses LF
```

## References

- `.github/prompts/ARCHITECTURE.md` - Detailed architecture docs
- `.github/prompts/BUILD.md` - Complete build instructions
- libvterm docs: https://www.leonerd.org.uk/code/libvterm/
