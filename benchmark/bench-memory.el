;;; bench-memory.el --- Memory performance benchmarks for vterm -*- lexical-binding: t -*-

;; This file provides memory allocation benchmarks for vterm

;;; Code:

(require 'vterm)
(require 'benchmark)

(defun vterm-bench--create-large-scrollback ()
  "Create a vterm buffer and generate large scrollback."
  (let ((vterm-buffer (vterm)))
    (with-current-buffer vterm-buffer
      ;; Generate 10,000 lines of output
      (vterm-send-string "for i in {1..10000}; do echo \"Line $i: $(date)\"; done\n")
      ;; Wait for output to complete
      (sit-for 5))
    vterm-buffer))

(defun vterm-bench--memory-allocation ()
  "Benchmark memory allocation performance."
  (message "=== Memory Allocation Benchmark ===")
  (message "Creating vterm buffer...")
  
  (let ((start-time (current-time))
        (start-mem (memory-use-counts))
        buffer)
    
    ;; Create buffer and generate scrollback
    (setq buffer (vterm-bench--create-large-scrollback))
    
    (let* ((end-time (current-time))
           (end-mem (memory-use-counts))
           (elapsed (float-time (time-subtract end-time start-time)))
           (mem-delta (- (nth 0 end-mem) (nth 0 start-mem))))
      
      (message "Time elapsed: %.2f seconds" elapsed)
      (message "Memory allocated: %d cons cells" mem-delta)
      (message "Cons cells per second: %.2f" (/ mem-delta elapsed))
      
      ;; Cleanup
      (kill-buffer buffer)
      
      (list :time elapsed :memory mem-delta))))

(defun vterm-bench--rapid-resize ()
  "Benchmark rapid terminal resizing (tests arena reset performance)."
  (message "\n=== Rapid Resize Benchmark ===")
  (let ((buffer (vterm))
        (start-time (current-time)))
    
    (with-current-buffer buffer
      ;; Rapidly resize terminal 100 times
      (dotimes (i 100)
        (vterm--invalidate)
        (vterm--redraw))
      
      (let* ((end-time (current-time))
             (elapsed (float-time (time-subtract end-time start-time))))
        
        (message "100 resize/redraw cycles: %.2f seconds" elapsed)
        (message "Average per cycle: %.4f seconds" (/ elapsed 100))
        
        ;; Cleanup
        (kill-buffer buffer)
        
        (list :time elapsed :avg (/ elapsed 100))))))

(defun vterm-bench--directory-tracking ()
  "Benchmark directory tracking with caching."
  (message "\n=== Directory Tracking Benchmark ===")
  (let ((buffer (vterm))
        (test-dirs '("/tmp" "/usr/local" "/var" "/etc" "/home"))
        (start-time (current-time)))
    
    (with-current-buffer buffer
      ;; Change directory 1000 times (should hit cache on all platforms)
      (dotimes (i 200)
        (dolist (dir test-dirs)
          (vterm-send-string (format "cd %s\n" dir))
          (sit-for 0.01)))
      
      (let* ((end-time (current-time))
             (elapsed (float-time (time-subtract end-time start-time))))
        
        (message "1000 directory changes: %.2f seconds" elapsed)
        (message "Average per change: %.4f seconds" (/ elapsed 1000))
        (when vterm--directory-cache
          (message "Cache size: %d entries" (length vterm--directory-cache)))
        
        ;; Cleanup
        (kill-buffer buffer)
        
        (list :time elapsed :avg (/ elapsed 1000))))))

(defun vterm-bench-run-all ()
  "Run all benchmarks."
  (interactive)
  (message "\n========================================")
  (message "vterm Performance Benchmarks")
  (message "System: %s" system-type)
  (message "Timer delay: %.2fs" vterm-timer-delay)
  (message "========================================\n")
  
  (let ((results nil))
    (push (cons 'memory (vterm-bench--memory-allocation)) results)
    (push (cons 'resize (vterm-bench--rapid-resize)) results)
    (push (cons 'directory (vterm-bench--directory-tracking)) results)
    
    (message "\n========================================")
    (message "Benchmark Complete!")
    (message "========================================")
    
    results))

(provide 'bench-memory)
;;; bench-memory.el ends here
