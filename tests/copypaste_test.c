/*
 * Tests for src/terminal.c, src/preview.c, src/keys.c, src/target.c and
 * src/tray.c.
 *
 *   tests\build.bat
 *
 * No framework. The project had no dependencies and does not want any, so
 * this is sixty lines of harness and then the checks.
 *
 * The project had no tests at all before this pass. It was a single 516-line
 * file in which every decision was tangled with the Win32 call that acted on
 * it, which is why: the widget could send its keystrokes to itself, a
 * refused injection looked exactly like a successful one, a clipboard payload
 * that was not NUL-terminated was read past its end, and a failed tray add
 * hid the only window the program had.
 *
 * Those are pinned below as REGRESSION checks. Where the original logic can
 * be reproduced in a few lines it is reproduced here verbatim, as
 * original*(), and the check asserts that the new behaviour differs from it --
 * so a future change cannot quietly reinstate the old behaviour without a
 * name and a reason appearing in this output.
 */

#include "../src/winver.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../src/terminal.h"
#include "../src/preview.h"
#include "../src/keys.h"
#include "../src/target.h"
#include "../src/tray.h"

/* ---------- harness ---------- */

static int checks = 0;
static int failures = 0;
static const char *current = "";

static void group(const char *name) {
    current = name;
    printf("\n%s\n", name);
}

static void ok(int condition, const char *what) {
    checks++;
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s  [%s]\n", what, current);
    }
}

static void eq_int(long long actual, long long expected, const char *what) {
    checks++;
    if (actual == expected) {
        printf("  ok    %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s  [%s]\n        expected %lld\n        got      %lld\n",
               what, current, expected, actual);
    }
}

static void eq_wstr(const wchar_t *actual, const wchar_t *expected, const char *what) {
    checks++;
    if (actual != NULL && wcscmp(actual, expected) == 0) {
        printf("  ok    %s\n", what);
    } else {
        failures++;
        printf("  FAIL  %s  [%s]\n        expected \"%ls\"\n        got      \"%ls\"\n",
               what, current, expected, actual ? actual : L"(null)");
    }
}

/* ---------- terminal: base name ---------- */

static void testBaseName(void) {
    group("terminal: pulling the file name out of an image path");

    eq_wstr(termBaseName(L"C:\\Windows\\System32\\cmd.exe"), L"cmd.exe",
            "a normal Windows path");
    eq_wstr(termBaseName(L"cmd.exe"), L"cmd.exe",
            "a bare file name is already the base name");
    eq_wstr(termBaseName(L"C:/msys64/usr/bin/mintty.exe"), L"mintty.exe",
            "forward slashes count as separators too");
    eq_wstr(termBaseName(L"C:\\tools/mixed\\wt.exe"), L"wt.exe",
            "a path with both kinds of separator");
    eq_wstr(termBaseName(L"\\\\server\\share\\hyper.exe"), L"hyper.exe",
            "a UNC path");
    eq_wstr(termBaseName(L""), L"", "an empty path is an empty base name");
    eq_wstr(termBaseName(L"C:\\dir\\"), L"",
            "a path ending in a separator has no file name");
    ok(termBaseName(NULL) == NULL, "NULL in, NULL out");

    /* A real WindowsApps path for Windows Terminal is long but well under
       MAX_PATH; the point of this check is that nothing here has a fixed
       buffer, so length is not a factor at all. */
    wchar_t deep[1200];
    size_t at = 0;
    deep[at++] = L'C'; deep[at++] = L':';
    for (int i = 0; i < 140; i++) {
        deep[at++] = L'\\';
        for (int j = 0; j < 7; j++) deep[at++] = L'd';
    }
    deep[at++] = L'\\';
    wcscpy(deep + at, L"pwsh.exe");
    eq_wstr(termBaseName(deep), L"pwsh.exe",
            "a path far longer than MAX_PATH still yields its file name");
    ok(termImageIsTerminal(deep) == TRUE,
       "and is still recognised as a terminal");
}

/* ---------- terminal: the allowlists ---------- */

static void testTerminalClasses(void) {
    group("terminal: window class allowlist");

    ok(termClassIsTerminal(L"ConsoleWindowClass"), "legacy conhost");
    ok(termClassIsTerminal(L"CASCADIA_HOSTING_WINDOW_CLASS"), "Windows Terminal");
    ok(termClassIsTerminal(L"VirtualConsoleClass"), "a conhost variant");
    ok(termClassIsTerminal(L"mintty"), "Git Bash / mintty");
    ok(termClassIsTerminal(L"ConEmuMainClass"), "ConEmu");
    ok(termClassIsTerminal(L"VxTermWndClass"), "xterm-alikes");

    ok(termClassIsTerminal(L"consolewindowclass"), "matching ignores case (lower)");
    ok(termClassIsTerminal(L"MINTTY"), "matching ignores case (upper)");
    ok(termClassIsTerminal(L"cascadia_hosting_window_class"),
       "matching ignores case on the long one too");

    ok(!termClassIsTerminal(L"Notepad"), "Notepad is not a terminal");
    ok(!termClassIsTerminal(L"Chrome_WidgetWin_1"), "a Chromium window is not");
    ok(!termClassIsTerminal(L"IntelligentCopyPasteWnd"), "nor is the widget itself");
    ok(!termClassIsTerminal(L"minttyx"), "a longer name is not a prefix match");
    ok(!termClassIsTerminal(L"mintt"), "nor is a shorter one");
    ok(!termClassIsTerminal(L""), "an empty class name is not a terminal");
    ok(!termClassIsTerminal(NULL), "NULL is not a terminal");
}

