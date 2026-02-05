/*
 * conpty.c - In-process ConPTY implementation for Windows
 *
 * This module handles Windows ConPTY directly within the Emacs dynamic module,
 * eliminating the need for an external proxy process.
 *
 * Key design decisions:
 * 1. Uses simple blocking reads in background thread (not IOCP async)
 *    - Simpler, works with regular pipes
 *    - IOCP handle kept for future optimization if needed
 *
 * 2. Double buffering for output
 *    - While one buffer is being copied to pending, next read can start
 *    - Improves throughput for bulk output
 *
 * 3. Arena allocation for ConPTYState
 *    - Allocated from term's persistent_arena
 *    - Automatic cleanup when term is freed
 *
 * 4. Thread-safe notification via open_channel
 *    - Background thread writes to notify_fd to wake Emacs
 *    - Emacs reads pending output on next event loop iteration
 */

#ifdef _WIN32

#include "conpty.h"
#include "arena.h"
#include "elisp.h"
#include "vterm-module.h"

#include <io.h> /* for _write on Windows */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Debug logging
 * ============================================================================
 */

#define CONPTY_DEBUG 1
#ifdef CONPTY_DEBUG
static FILE *g_conpty_debug_file = NULL;

static void conpty_debug_init(void) {
  if (!g_conpty_debug_file) {
    g_conpty_debug_file =
        fopen("C:\\Users\\kingu\\.cache\\vterm\\conpty_debug.log", "a");
    if (g_conpty_debug_file) {
      fprintf(g_conpty_debug_file, "\n=== New session started ===\n");
      fflush(g_conpty_debug_file);
    }
  }
}

static void conpty_debug(const char *fmt, ...) {
  conpty_debug_init();
  if (g_conpty_debug_file) {
    va_list args;
    va_start(args, fmt);
    vfprintf(g_conpty_debug_file, fmt, args);
    va_end(args);
    fflush(g_conpty_debug_file);
  }
}
#define CONPTY_LOG(...) conpty_debug(__VA_ARGS__)
#else
#define CONPTY_LOG(...) ((void)0)
#endif

/* ============================================================================
 * ConPTY API initialization
 * ============================================================================
 */

/* PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE may not be defined in older SDKs */
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE                                    \
  ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
#endif

/* Global ConPTY API function pointers */
CreatePseudoConsole_t g_CreatePseudoConsole = NULL;
ResizePseudoConsole_t g_ResizePseudoConsole = NULL;
ClosePseudoConsole_t g_ClosePseudoConsole = NULL;
static int g_conpty_api_initialized = 0;

int conpty_api_init(void) {
  if (g_conpty_api_initialized == -1)
    return -1;
  if (g_conpty_api_initialized == 1)
    return 0;

  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  if (!kernel32) {
    g_conpty_api_initialized = -1;
    return -1;
  }

  g_CreatePseudoConsole =
      (CreatePseudoConsole_t)GetProcAddress(kernel32, "CreatePseudoConsole");
  g_ResizePseudoConsole =
      (ResizePseudoConsole_t)GetProcAddress(kernel32, "ResizePseudoConsole");
  g_ClosePseudoConsole =
      (ClosePseudoConsole_t)GetProcAddress(kernel32, "ClosePseudoConsole");

  if (!g_CreatePseudoConsole || !g_ResizePseudoConsole ||
      !g_ClosePseudoConsole) {
    g_conpty_api_initialized = -1;
    return -1;
  }

  g_conpty_api_initialized = 1;
  return 0;
}

/* ============================================================================
 * Background output thread
 * ============================================================================
 */

/* IOCP completion key - not currently used, kept for future optimization */
#define CONPTY_COMPLETION_KEY_READ 1

/* Background thread that reads ConPTY output and notifies Emacs
 * Uses simple blocking reads - simpler and works with regular pipes
 */
