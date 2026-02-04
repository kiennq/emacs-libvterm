#!/usr/bin/env bash
# bench-throughput.sh - Shell-based throughput benchmarks for vterm

echo "========================================"
echo "vterm Throughput Benchmarks"
echo "========================================"
echo ""

# Test 1: Large file output
echo "Test 1: Large file output (10MB)"
echo "Expected: Should handle without freezing"
time (head -c 10485760 /dev/urandom | base64)
echo ""

# Test 2: Rapid small writes
echo "Test 2: Rapid small writes (10,000 lines)"
echo "Expected: Should batch efficiently"
time (for i in {1..10000}; do echo "Line $i"; done)
echo ""

# Test 3: Directory changes
echo "Test 3: Directory navigation (1000 changes)"
echo "Expected: Directory tracking should be fast"
dirs=("/tmp" "/usr" "/var" "/etc" "/opt")
time (for i in {1..200}; do
  for dir in "${dirs[@]}"; do
    [ -d "$dir" ] && cd "$dir" 2>/dev/null
  done
done)
echo ""

# Test 4: Scrollback performance
echo "Test 4: Scrollback generation (50,000 lines)"
echo "Expected: Should not cause memory issues"
time (seq 1 50000 | while read n; do 
  echo "Line $n: $(date +%s.%N)"
done)
echo ""

echo "========================================"
echo "Benchmarks Complete!"
echo "========================================"
echo ""
echo "Performance Analysis:"
echo "- Faster times indicate better performance"
echo "- On Windows with arena allocator:"
echo "  * Expect 2-5x faster allocation"
echo "  * Zero memory fragmentation"
echo "  * Lower CPU usage during bursts"