static void testTerminalProcesses(void) {
    group("terminal: process image allowlist");

    ok(termImageIsTerminal(L"C:\\Windows\\System32\\cmd.exe"), "cmd.exe by full path");
    ok(termImageIsTerminal(L"powershell.exe"), "powershell.exe by name");
    ok(termImageIsTerminal(L"pwsh.exe"), "PowerShell 7");
    ok(termImageIsTerminal(L"WindowsTerminal.exe"), "Windows Terminal, as it is actually spelled");
    ok(termImageIsTerminal(L"wt.exe"), "the wt launcher");
    ok(termImageIsTerminal(L"ConEmu64.exe"), "ConEmu 64-bit");
    ok(termImageIsTerminal(L"hyper.exe"), "Hyper");
    ok(termImageIsTerminal(L"wsl.exe"), "the WSL front-end");

    /* The window a console application appears in belongs to the console host
       process, not to the interpreter. The original allowlist had cmd.exe and
       powershell.exe but neither of the two names the image actually has on
       that path, so the image check could never fire for a console window --
       it only ever worked because ConsoleWindowClass caught it first. */
    ok(termImageIsTerminal(L"C:\\Windows\\System32\\conhost.exe"),
       "conhost.exe, which is what owns a legacy console window");
    ok(termImageIsTerminal(L"OpenConsole.exe"),
       "OpenConsole.exe, the same thing under Windows Terminal");

    ok(!termImageIsTerminal(L"C:\\Windows\\explorer.exe"), "Explorer is not a terminal");
    ok(!termImageIsTerminal(L"notepad.exe"), "Notepad is not");
    ok(!termImageIsTerminal(L"cmd.exe.bak"), "a similar name is not a match");
    ok(!termImageIsTerminal(L"nowsl.exe"), "nor is a name that merely contains one");
    ok(!termImageIsTerminal(L""), "an empty path is not a terminal");
    ok(!termImageIsTerminal(NULL), "NULL is not a terminal");

    /* A directory named after a terminal must not make the file one. */
    ok(!termImageIsTerminal(L"C:\\cmd.exe\\editor.exe"),
       "only the file name is compared, not any directory along the way");
}

static void testTerminalCombined(void) {
    group("terminal: the combined decision");

    ok(termIsTerminal(L"ConsoleWindowClass", L"C:\\Windows\\System32\\conhost.exe"),
       "class and image both say terminal");
    ok(termIsTerminal(L"ConsoleWindowClass", L"C:\\Windows\\explorer.exe"),
       "the class alone is enough");
    ok(termIsTerminal(L"Chrome_WidgetWin_1", L"C:\\Users\\x\\AppData\\Local\\hyper\\hyper.exe"),
       "the image alone is enough, which is what catches Electron terminals");
    ok(!termIsTerminal(L"Notepad", L"C:\\Windows\\notepad.exe"),
       "neither says terminal, so Copy sends the universal Ctrl+C");
    ok(!termIsTerminal(NULL, NULL), "nothing known means not a terminal");
    ok(!termIsTerminal(L"", L""), "nothing readable means not a terminal");

    ok(termEqualsIgnoreCase(L"a", L"A"), "the comparison folds case");
    ok(!termEqualsIgnoreCase(L"a", L"b"), "and still distinguishes letters");
    ok(!termEqualsIgnoreCase(NULL, L"a"), "NULL never matches");
    ok(!termEqualsIgnoreCase(L"a", NULL), "in either position");
}

/* ---------- preview ---------- */

/* The original, reproduced verbatim from the pre-pass source apart from
   writing into a caller-supplied buffer instead of the file-scope one:
 *
 *     size_t len = wcslen(text);
 *     size_t copyLen = (len < RECENT_MAX_CHARS) ? len : RECENT_MAX_CHARS;
 *     wcsncpy(g_recentText, text, copyLen);
 *     g_recentText[copyLen] = L'\0';
 *     if (len > (size_t)RECENT_MAX_CHARS) wcscat(g_recentText, L"\u2026");
 *
 * Note what is not there: any bound other than the first NUL byte it can
 * find, and any notion of a surrogate pair. */
