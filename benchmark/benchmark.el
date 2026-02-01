;;; benchmark.el --- Vterm performance benchmark -*- lexical-binding: t; -*-

;;; Commentary:
;; Benchmark script for measuring vterm rendering performance
;; Tests various scenarios: large output, scrolling, color changes, etc.

;;; Code:

(require 'vterm)
(require 'benchmark)

(defvar vterm-benchmark-results nil
  "List of benchmark results.")

(defun vterm-benchmark--generate-test-data (lines cols with-colors)
  "Generate test data with LINES rows and COLS columns.
If WITH-COLORS is non-nil, include ANSI color codes."
  (let ((data ""))
    (dotimes (i lines)
      (when with-colors
        (setq data (concat data (format "\033[%dm" (+ 31 (mod i 7))))))
      (dotimes (j cols)
        (setq data (concat data (char-to-string (+ 65 (mod j 26))))))
      (when with-colors
        (setq data (concat data "\033[0m")))
      (setq data (concat data "\n")))
    data))

(defun vterm-benchmark--run-test (name test-fn)
  "Run benchmark TEST-FN with NAME and record results."
  (message "Running benchmark: %s" name)
  (let* ((result (benchmark-run 1 (funcall test-fn)))
         (time (car result))
         (gcs (nth 1 result))
         (gc-time (nth 2 result)))
    (push (list name time gcs gc-time) vterm-benchmark-results)
    (message "  Time: %.3fs  GCs: %d  GC time: %.3fs" time gcs gc-time)
    result))

(defun vterm-benchmark-large-output ()
  "Test: Large text output (1000 lines x 80 cols, no colors)."
  (let ((buf (vterm)))
    (with-current-buffer buf
      (vterm-send-string (vterm-benchmark--generate-test-data 1000 80 nil))
      (sit-for 0.1)
      (while (accept-process-output (get-buffer-process buf) 0.1))
      (kill-buffer buf))))

(defun vterm-benchmark-large-output-colors ()
  "Test: Large text output with ANSI colors (1000 lines x 80 cols)."
  (let ((buf (vterm)))
    (with-current-buffer buf
      (vterm-send-string (vterm-benchmark--generate-test-data 1000 80 t))
      (sit-for 0.1)
      (while (accept-process-output (get-buffer-process buf) 0.1))
      (kill-buffer buf))))

(defun vterm-benchmark-rapid-updates ()
  "Test: Rapid small updates (100 iterations)."
  (let ((buf (vterm)))
    (with-current-buffer buf
      (dotimes (i 100)
        (vterm-send-string (format "Line %d\n" i))
        (accept-process-output (get-buffer-process buf) 0.01))
      (kill-buffer buf))))

(defun vterm-benchmark-scrolling ()
  "Test: Scrolling performance (500 lines)."
  (let ((buf (vterm)))
    (with-current-buffer buf
      (vterm-send-string "seq 1 500\n")
      (sit-for 0.1)
      (while (accept-process-output (get-buffer-process buf) 0.1))
      (kill-buffer buf))))

(defun vterm-benchmark-wide-output ()
  "Test: Very wide lines (100 lines x 200 cols)."
  (let ((buf (vterm)))
    (with-current-buffer buf
      (vterm-send-string (vterm-benchmark--generate-test-data 100 200 nil))
      (sit-for 0.1)
      (while (accept-process-output (get-buffer-process buf) 0.1))
      (kill-buffer buf))))

(defun vterm-benchmark-mixed-content ()
  "Test: Mixed content with escape sequences."
  (let ((buf (vterm)))
    (with-current-buffer buf
      (vterm-send-string "ls -lah /usr/bin\n")
      (sit-for 0.5)
      (while (accept-process-output (get-buffer-process buf) 0.1))
      (kill-buffer buf))))

(defun vterm-benchmark-run-all ()
  "Run all benchmarks and print results."
  (interactive)
  (setq vterm-benchmark-results nil)
  (message "=== Vterm Performance Benchmark ===")
  (message "")
  
  (vterm-benchmark--run-test "Large output (plain)" #'vterm-benchmark-large-output)
  (vterm-benchmark--run-test "Large output (colors)" #'vterm-benchmark-large-output-colors)
  (vterm-benchmark--run-test "Rapid updates" #'vterm-benchmark-rapid-updates)
  (vterm-benchmark--run-test "Scrolling" #'vterm-benchmark-scrolling)
  (vterm-benchmark--run-test "Wide lines" #'vterm-benchmark-wide-output)
  (vterm-benchmark--run-test "Mixed content" #'vterm-benchmark-mixed-content)
  
  (message "")
  (message "=== Summary ===")
  (message "%-30s %10s %6s %10s" "Test" "Time (s)" "GCs" "GC Time (s)")
  (message "%s" (make-string 60 ?-))
  (dolist (result (reverse vterm-benchmark-results))
    (message "%-30s %10.3f %6d %10.3f"
             (nth 0 result)
             (nth 1 result)
             (nth 2 result)
             (nth 3 result)))
  
  (let ((total-time (cl-reduce #'+ vterm-benchmark-results :key #'cadr)))
    (message "%s" (make-string 60 ?-))
    (message "%-30s %10.3f" "Total" total-time))
  
  (message "")
  (message "Results saved to: %s" (expand-file-name "benchmark-results.txt" default-directory))
  (with-temp-file (expand-file-name "benchmark-results.txt" default-directory)
    (insert (format "Vterm Benchmark Results - %s\n" (current-time-string)))
    (insert (make-string 60 ?=) "\n\n")
    (insert (format "%-30s %10s %6s %10s\n" "Test" "Time (s)" "GCs" "GC Time (s)"))
    (insert (make-string 60 ?-) "\n")
    (dolist (result (reverse vterm-benchmark-results))
      (insert (format "%-30s %10.3f %6d %10.3f\n"
                      (nth 0 result)
                      (nth 1 result)
                      (nth 2 result)
                      (nth 3 result))))
    (insert (make-string 60 ?-) "\n")
    (insert (format "%-30s %10.3f\n" "Total" (cl-reduce #'+ vterm-benchmark-results :key #'cadr)))))

(provide 'vterm-benchmark)
;;; benchmark.el ends here
