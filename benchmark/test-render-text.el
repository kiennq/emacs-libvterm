;;; test-render-text.el --- Test render_text profiling
;;; 
;;; Usage: M-x load-file RET C:/Users/kienn/.cache/quelpa/build/vterm/benchmark/test-render-text.el RET
;;; 
;;; This will:
;;;   1. Run vterm with some commands
;;;   2. Generate profile data with render_text timing
;;;   3. Display the results

(require 'vterm)

(defun test-render-text-profile ()
  "Test render_text profiling."
  (interactive)
  (message "\n=== Starting render_text profiling test ===\n")
  
  ;; Run a single comprehensive test
  (message "Creating vterm buffer...")
  (let ((buf (vterm)))
    (sleep-for 1)  ;; Wait for shell to initialize
    
    (message "Running test commands...")
    (with-current-buffer buf
      ;; Generate varied output to trigger render_text
      (vterm-send-string "echo === Test 1: Simple output ===\n")
      (sleep-for 0.3)
      
      (vterm-send-string "dir\n")  ;; List files
      (sleep-for 0.5)
      
      (vterm-send-string "echo === Test 2: Multi-line ===\n")
      (sleep-for 0.2)
      
      (vterm-send-string "echo Line 1 && echo Line 2 && echo Line 3\n")
      (sleep-for 0.3)
      
      (vterm-send-string "echo === Test 3: Color test ===\n")
      (sleep-for 0.2)
      
      ;; Generate colored output (git status is good for this)
      (vterm-send-string "git status\n")
      (sleep-for 0.5)
      
      (vterm-send-string "echo === Test complete ===\n")
      (sleep-for 0.3))
    
    (message "Collecting profile data...")
    (vterm--print-profile)
    
    (message "Cleaning up...")
    (kill-buffer buf)
    (sleep-for 0.2))
  
  ;; Display results
  (let ((profile-file "C:/Users/kienn/.cache/vterm/vterm-profile.txt"))
    (if (file-exists-p profile-file)
        (progn
          (find-file profile-file)
          (goto-char (point-min))
          (message "\n=== Profile results displayed ===\n")
          (message "Check buffer: vterm-profile.txt")
          (message "Look for 'render_text' timing data"))
      (message "ERROR: Profile file not found at %s" profile-file))))

;; Auto-run
(test-render-text-profile)