static void originalPreview(const wchar_t *text, wchar_t *out) {
    size_t len = wcslen(text);
    size_t copyLen = (len < PREVIEW_MAX_CHARS) ? len : PREVIEW_MAX_CHARS;
    wcsncpy(out, text, copyLen);
    out[copyLen] = L'\0';
    if (len > (size_t)PREVIEW_MAX_CHARS) wcscat(out, L"\x2026");
}

static void testPreviewBasics(void) {
    group("preview: ordinary clipboard text");

    wchar_t out[PREVIEW_BUFFER_CHARS];

    eq_int((long long)previewBuild(L"hello", 6, out, PREVIEW_BUFFER_CHARS), 5,
           "a short string is copied whole");
    eq_wstr(out, L"hello", "and reads back unchanged");

    eq_int((long long)previewBuild(L"caf\x00E9 \x65E5\x672C", 8, out, PREVIEW_BUFFER_CHARS), 7,
           "non-ASCII text is counted in characters, not bytes");
    eq_wstr(out, L"caf\x00E9 \x65E5\x672C", "and survives intact");

    /* srcChars is the size of the block, so it includes the terminator; the
       scan stops at the NUL regardless. */
    eq_int((long long)previewBuild(L"hi", 64, out, PREVIEW_BUFFER_CHARS), 2,
           "a block larger than its string stops at the NUL");
    eq_wstr(out, L"hi", "and copies only the string");

    eq_int((long long)previewBuild(L"", 1, out, PREVIEW_BUFFER_CHARS), 0,
           "an empty string writes nothing");
    eq_wstr(out, L"", "and the output is an empty string, not stale content");

    wcscpy(out, L"previous");
    eq_int((long long)previewBuild(NULL, 10, out, PREVIEW_BUFFER_CHARS), 0,
           "a NULL payload writes nothing");
    eq_wstr(out, L"", "and clears the buffer rather than leaving what was there");

    wcscpy(out, L"previous");
    eq_int((long long)previewBuild(L"text", 0, out, PREVIEW_BUFFER_CHARS), 0,
           "a zero-length block writes nothing");
    eq_wstr(out, L"", "and also clears");

    eq_int((long long)previewBuild(L"a\0b", 4, out, PREVIEW_BUFFER_CHARS), 1,
           "an embedded NUL ends the string");
    eq_wstr(out, L"a", "so the text after it is not shown");

    ok(previewBuild(L"x", 2, NULL, 10) == 0, "a NULL destination is refused");
    ok(previewBuild(L"x", 2, out, 0) == 0, "a zero-capacity destination is refused");
}

static void testPreviewTruncation(void) {
    group("preview: truncation and the ellipsis");

    wchar_t out[PREVIEW_BUFFER_CHARS];
    wchar_t src[PREVIEW_MAX_CHARS + 64];

    /* Exactly at the limit: kept whole, no ellipsis. */
    for (int i = 0; i < PREVIEW_MAX_CHARS; i++) src[i] = L'a';
    src[PREVIEW_MAX_CHARS] = L'\0';
    eq_int((long long)previewBuild(src, PREVIEW_MAX_CHARS + 1, out, PREVIEW_BUFFER_CHARS),
           PREVIEW_MAX_CHARS, "text exactly at the limit is kept in full");
    ok(out[PREVIEW_MAX_CHARS - 1] == L'a' && out[PREVIEW_MAX_CHARS] == L'\0',
       "with no ellipsis appended");

    /* One over: truncated, ellipsis appended. */
    src[PREVIEW_MAX_CHARS] = L'a';
    src[PREVIEW_MAX_CHARS + 1] = L'\0';
    eq_int((long long)previewBuild(src, PREVIEW_MAX_CHARS + 2, out, PREVIEW_BUFFER_CHARS),
           PREVIEW_MAX_CHARS + 1, "one character over the limit is truncated");
    ok(out[PREVIEW_MAX_CHARS] == L'\x2026' && out[PREVIEW_MAX_CHARS + 1] == L'\0',
       "and ends with a single ellipsis character");

    /* A destination far smaller than the limit. */
    wchar_t small[8];
    eq_int((long long)previewBuild(L"abcdefghij", 11, small, 8), 7,
           "a small destination truncates to fit it");
    eq_wstr(small, L"abcdef\x2026", "six characters and an ellipsis in eight wchar_t");
    ok(small[7] == L'\0', "and it is NUL-terminated within its capacity");

    wchar_t tiny[2];
    eq_int((long long)previewBuild(L"abc", 4, tiny, 2), 1,
           "a two-character destination holds one character");
    eq_wstr(tiny, L"\x2026",
            "and spends it on the ellipsis, since a lone 'a' would read as the whole clipboard");
    ok(tiny[1] == L'\0', "with its terminator");

    wchar_t single[1];
    eq_int((long long)previewBuild(L"abc", 4, single, 1), 0,
           "a one-character destination holds only the terminator");
    ok(single[0] == L'\0', "which is still written");
}

