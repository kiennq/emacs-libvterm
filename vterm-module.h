#ifndef VTERM_MODULE_H
#define VTERM_MODULE_H

#include "emacs-module.h"
#include <inttypes.h>
#include <stdbool.h>
#include <vterm.h>

#ifdef _WIN32
#include "conpty-proxy/arena.h"
#include <windows.h>

/* ConPTY API function pointers (loaded dynamically) */
typedef HRESULT(WINAPI *CreatePseudoConsole_t)(COORD, HANDLE, HANDLE, DWORD,
                                               HPCON *);
typedef HRESULT(WINAPI *ResizePseudoConsole_t)(HPCON, COORD);
typedef void(WINAPI *ClosePseudoConsole_t)(HPCON);

/* In-process ConPTY state */
typedef struct ConPTYState {
  HPCON hpc;            /* Pseudo console handle */
  HANDLE pty_input;     /* Write to shell stdin */
  HANDLE pty_output;    /* Read from shell stdout (async) */
  HANDLE shell_process; /* Shell process handle */

  HANDLE iocp;        /* I/O completion port */
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

#endif

// https://gcc.gnu.org/wiki/Visibility
#if defined _WIN32 || defined __CYGWIN__
#ifdef __GNUC__
#define VTERM_EXPORT __attribute__((dllexport))
#else
#define VTERM_EXPORT __declspec(dllexport)
#endif
#else
#if __GNUC__ >= 4
#define VTERM_EXPORT __attribute__((visibility("default")))
#else
#define VTERM_EXPORT
#endif
#endif

/* Inline hint for hot functions - improves performance on critical paths */
#if defined(__GNUC__) || defined(__clang__)
#define VTERM_INLINE static inline __attribute__((always_inline))
#define VTERM_HOT __attribute__((hot))
#define VTERM_LIKELY(x) __builtin_expect(!!(x), 1)
#define VTERM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define VTERM_INLINE static inline
#define VTERM_HOT
#define VTERM_LIKELY(x) (x)
#define VTERM_UNLIKELY(x) (x)
#endif

VTERM_EXPORT int plugin_is_GPL_compatible;

#ifndef SB_MAX
#define SB_MAX 100000 // Maximum 'scrollback' value.
#endif

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif
#ifndef MAX
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif

typedef struct LineInfo {
  char *directory; /* working directory */

  int prompt_col; /* end column of the prompt, if the current line contains the
                   * prompt */
} LineInfo;

typedef struct ScrollbackLine {
  size_t cols;
  LineInfo *info;
  VTermScreenCell cells[];
} ScrollbackLine;

typedef struct ElispCodeListNode {
  char *code;
  size_t code_len;
  struct ElispCodeListNode *next;
} ElispCodeListNode;

/*  c , p , q , s , 0 , 1 , 2 , 3 , 4 , 5 , 6 , and 7  */
/* clipboard, primary, secondary, select, or cut buffers 0 through 7 */
#define SELECTION_BUF_LEN 4096

typedef struct Cursor {
  int row, col;
  int cursor_type;
  bool cursor_visible;
  bool cursor_blink;
  bool cursor_type_changed;
  bool cursor_blink_changed;
} Cursor;

typedef struct Term {
  VTerm *vt;
  VTermScreen *vts;
  // buffer used to:
  //  - convert VTermScreen cell arrays into utf8 strings
  //  - receive data from libvterm as a result of key presses.
  ScrollbackLine *
      *sb_buffer;    // Scrollback buffer storage for libvterm (circular buffer)
  size_t sb_current; // number of rows pushed to sb_buffer
  size_t sb_size;    // sb_buffer size
  size_t sb_head;    // head index for circular buffer (oldest entry)
  size_t sb_tail;    // tail index for circular buffer (newest entry)
  // "virtual index" that points to the first sb_buffer row that we need to
  // push to the terminal buffer when refreshing the scrollback. When negative,
  // it actually points to entries that are no longer in sb_buffer (because the
  // window height has increased) and must be deleted from the terminal buffer
  int sb_pending;
  int sb_pending_by_height_decr;
  bool sb_clear_pending;
  long linenum;
  long linenum_added;

  int invalid_start, invalid_end; // invalid rows in libvterm screen
  bool is_invalidated;
  bool queued_bell;

  Cursor cursor;
  char *title;
  bool title_changed;

  char *directory;
  bool directory_changed;

  // Single-linked list of elisp_code.
  // Newer commands are added at the tail.
  ElispCodeListNode *elisp_code_first;
  ElispCodeListNode **elisp_code_p_insert; // pointer to the position where new
                                           // node should be inserted

  /*  c , p , q , s , 0 , 1 , 2 , 3 , 4 , 5 , 6 , and 7  */
  /* clipboard, primary, secondary, select, or cut buffers 0 through 7 */
  int selection_mask; /* see VTermSelectionMask in vterm.h */
  char *selection_data;
  char selection_buf[SELECTION_BUF_LEN];

  /* the size of dirs almost = window height, value = directory of that line */
  LineInfo **lines;
  int lines_len;

  int width, height;
  int height_resize;
  bool resizing;
  bool disable_bold_font;
  bool disable_underline;
  bool disable_inverse_video;
  bool ignore_blink_cursor;
  bool ignore_cursor_change;

  char *cmd_buffer;

  int pty_fd;

#ifdef _WIN32
  // Arena allocators for Windows performance optimization
  arena_allocator_t
      *persistent_arena;         // Long-lived data (LineInfo, directories)
  arena_allocator_t *temp_arena; // Temporary render buffers (reset per frame)

  // In-process ConPTY (Windows only)
  ConPTYState *conpty; // NULL if not using in-process ConPTY
#endif
} Term;

static bool compare_cells(VTermScreenCell *a, VTermScreenCell *b);
static bool is_key(unsigned char *key, size_t len, char *key_description);
static emacs_value render_text(emacs_env *env, Term *term, char *string,
                               int len, VTermScreenCell *cell);
static emacs_value render_fake_newline(emacs_env *env, Term *term);
static emacs_value render_prompt(emacs_env *env, emacs_value text);
static emacs_value cell_rgb_color(emacs_env *env, Term *term,
                                  VTermScreenCell *cell, bool is_foreground);

static int term_settermprop(VTermProp prop, VTermValue *val, void *user_data);

static void term_redraw(Term *term, emacs_env *env);
static void term_flush_output(Term *term, emacs_env *env);
static void term_process_key(Term *term, emacs_env *env, unsigned char *key,
                             size_t len, VTermModifier modifier);
static void invalidate_terminal(Term *term, int start_row, int end_row);

void term_finalize(void *object);

emacs_value Fvterm_new(emacs_env *env, ptrdiff_t nargs, emacs_value args[],
                       void *data);
emacs_value Fvterm_update(emacs_env *env, ptrdiff_t nargs, emacs_value args[],
                          void *data);
emacs_value Fvterm_redraw(emacs_env *env, ptrdiff_t nargs, emacs_value args[],
                          void *data);
emacs_value Fvterm_write_input(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data);
emacs_value Fvterm_set_size(emacs_env *env, ptrdiff_t nargs, emacs_value args[],
                            void *data);
emacs_value Fvterm_set_pty_name(emacs_env *env, ptrdiff_t nargs,
                                emacs_value args[], void *data);
emacs_value Fvterm_get_icrnl(emacs_env *env, ptrdiff_t nargs,
                             emacs_value args[], void *data);

emacs_value Fvterm_get_pwd(emacs_env *env, ptrdiff_t nargs, emacs_value args[],
                           void *data);

emacs_value Fvterm_get_prompt_point(emacs_env *env, ptrdiff_t nargs,
                                    emacs_value args[], void *data);
emacs_value Fvterm_reset_cursor_point(emacs_env *env, ptrdiff_t nargs,
                                      emacs_value args[], void *data);

#ifdef _WIN32
/* In-process ConPTY functions (Windows only) */
emacs_value Fvterm_conpty_init(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data);
emacs_value Fvterm_conpty_read_pending(emacs_env *env, ptrdiff_t nargs,
                                       emacs_value args[], void *data);
emacs_value Fvterm_conpty_write(emacs_env *env, ptrdiff_t nargs,
                                emacs_value args[], void *data);
emacs_value Fvterm_conpty_resize(emacs_env *env, ptrdiff_t nargs,
                                 emacs_value args[], void *data);
emacs_value Fvterm_conpty_is_alive(emacs_env *env, ptrdiff_t nargs,
                                   emacs_value args[], void *data);
emacs_value Fvterm_conpty_kill(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data);
#endif

VTERM_EXPORT int emacs_module_init(struct emacs_runtime *ert);

#endif /* VTERM_MODULE_H */
