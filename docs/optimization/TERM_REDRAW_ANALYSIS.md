# term_redraw Optimization Analysis

## Current Structure

### term_redraw (vterm-module.c:1207-1267)

```c
static void term_redraw(Term *term, emacs_env *env) {
  PROFILE_START(PROFILE_TERM_REDRAW);
  
  term_redraw_cursor(term, env);                    // Cursor updates
  
  if (term->is_invalidated) {
    int oldlinenum = term->linenum;
    refresh_scrollback(term, env);                  // Profiled: ~0.5 ms/call
    refresh_screen(term, env);                       // Profiled: ~1.3 ms/call
    term->linenum_added = term->linenum - oldlinenum;
    adjust_topline(term, env);                       // Not profiled
    term->linenum_added = 0;
    if (term->queued_bell) {
      ding(env, Qt);
      term->queued_bell = false;
    }
  }
  
  if (term->title_changed) {                         // String creation
    set_title(env, env->make_string(...));
    term->title_changed = false;
  }
  
  if (term->directory_changed) {                     // String creation
    set_directory(env, env->make_string(...));
    term->directory_changed = false;
  }
  
  while (term->elisp_code_first) {                   // Elisp code execution
    ElispCodeListNode *node = term->elisp_code_first;
    term->elisp_code_first = node->next;
    emacs_value elisp_code = env->make_string(env, node->code, node->code_len);
    vterm_eval(env, elisp_code);
    free(node->code);
    free(node);
  }
  term->elisp_code_p_insert = &term->elisp_code_first;
  
  if (term->selection_data) {                        // Selection handling
    emacs_value selection_mask = env->make_integer(env, term->selection_mask);
    emacs_value selection_data = env->make_string(...);
    vterm_set_selection(env, selection_mask, selection_data);
    free(term->selection_data);
    term->selection_data = NULL;
    term->selection_mask = 0;
  }
  
  term->is_invalidated = false;
  
  #ifdef _WIN32
  if (term->temp_arena != NULL) {
    arena_reset(term->temp_arena);                   // O(1) memory reset
  }
  #endif
  
  PROFILE_END(PROFILE_TERM_REDRAW);
}
```

---

## Current Performance (Phase 2)

```
term_redraw:        7.167 ms avg (includes all sub-functions)
├─ refresh_screen:  1.251 ms (17.5%)
├─ refresh_lines:   1.399 ms (19.5%) [called by refresh_screen]
├─ refresh_scrollback: 0.530 ms (7.4%)
├─ adjust_topline:  ??? ms (not profiled)
├─ term_redraw_cursor: ??? ms (not profiled)
└─ Other ops:       ??? ms (not profiled)
```

**Mystery**: We account for ~2.2 ms out of 7.167 ms = **~4.9 ms unaccounted (68%)**

This suggests the unaccounted time is in:
1. **adjust_topline** (lines 1103-1140)
2. **term_redraw_cursor** (lines 1176-1205)
3. **Elisp code execution** (lines 1235-1244)
4. **String creation overhead** (lines 1225, 1231, 1238, 1248)
5. **Profiling overhead itself**

---

## Optimization Opportunities

### 1. Profile Missing Functions (High Priority)

Add profiling to identify the mystery 4.9 ms:

```c
#define PROFILE_ADJUST_TOPLINE 9
#define PROFILE_TERM_REDRAW_CURSOR 10
```

**Expected gain**: None directly, but identifies bottleneck

---

### 2. Cache String Values (Medium Priority)

**Current**: Creates new strings on every redraw:
```c
set_title(env, env->make_string(env, term->title, strlen(term->title)));
set_directory(env, env->make_string(env, term->directory, strlen(term->directory)));
```

**Optimization**: Cache strings, only recreate when changed:
```c
if (term->title_changed) {
  if (term->cached_title_string) {
    env->free_global_ref(env, term->cached_title_string);
  }
  term->cached_title_string = env->make_global_ref(env, 
    env->make_string(env, term->title, strlen(term->title)));
  set_title(env, term->cached_title_string);
  term->title_changed = false;
}
```

**Expected gain**: 5-10% if title/directory changes are frequent

---

### 3. Batch Elisp Code Execution (Low Priority)

**Current**: Executes elisp code one at a time in a loop
```c
while (term->elisp_code_first) {
  vterm_eval(env, elisp_code);  // One call per node
}
```