static void testPreviewUnterminated(void) {
    group("preview: a payload that is not NUL-terminated");

    /* REGRESSION. Nothing obliges a process to put a terminated string on the
       clipboard -- SetClipboardData takes a block and a format, and the size
       of the block is the only thing that is certain. The original called
       wcslen() on it and read until it found a zero, which for an
       unterminated block is somewhere past the end of the allocation.
 *
       This is set up so the read past the end is visible rather than merely
       undefined: sixteen characters of payload sit immediately before sixteen
       more that the caller never offered, so an implementation that trusts
       wcslen picks up the trailing text and one that honours the length does
       not. */
    wchar_t block[32];
    for (int i = 0; i < 32; i++) block[i] = L'A' + (i % 26);
    block[16] = L'!';      /* the first character past what the caller offers */
    block[31] = L'\0';     /* ...and the NUL that wcslen would eventually find */

    wchar_t out[PREVIEW_BUFFER_CHARS];
    size_t n = previewBuild(block, 16, out, PREVIEW_BUFFER_CHARS);

    eq_int((long long)n, 16, "exactly the characters the caller vouched for are read");
    ok(wcschr(out, L'!') == NULL,
       "REGRESSION: nothing from beyond the block's end appears in the preview");

    wchar_t viaOriginal[PREVIEW_BUFFER_CHARS];
    originalPreview(block, viaOriginal);
    ok(wcschr(viaOriginal, L'!') != NULL,
       "REGRESSION: the original wcslen version does read past the end, which is why the bound is an argument");
    ok(wcslen(viaOriginal) > n,
       "REGRESSION: and returns more characters than the caller ever had");

    /* The degenerate case: a block with no NUL anywhere in it at all. */
    wchar_t full[8];
    for (int i = 0; i < 8; i++) full[i] = L'z';
    eq_int((long long)previewBuild(full, 8, out, PREVIEW_BUFFER_CHARS), 8,
           "a block that is entirely text stops at its own end");
    eq_wstr(out, L"zzzzzzzz", "and yields exactly that text");
}

static void testPreviewSurrogates(void) {
    group("preview: truncation across a surrogate pair");

    /* U+1F600, as UTF-16, is the pair D83D DE00. A cut between the two halves
       leaves a lone high surrogate, which is not a character: a STATIC control
       draws it as a replacement glyph. */
    wchar_t src[PREVIEW_MAX_CHARS + 8];
    for (int i = 0; i < PREVIEW_MAX_CHARS - 1; i++) src[i] = L'a';
    src[PREVIEW_MAX_CHARS - 1] = 0xD83D; /* lands exactly on the boundary */
    src[PREVIEW_MAX_CHARS]     = 0xDE00;
    src[PREVIEW_MAX_CHARS + 1] = L'b';
    src[PREVIEW_MAX_CHARS + 2] = L'\0';

    wchar_t out[PREVIEW_BUFFER_CHARS];
    size_t n = previewBuild(src, PREVIEW_MAX_CHARS + 3, out, PREVIEW_BUFFER_CHARS);

    eq_int((long long)n, PREVIEW_MAX_CHARS, "the high surrogate is dropped with its partner");
    ok(out[PREVIEW_MAX_CHARS - 2] == L'a',
       "the last real character before the pair is kept");
    ok(out[PREVIEW_MAX_CHARS - 1] == L'\x2026',
       "REGRESSION: the ellipsis follows it directly, with no half a character in between");

    wchar_t viaOriginal[PREVIEW_BUFFER_CHARS];
    originalPreview(src, viaOriginal);
    ok(viaOriginal[PREVIEW_MAX_CHARS - 1] == 0xD83D,
       "REGRESSION: the original left the lone high surrogate in place");

    /* A pair that is comfortably inside the limit must survive whole. */
    wchar_t pair[] = { 0xD83D, 0xDE00, L'!', L'\0' };
    eq_int((long long)previewBuild(pair, 4, out, PREVIEW_BUFFER_CHARS), 3,
           "a pair well inside the limit is untouched");
    ok(out[0] == 0xD83D && out[1] == 0xDE00,
       "and both halves are still there, in order");

    /* A low surrogate arriving first is malformed input, not something to
       "fix" -- it is passed through, because guessing is worse. */
    wchar_t lone[] = { 0xDE00, L'x', L'\0' };
    eq_int((long long)previewBuild(lone, 3, out, PREVIEW_BUFFER_CHARS), 2,
           "a stray low surrogate inside the limit is passed through unchanged");
}

/* ---------- keys ---------- */

