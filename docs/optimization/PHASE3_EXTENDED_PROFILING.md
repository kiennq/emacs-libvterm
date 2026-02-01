# Phase 3: Extended Profiling Analysis

## Objective
Identify the mystery 6.2ms bottleneck in `term_redraw` by adding profiling to:
- `adjust_topline` (lines 1105-1145)
- `term_redraw_cursor` (lines 1180-1213)

## Build Information

**Date**: 2026-02-01 00:34:06
**DLL Size**: 354732 bytes (Phase 2: 354220 bytes, +512 bytes)
**Profiling Functions**: 11 (was 9)
**Build Command**: `cmake --build build --target vterm-module -j8`

### New Profile Indices
```c
#define PROFILE_ADJUST_TOPLINE 9
#define PROFILE_TERM_REDRAW_CURSOR 10
```

### Profile Stats Array Extended
```c
static ProfileStats profile_stats[11] = {
    // ... existing 9 entries ...
    {"adjust_topline", 0.0, 0},          // Index 9
    {"term_redraw_cursor", 0.0, 0},      // Index 10
};
```

## Phase 2 Mystery Bottleneck

### Phase 2 Profile Summary (99 redraws, 8.646 ms avg)
```
Function                Total(ms)   Calls    Per-call(ms)  % of term_redraw
------------------------------------------------------------------------
term_redraw             855.746     99       8.646         100.0%
  refresh_screen        139.192     99       1.406         16.3%
  refresh_scrollback    106.871     99       1.079         12.5%
  refresh_lines         14.065      6388     0.0022        1.6%
    render_text         13.830      6388     0.0022        1.6%
    insert_batch        7.322       10       0.732         0.9%
    fast_compare_cells  0.011       11748    0.0000009     0.001%
    fetch_cell          0.000       0        -             0.0%
  codepoint_to_utf8     0.000       0        -             0.0%

ACCOUNTED:              267.221                            31.2%
UNACCOUNTED:            588.525                            68.8%  ← MYSTERY!
```

### Mystery Time Breakdown
**Total unaccounted**: 588.525 ms / 99 redraws = **5.95 ms per redraw**

**Hypotheses**:
1. **adjust_topline** (likely 4-5 ms): Heavy window iteration + Emacs API
2. **term_redraw_cursor** (likely <0.5 ms): Simple cursor updates with early returns
3. **Other term_redraw operations** (likely 1-2 ms):
   - String value creation (title, directory)
   - Elisp code execution (lines 1235-1244)
   - Redisplay trigger (line 1253)
4. **Profiling overhead** (~0.5-1.0 ms): QueryPerformanceCounter calls

## Expected Results

### If adjust_topline is the bottleneck (most likely)
```
adjust_topline          450.000     99       4.545         52.6%
```

**Analysis**: Window iteration in adjust_topline (lines 1115-1142):
- Iterates through ALL Emacs windows
- Multiple Emacs API calls per window:
  - `Fwindow_buffer()` - 1 call per window
  - `window_body_height()` - 1 call per window
  - `Fset_window_point()` - 1 call if recentering needed
- Typical Emacs session: 3-10 windows
- Per-window overhead: ~0.5-1.0 ms

**Optimization Opportunities**:
1. Cache window list if unchanged
2. Skip iteration if cursor position unchanged since last redraw
3. Cache window_body_height (rarely changes)
4. Only iterate windows displaying vterm buffers

**Expected Gain**: +15-25% (4.5 ms → 1.0 ms)

### If term_redraw_cursor is significant (unlikely)
```
term_redraw_cursor      40.000      99       0.404         4.7%
```

**Analysis**: Cursor updates (lines 1180-1213):
- Early return if cursor unchanged (line 1189)
- Cursor type/color updates via Emacs API
- Should be fast due to guards

**Optimization Opportunities**:
1. Cache cursor_type emacs_values
2. Skip color updates if unchanged

**Expected Gain**: +2-5% (0.4 ms → 0.1 ms)

### If neither explains mystery (need deeper investigation)
```
adjust_topline          50.000      99       0.505         5.8%
term_redraw_cursor      30.000      99       0.303         3.5%
STILL UNACCOUNTED:      508.525                            59.4%
```

**Next Steps**:
1. Profile string value creation (title, directory)
2. Profile Elisp code execution
3. Profile redisplay trigger overhead
4. Check for hidden Emacs GC triggers
5. Measure profiling overhead itself

## Performance Targets

