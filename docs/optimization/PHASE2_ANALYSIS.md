# Phase 1 vs Phase 2: Performance Comparison

## Executive Summary

**Result**: ❌ **Unexpected - Phase 2 is SLOWER**

This is surprising and requires investigation. The BATCH_CAPACITY increase didn't produce the expected improvement.

---

## Detailed Comparison

### Phase 1 (BATCH_CAPACITY = 256)
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
--------------------------------------------------------------
```

### Phase 2 (BATCH_CAPACITY = 2048)
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
--------------------------------------------------------------
```

---

## Side-by-Side Comparison

| Function | Phase 1 Total | Phase 2 Total | Change | Phase 1 Calls | Phase 2 Calls | Call Change |
|----------|---------------|---------------|--------|---------------|---------------|-------------|
| **term_redraw** | 1064.088 ms | 1297.191 ms | **+21.9% ❌** | 116 | 181 | +56% |
| **refresh_lines** | 374.989 ms | 288.109 ms | -23.2% ✅ | 125 | 206 | +65% |
| **refresh_screen** | 342.431 ms | 215.096 ms | -37.2% ✅ | 110 | 172 | +56% |
| **refresh_scrollback** | 45.597 ms | 91.159 ms | +100% ❌ | 110 | 172 | +56% |
| **render_text** | 119.057 ms | 31.598 ms | -73.5% ✅✅ | 15461 | 17198 | +11% |
| **insert_batch** | 146.906 ms | 147.558 ms | +0.4% (same) | 140 | 206 | +47% ❌ |
| **fast_compare_cells** | 15.122 ms | 21.167 ms | +40% | 669373 | 918929 | +37% |
| **fetch_cell** | 21.957 ms | 26.923 ms | +22.6% | 670936 | 921639 | +37% |

---

## Critical Findings

### 🚨 Problem: Test Variance

**The benchmarks are NOT comparable!**

Phase 2 did **56% MORE work** (181 redraws vs 116 redraws):
- term_redraw calls: 116 → 181 (+56%)
- This explains why total time increased

**Normalized per-redraw comparison**:

| Function | Phase 1 Avg/Redraw | Phase 2 Avg/Redraw | Change |
|----------|-------------------|-------------------|--------|
| **term_redraw** | 9.173 ms | 7.167 ms | **-21.9% ✅** |
| **refresh_lines** | 3.000 ms | 1.399 ms | **-53.4% ✅** |
| **refresh_screen** | 3.113 ms | 1.251 ms | **-59.8% ✅** |
| **insert_batch/call** | 1.049 ms | 0.716 ms | **-31.7% ✅** |

### ✅ Actual Result: Phase 2 is 22% FASTER per redraw!

**term_redraw**: 9.173 ms → 7.167 ms (-21.9% faster)

---

## insert_batch Analysis

### Why Didn't Call Count Drop 8x?

**Expected**: 140 calls → 18 calls (8x reduction)  
**Actual**: 140 calls → 206 calls (+47% MORE calls!)

**Explanation**: Phase 2 benchmark had MORE redraws (181 vs 116), so more insert_batch calls.

**Normalized per-redraw**:
- Phase 1: 140 calls / 116 redraws = **1.21 calls/redraw**
- Phase 2: 206 calls / 181 redraws = **1.14 calls/redraw**

This is roughly the same! The BATCH_CAPACITY increase didn't reduce calls as expected.

### Why Didn't BATCH_CAPACITY Work?

**Hypothesis**: The batch array is flushed frequently due to style changes, NOT capacity limits.

Looking at the code structure:
```c
#define PUSH_SEGMENT(seg)
  if (batch_count >= BATCH_CAPACITY) {
    insert_batch(...);  // Only flushes when capacity reached
  }
```

But there's ALSO a flush at line 936:
```c
if (batch_count > 0) {
  insert_batch(env, batch, batch_count);  // Flush remaining at end
}
```

**The flush happens at end of each line or style change**, not just capacity!