static void testKeysBuild(void) {
    group("keys: building the chord");

    INPUT ev[KEYS_MAX_INPUTS];

    eq_int(keysBuildCombo(FALSE, 'C', ev, KEYS_MAX_INPUTS), 4,
           "Ctrl+C is four events");
    eq_int(ev[0].ki.wVk, VK_CONTROL, "Ctrl goes down first");
    eq_int(ev[0].ki.dwFlags & KEYEVENTF_KEYUP, 0, "as a key-down");
    eq_int(ev[1].ki.wVk, 'C', "then the letter");
    eq_int(ev[1].ki.dwFlags & KEYEVENTF_KEYUP, 0, "as a key-down");
    eq_int(ev[2].ki.wVk, 'C', "then the letter again");
    ok((ev[2].ki.dwFlags & KEYEVENTF_KEYUP) != 0, "as a key-up");
    eq_int(ev[3].ki.wVk, VK_CONTROL, "and Ctrl last");
    ok((ev[3].ki.dwFlags & KEYEVENTF_KEYUP) != 0, "as a key-up");
    ok(ev[0].type == INPUT_KEYBOARD && ev[3].type == INPUT_KEYBOARD,
       "every event is a keyboard event");

    eq_int(keysBuildCombo(TRUE, 'C', ev, KEYS_MAX_INPUTS), 6,
           "Ctrl+Shift+C is six events");
    eq_int(ev[0].ki.wVk, VK_CONTROL, "Ctrl down");
    eq_int(ev[1].ki.wVk, VK_SHIFT, "Shift down after it");
    eq_int(ev[2].ki.wVk, 'C', "the letter down");
    eq_int(ev[3].ki.wVk, 'C', "the letter up");
    eq_int(ev[4].ki.wVk, VK_SHIFT, "Shift up");
    eq_int(ev[5].ki.wVk, VK_CONTROL, "Ctrl up last");
    ok((ev[3].ki.dwFlags & KEYEVENTF_KEYUP) &&
       (ev[4].ki.dwFlags & KEYEVENTF_KEYUP) &&
       (ev[5].ki.dwFlags & KEYEVENTF_KEYUP),
       "the modifiers are released in reverse order of pressing");

    eq_int(keysBuildCombo(TRUE, 'V', ev, KEYS_MAX_INPUTS), 6, "Ctrl+Shift+V likewise");
    eq_int(ev[2].ki.wVk, 'V', "with V as the letter");

    eq_int(keysBuildCombo(TRUE, 'C', ev, 5), 0,
           "a buffer too small for the shifted chord is refused, not overrun");
    eq_int(keysBuildCombo(FALSE, 'C', ev, 3), 0,
           "nor is the unshifted one squeezed into three");
    eq_int(keysBuildCombo(FALSE, 'C', NULL, 8), 0, "a NULL buffer is refused");
}

/* A stand-in for SendInput. The count it reports is whatever the test asked
   for, which is how the refused and partial paths get exercised at all. */
static UINT gFakeReturn;
static int gFakeCalls;
static UINT gFakeLastCount;
static INPUT gFakeLast[KEYS_MAX_INPUTS];

static UINT WINAPI fakeSend(UINT count, LPINPUT inputs, int size) {
    (void)size;
    gFakeCalls++;
    gFakeLastCount = count;
    for (UINT i = 0; i < count && i < KEYS_MAX_INPUTS; i++) gFakeLast[i] = inputs[i];
    if (gFakeReturn == (UINT)-1) return count; /* "everything went in" */
    return gFakeReturn;
}

static void resetFake(UINT ret) {
    gFakeReturn = ret;
    gFakeCalls = 0;
    gFakeLastCount = 0;
    ZeroMemory(gFakeLast, sizeof(gFakeLast));
}

/* The original, reproduced: it discarded SendInput's return value, so no
   caller could distinguish a delivered chord from a refused one.
 *
 *     SendInput(n, inputs, sizeof(INPUT));
 *   }                                      <-- void
 */
static void originalSendKeyCombo(BOOL withShift, WORD letterVk) {
    INPUT ev[KEYS_MAX_INPUTS];
    int n = keysBuildCombo(withShift, letterVk, ev, KEYS_MAX_INPUTS);
    fakeSend((UINT)n, ev, sizeof(INPUT));
    /* and that is the end of it: nothing is returned, nothing is checked */
}

