;;; quick-test.el --- Quick profiling test for vterm -*- lexical-binding: t; -*-

;; Simple test to verify profiling instrumentation works

(require 'vterm)

(defun vterm-quick-test ()
  "Run a quick vterm test and exit."
  (interactive)
  (let ((buf (vterm)))
    (with-current-buffer buf
      ;; Send some test commands
      (vterm-send-string "echo 'Test 1'\n")
      (sleep-for 0.5)
      (vterm-send-string "echo 'Test 2'\n")
      (sleep-for 0.5)
      (vterm-send-string "echo 'Test 3'\n")
      (sleep-for 0.5)
      
      ;; Exit shell to close vterm (this triggers profile_print_stats)
      (vterm-send-string "exit\n")
      (sleep-for 1)
      
      ;; Force kill if still alive
      (when (process-live-p (get-buffer-process buf))
        (kill-buffer buf))))
  
  ;; Exit Emacs
  (sleep-for 1)
  (kill-emacs 0))

;; Auto-run when loaded
(add-hook 'after-init-hook 'vterm-quick-test)

(provide 'quick-test)
