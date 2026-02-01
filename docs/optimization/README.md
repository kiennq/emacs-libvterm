# Performance Optimization Documentation

This directory contains detailed documentation of the emacs-libvterm Windows performance optimization work completed in February 2026.

## Quick Reference

**Start here**: [SUMMARY.md](SUMMARY.md) - Comprehensive overview of all optimization work

**Final Result**: -66.5% improvement (9.17 ms → 3.07 ms per redraw)

---

## Document Index

### Executive Summary
- **SUMMARY.md** - Complete optimization story, results, and technical details

### Phase-by-Phase Analysis

#### Phase 1: Profiling Infrastructure
- **PROFILING_ANALYSIS.md** - Initial profiling setup and baseline measurements
- **COMPREHENSIVE_PROFILING.md** - Extended profiling data across all instrumented functions

#### Phase 2: BATCH_CAPACITY Optimization
- **PHASE2_ANALYSIS.md** - Analysis of batch insertion performance
- **PHASE2_READY.md** - Pre-implementation planning
- **PHASE2_TEST.md** - Initial testing results
- **PHASE2_RETEST_ANALYSIS.md** - Validation and benchmarking

#### Phase 3: adjust_topline Optimization
- **PHASE3_EXTENDED_PROFILING.md** - Discovery of the bottleneck (82% of redraw time)
- **PHASE3_RESULTS.md** - Detailed bottleneck analysis
- **TERM_REDRAW_ANALYSIS.md** - Optimization opportunities identified

#### Phase 3a: Cursor Position Caching
- **PHASE3A_IMPLEMENTATION.md** - Cursor cache design and implementation
- **PHASE3A_RESULTS.md** - Benchmark results (-41.5% from baseline)

#### Phase 3b: Hybrid Optimization
- **PHASE3B_IMPLEMENTATION.md** - Smart viewport + single window design
- **PHASE3B_RESULTS.md** - Final benchmark results (-66.5% from baseline)

### Planning
- **NEXT_STEPS.md** - Follow-up tasks and future optimization opportunities

---

## Key Files Modified

### Core Implementation
- `vterm-module.c` (lines 17-133): Profiling infrastructure
- `vterm-module.c` (line ~840): BATCH_CAPACITY=2048
- `vterm-module.c` (lines 1107-1182): Optimized adjust_topline function

### Build System
- `CMakeLists.txt`: Added `-DENABLE_PROFILING` flag

---

## Optimization Highlights

### Phase 2: BATCH_CAPACITY
```
Before: BATCH_CAPACITY = 256
After:  BATCH_CAPACITY = 2048
Result: -16.0% (9.173 ms → 7.704 ms)
```

### Phase 3a: Cursor Cache
```
Added: Static cursor position cache
Logic: Skip adjust_topline if cursor unchanged
Result: -41.5% from baseline (7.704 ms → 5.369 ms)
```

### Phase 3b: Hybrid Optimization
```
Added: Smart viewport skipping (60-70% coverage)
Added: Single window mode (removed multi-window sync)
Result: -66.5% from baseline (5.369 ms → 3.070 ms)
```

---

## Bottleneck Discovery

**Original adjust_topline performance** (Phase 3):
```
Average: 6.348 ms per call
82.4% of term_redraw time
Called on 97% of redraws
```

**Root causes**:
1. Called `get_buffer_window_list()` every redraw (~0.5 ms)
2. Iterated through ALL Emacs windows (~0.5 ms)
3. Called `recenter()` even when unnecessary (~1.0 ms)
4. No caching whatsoever

**Optimized performance** (Phase 3b):
```
Average: 1.832 ms per call
59.7% of term_redraw time
-71% improvement on the bottleneck
```

---

## Trade-offs

### Single Window Mode
**Benefit**: 1.0 ms saved per redraw (~30% of total gain)  
**Cost**: Multi-window vterm buffers won't auto-sync cursor  
**Impact**: ~5% of users (most don't split vterm buffers)

### Smart Viewport Skipping
**Benefit**: Skips recenter on 60-70% of redraws  
**Cost**: Cursor may occasionally jump during rapid movement  
**Tuning**: Adjust `margin=3` or `cursor_delta<=2` if needed

---

## Rollback Instructions

Backups in `~\.cache\vterm\`:

```powershell
# Rollback to Phase 3a (safer optimization)
Copy-Item ~\.cache\vterm\vterm-module-phase3a.dll `
          ~\.cache\vterm\vterm-module.dll -Force

# Rollback to Phase 2 (conservative)
Copy-Item ~\.cache\vterm\vterm-module-phase2.dll `
          ~\.cache\vterm\vterm-module.dll -Force

# Full rollback to baseline
Copy-Item ~\.cache\vterm\vterm-module-baseline.dll `
          ~\.cache\vterm\vterm-module.dll -Force
```

Then restart Emacs.

---

## Build Instructions

See `../../.github/prompts/BUILD.md` for full build instructions.

**Quick rebuild**:
```bash
cd ~\.cache\quelpa\build\vterm
cmake --build build --target vterm-module -j8
cp vterm-module.dll ~\.cache\vterm\
```

**Toggle profiling**:
```bash
# Enable profiling (for analysis)
cmake .. -DENABLE_PROFILING=ON

# Disable profiling (for production)
cmake .. -DENABLE_PROFILING=OFF
```

---

## Validation

### Expected Performance (with profiling)
```
term_redraw avg: 2.5-3.5 ms
adjust_topline avg: 1.5-2.5 ms
adjust_topline % of redraw: 50-70%
```

### Red Flags
```
adjust_topline > 4.0 ms   ← Optimization broken
term_redraw > 6.0 ms      ← Major regression
adjust_topline > 80%      ← Bottleneck returned
```

### How to Check
1. Build with `-DENABLE_PROFILING=ON`
2. Run benchmark: `M-x load-file RET ~/.cache/quelpa/build/vterm/benchmark/test-render-text.el`
3. Check: `~\.cache\vterm\vterm-profile.txt`

---

## Related Documentation

- **../ARCHITECTURE.md** - System architecture (ConPTY proxy, I/O model)
- **../BUILD.md** - Build system and dependencies
- **../../benchmark/README.md** - Benchmark methodology

---

## Timeline

- **Phase 1**: Profiling infrastructure setup
- **Phase 2**: BATCH_CAPACITY optimization (-16%)
- **Phase 3**: Bottleneck discovery (adjust_topline = 82% of time)
- **Phase 3a**: Cursor cache implementation (-42% cumulative)
- **Phase 3b**: Hybrid optimization (-67% cumulative)
- **Production**: Profiling disabled (estimated -70-73%)

**Total Duration**: ~1-2 weeks of iterative optimization work

**Final Status**: Production-ready, deployed, validated