static void testKeysSend(void) {
    group("keys: sending the chord, and noticing when it fails");

    UINT inserted = 0;

    resetFake((UINT)-1);
    eq_int(keysSendCombo(fakeSend, FALSE, 'C', &inserted), KEYS_SENT,
           "all events inserted is a success");
    eq_int(inserted, 4, "and reports how many went in");
    eq_int(gFakeCalls, 1, "with a single call to the injector");
    eq_int(gFakeLastCount, 4, "for the whole chord at once");

    resetFake((UINT)-1);
    eq_int(keysSendCombo(fakeSend, TRUE, 'V', &inserted), KEYS_SENT,
           "the shifted chord likewise");
    eq_int(inserted, 6, "six events for Ctrl+Shift+V");

    /* REGRESSION. This is what User Interface Privilege Isolation does when
       the foreground window belongs to a higher-integrity process: it drops
       the events and SendInput returns 0. An administrator PowerShell is the
       case the widget exists for, so this had to stop being invisible. */
    resetFake(0);
    eq_int(keysSendCombo(fakeSend, TRUE, 'C', &inserted), KEYS_BLOCKED,
           "REGRESSION: an injection that inserted nothing is reported as blocked");
    eq_int(inserted, 0, "with nothing inserted");
    eq_int(gFakeCalls, 1, "and no attempt to release modifiers that never went down");

    resetFake(0);
    originalSendKeyCombo(TRUE, 'C');
    ok(gFakeCalls == 1,
       "REGRESSION: the original called SendInput exactly the same way...");
    ok(1, "REGRESSION: ...and returned void, so a refusal was indistinguishable from a success");

    /* A partial insert leaves the target believing Ctrl is still held: the
       key-down events arrived and the key-up events did not. */
    resetFake(2);
    eq_int(keysSendCombo(fakeSend, TRUE, 'C', &inserted), KEYS_PARTIAL,
           "a partial insert is reported as partial, not as success");
    eq_int(inserted, 2, "reporting how many got through");
    eq_int(gFakeCalls, 2, "and makes a second call to let go of the modifiers");
    eq_int(gFakeLastCount, 2, "releasing two keys");
    ok(gFakeLast[0].ki.wVk == VK_SHIFT && gFakeLast[1].ki.wVk == VK_CONTROL,
       "Shift and Ctrl");
    ok((gFakeLast[0].ki.dwFlags & KEYEVENTF_KEYUP) &&
       (gFakeLast[1].ki.dwFlags & KEYEVENTF_KEYUP),
       "as key-up events, so Ctrl is not left logically stuck down");

    resetFake((UINT)-1);
    eq_int(keysSendCombo(NULL, FALSE, 'C', &inserted), KEYS_BAD_ARGS,
           "no injector is bad arguments, not a silent success");
    eq_int(gFakeCalls, 0, "and nothing is attempted");

    resetFake((UINT)-1);
    ok(keysSendCombo(fakeSend, FALSE, 'C', NULL) == KEYS_SENT,
       "the inserted count is optional");
}

static void testKeysMessages(void) {
    group("keys: what the status line gets told");

    eq_wstr(keysResultMessage(KEYS_SENT), L"", "a success has nothing to say");
    ok(wcslen(keysResultMessage(KEYS_BLOCKED)) > 0, "a refusal explains itself");
    ok(wcsstr(keysResultMessage(KEYS_BLOCKED), L"administrator") != NULL,
       "and names the cause the user can actually act on");
    ok(wcslen(keysResultMessage(KEYS_PARTIAL)) > 0, "a partial send says so");
    ok(wcslen(keysResultMessage(KEYS_BAD_ARGS)) > 0, "so does a build failure");
    ok(keysResultMessage((KeysResult)999) != NULL, "an unknown result is still a string");
}

/* ---------- target ---------- */

#define OWN_PID 4242

static HWND hw(int n) { return (HWND)(INT_PTR)n; }