static DWORD WINAPI conpty_output_thread(LPVOID param) {
  Term *term = (Term *)param;
  ConPTYState *state = term->conpty;
  DWORD bytes_read;
  int current_buf = 0;

  CONPTY_LOG("conpty_output_thread: started\n");

  while (InterlockedCompareExchange(&state->running, 1, 1) == 1) {
    /* Simple blocking read from ConPTY output pipe */
    BOOL ok = ReadFile(state->pty_output, state->output_buf[current_buf],
                       sizeof(state->output_buf[0]), &bytes_read, NULL);

    if (!ok || bytes_read == 0) {
      DWORD err = GetLastError();
      CONPTY_LOG("conpty_output_thread: ReadFile failed/EOF, error=%lu\n", err);
      /* Pipe broken or EOF - shell likely exited */
      break;
    }

    CONPTY_LOG("conpty_output_thread: read %lu bytes\n", bytes_read);

    /* Copy to pending buffer for Emacs */
    EnterCriticalSection(&state->pending_lock);
    size_t space = sizeof(state->pending_output) - state->pending_output_len;
    size_t to_copy = (bytes_read < space) ? bytes_read : space;
    if (to_copy > 0) {
      memcpy(state->pending_output + state->pending_output_len,
             state->output_buf[current_buf], to_copy);
      state->pending_output_len += to_copy;
    }
    LeaveCriticalSection(&state->pending_lock);

    /* Toggle double buffer for next read */
    current_buf = 1 - current_buf;

    /* Notify Emacs via open_channel FD (thread-safe!) */
    if (state->notify_fd >= 0) {
      _write(state->notify_fd, "1", 1);
    }
  }

  CONPTY_LOG("conpty_output_thread: exiting\n");
  return 0;
}

/* ============================================================================
 * Cleanup
 * ============================================================================
 */

void conpty_cleanup(Term *term) {
  if (!term->conpty)
    return;

  ConPTYState *state = term->conpty;

  /* Signal thread to stop */
  InterlockedExchange(&state->running, 0);

  /* Cancel pending I/O */
  if (state->pty_output && state->pty_output != INVALID_HANDLE_VALUE) {
    CancelIo(state->pty_output);
  }

  /* Wait for thread with timeout */
  if (state->iocp_thread && state->iocp_thread != INVALID_HANDLE_VALUE) {
    WaitForSingleObject(state->iocp_thread, 2000);
    CloseHandle(state->iocp_thread);
    state->iocp_thread = NULL;
  }

  /* Close IOCP */
  if (state->iocp && state->iocp != INVALID_HANDLE_VALUE) {
    CloseHandle(state->iocp);
    state->iocp = NULL;
  }

  /* Close shell process */
  if (state->shell_process && state->shell_process != INVALID_HANDLE_VALUE) {
    TerminateProcess(state->shell_process, 0);
    CloseHandle(state->shell_process);
    state->shell_process = NULL;
  }

  /* Close PTY handles */
  if (state->pty_input && state->pty_input != INVALID_HANDLE_VALUE) {
    CloseHandle(state->pty_input);
    state->pty_input = NULL;
  }
  if (state->pty_output && state->pty_output != INVALID_HANDLE_VALUE) {
    CloseHandle(state->pty_output);
    state->pty_output = NULL;
  }

  /* Close pseudo console */
  if (state->hpc) {
    g_ClosePseudoConsole(state->hpc);
    state->hpc = NULL;
  }

  /* Delete critical section */
  DeleteCriticalSection(&state->pending_lock);

  /* Note: state itself is in arena, will be freed with term */
  term->conpty = NULL;
}

/* ============================================================================
 * Emacs-exposed functions
 * ============================================================================
 */

