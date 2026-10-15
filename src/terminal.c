#include "terminal.h"

/* Matching a window class is the reliable half. A console window's class is
   set by the host that draws it, and the host is what decides what Ctrl+C
   does, so this is the signal that actually answers the question. */
static const wchar_t *const kTerminalClassNames[] = {
    L"ConsoleWindowClass",            /* legacy conhost (cmd.exe, old PowerShell host) */
    L"CASCADIA_HOSTING_WINDOW_CLASS", /* Windows Terminal */
    L"VirtualConsoleClass",           /* some conhost variants */
    L"mintty",                        /* Git Bash / Cygwin / MSYS2 */
    L"ConEmuMainClass",               /* ConEmu */
    L"VxTermWndClass",                /* various xterm-alikes ported to Windows */
    NULL
};

/* Matching the process image is the fallback, for terminals that draw their
   own window with a class name of their own choosing -- Hyper and the other
   Electron front-ends, mostly.
 *
 * cmd.exe and powershell.exe are on this list for completeness rather than
 * because it is how they are recognised: the window a console application
 * appears in belongs to the console host process, not to the command
 * interpreter, so those two are matched by ConsoleWindowClass above long
 * before the image name is looked at. conhost.exe and openconsole.exe are
 * here because they are what the image name actually is on that path. */
static const wchar_t *const kTerminalProcessNames[] = {
    L"cmd.exe",
    L"powershell.exe",
    L"pwsh.exe",
    L"windowsterminal.exe",
    L"wt.exe",
    L"openconsole.exe",
    L"conhost.exe",
    L"conemu64.exe",
    L"conemu.exe",
    L"mintty.exe",
    L"hyper.exe",
    L"wsl.exe",
    L"wslhost.exe",
    NULL
};

BOOL termEqualsIgnoreCase(const wchar_t *a, const wchar_t *b) {
    if (a == NULL || b == NULL) return FALSE;
    /* CompareStringOrdinal rather than _wcsicmp: the case folding here has to
       be a property of the two strings and nothing else. _wcsicmp folds
       through the CRT's current locale, so the answer for "MINTTY" against
       "mintty" depends on whether anything, anywhere in the process, has
       called setlocale -- under a Turkish locale I does not fold to i. */
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

const wchar_t *termBaseName(const wchar_t *path) {
    if (path == NULL) return NULL;

    const wchar_t *name = path;
    for (const wchar_t *p = path; *p; p++) {
        if (*p == L'\\' || *p == L'/') name = p + 1;
    }
    return name;
}

BOOL termClassIsTerminal(const wchar_t *className) {
    if (className == NULL || className[0] == L'\0') return FALSE;

    for (int i = 0; kTerminalClassNames[i]; i++) {
        if (termEqualsIgnoreCase(className, kTerminalClassNames[i])) return TRUE;
    }
    return FALSE;
}

BOOL termImageIsTerminal(const wchar_t *imagePath) {
    if (imagePath == NULL || imagePath[0] == L'\0') return FALSE;

    const wchar_t *name = termBaseName(imagePath);
    for (int i = 0; kTerminalProcessNames[i]; i++) {
        if (termEqualsIgnoreCase(name, kTerminalProcessNames[i])) return TRUE;
    }
    return FALSE;
}

BOOL termIsTerminal(const wchar_t *className, const wchar_t *imagePath) {
    return termClassIsTerminal(className) || termImageIsTerminal(imagePath);
}