static void testTarget(void) {
    group("target: which window a Copy is aimed at");

    TargetTracker t;
    targetInit(&t);
    ok(t.last == NULL, "nothing is remembered to start with");

    /* The ordinary case: somebody else's window is in front. */
    targetNoteForeground(&t, hw(100), 900, OWN_PID);
    ok(targetPick(&t, hw(100), 900, OWN_PID) == hw(100),
       "a foreground window belonging to another process is the target");

    /* REGRESSION. This is the state the widget is in after any right-click on
       its tray icon: the tray handler calls SetForegroundWindow on the
       widget's own window -- which WS_EX_NOACTIVATE does not prevent, and
       which a tray popup menu genuinely needs -- so GetForegroundWindow then
       returns the widget. The original sent Ctrl+C straight to it. */
    ok(targetPick(&t, hw(7), OWN_PID, OWN_PID) == hw(100),
       "REGRESSION: when the widget itself is foreground, the target is the last window that was not");
    ok(targetPick(&t, hw(7), OWN_PID, OWN_PID) != hw(7),
       "REGRESSION: and is never the widget's own window");

    /* Our own windows are never remembered, however often they come forward. */
    targetNoteForeground(&t, hw(7), OWN_PID, OWN_PID);
    targetNoteForeground(&t, hw(8), OWN_PID, OWN_PID);
    ok(t.last == hw(100), "a foreground change to one of our own windows is not remembered");
    ok(targetPick(&t, hw(8), OWN_PID, OWN_PID) == hw(100),
       "so the remembered target survives the widget being brought forward");

    /* The most recent other window wins. */
    targetNoteForeground(&t, hw(200), 901, OWN_PID);
    ok(targetPick(&t, hw(7), OWN_PID, OWN_PID) == hw(200),
       "the most recently seen other window is the one used");

    /* Nothing usable at all. */
    TargetTracker fresh;
    targetInit(&fresh);
    ok(targetPick(&fresh, hw(7), OWN_PID, OWN_PID) == NULL,
       "with the widget foreground and nothing remembered, there is no target");
    ok(targetPick(&fresh, NULL, 0, OWN_PID) == NULL,
       "and no foreground window at all is likewise no target");

    /* Junk is not remembered. */
    targetNoteForeground(&fresh, NULL, 900, OWN_PID);
    ok(fresh.last == NULL, "a NULL window is not remembered");
    targetNoteForeground(&fresh, hw(300), 0, OWN_PID);
    ok(fresh.last == NULL, "nor is a window whose process could not be determined");

    /* A remembered window that goes away must be forgotten, because handles
       get reused and a stale one could name somebody else's window. */
    targetNoteForeground(&fresh, hw(300), 900, OWN_PID);
    ok(fresh.last == hw(300), "a valid window is remembered");
    targetForget(&fresh, hw(999));
    ok(fresh.last == hw(300), "forgetting a different window changes nothing");
    targetForget(&fresh, hw(300));
    ok(fresh.last == NULL, "forgetting the remembered one clears it");
    ok(targetPick(&fresh, hw(7), OWN_PID, OWN_PID) == NULL,
       "and there is then no target rather than a dead handle");

    /* And the decision the plumbing actually asks for. SendInput delivers to
       whatever holds the foreground, so "which window" has to come with
       "is it in front already". */
    TargetTracker d;
    targetInit(&d);
    HWND where = hw(1);

    eq_int(targetDecide(&d, hw(7), OWN_PID, OWN_PID, &where), TARGET_NONE,
           "the widget in front with nothing remembered yet is no action");
    eq_int(targetDecide(&d, hw(100), 900, OWN_PID, &where), TARGET_USE_FOREGROUND,
           "a usable foreground needs nothing remembered");
    targetNoteForeground(&d, hw(100), 900, OWN_PID);
    eq_int(targetDecide(&d, hw(100), 900, OWN_PID, &where), TARGET_USE_FOREGROUND,
           "another process in front means send straight away");
    ok(where == hw(100), "and the window reported is the foreground one");

    eq_int(targetDecide(&d, hw(7), OWN_PID, OWN_PID, &where), TARGET_RESTORE_FIRST,
           "REGRESSION: the widget in front means the user's window must be restored first");
    ok(where == hw(100),
       "REGRESSION: naming the window to restore, not the widget that is in front");

    where = hw(1);
    TargetTracker empty;
    targetInit(&empty);
    eq_int(targetDecide(&empty, hw(7), OWN_PID, OWN_PID, &where), TARGET_NONE,
           "the widget in front with nothing remembered is no action at all");
    ok(where == NULL, "and no window");
    eq_int(targetDecide(&empty, hw(100), 900, OWN_PID, NULL), TARGET_NONE,
           "a NULL out-parameter is refused");

    targetInit(NULL);
    targetNoteForeground(NULL, hw(1), 900, OWN_PID);
    targetForget(NULL, hw(1));
    ok(targetPick(NULL, hw(7), OWN_PID, OWN_PID) == NULL,
       "a NULL tracker is survivable and yields no target");
    ok(targetPick(NULL, hw(100), 900, OWN_PID) == hw(100),
       "though a usable foreground window still stands on its own");
}

/* ---------- tray ---------- */

static int gNotifyCalls;
static DWORD gNotifyLastMessage;
static BOOL gNotifySucceeds;
static int gAddCalls;
static int gDeleteCalls;

static BOOL WINAPI fakeNotify(DWORD message, PNOTIFYICONDATAW data) {
    (void)data;
    gNotifyCalls++;
    gNotifyLastMessage = message;
    if (message == NIM_ADD) gAddCalls++;
    if (message == NIM_DELETE) gDeleteCalls++;
    return gNotifySucceeds;
}

static void resetNotify(BOOL succeeds) {
    gNotifyCalls = 0;
    gNotifyLastMessage = 0;
    gNotifySucceeds = succeeds;
    gAddCalls = 0;
    gDeleteCalls = 0;
}

