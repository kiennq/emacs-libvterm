# Phase 2 Testing Instructions

## ✅ Status: Deployed and Ready

**Deployed**: Phase 2 DLL with BATCH_CAPACITY=2048  
**Timestamp**: 2/1/2026 12:20:42 AM  
**Method Used**: Renamed old DLL (great tip!) to avoid lock issues

---

## Quick Test

### 1. Restart Emacs
The new DLL won't be loaded until Emacs restarts:
```
1. Close all vterm buffers (optional, but clean)
2. Restart Emacs (required)
```

### 2. Run Benchmark
```
M-x load-file RET ~/.cache/quelpa/build/vterm/benchmark/test-render-text.el RET
```

### 3. Check Results
Results will auto-open in buffer. Look for:

**Key Metrics to Compare**:

| Metric | Phase 1 (BATCH=256) | Phase 2 (BATCH=2048) Target | Actual |
|--------|---------------------|----------------------------|--------|
| insert_batch calls | 140 | ~18 (8x fewer) | ? |
| insert_batch time | 146.906 ms | ~18 ms (8x less) | ? |
| term_redraw time | 1064.088 ms | ~935 ms (12% less) | ? |

---

## Comparison Files

**Phase 1 (Baseline)**:
```
~\.cache\vterm\vterm-profile-phase1.txt
```

**Phase 2 (After test)**:
```
~\.cache\vterm\vterm-profile.txt
```

---

## What to Look For

### Success Indicators:
✅ **insert_batch calls dropped 8x** (140 → ~18)  
✅ **term_redraw improved 10-15%** (~1064 → ~935 ms)  
✅ **All other functions unchanged** (render_text, fetch_cell, etc.)

### If Something's Wrong:
- insert_batch calls still ~140: BATCH_CAPACITY didn't increase (build issue)
- Performance worse: Unexpected regression (investigate)
- Emacs crashes: Buffer overflow (very unlikely, but possible)

---

## Pro Tip Applied

Thanks for the suggestion! For future deployments with locked DLLs:

```powershell
# Instead of waiting for Emacs to close:
Move-Item vterm-module.dll vterm-module-old.dll -Force
Copy-Item new-build.dll vterm-module.dll -Force
# Restart Emacs to pick up new DLL
```

This allows continuous development without manual DLL management!

---

**Ready to test!** Restart Emacs and run the benchmark. 🚀
