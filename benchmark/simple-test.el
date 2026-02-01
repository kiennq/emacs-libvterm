;;; Run this in Emacs to test the optimized version
;;; M-x load-file RET C:/Users/kienn/.cache/quelpa/build/vterm/benchmark/simple-test.el RET

(require 'vterm)

(defun vterm-simple-benchmark ()
  "Simple benchmark - run commands and print profile."
  (interactive)
  (message "Starting benchmark...")
  
  ;; Run test 3 times
  (dotimes (i 3)
    (message "Iteration %d/3" (1+ i))
    (let ((buf (vterm)))
      (sleep-for 1)
      (with-current-buffer buf
        (vterm-send-string "echo Test 1\n")
        (sleep-for 0.2)
        (vterm-send-string "dir\n")
        (sleep-for 0.3)
        (vterm-send-string "echo Test 2\n")
        (sleep-for 0.2))
      (kill-buffer buf)
      (sleep-for 0.5)))
  
  ;; Print profile
  (message "Collecting profile...")
  (vterm--print-profile)
  
  ;; Display results
  (let ((profile-file "C:/Users/kienn/.cache/vterm/vterm-profile.txt"))
    (when (file-exists-p profile-file)
      (find-file profile-file)
      (message "Profile displayed in buffer"))))

;; Auto-run
(vterm-simple-benchmark)