**Optimization**: Batch multiple elisp expressions into single eval
```c
// Concatenate all elisp code with ; separator
// Single vterm_eval() call
```

**Expected gain**: 10-20% if elisp code is frequent

---

### 4. Optimize adjust_topline (Unknown Potential)

**Current**: Lines 1103-1140 (38 lines, complex window handling)
- Calls `get_buffer_window_list(env)` - Emacs API
- Calls `selected_window(env)` - Emacs API  
- Calls `length(env, windows)` - Emacs API
- Loop over windows with multiple Emacs API calls per iteration
- Calls `window_body_height(env, window)` - Emacs API
- Calls `recenter(env, ...)` - Emacs API

**Problem**: Heavy Emacs API usage in potentially hot path

**Optimization possibilities**:
1. Cache window list if it doesn't change often
2. Skip window iteration if cursor position unchanged
3. Cache window_body_height (rarely changes)

**Expected gain**: 10-30% if this is the bottleneck

---

### 5. Optimize term_redraw_cursor (Unknown Potential)

**Current**: Lines 1176-1205 (29 lines)
- Conditional cursor updates (only if changed)
- Already fairly optimized with early returns

**Optimization possibilities**:
1. Batch cursor property updates
2. Cache cursor type emacs_values

**Expected gain**: 5-10% (likely small, already has guards)

---

### 6. Early Exit Optimization (Easy Win)

**Current**: Always calls `term_redraw_cursor()` even if nothing changed

**Optimization**:
```c
static void term_redraw(Term *term, emacs_env *env) {
  PROFILE_START(PROFILE_TERM_REDRAW);
  
  // Early exit if nothing changed
  if (!term->is_invalidated && 
      !term->title_changed && 
      !term->directory_changed &&
      !term->elisp_code_first &&
      !term->selection_data &&
      !term->cursor.cursor_blink_changed &&
      !term->cursor.cursor_type_changed) {
    PROFILE_END(PROFILE_TERM_REDRAW);
    return;
  }
  
  // ... rest of function
}
```

**Expected gain**: Huge (100%) for no-op redraws, but likely rare

---

## Recommended Next Steps

### Step 1: Profile Missing Functions (Immediate)

Add profiling to:
1. `adjust_topline` 
2. `term_redraw_cursor`

This will identify where the mystery 4.9 ms is spent.

### Step 2: Based on Profiling Results

**If adjust_topline is the bottleneck (likely)**:
- Optimize window list caching
- Skip unnecessary iterations

**If term_redraw_cursor is the bottleneck**:
- Cache cursor emacs_values
- Batch cursor updates

**If neither**:
- Investigate Emacs API overhead
- Consider profiling overhead removal for production

### Step 3: Low-Hanging Fruit

- Add early exit for no-op redraws
- Cache title/directory strings if they change frequently

---

## Architecture Analysis

### Where is term_redraw Called From?

Need to check:
```bash
grep -n "term_redraw(" vterm-module.c
```

If it's called on **every keystroke** or **timer**, optimizing it has huge impact.
If it's only called on **actual screen changes**, optimization is less critical.

---

## Profiling Overhead

**Current profiling adds ~0.5-1.0 ms overhead** (rough estimate):
- Each PROFILE_START/END: ~10-50 microseconds
- 8 profiled functions × 2 (start+end) = 16 calls per redraw
- Total overhead: 0.16-0.8 ms

**For production**, removing profiling should give another **5-10% improvement**.

---

## Expected Total Improvements

**Phase 2 (Current)**: 21.9% faster than baseline ✅

**With additional optimizations**:
- Profile missing functions: +0% (identifies targets)
- Optimize adjust_topline: +10-20% (if bottleneck)
- Cache strings: +5-10%
- Remove profiling overhead: +5-10%
- **Total potential: 40-50% faster than baseline**

---

## Next Actions

1. ✅ **Retest Phase 2** to verify consistency
2. 🔍 **Add profiling to adjust_topline and term_redraw_cursor**
3. 📊 **Analyze where the mystery 4.9 ms is spent**
4. ⚡ **Implement targeted optimizations based on findings**
5. 🚀 **Build production version without profiling**

**Status**: Waiting for retest results, then will add more profiling.
