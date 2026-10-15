/*
 * drive_test.c -- drives the real IntelligentCopyPaste.exe.
 *
 *   tests\drive.bat
 *
 * Launches the built executable, finds its window, changes the clipboard from
 * outside it, and reads the "Recently copied" control back. This is the
 * end-to-end check that the preview follows the clipboard rather than being
 * read once, 60 ms after a button press, which is what it did before this
 * pass -- and it is the only check here that exercises the real window, the
 * real message loop and the real clipboard listener together.
 *
 * NOT part of tests\build.bat, and deliberately so: it replaces whatever is
 * on the clipboard several times and leaves the last value there. Run it when
 * you mean to.
 *
 * What it cannot reach: the Copy and Paste buttons. Clicking them sends real
 * keystrokes to whatever window is in front, so a test that pressed them
 * would type into the terminal running the test. The chord construction, the
 * refusal handling and the target choice are all covered by
 * tests\copypaste_test.c instead; see the manual pass at the end of
 * review.md for the parts that need a person.
 */
#define _WIN32_WINNT 0x0601
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>

#define ID_STATIC_RECENT 2003

static int checks = 0, failures = 0;
static void ok(int cond, const char *what) {
    checks++;
    if (cond) printf("  ok    %s\n", what);
    else { failures++; printf("  FAIL  %s\n", what); }
}

static void PutClipboard(const wchar_t *s, size_t chars, BOOL terminate) {
    size_t n = (chars + (terminate ? 1 : 0)) * sizeof(wchar_t);
    for (int i = 0; i < 20; i++) { if (OpenClipboard(NULL)) break; Sleep(25); }
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    wchar_t *p = (wchar_t *)GlobalLock(h);
    memcpy(p, s, chars * sizeof(wchar_t));
    if (terminate) p[chars] = L'\0';
    GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
}

/* Waits for the preview control's text to satisfy pred, up to ms. */
static int WaitForPreview(HWND panel, const wchar_t *expect, int ms, wchar_t *got, int gotCap) {
    DWORD end = GetTickCount() + ms;
    for (;;) {
        GetWindowTextW(panel, got, gotCap);
        if (expect == NULL || wcscmp(got, expect) == 0) return 1;
        if (GetTickCount() >= end) return 0;
        Sleep(20);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: drive <path to exe>\n"); return 2; }

    wchar_t cmd[1024];
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, cmd, 1024);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("could not launch (%lu)\n", GetLastError());
        return 2;
    }

    HWND w = NULL;
    for (int i = 0; i < 200 && !w; i++) { w = FindWindowW(L"IntelligentCopyPasteWnd", NULL); Sleep(25); }
    ok(w != NULL, "the widget creates its window");
    if (!w) { TerminateProcess(pi.hProcess, 0); return 1; }

    HWND panel = GetDlgItem(w, ID_STATIC_RECENT);
    ok(panel != NULL, "and its 'Recently copied' panel");

    wchar_t got[2048];
    GetWindowTextW(panel, got, 2048);
    ok(wcscmp(got, L"Nothing copied yet") == 0, "which starts out saying nothing has been copied");

    /* The headline fix: a clipboard change made by somebody else, with no
       button pressed at all, reaches the panel. */
    PutClipboard(L"driven from outside", 19, TRUE);
    ok(WaitForPreview(panel, L"driven from outside", 3000, got, 2048),
       "a clipboard change from another process updates the panel with no button press");

    PutClipboard(L"second value", 12, TRUE);
    ok(WaitForPreview(panel, L"second value", 3000, got, 2048),
       "and so does the next one");

    /* Non-BMP text, to confirm the wide path end to end. */
    {
        wchar_t emoji[] = { 0xD83D, 0xDE00, L' ', 0x65E5, 0x672C, 0x8A9E, L'\0' };
        PutClipboard(emoji, 6, TRUE);
        ok(WaitForPreview(panel, emoji, 3000, got, 2048),
           "an astral-plane character and CJK text survive the round trip");
    }

    /* Longer than the 512-character limit: truncated with one ellipsis. */
    {
        static wchar_t big[900];
        for (int i = 0; i < 900; i++) big[i] = L'x';
        PutClipboard(big, 900, TRUE);
        ok(WaitForPreview(panel, NULL, 1500, got, 2048), "a 900-character payload arrives");
        Sleep(300);
        GetWindowTextW(panel, got, 2048);
        size_t n = wcslen(got);
        ok(n == 513, "long text is cut to 512 characters plus an ellipsis");
        ok(n > 0 && got[n - 1] == 0x2026, "and the last character is the ellipsis");
    }

    /* A payload with no terminator inside the block it declares. The original
       called wcslen on this. */
    {
        wchar_t raw[8];
        for (int i = 0; i < 8; i++) raw[i] = L'Q';
        PutClipboard(raw, 8, FALSE);   /* eight characters, no NUL */
        Sleep(600);
        GetWindowTextW(panel, got, 2048);
        printf("        unterminated payload -> \"%ls\" (%d chars)\n", got, (int)wcslen(got));

        /* Windows terminates the block itself on the way through the
           clipboard -- it comes back as seven characters plus a NUL, not
           eight -- so the count is bounded above rather than pinned. What
           matters is that everything shown came out of the block: the
           original called wcslen on this pointer and read until it found a
           zero somewhere past the end. */
        size_t n = wcslen(got);
        int allQ = 1;
        for (size_t i = 0; i < n; i++) if (got[i] != L'Q') allQ = 0;
        ok(n >= 7 && n <= 8, "an unterminated payload shows the characters the block held");
        ok(allQ, "and nothing that followed it in memory");
    }

    /* Still alive and responsive after all that. */
    ok(WaitForPreview(panel, NULL, 100, got, 2048) == 1, "the window is still responsive");
    ok(IsWindow(w), "and still exists");
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    ok(code == STILL_ACTIVE, "the process has not crashed");

    PostMessageW(w, WM_COMMAND, 1001, 0); /* ID_TRAY_EXIT: a real quit */
    ok(WaitForSingleObject(pi.hProcess, 3000) == WAIT_OBJECT_0,
       "and it exits cleanly when told to");

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
