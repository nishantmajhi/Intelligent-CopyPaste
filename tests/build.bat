@echo off
REM Builds and runs the tests with MinGW-w64 (gcc on PATH).
REM Run from the repository root:  tests\build.bat
REM
REM No -municode here: the test harness has a plain main(), and nothing it
REM links against reads the command line.

gcc -std=c11 -Wall -Wextra -o tests\copypaste_test.exe ^
    tests\copypaste_test.c ^
    src\terminal.c src\preview.c src\keys.c src\target.c src\tray.c ^
    -luser32 -lshell32
if %ERRORLEVEL% NEQ 0 (
    echo Test build failed.
    exit /b 1
)

tests\copypaste_test.exe
exit /b %ERRORLEVEL%