### Current Performance (Phase 2)
- **Baseline (Phase 1)**: 9.173 ms avg
- **Phase 2 (BATCH=2048)**: 8.646 ms avg (7.907 ms in retest)
- **Improvement**: 13.8%

### Phase 3 Target
- **If adjust_topline optimized**: 6.5-7.0 ms avg (+20-25% total)
- **If multiple optimizations**: 5.5-6.0 ms avg (+35-40% total)

### Ultimate Target (Production)
- **Remove profiling overhead**: 5.0-5.5 ms avg (+40-45% total)

## Optimization Roadmap

### Phase 3a: adjust_topline optimization (high priority)
```c
// Current: Iterates ALL windows every redraw
// Optimized: Cache window list, skip if cursor unchanged

static Lisp_Object cached_window_list = Qnil;
static int last_cursor_row = -1;
static int last_cursor_col = -1;

static void adjust_topline(emacs_env *env, Term *term, Lisp_Object window) {
  int cursor_row = term->invalid_start;
  int cursor_col = vterm_state_get_cursorpos(term->vts)->col;
  
  // Skip if cursor unchanged
  if (cursor_row == last_cursor_row && cursor_col == last_cursor_col) {
    return;
  }
  
  last_cursor_row = cursor_row;
  last_cursor_col = cursor_col;
  
  // Cache window list (invalidate on frame config change)
  if (NILP(cached_window_list)) {
    cached_window_list = Fwindow_list(Qnil, Qnil, Qnil);
  }
  
  // ... rest of logic ...
}
```

**Expected Gain**: 4.5 ms → 1.0 ms (-3.5 ms, +15-20%)

### Phase 3b: String value caching (medium priority)
```c
// Cache title/directory emacs_values
static emacs_value cached_title = NULL;
static char *cached_title_str = NULL;

// Only recreate if changed
if (strcmp(title, cached_title_str) != 0) {
  free(cached_title_str);
  cached_title_str = strdup(title);
  cached_title = env->make_string(env, title, strlen(title));
}
```

**Expected Gain**: 0.5 ms → 0.1 ms (-0.4 ms, +5%)

### Phase 3c: Elisp code batching (low priority)
```c
// Batch multiple elisp expressions into single eval
char elisp_batch[1024];
snprintf(elisp_batch, sizeof(elisp_batch),
  "(progn "
  "  (vterm--set-title \"%s\")"
  "  (vterm--set-directory \"%s\")"
  "  %s"  // Custom elisp code
  ")",
  title, directory, elisp_code);

vterm_eval(env, elisp_batch);  // Single eval instead of 3+
```

**Expected Gain**: 1.0 ms → 0.5 ms (-0.5 ms, +5-10%)

### Phase 3d: Production build (final step)
```bash
# Remove profiling overhead
cmake .. -DENABLE_PROFILING=OFF
cmake --build build --target vterm-module -j8
```

**Expected Gain**: -0.5-1.0 ms (+5-10%)

## Total Expected Improvement

| Phase | Change | Avg Time | Improvement | Cumulative |
|-------|--------|----------|-------------|------------|
| Baseline | Original | 9.173 ms | - | - |
| Phase 2 | BATCH=2048 | 7.907 ms | -13.8% | -13.8% |
| Phase 3a | adjust_topline | 6.0 ms | -24.1% | -34.6% |
| Phase 3b | String cache | 5.6 ms | -6.7% | -38.9% |
| Phase 3c | Elisp batch | 5.3 ms | -5.4% | -42.2% |
| Phase 3d | No profiling | 4.8 ms | -9.4% | -47.7% |

**Total Target**: 4.8-5.3 ms avg (**~48% faster than baseline**)

## Waiting For

**Manual Action Required**:
1. Restart Emacs to load new DLL
2. Run benchmark: `M-x load-file RET ~/.cache/quelpa/build/vterm/benchmark/test-render-text.el RET`
3. Share profile results: `~\.cache\vterm\vterm-profile.txt`

**Status**: Built and deployed, ready for benchmark.

## Files Modified

### vterm-module.c
- Lines 71-83: Extended profile_stats[11]
- Lines 85-93: Added PROFILE_ADJUST_TOPLINE, PROFILE_TERM_REDRAW_CURSOR
- Lines 98-106: Updated initialization loop (9 → 11)
- Lines 1105-1145: Added profiling to adjust_topline
- Lines 1180-1213: Added profiling to term_redraw_cursor

### Build Files
- `vterm-module.dll`: Deployed (354732 bytes)
- `vterm-module-phase2.dll`: Previous version backup (354220 bytes)
- `vterm-profile.txt`: Cleared, ready for fresh data
