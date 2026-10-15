@echo off
REM Builds IntelligentCopyPaste.exe with MinGW-w64 (gcc on PATH).
REM Run from the repository root:  src\build.bat
REM
REM windres first: app.rc embeds app.manifest, which is what asks for
REM Common Controls v6 and so decides whether the group box and the preview
REM text are themed or look like Windows 95.

windres app.rc -O coff -o app_res.o
if %ERRORLEVEL% NEQ 0 (
    echo Resource build failed.
    exit /b 1
)

REM -municode selects the wide entry point, wWinMain.
REM -mwindows builds a GUI application, with no console window.
gcc -std=c11 -Wall -Wextra -municode -mwindows -o IntelligentCopyPaste.exe ^
    src\intelligent_copy_paste.c ^
    src\terminal.c src\preview.c src\keys.c src\target.c src\tray.c ^
    app_res.o ^
    -luser32 -lgdi32 -lcomctl32 -lshell32 -ladvapi32 -static
if %ERRORLEVEL% NEQ 0 (
    echo Build failed.
    exit /b 1
)

echo Built IntelligentCopyPaste.exe
exit /b 0
