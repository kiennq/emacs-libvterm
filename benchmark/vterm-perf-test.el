;;; vterm-perf-test.el --- Automated vterm performance test -*- lexical-binding: t; -*-

;; This script runs vterm performance tests and outputs profiling data

(defvar vterm-perf-test-iterations 5
  "Number of test iterations to run.")

(defvar vterm-perf-test-output-file "C:/Users/kienn/.cache/vterm/benchmark-auto.txt"
  "File to write benchmark results.")

(defun vterm-perf-test--run-commands (buf command-list)
  "Run COMMAND-LIST in vterm buffer BUF."
  (with-current-buffer buf
    (dolist (cmd command-list)
      (vterm-send-string cmd)
      (vterm-send-return)
      (sleep-for 0.1))))

(defun vterm-perf-test--run-single-test (test-name commands)
  "Run a single test named TEST-NAME with COMMANDS."
  (message "Running test: %s" test-name)
  (let ((buf (vterm)))
    (sleep-for 1) ; Wait for vterm to initialize
    (vterm-perf-test--run-commands buf commands)
    (sleep-for 0.5) ; Let output settle
    (kill-buffer buf)
    (sleep-for 0.5))) ; Let cleanup happen

(defun vterm-perf-test--test-suite ()
  "Run the full test suite."
  (list
   ;; Test 1: Simple commands
   (list "simple-commands"
         '("echo 'Starting test'"
           "dir"
           "echo 'Test 1'"
           "echo 'Test 2'"
           "echo 'Test 3'"))
   
   ;; Test 2: Large output
   (list "large-output"
         '("echo 'Generating large output...'"
           "dir /s C:\\Windows\\System32 2>nul | findstr /i \".exe\" | findstr /n \".\""))
   
   ;; Test 3: Rapid small commands
   (list "rapid-commands"
         '("echo 1"
           "echo 2"
           "echo 3"
           "echo 4"
           "echo 5"
           "echo 6"
           "echo 7"
           "echo 8"
           "echo 9"
           "echo 10"))))

(defun vterm-perf-test-run ()
  "Run vterm performance tests and collect profiling data."
  (interactive)
  (message "=== Starting Vterm Performance Test ===")
  (message "Iterations: %d" vterm-perf-test-iterations)
  
  ;; Clear previous profile data by creating a fresh vterm and closing it
  (let ((buf (vterm)))
    (sleep-for 0.5)
    (kill-buffer buf))
  
  ;; Run test suite multiple times
  (dotimes (iter vterm-perf-test-iterations)
    (message "\n--- Iteration %d/%d ---" (1+ iter) vterm-perf-test-iterations)
    (dolist (test (vterm-perf-test--test-suite))
      (let ((test-name (nth 0 test))
            (commands (nth 1 test)))
        (vterm-perf-test--run-single-test test-name commands))))
  
  ;; Print profiling stats
  (message "\n=== Collecting profiling data ===")
  (when (fboundp 'vterm--print-profile)
    (vterm--print-profile))
  
  ;; Copy profile to benchmark output
  (let ((profile-file "C:/Users/kienn/.cache/vterm/vterm-profile.txt"))
    (when (file-exists-p profile-file)
      (with-temp-buffer
        (insert-file-contents profile-file)
        (write-region (point-min) (point-max) vterm-perf-test-output-file))
      (message "Profiling data written to: %s" vterm-perf-test-output-file)
      (message "\n=== Results ===")
      (with-temp-buffer
        (insert-file-contents profile-file)
        (message "%s" (buffer-string)))))
  
  (message "\n=== Test Complete ===")
  (sleep-for 1))

;; Auto-run when loaded in batch mode
(when noninteractive
  (vterm-perf-test-run)
  (sleep-for 2)
  (kill-emacs 0))

(provide 'vterm-perf-test)
;;; vterm-perf-test.el ends here