emacs_value Fvterm_conpty_init(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data) {
  (void)data;

  CONPTY_LOG("Fvterm_conpty_init: nargs=%td\n", nargs);

  if (nargs < 5) {
    CONPTY_LOG("Fvterm_conpty_init: nargs < 5, returning nil\n");
    return Qnil;
  }

  Term *term = env->get_user_ptr(env, args[0]);
  if (!term) {
    CONPTY_LOG("Fvterm_conpty_init: term is NULL\n");
    return Qnil;
  }
  CONPTY_LOG("Fvterm_conpty_init: term=%p\n", (void *)term);

  /* Initialize ConPTY API if needed */
  if (conpty_api_init() != 0) {
    CONPTY_LOG("Fvterm_conpty_init: conpty_api_init failed\n");
    return Qnil;
  }
  CONPTY_LOG("Fvterm_conpty_init: conpty_api_init OK\n");

  /* Extract shell command */
  ptrdiff_t shell_len = 0;
  env->copy_string_contents(env, args[2], NULL, &shell_len);
  char *shell_cmd = (char *)malloc(shell_len);
  if (!shell_cmd) {
    CONPTY_LOG("Fvterm_conpty_init: malloc for shell_cmd failed\n");
    return Qnil;
  }
  env->copy_string_contents(env, args[2], shell_cmd, &shell_len);
  CONPTY_LOG("Fvterm_conpty_init: shell_cmd='%s'\n", shell_cmd);

  int width = env->extract_integer(env, args[3]);
  int height = env->extract_integer(env, args[4]);
  CONPTY_LOG("Fvterm_conpty_init: width=%d height=%d\n", width, height);

  if (width <= 0 || height <= 0) {
    CONPTY_LOG("Fvterm_conpty_init: invalid dimensions\n");
    free(shell_cmd);
    return Qnil;
  }

  /* Allocate ConPTY state (use arena for automatic cleanup) */
  CONPTY_LOG("Fvterm_conpty_init: persistent_arena=%p\n",
             (void *)term->persistent_arena);
  ConPTYState *state =
      (ConPTYState *)arena_alloc(term->persistent_arena, sizeof(ConPTYState));
  if (!state) {
    CONPTY_LOG("Fvterm_conpty_init: arena_alloc failed\n");
    free(shell_cmd);
    return Qnil;
  }
  memset(state, 0, sizeof(ConPTYState));
  term->conpty = state;
  CONPTY_LOG("Fvterm_conpty_init: ConPTYState allocated at %p\n",
             (void *)state);

  /* Initialize critical section */
  InitializeCriticalSection(&state->pending_lock);
  state->running = 1;
  state->notify_fd = -1;

  /* Get notify FD via open_channel (Emacs 28+) */
  CONPTY_LOG("Fvterm_conpty_init: calling open_channel...\n");
  state->notify_fd = env->open_channel(env, args[1]);
  CONPTY_LOG("Fvterm_conpty_init: notify_fd=%d\n", state->notify_fd);
  if (state->notify_fd < 0) {
    CONPTY_LOG("Fvterm_conpty_init: open_channel failed\n");
    DeleteCriticalSection(&state->pending_lock);
    term->conpty = NULL;
    free(shell_cmd);
    return Qnil;
  }

  /* Create pipes for ConPTY */
  CONPTY_LOG("Fvterm_conpty_init: creating pipes...\n");
  HANDLE in_read = INVALID_HANDLE_VALUE, in_write = INVALID_HANDLE_VALUE;
  HANDLE out_read = INVALID_HANDLE_VALUE, out_write = INVALID_HANDLE_VALUE;
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

  if (!CreatePipe(&in_read, &in_write, &sa, 0) ||
      !CreatePipe(&out_read, &out_write, &sa, 0)) {
    CONPTY_LOG("Fvterm_conpty_init: CreatePipe failed, error=%lu\n",
               GetLastError());
    if (in_read != INVALID_HANDLE_VALUE)
      CloseHandle(in_read);
    if (in_write != INVALID_HANDLE_VALUE)
      CloseHandle(in_write);
    if (out_read != INVALID_HANDLE_VALUE)
      CloseHandle(out_read);
    if (out_write != INVALID_HANDLE_VALUE)
      CloseHandle(out_write);
    DeleteCriticalSection(&state->pending_lock);
    term->conpty = NULL;
    free(shell_cmd);
    return Qnil;
  }
  CONPTY_LOG("Fvterm_conpty_init: pipes created OK\n");

  /* Create pseudo console */
  CONPTY_LOG("Fvterm_conpty_init: creating pseudo console...\n");
  COORD size = {(SHORT)width, (SHORT)height};
  HRESULT hr = g_CreatePseudoConsole(size, in_read, out_write, 0, &state->hpc);
  CONPTY_LOG("Fvterm_conpty_init: CreatePseudoConsole hr=0x%lx\n",
             (unsigned long)hr);

  /* Close handles now owned by ConPTY */
  CloseHandle(in_read);
  CloseHandle(out_write);

  if (FAILED(hr)) {
    CONPTY_LOG("Fvterm_conpty_init: CreatePseudoConsole FAILED\n");
    CloseHandle(in_write);
    CloseHandle(out_read);
    DeleteCriticalSection(&state->pending_lock);
    term->conpty = NULL;
    free(shell_cmd);
    return Qnil;
  }
  CONPTY_LOG("Fvterm_conpty_init: pseudo console created OK\n");

  state->pty_input = in_write;
  state->pty_output = out_read;

  /* Spawn shell process attached to ConPTY */
  CONPTY_LOG("Fvterm_conpty_init: spawning shell process...\n");
  STARTUPINFOEXW si;
  memset(&si, 0, sizeof(si));
  si.StartupInfo.cb = sizeof(si);

  size_t attr_size = 0;
  InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
  CONPTY_LOG("Fvterm_conpty_init: attr_size=%zu\n", attr_size);
  si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
  if (!si.lpAttributeList) {
    CONPTY_LOG("Fvterm_conpty_init: malloc for attr list failed\n");
    conpty_cleanup(term);
    free(shell_cmd);
    return Qnil;
  }

  if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0,
                                         &attr_size) ||
      !UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                 state->hpc, sizeof(state->hpc), NULL, NULL)) {
    CONPTY_LOG("Fvterm_conpty_init: InitializeProcThreadAttributeList or "
               "UpdateProcThreadAttribute failed, error=%lu\n",
               GetLastError());
    free(si.lpAttributeList);
    conpty_cleanup(term);
    free(shell_cmd);
    return Qnil;
  }
  CONPTY_LOG("Fvterm_conpty_init: attribute list initialized OK\n");

  /* Convert shell command to wide string */
  int wlen = MultiByteToWideChar(CP_UTF8, 0, shell_cmd, -1, NULL, 0);
  wchar_t *wshell = (wchar_t *)malloc(wlen * sizeof(wchar_t));
  if (!wshell) {
    CONPTY_LOG("Fvterm_conpty_init: malloc for wshell failed\n");
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    conpty_cleanup(term);
    free(shell_cmd);
    return Qnil;
  }
  MultiByteToWideChar(CP_UTF8, 0, shell_cmd, -1, wshell, wlen);
  free(shell_cmd);
  CONPTY_LOG("Fvterm_conpty_init: shell command converted to wide string\n");

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  CONPTY_LOG("Fvterm_conpty_init: calling CreateProcessW...\n");
  BOOL created = CreateProcessW(NULL, wshell, NULL, NULL, FALSE,
                                EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                                &si.StartupInfo, &pi);
  CONPTY_LOG("Fvterm_conpty_init: CreateProcessW returned %d, error=%lu\n",
             created, GetLastError());

  free(wshell);
  DeleteProcThreadAttributeList(si.lpAttributeList);
  free(si.lpAttributeList);

  if (!created) {
    CONPTY_LOG("Fvterm_conpty_init: CreateProcessW FAILED\n");
    conpty_cleanup(term);
    return Qnil;
  }
  CONPTY_LOG("Fvterm_conpty_init: shell process created, pid=%lu\n",
             pi.dwProcessId);

  state->shell_process = pi.hProcess;
  CloseHandle(pi.hThread);

  /* No IOCP needed - using simple blocking reads in background thread */
  state->iocp = NULL;

  /* Start background output thread */
  CONPTY_LOG("Fvterm_conpty_init: starting output thread...\n");
  state->iocp_thread =
      CreateThread(NULL, 0, conpty_output_thread, term, 0, NULL);
  if (!state->iocp_thread) {
    CONPTY_LOG("Fvterm_conpty_init: CreateThread failed, error=%lu\n",
               GetLastError());
    conpty_cleanup(term);
    return Qnil;
  }
  CONPTY_LOG("Fvterm_conpty_init: output thread started, SUCCESS!\n");

  return Qt;
}