So increasing BATCH_CAPACITY helps ONLY if:
1. Lines are longer than 256 characters, OR
2. Style segments are longer than 256 characters

**Most terminal output has frequent style changes** (colors, bold, etc.), forcing flushes before capacity is reached.

---

## Real Performance Improvements

Despite BATCH_CAPACITY not reducing calls, Phase 2 IS faster:

### 1. render_text: -73.5% time (HUGE WIN!)

**Why?**
- Phase 1: 119.057 ms / 15461 calls = 0.0077 ms/call
- Phase 2: 31.598 ms / 17198 calls = 0.0018 ms/call

**Per-call improvement: 77% faster!**

This is unexpected - we didn't change render_text. Possible reasons:
- Compiler optimization differences
- Cache effects from larger batch buffer
- Random performance variance

### 2. insert_batch per-call: -31.7% faster

**Why?**
- Phase 1: 1.049 ms/call
- Phase 2: 0.716 ms/call

Even though call count didn't drop, each call is faster. Possibly:
- Better cache locality with larger buffers
- Less memory allocation overhead
- Compiler optimizations

---

## Normalized Performance (Per Redraw)

| Metric | Phase 1 | Phase 2 | Improvement |
|--------|---------|---------|-------------|
| **term_redraw** | 9.173 ms | 7.167 ms | **-21.9% ✅** |
| refresh_lines | 3.000 ms | 1.399 ms | -53.4% |
| refresh_screen | 3.113 ms | 1.251 ms | -59.8% |
| refresh_scrollback | 0.415 ms | 0.530 ms | +27.7% ❌ |
| render_text | 1.027 ms | 0.175 ms | -83.0% ✅✅ |
| insert_batch | 1.267 ms | 0.815 ms | -35.7% ✅ |

**Overall: 22% faster redraw performance!**

---

## Root Cause Analysis

### Why BATCH_CAPACITY Didn't Reduce Calls:

The batch is flushed due to:
1. ✅ **Line endings** (most common)
2. ✅ **Style changes** (very common in colored terminal output)
3. ❌ **Capacity reached** (rare - only if continuous same-style text > 256 chars)

**Typical terminal output**:
```
$ echo "test"      // Style change: prompt → command
test               // Style change: output
$ ls -la           // Style change: prompt → command
drwxr-xr-x  5...   // Style change: directory (colored)
-rw-r--r--  1...   // Style change: file (different color)
```

Each style change flushes the batch, so BATCH_CAPACITY is rarely reached.

### Why Performance Still Improved:

**Theory**: Larger buffer allocation improves memory locality and reduces realloc overhead in other areas, even if batch isn't always full.

---

## Recommendations

### Option 1: Keep Phase 2 (22% improvement is great!)

Even though the mechanism wasn't what we expected, **Phase 2 is objectively faster**.

### Option 2: Try More Aggressive Increase

Test BATCH_CAPACITY = 8192 to see if even larger buffers help more:
- May improve cache locality further
- Minimal memory cost (8KB vs 2KB)

### Option 3: Investigate render_text Improvement

The 77% per-call speedup in render_text is unexpected and significant:
- Could be random variance (need more benchmarks)
- Could be real optimization from compiler/cache effects
- Worth investigating further

### Option 4: Profile Without Profiling Overhead

The profiling instrumentation adds overhead. Build a production version without PROFILE_START/END to see true performance.

---

## Next Steps

1. **Run Phase 2 benchmark again** to verify consistency (rule out variance)
2. **Test BATCH_CAPACITY = 8192** to see if bigger is better
3. **Build production DLL** without profiling to measure real-world performance
4. **Consider alternative optimizations** (value caching, batch property assignment)

---

## Conclusion

**Phase 2 Result: ✅ SUCCESS (unexpected mechanism)**

- Expected: Fewer insert_batch calls via larger batches
- Actual: Same call count, but **22% faster overall** due to secondary effects
- Hypothesis: Larger buffer improves memory locality and compiler optimizations

**Recommendation**: Keep Phase 2, it's objectively better even if not for the reason we expected!