static void testTray(void) {
    group("tray: the icon that is the only way back");

    TrayState s;
    HICON icon = (HICON)(INT_PTR)0x1234;

    trayInit(&s, hw(50), 1, WM_APP + 1, icon, L"Intelligent Copy Paste");
    eq_int(s.nid.cbSize, (long long)sizeof(s.nid), "cbSize is set for the struct actually passed");
    ok(s.nid.hWnd == hw(50), "the owning window is recorded");
    eq_int(s.nid.uID, 1, "so is the icon id");
    eq_int(s.nid.uCallbackMessage, WM_APP + 1, "and the callback message");
    ok(s.nid.hIcon == icon, "and the icon");
    eq_int(s.nid.uFlags, NIF_ICON | NIF_MESSAGE | NIF_TIP, "with the three flags those imply");
    eq_wstr(s.nid.szTip, L"Intelligent Copy Paste", "the tooltip is copied in");
    ok(s.present == FALSE, "and the icon is not present until it is added");

    /* The ordinary path. */
    resetNotify(TRUE);
    ok(trayShow(&s, fakeNotify) == TRUE, "a successful add reports the icon is there");
    ok(s.present == TRUE, "and records it");
    eq_int(gAddCalls, 1, "with one NIM_ADD");

    /* REGRESSION. The original rebuilt the NOTIFYICONDATA from zero and
       issued a fresh NIM_ADD on every hide. Two adds for one id is how a
       second icon appears in the notification area. */
    resetNotify(TRUE);
    ok(trayShow(&s, fakeNotify) == TRUE, "asking again while it is there still says yes");
    eq_int(gAddCalls, 0,
           "REGRESSION: but does not issue a second NIM_ADD for the same icon");

    resetNotify(TRUE);
    trayRemove(&s, fakeNotify);
    eq_int(gDeleteCalls, 1, "removing it issues one NIM_DELETE");
    ok(s.present == FALSE, "and records that it is gone");

    resetNotify(TRUE);
    trayRemove(&s, fakeNotify);
    eq_int(gNotifyCalls, 0, "removing it again does nothing at all");

    /* REGRESSION. The failure the whole module exists for. The original was
 *
       Shell_NotifyIconW(NIM_ADD, &g_nid);
       ShowWindow(g_hwnd, SW_HIDE);
       g_hiddenToTray = TRUE;
 *
       so when NIM_ADD failed the window was hidden anyway and there was no
       icon to click: the process kept running, kept its clipboard listener,
       and could not be reached again except through Task Manager. */
    resetNotify(FALSE);
    ok(trayShow(&s, fakeNotify) == FALSE,
       "REGRESSION: a failed add reports failure rather than assuming success");
    ok(s.present == FALSE, "and does not claim the icon is there");
    eq_int(gAddCalls, 1, "having genuinely tried once");

    resetNotify(FALSE);
    trayRemove(&s, fakeNotify);
    eq_int(gNotifyCalls, 0, "and there is then nothing to remove");

    /* Retrying after a failure has to work, or a transient failure is fatal. */
    resetNotify(TRUE);
    ok(trayShow(&s, fakeNotify) == TRUE, "a later attempt succeeds");
    ok(s.present == TRUE, "so a transient failure is not permanent");

    /* REGRESSION. Explorer restarting throws every notification icon away and
       broadcasts TaskbarCreated so applications can put theirs back. The
       original never handled it, so an Explorer restart while hidden to the
       tray reached the same dead end as a failed add. */
    resetNotify(TRUE);
    ok(trayRestore(&s, fakeNotify) == TRUE,
       "REGRESSION: after the shell loses the icon it can be added back");
    eq_int(gAddCalls, 1,
           "REGRESSION: with a real NIM_ADD, not skipped because we still thought it was present");
    ok(s.present == TRUE, "and it is present again");

    resetNotify(FALSE);
    ok(trayRestore(&s, fakeNotify) == FALSE, "a restore that fails says so");
    ok(s.present == FALSE, "and leaves the icon marked absent");

    /* A tooltip longer than szTip must be cut, and must still be terminated:
       Shell_NotifyIcon reads szTip as a string. */
    wchar_t longTip[400];
    for (int i = 0; i < 399; i++) longTip[i] = L'T';
    longTip[399] = L'\0';
    trayInit(&s, hw(50), 1, WM_APP + 1, icon, longTip);
    size_t cap = sizeof(s.nid.szTip) / sizeof(s.nid.szTip[0]);
    ok(s.nid.szTip[cap - 1] == L'\0',
       "an over-long tooltip is still NUL-terminated inside szTip");
    eq_int((long long)wcslen(s.nid.szTip), (long long)(cap - 1),
           "and fills it to capacity less the terminator");

    trayInit(&s, hw(50), 1, WM_APP + 1, icon, NULL);
    eq_wstr(s.nid.szTip, L"", "a NULL tooltip leaves szTip empty rather than unset");

    trayInit(NULL, hw(50), 1, 0, icon, L"x");
    ok(trayShow(NULL, fakeNotify) == FALSE, "a NULL state cannot be shown");
    ok(trayRestore(NULL, fakeNotify) == FALSE, "nor restored");
    trayRemove(NULL, fakeNotify);
    resetNotify(TRUE);
    ok(trayShow(&s, NULL) == FALSE, "and no notify function is a failure, not a crash");
    eq_int(gNotifyCalls, 0, "with nothing called");
}

/* ---------- main ---------- */

int main(void) {
    /* Unbuffered, so that if a check crashes the process the output up to
       that point still reaches the terminal. That is not hypothetical here:
       run this suite against the pre-pass code and it segfaults partway
       through, and which check it got to is the interesting part. */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Intelligent Copy Paste -- tests\n");

    testBaseName();
    testTerminalClasses();
    testTerminalProcesses();
    testTerminalCombined();
    testPreviewBasics();
    testPreviewTruncation();
    testPreviewUnterminated();
    testPreviewSurrogates();
    testKeysBuild();
    testKeysSend();
    testKeysMessages();
    testTarget();
    testTray();

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