emacs_value Fvterm_conpty_read_pending(emacs_env *env, ptrdiff_t nargs,
                                       emacs_value args[], void *data) {
  (void)data;
  (void)nargs;

  Term *term = env->get_user_ptr(env, args[0]);
  if (!term || !term->conpty)
    return Qnil;

  ConPTYState *state = term->conpty;
  emacs_value result = Qnil;

  EnterCriticalSection(&state->pending_lock);
  if (state->pending_output_len > 0) {
    result = env->make_string(env, state->pending_output,
                              (ptrdiff_t)state->pending_output_len);
    state->pending_output_len = 0;
  }
  LeaveCriticalSection(&state->pending_lock);

  return result;
}

emacs_value Fvterm_conpty_write(emacs_env *env, ptrdiff_t nargs,
                                emacs_value args[], void *data) {
  (void)data;

  if (nargs < 2) {
    CONPTY_LOG("Fvterm_conpty_write: nargs < 2\n");
    return Qnil;
  }

  Term *term = env->get_user_ptr(env, args[0]);
  if (!term || !term->conpty) {
    CONPTY_LOG("Fvterm_conpty_write: term=%p conpty=%p\n", (void *)term,
               term ? (void *)term->conpty : NULL);
    return Qnil;
  }

  ptrdiff_t len = 0;
  env->copy_string_contents(env, args[1], NULL, &len);
  if (len <= 1) {
    CONPTY_LOG("Fvterm_conpty_write: empty string\n");
    return env->make_integer(env, 0); /* Empty string (just null terminator) */
  }

  char *bytes = (char *)malloc(len);
  if (!bytes) {
    CONPTY_LOG("Fvterm_conpty_write: malloc failed\n");
    return Qnil;
  }
  env->copy_string_contents(env, args[1], bytes, &len);

  CONPTY_LOG("Fvterm_conpty_write: writing %td bytes: ", len - 1);
  for (ptrdiff_t i = 0; i < len - 1 && i < 20; i++) {
    CONPTY_LOG("%02x ", (unsigned char)bytes[i]);
  }
  CONPTY_LOG("\n");

  DWORD written = 0;
  BOOL ok = WriteFile(term->conpty->pty_input, bytes, (DWORD)(len - 1),
                      &written, NULL);
  CONPTY_LOG("Fvterm_conpty_write: WriteFile ok=%d written=%lu error=%lu\n", ok,
             written, GetLastError());
  free(bytes);

  return env->make_integer(env, written);
}

