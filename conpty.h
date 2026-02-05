/*
 * conpty.h - In-process ConPTY for Windows
 *
 * This module handles Windows ConPTY directly within the Emacs dynamic module,
 * eliminating the need for an external proxy process.
 *
 * Architecture:
 *   User Input -> vterm.el -> vterm-module.dll -> ConPTY pipe -> Shell
 *   Shell Output <- ConPTY pipe <- background thread <- vterm-module.dll
 *                <- write(notify_fd) <- Emacs pipe filter <- libvterm
 */

#ifndef CONPTY_H
#define CONPTY_H

#ifdef _WIN32

#include "emacs-module.h"
#include <windows.h>

/* Forward declaration for Term struct (defined in vterm-module.h) */
struct Term;

/* ConPTY API function pointer types (loaded dynamically from kernel32.dll) */
typedef HRESULT(WINAPI *CreatePseudoConsole_t)(COORD, HANDLE, HANDLE, DWORD,
                                               HPCON *);
typedef HRESULT(WINAPI *ResizePseudoConsole_t)(HPCON, COORD);
typedef void(WINAPI *ClosePseudoConsole_t)(HPCON);

/* In-process ConPTY state
 *
 * Lifecycle:
 * 1. Allocated via arena_alloc in Fvterm_conpty_init
 * 2. Background thread reads from pty_output, writes to pending_output
 * 3. Thread notifies Emacs via notify_fd (from open_channel)
 * 4. Emacs calls Fvterm_conpty_read_pending to get output
 * 5. Cleanup via conpty_cleanup when term is finalized
 */
typedef struct ConPTYState {
  HPCON hpc;            /* Pseudo console handle */
  HANDLE pty_input;     /* Write to shell stdin */
  HANDLE pty_output;    /* Read from shell stdout (async) */
  HANDLE shell_process; /* Shell process handle */

  HANDLE iocp;        /* I/O completion port (not currently used) */
  HANDLE iocp_thread; /* Background reader thread */
  int notify_fd;      /* FD from open_channel (write to wake Emacs) */

  char output_buf[2][131072]; /* 128KB double buffer for async reads */
  int output_buf_active;      /* Active buffer index (0 or 1) */

  char pending_output[262144]; /* 256KB pending for Emacs to read */
  size_t pending_output_len;
  CRITICAL_SECTION pending_lock; /* Protect pending buffer access */

  volatile LONG running;      /* Thread control flag (1 = running, 0 = stop) */
  OVERLAPPED read_overlapped; /* For async ReadFile */
} ConPTYState;

/* Initialize ConPTY API (load from kernel32.dll)
 * Returns 0 on success, -1 on failure
 * Thread-safe, idempotent
 */
int conpty_api_init(void);

/* Cleanup ConPTY resources for a term
 * Safe to call multiple times
 */
void conpty_cleanup(struct Term *term);

/* Emacs-exposed functions */

/* Initialize in-process ConPTY
 * Args: term, notify_pipe, shell_cmd, width, height
 * Returns: t on success, nil on failure
 */
emacs_value Fvterm_conpty_init(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data);

/* Read pending output from ConPTY
 * Args: term
 * Returns: string of pending output, or nil if none
 */
emacs_value Fvterm_conpty_read_pending(emacs_env *env, ptrdiff_t nargs,
                                       emacs_value args[], void *data);

/* Write input to ConPTY
 * Args: term, string
 * Returns: number of bytes written
 */
emacs_value Fvterm_conpty_write(emacs_env *env, ptrdiff_t nargs,
                                emacs_value args[], void *data);

/* Resize ConPTY
 * Args: term, width, height
 * Returns: t on success, nil on failure
 */
emacs_value Fvterm_conpty_resize(emacs_env *env, ptrdiff_t nargs,
                                 emacs_value args[], void *data);

/* Check if ConPTY shell process is still alive
 * Args: term
 * Returns: t if alive, nil if dead
 */
emacs_value Fvterm_conpty_is_alive(emacs_env *env, ptrdiff_t nargs,
                                   emacs_value args[], void *data);

/* Kill ConPTY and cleanup resources
 * Args: term
 * Returns: t
 */
emacs_value Fvterm_conpty_kill(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data);

/* Global ConPTY API function pointers (initialized by conpty_api_init) */
extern CreatePseudoConsole_t g_CreatePseudoConsole;
extern ResizePseudoConsole_t g_ResizePseudoConsole;
extern ClosePseudoConsole_t g_ClosePseudoConsole;

#endif /* _WIN32 */

#endif /* CONPTY_H */
