@echo off
REM Simple test to capture vterm profiling output
SET EMACS_PATH=c:\ProgramData\scoop\apps\emacs-k\current\bin\emacs.exe
SET VTERM_PATH=C:\Users\kienn\.cache\quelpa\build\vterm
SET OUTPUT_FILE=%VTERM_PATH%\benchmark-results\profile-stderr.txt

echo Running Emacs with vterm profiling...
echo Output will be in: %OUTPUT_FILE%
echo.
echo Instructions:
echo 1. Emacs will open
echo 2. Run M-x vterm
echo 3. Do some terminal commands
echo 4. Close vterm buffer (kills shell)
echo 5. Close Emacs
echo 6. Check %OUTPUT_FILE% for profiling data
echo.
pause

"%EMACS_PATH%" -Q --load "%VTERM_PATH%\vterm.el" 2> "%OUTPUT_FILE%"

echo.
echo Done! Check %OUTPUT_FILE%
pause