emacs_value Fvterm_conpty_resize(emacs_env *env, ptrdiff_t nargs,
                                 emacs_value args[], void *data) {
  (void)data;

  CONPTY_LOG("Fvterm_conpty_resize: called with nargs=%td\n", nargs);

  if (nargs < 3) {
    CONPTY_LOG("Fvterm_conpty_resize: ERROR nargs < 3\n");
    return Qnil;
  }

  Term *term = env->get_user_ptr(env, args[0]);
  if (!term) {
    CONPTY_LOG("Fvterm_conpty_resize: ERROR term is NULL\n");
    return Qnil;
  }
  if (!term->conpty) {
    CONPTY_LOG("Fvterm_conpty_resize: ERROR term->conpty is NULL\n");
    return Qnil;
  }
  if (!term->conpty->hpc) {
    CONPTY_LOG("Fvterm_conpty_resize: ERROR term->conpty->hpc is NULL\n");
    return Qnil;
  }

  int width = env->extract_integer(env, args[1]);
  int height = env->extract_integer(env, args[2]);

  CONPTY_LOG("Fvterm_conpty_resize: width=%d height=%d\n", width, height);

  if (width <= 0 || height <= 0) {
    CONPTY_LOG("Fvterm_conpty_resize: ERROR invalid dimensions\n");
    return Qnil;
  }

  COORD size = {(SHORT)width, (SHORT)height};
  HRESULT hr = g_ResizePseudoConsole(term->conpty->hpc, size);

  CONPTY_LOG("Fvterm_conpty_resize: ResizePseudoConsole hr=0x%lx %s\n", hr,
             SUCCEEDED(hr) ? "SUCCESS" : "FAILED");

  return SUCCEEDED(hr) ? Qt : Qnil;
}

emacs_value Fvterm_conpty_is_alive(emacs_env *env, ptrdiff_t nargs,
                                   emacs_value args[], void *data) {
  (void)data;
  (void)nargs;

  Term *term = env->get_user_ptr(env, args[0]);
  if (!term || !term->conpty || !term->conpty->shell_process)
    return Qnil;

  DWORD exit_code;
  if (GetExitCodeProcess(term->conpty->shell_process, &exit_code)) {
    return (exit_code == STILL_ACTIVE) ? Qt : Qnil;
  }

  return Qnil;
}

emacs_value Fvterm_conpty_kill(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data) {
  (void)data;
  (void)nargs;

  Term *term = env->get_user_ptr(env, args[0]);
  if (!term)
    return Qnil;

  conpty_cleanup(term);
  return Qt;
}

#endif /* _WIN32 */
