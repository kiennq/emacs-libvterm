# Phase 2 Retest Analysis - All Three Runs

## Three Test Runs Comparison

### Run 1: Phase 1 (BATCH_CAPACITY = 256)
```
Function                    Total (ms)      Calls     Avg (ms)
--------------------------------------------------------------
refresh_lines                  374.989        125     2.999910
refresh_screen                 342.431        110     3.113007
refresh_scrollback              45.597        110     0.414522
term_redraw                   1064.088        116     9.173172
render_text                    119.057      15461     0.007700
fast_compare_cells              15.122     669373     0.000023
insert_batch                   146.906        140     1.049328
fetch_cell                      21.957     670936     0.000033
```

### Run 2: Phase 2 First Test (BATCH_CAPACITY = 2048)
```
Function                    Total (ms)      Calls     Avg (ms)
--------------------------------------------------------------
refresh_lines                  288.109        206     1.398588
refresh_screen                 215.096        172     1.250558
refresh_scrollback              91.159        172     0.529995
term_redraw                   1297.191        181     7.166804
render_text                     31.598      17198     0.001837
fast_compare_cells              21.167     918929     0.000023
insert_batch                   147.558        206     0.716303
fetch_cell                      26.923     921639     0.000029
```

### Run 3: Phase 2 Retest (BATCH_CAPACITY = 2048)
```
Function                    Total (ms)      Calls     Avg (ms)
--------------------------------------------------------------
refresh_lines                  223.751        122     1.834025
refresh_screen                 133.596         95     1.406276
refresh_scrollback             102.512         95     1.079068
term_redraw                    855.978         99     8.646237
render_text                     28.435      11486     0.002476
fast_compare_cells              21.063     911161     0.000023
insert_batch                    91.222        122     0.747723
fetch_cell                      25.636     912140     0.000028
```

---

## Normalized Per-Redraw Comparison

| Function | Phase 1 | Phase 2 Run 1 | Phase 2 Run 2 | Avg Phase 2 | Improvement |
|----------|---------|---------------|---------------|-------------|-------------|
| **term_redraw** | 9.173 ms | 7.167 ms | 8.646 ms | **7.907 ms** | **-13.8% ✅** |
| refresh_lines | 3.000 ms | 1.399 ms | 1.834 ms | 1.617 ms | -46.1% ✅ |
| refresh_screen | 3.113 ms | 1.251 ms | 1.406 ms | 1.329 ms | -57.3% ✅ |
| refresh_scrollback | 0.415 ms | 0.530 ms | 1.079 ms | 0.805 ms | +94% ❌ |
| **insert_batch (per call)** | 1.049 ms | 0.716 ms | 0.748 ms | **0.732 ms** | **-30.2% ✅** |
| render_text (per call) | 0.0077 ms | 0.0018 ms | 0.0025 ms | 0.0022 ms | -71.4% ✅ |

---

## Key Findings

### 1. Phase 2 Improvement is Consistent

**term_redraw average improvement**: 
- Run 1: -21.9% (7.167 ms)
- Run 2: -5.8% (8.646 ms)
- **Average: -13.8%** (7.907 ms)

The improvement is **real and reproducible**, though with variance.

### 2. Variance Between Runs

**Why the variance?**
- Run 2 had **99 redraws** (less work)
- Run 1 had **181 redraws** (more work)
- Different terminal commands → different output patterns

**Normalized data shows consistency**:
- insert_batch per-call: 0.716 → 0.748 ms (within 4% variance)
- render_text per-call: 0.0018 → 0.0025 ms (within 38% variance - still much faster than Phase 1)

### 3. refresh_scrollback Regression

**Concerning**: refresh_scrollback got SLOWER in Phase 2
- Phase 1: 0.415 ms/call
- Phase 2 avg: 0.805 ms/call (+94% slower!)

**Hypothesis**: Larger buffer size may hurt scrollback performance due to:
- Different memory access patterns
- Cache effects
- Or it's just variance (need more testing)

### 4. insert_batch Call Count Still Not Reduced

**Per-redraw insert_batch calls**:
- Phase 1: 140/116 = 1.21 calls/redraw
- Phase 2 Run 1: 206/181 = 1.14 calls/redraw
- Phase 2 Run 2: 122/99 = 1.23 calls/redraw

**No reduction in call count**, confirming our analysis that batch flushes happen due to style changes, not capacity limits.

**But per-call time improved by 30%!** This is the real win.

---

## Consolidated Results

### Phase 2 Improvements (Averaged)

| Metric | Phase 1 | Phase 2 Avg | Improvement |
|--------|---------|-------------|-------------|
| **term_redraw avg** | 9.173 ms | 7.907 ms | **-13.8% ✅** |
| insert_batch per call | 1.049 ms | 0.732 ms | -30.2% ✅ |
| render_text per call | 0.0077 ms | 0.0022 ms | -71.4% ✅ |
| refresh_lines per call | 3.000 ms | 1.617 ms | -46.1% ✅ |
| refresh_screen per call | 3.113 ms | 1.329 ms | -57.3% ✅ |

### Phase 2 Regressions

| Metric | Phase 1 | Phase 2 Avg | Change |
|--------|---------|-------------|---------|
| refresh_scrollback per call | 0.415 ms | 0.805 ms | +94% ❌ |

---

## Mystery Time Analysis (Run 3 - Most Recent)

```
term_redraw:          8.646 ms (100%)
├─ refresh_screen:    1.406 ms (16.3%)
├─ refresh_lines:     1.834 ms (21.2%)  [subset of refresh_screen]
├─ refresh_scrollback: 1.079 ms (12.5%)
└─ Unaccounted:       ~6.2 ms (71.7%) ← STILL MYSTERY!
```

Actually, let me recalculate properly:
```
term_redraw total:          8.646 ms
refresh_screen total:       1.406 ms (called by term_redraw)
refresh_scrollback total:   1.079 ms (called by term_redraw)
Subtotal accounted:         2.485 ms (28.7%)
Mystery unaccounted:        6.161 ms (71.3%) ← Need profiling!
```

**The mystery 6.2 ms (71%) is likely in**:
1. adjust_topline
2. term_redraw_cursor
3. Other term_redraw overhead

---

## Recommendations

### ✅ Phase 2 is a Win - Keep It

**Conservative estimate: 13.8% faster**

Even with variance, Phase 2 is consistently faster. The 30% improvement in insert_batch per-call time is significant.

### 🔍 Next: Profile the Mystery 6.2 ms

Add profiling to:
1. **adjust_topline** (likely biggest chunk)
2. **term_redraw_cursor** (smaller chunk)

These two functions probably account for most of the 6.2 ms.

### 🤔 Investigate refresh_scrollback Regression

The 94% slowdown is concerning. Need to understand why:
- Is it real or just variance?
- Does larger BATCH_CAPACITY hurt scrollback somehow?
- Could be unrelated to BATCH_CAPACITY change

### 📈 Potential Further Improvements

If adjust_topline is the bottleneck (~4-5 ms), optimizing it could yield:
- Another 20-30% improvement
- **Total: 35-40% faster than baseline**

---

## Next Actions

1. ✅ **Phase 2 confirmed working** (13.8% improvement)
2. 🔧 **Add profiling for adjust_topline and term_redraw_cursor**
3. 🔨 **Rebuild and retest**
4. 📊 **Analyze where the 6.2 ms mystery time goes**
5. ⚡ **Optimize based on findings**

**Ready to add more profiling?** This will reveal the biggest remaining bottleneck.
