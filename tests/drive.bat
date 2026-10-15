@echo off
REM Drives the built IntelligentCopyPaste.exe end to end: launches it, finds
REM its window, changes the clipboard from outside and reads the preview back.
REM Run from the repository root:  tests\drive.bat
REM
REM NOT part of tests\build.bat. This REPLACES THE CLIPBOARD several times and
REM leaves the last value on it, so it is opt-in.
REM
REM Build the executable first:  src\build.bat

if not exist IntelligentCopyPaste.exe (
    echo IntelligentCopyPaste.exe not found. Run src\build.bat first.
    exit /b 1
)

gcc -std=c11 -Wall -Wextra -o tests\drive_test.exe tests\drive_test.c -luser32
if %ERRORLEVEL% NEQ 0 (
    echo Test build failed.
    exit /b 1
)

REM An absolute path: the harness launches this with CreateProcessW, whose
REM search order starts at the directory drive_test.exe itself loaded from
REM (tests\), not at the current directory.
tests\drive_test.exe "%CD%\IntelligentCopyPaste.exe"
exit /b %ERRORLEVEL%
