// Intelligent Copy Paste
//
// A tiny always-on-top Windows widget with two buttons, Copy and Paste:
//
//   - Copy is context-aware. Terminal-ish windows (cmd.exe, PowerShell,
//     Windows Terminal, ConEmu, mintty/Git Bash, WSL front-ends, ...) get
//     Ctrl+Shift+C, the "copy as plain text" chord those apps use, because
//     Ctrl+C in a console means "send SIGINT", not "copy". Everything else
//     gets the universal Ctrl+C, because that is what every other Windows
//     app expects, and fighting it (e.g. sending Ctrl+Shift+C into an app
//     that has that chord bound to something unrelated) does more harm
//     than good.
//   - Paste always sends Ctrl+Shift+V, regardless of what has focus. There
//     is no equivalent conflict on the paste side, so no detection is
//     needed there.
//   - The native minimize box doesn't do a real OS iconify: it toggles a
//     compact view where the "Recently copied" panel hides but Copy/Paste
//     stay visible, and the window just shrinks in place.
//
// This file is the Win32 plumbing: the window, its controls, the message
// loop, and the calls that actually touch the clipboard and the input
// queue. Every decision it makes is made somewhere else, in a file with no
// window handle in it, so that it can be tested:
//
//   src/terminal.c  is this a terminal? (from a class name and an image path)
//   src/preview.c   what goes in the "Recently copied" panel
//   src/keys.c      the Ctrl(+Shift)+letter chord, and whether it was accepted
//   src/target.c    which window the chord should go to
//   src/tray.c      the notification-area icon's presence
//
// See README.md for how to build it.

#include "winver.h"

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string.h>
#include <stdio.h>

#include "terminal.h"
#include "preview.h"
#include "keys.h"
#include "target.h"
#include "tray.h"

// ----------------------------------------------------------------------
// Constants / IDs
// ----------------------------------------------------------------------
#define APP_TITLE           L"Intelligent Copy Paste"
#define WND_CLASS_NAME       L"IntelligentCopyPasteWnd"
#define WM_TRAYICON          (WM_APP + 1)
#define ID_TRAY_EXIT         1001
#define ID_TRAY_RESTORE      1002
#define ID_BTN_COPY          2001
#define ID_BTN_PASTE         2002
#define ID_STATIC_RECENT     2003
#define ID_GROUP_RECENT      2004
#define TRAY_ICON_ID         1

// Client-area layout. The titlebar (with its native minimize/close boxes)
// is entirely native now (drawn by the OS per the current theme), so
// these are just the content below that - no custom chrome to account for.
#define CLIENT_W    260
#define PADDING     14
#define LABEL_H     16
#define PREVIEW_H   96
#define GROUP_H     (LABEL_H + PREVIEW_H)
#define GROUP_INSET 10
#define PREVIEW_VPAD 10
#define ROW_GAP     10
#define BTN_H       56
#define BTN_W       108
#define CLIENT_H          (PADDING + GROUP_H + ROW_GAP + BTN_H + PADDING)
#define CLIENT_H_COMPACT  (PADDING + BTN_H + PADDING)

// Same style/exstyle used both at window creation and when recomputing
// the window rect for the compact/full toggle below. Deliberately NOT
// WS_EX_TOOLWINDOW: that style suppresses the minimize glyph on the
// titlebar even when WS_MINIMIZEBOX is set, which is why it wasn't
// showing up - a plain overlapped window renders both native buttons.
#define WIN_STYLE   (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)
#define WIN_EXSTYLE (WS_EX_TOPMOST | WS_EX_NOACTIVATE)

// ----------------------------------------------------------------------
// Global state
// ----------------------------------------------------------------------
static HWND    g_hwnd = NULL;
static HWND    g_groupRecent = NULL;
static HWND    g_recentPreview = NULL;
static HWND    g_btnCopy = NULL;
static HWND    g_btnPaste = NULL;
static HFONT   g_labelFont = NULL;   // small/bold/underlined - a caption, not body text
static HFONT   g_previewFont = NULL; // regular weight - the actual copied text
static HFONT   g_buttonFont = NULL;  // Copy/Paste button text
static TrayState g_tray;
static BOOL    g_hiddenToTray = FALSE;
static BOOL    g_minimized = FALSE;  // compact view: recent panel hidden, buttons stay
static wchar_t g_recentText[PREVIEW_BUFFER_CHARS] = L"Nothing copied yet";

// Which window a Copy or Paste is for. See src/target.h: the widget can end
// up in the foreground itself, and a chord sent then goes nowhere.
static TargetTracker g_target;
static DWORD   g_ownPid = 0;
static HWINEVENTHOOK g_foregroundHook = NULL;

// Broadcast by the shell when Explorer restarts and every notification icon
// has to be added again. Registered at startup because the value is only
// known at run time.
static UINT    g_taskbarCreatedMsg = 0;

// ----------------------------------------------------------------------
// The "Recently copied" panel, which doubles as the status line
// ----------------------------------------------------------------------
static void ShowRecentText(void) {
    if (g_recentPreview) SetWindowTextW(g_recentPreview, g_recentText);
}

// Puts a message where the clipboard preview goes. Used only for failures
// that would otherwise be silent -- the panel already opens showing
// "Nothing copied yet", so it is a status line as much as a mirror of the
// clipboard, and the next successful copy replaces whatever is here.
static void SetStatus(const wchar_t *message) {
    if (message == NULL || message[0] == L'\0') return;
    wcsncpy(g_recentText, message, PREVIEW_BUFFER_CHARS - 1);
    g_recentText[PREVIEW_BUFFER_CHARS - 1] = L'\0';
    ShowRecentText();
}

// ----------------------------------------------------------------------
// Terminal detection
// ----------------------------------------------------------------------

// The image path of the process owning hwnd, or NULL on failure. Caller frees
// with free().
//
// This grows its buffer rather than assuming MAX_PATH. A path longer than
// that makes QueryFullProcessImageNameW fail with ERROR_INSUFFICIENT_BUFFER,
// and the original treated any failure as "not a terminal" -- so a terminal
// installed somewhere deep (an MSIX package under WindowsApps, a portable
// build inside a nest of project folders) silently got plain Ctrl+C, which in
// a console means interrupt.
static wchar_t *ProcessImagePath(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return NULL;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return NULL;

    wchar_t *path = NULL;
    for (DWORD cap = MAX_PATH; cap <= 32768; cap *= 2) {
        wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t));
        if (!buf) break;

        DWORD len = cap;
        if (QueryFullProcessImageNameW(hProc, 0, buf, &len)) {
            buf[len < cap ? len : cap - 1] = L'\0';
            path = buf;
            break;
        }
        free(buf);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
    }

    CloseHandle(hProc);
    return path;
}

// Returns TRUE if hwnd (or its owning process) looks like a terminal /
// console-hosted application where Ctrl+C means "interrupt", not "copy".
// The allowlists and the matching live in src/terminal.c.
static BOOL IsTerminalWindow(HWND hwnd) {
    if (!hwnd) return FALSE;

    wchar_t className[256] = { 0 };
    GetClassNameW(hwnd, className, ARRAYSIZE(className));
    if (termClassIsTerminal(className)) return TRUE;

    wchar_t *path = ProcessImagePath(hwnd);
    if (!path) return FALSE;
    BOOL isTerminal = termImageIsTerminal(path);
    free(path);
    return isTerminal;
}

// ----------------------------------------------------------------------
// Reading the clipboard for the preview
// ----------------------------------------------------------------------

// OpenClipboard fails with ERROR_ACCESS_DENIED while another window has the
// clipboard open, and the moment this runs is exactly when that is likely:
// the application the user just copied from may still be holding it, and so
// may every other clipboard listener on the machine. The original gave up on
// the first refusal and returned, leaving the panel showing the previous
// clipboard content as though the copy had worked.
static BOOL OpenClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 10; attempt++) {
        if (OpenClipboard(owner)) return TRUE;
        Sleep(15);
    }
    return FALSE;
}

// Pull clipboard text into the "Recently copied" panel.
static void UpdateRecentTextFromClipboard(void) {
    if (!OpenClipboardWithRetry(g_hwnd)) {
        SetStatus(L"Could not read the clipboard: another program is holding it open.");
        return;
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        // CF_TEXT needs no separate path: Windows synthesises CF_UNICODETEXT
        // from it, so an application that offers only ANSI text is still
        // readable here. Verified rather than assumed -- see review.md.
        wchar_t *text = (wchar_t *)GlobalLock(hData);
        if (text) {
            // GlobalSize is the only thing that is certain about the block.
            // Nothing obliges the process that wrote it to NUL-terminate it,
            // and the original called wcslen on it.
            size_t chars = GlobalSize(hData) / sizeof(wchar_t);
            previewBuild(text, chars, g_recentText, PREVIEW_BUFFER_CHARS);
            GlobalUnlock(hData);
        }
    }

    CloseClipboard();
    ShowRecentText();
}

// ----------------------------------------------------------------------
// Copy / Paste actions
// ----------------------------------------------------------------------

// Works out where the chord should go, brings that window forward if it is
// not already there, and returns it. NULL means "do not send anything", and
// the status line has already been written.
//
// The widget is WS_EX_NOACTIVATE so that clicking its buttons never takes the
// foreground away from whatever the user is copying from -- that is the
// insight the whole tool rests on. But WS_EX_NOACTIVATE only stops activation
// by clicking: the tray handler calls SetForegroundWindow on this window
// every time the icon is right-clicked, because a tray popup menu will not
// dismiss without it. After that the widget itself is the foreground window,
// and SendInput delivers to the foreground window whatever we would prefer.
static HWND AcquireTarget(void) {
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);

    HWND target = NULL;
    TargetAction action = targetDecide(&g_target, foreground, pid, g_ownPid, &target);

    // A remembered handle can outlive its window, and handles get reused.
    if (target && !IsWindow(target)) {
        targetForget(&g_target, target);
        action = TARGET_NONE;
    }

    switch (action) {
    case TARGET_USE_FOREGROUND:
        return target;

    case TARGET_RESTORE_FIRST:
        // We hold the foreground at this point, so this is allowed to work.
        SetForegroundWindow(target);
        if (GetForegroundWindow() != target) {
            SetStatus(L"Could not return to the window you were using. "
                      L"Click into it and try again.");
            return NULL;
        }
        return target;

    case TARGET_NONE:
    default:
        SetStatus(L"No window to copy from yet. Click into the window you "
                  L"want to use, then try again.");
        return NULL;
    }
}

static void DoCopy(void) {
    HWND target = AcquireTarget();
    if (!target) return;

    BOOL terminal = IsTerminalWindow(target);

    // The return value is the whole point. SendInput cannot deliver to a
    // window belonging to a higher-integrity process -- an administrator
    // console is exactly where "Ctrl+C means interrupt" matters most -- and
    // when it is blocked it inserts nothing and says so. The original
    // discarded that, so the button looked like it had worked and the panel
    // went on showing the previous clipboard content.
    KeysResult result = keysSendCombo(SendInput, terminal, 'C', NULL);
    if (result != KEYS_SENT) {
        SetStatus(keysResultMessage(result));
        return;
    }

    // No sleep, and no read here: the preview is updated from
    // WM_CLIPBOARDUPDATE when the clipboard actually changes. The original
    // slept 60 ms and read, which froze the widget for that long and showed
    // stale content whenever the source application took longer than that.
}

static void DoPaste(void) {
    HWND target = AcquireTarget();
    if (!target) return;

    // Paste always uses the plain-text chord, regardless of what has
    // focus - unlike Ctrl+C (which means "interrupt" in a terminal),
    // Ctrl+Shift+V is safe and expected everywhere the widget is used.
    KeysResult result = keysSendCombo(SendInput, TRUE, 'V', NULL);
    if (result != KEYS_SENT) SetStatus(keysResultMessage(result));
}

// ----------------------------------------------------------------------
// Tray icon
// ----------------------------------------------------------------------

// Returns FALSE if the icon could not be added, in which case the window has
// NOT been hidden. Hiding it anyway is how the original could leave the
// process running with no window and no icon: alive, listening, and
// unreachable short of Task Manager.
static BOOL HideToTray(void) {
    if (!trayShow(&g_tray, Shell_NotifyIconW)) return FALSE;

    ShowWindow(g_hwnd, SW_HIDE);
    g_hiddenToTray = TRUE;
    return TRUE;
}

static void RestoreFromTray(void) {
    trayRemove(&g_tray, Shell_NotifyIconW);
    ShowWindow(g_hwnd, SW_SHOW);
    g_hiddenToTray = FALSE;
}

// ----------------------------------------------------------------------
// Foreground tracking
//
// Records the last foreground window that was not ours, so that Copy has
// somewhere to aim when the widget itself is in front. Out-of-context, so
// the callback runs on this thread from the message loop; the filtering is
// in src/target.c.
// ----------------------------------------------------------------------
static void CALLBACK ForegroundChanged(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                       LONG idObject, LONG idChild,
                                       DWORD idEventThread, DWORD dwmsEventTime) {
    (void)hook; (void)event; (void)idChild; (void)idEventThread; (void)dwmsEventTime;
    if (idObject != OBJID_WINDOW || hwnd == NULL) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    targetNoteForeground(&g_target, hwnd, pid, g_ownPid);
}

// ----------------------------------------------------------------------
// Child control creation. The group box, label, and preview text stay
// fully native/themed via comctl32 v6 (see app.manifest) - only the
// Copy/Paste buttons are owner-drawn (WM_DRAWITEM below), which is what
// it takes to give them a custom background color; themed push buttons
// always paint their own background regardless of any brush handed back.
// ----------------------------------------------------------------------
static void CreateChildControls(HWND hwnd, HINSTANCE hInst) {
    // Distinct typography for the "Recently copied" caption vs. the
    // preview text under it - it's a label, not body copy, so it gets a
    // semibold treatment (see WM_CTLCOLORSTATIC for the matching muted
    // color). The preview text uses a plain regular font; Copy/Paste
    // share the label's size, just without the bold weight.
    g_labelFont = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_previewFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_buttonFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    // A BS_GROUPBOX gives a themed frame with the label as its caption,
    // in one native control - a bordered "Recently copied" panel with no
    // owner-drawing needed.
    g_groupRecent = CreateWindowExW(0, L"BUTTON", L"Recently copied",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        PADDING, PADDING, CLIENT_W - PADDING * 2, GROUP_H,
        hwnd, (HMENU)(INT_PTR)ID_GROUP_RECENT, hInst, NULL);
    SendMessageW(g_groupRecent, WM_SETFONT, (WPARAM)g_labelFont, TRUE);

    // Plain STATIC, not an EDIT box: this is a preview, not something the
    // user is meant to type into or think of as an input field. SS_LEFT
    // gives automatic word-wrap within the rect; SS_NOPREFIX stops any
    // literal "&" in copied text from being misread as an accelerator.
    // Sits inside the groupbox's frame, below its caption line, with
    // PREVIEW_VPAD of breathing room above and below the text itself.
    g_recentPreview = CreateWindowExW(0, L"STATIC", g_recentText,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        PADDING + GROUP_INSET, PADDING + LABEL_H + PREVIEW_VPAD,
        CLIENT_W - PADDING * 2 - GROUP_INSET * 2, GROUP_H - LABEL_H - PREVIEW_VPAD * 2,
        hwnd, (HMENU)(INT_PTR)ID_STATIC_RECENT, hInst, NULL);
    SendMessageW(g_recentPreview, WM_SETFONT, (WPARAM)g_previewFont, TRUE);

    // BS_OWNERDRAW instead of BS_PUSHBUTTON: a themed push button always
    // paints its own background regardless of what WM_CTLCOLORBTN returns,
    // so a custom button background color isn't reachable any other way.
    // WM_DRAWITEM in WndProc does the actual painting.
    int btnY = PADDING + GROUP_H + ROW_GAP;
    g_btnCopy = CreateWindowExW(0, L"BUTTON", L"Copy",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        PADDING, btnY, BTN_W, BTN_H,
        hwnd, (HMENU)(INT_PTR)ID_BTN_COPY, hInst, NULL);
    SendMessageW(g_btnCopy, WM_SETFONT, (WPARAM)g_buttonFont, TRUE);

    g_btnPaste = CreateWindowExW(0, L"BUTTON", L"Paste",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        CLIENT_W - PADDING - BTN_W, btnY, BTN_W, BTN_H,
        hwnd, (HMENU)(INT_PTR)ID_BTN_PASTE, hInst, NULL);
    SendMessageW(g_btnPaste, WM_SETFONT, (WPARAM)g_buttonFont, TRUE);
}

// Toggles the compact view: hides the "Recently copied" group/preview,
// moves the buttons up to where it was, and shrinks the window - it
// never actually iconifies (see the WM_SYSCOMMAND / SC_MINIMIZE handler
// in WndProc for why).
static void ApplyLayout(HWND hwnd) {
    int clientH = g_minimized ? CLIENT_H_COMPACT : CLIENT_H;
    int btnY    = g_minimized ? PADDING : (PADDING + GROUP_H + ROW_GAP);

    ShowWindow(g_groupRecent,   g_minimized ? SW_HIDE : SW_SHOW);
    ShowWindow(g_recentPreview, g_minimized ? SW_HIDE : SW_SHOW);

    SetWindowPos(g_btnCopy, NULL, PADDING, btnY, BTN_W, BTN_H, SWP_NOZORDER);
    SetWindowPos(g_btnPaste, NULL, CLIENT_W - PADDING - BTN_W, btnY, BTN_W, BTN_H, SWP_NOZORDER);

    RECT wr = { 0, 0, CLIENT_W, clientH };
    AdjustWindowRectEx(&wr, WIN_STYLE, FALSE, WIN_EXSTYLE);
    SetWindowPos(hwnd, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
        SWP_NOMOVE | SWP_NOZORDER);
}

// ----------------------------------------------------------------------
// Window procedure
// ----------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Registered at run time, so it cannot be a case label.
    if (msg == g_taskbarCreatedMsg && g_taskbarCreatedMsg != 0) {
        // Explorer has restarted and thrown every notification icon away. If
        // we were hidden, the icon was the only way back to this window, so
        // put it back -- and if that fails, show the window rather than stay
        // hidden with nothing to click.
        if (g_hiddenToTray && !trayRestore(&g_tray, Shell_NotifyIconW)) {
            ShowWindow(g_hwnd, SW_SHOW);
            g_hiddenToTray = FALSE;
        }
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        CreateChildControls(hwnd, ((LPCREATESTRUCT)lp)->hInstance);
        // The preview now follows the clipboard itself rather than being read
        // once, 60 ms after a button press, and on the assumption that the
        // source application had finished by then. This also means a copy
        // made with the keyboard updates the panel, which it never did.
        AddClipboardFormatListener(hwnd);
        return 0;
    }
    case WM_CLIPBOARDUPDATE: {
        UpdateRecentTextFromClipboard();
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD notif = HIWORD(wp);
        if (notif == BN_CLICKED) {
            if (id == ID_BTN_COPY)  { DoCopy();  return 0; }
            if (id == ID_BTN_PASTE) { DoPaste(); return 0; }
        }
        if (id == ID_TRAY_RESTORE) { RestoreFromTray(); return 0; }
        if (id == ID_TRAY_EXIT)    { DestroyWindow(hwnd); return 0; }
        return 0;
    }
    // BS_GROUPBOX also routes its caption through WM_CTLCOLORSTATIC (a
    // long-standing Win32 quirk). Give the label and the preview text
    // distinct colors to go with their distinct fonts, and keep both on
    // the same white background as the window.
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND ctrl = (HWND)lp;
        SetBkMode(hdc, TRANSPARENT);
        if (GetDlgCtrlID(ctrl) == ID_STATIC_RECENT) {
            SetTextColor(hdc, RGB(0x1C, 0x2B, 0x36)); // preview: dark, regular text
        } else {
            SetTextColor(hdc, RGB(0x3A, 0x4C, 0x58)); // label: muted, caption-like
        }
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (dis->CtlID != ID_BTN_COPY && dis->CtlID != ID_BTN_PASTE) break;

        BOOL pressed = (dis->itemState & ODS_SELECTED) != 0;

        // The grey here is deliberately the same COLOR_BTNFACE the whole
        // window background used to be, before the window itself became
        // white - just moved onto the buttons instead.
        FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(dis->hDC, &dis->rcItem, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);

        wchar_t text[32];
        GetWindowTextW(dis->hwndItem, text, ARRAYSIZE(text));

        RECT textRect = dis->rcItem;
        if (pressed) OffsetRect(&textRect, 1, 1); // classic "pressed" text nudge

        HFONT oldFont = (HFONT)SelectObject(dis->hDC, g_buttonFont);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, GetSysColor(COLOR_BTNTEXT));
        DrawTextW(dis->hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, oldFont);

        if (dis->itemState & ODS_FOCUS) {
            RECT focusRect = dis->rcItem;
            InflateRect(&focusRect, -4, -4);
            DrawFocusRect(dis->hDC, &focusRect);
        }
        return TRUE;
    }
    case WM_SYSCOMMAND: {
        // The native minimize box doesn't do a real OS iconify here - it
        // toggles a compact view (recent panel hidden, Copy/Paste still
        // visible, window just shrinks in place). A real iconify would
        // hide the buttons too, which isn't what this widget is for.
        if ((wp & 0xFFF0) == SC_MINIMIZE) {
            g_minimized = !g_minimized;
            ApplyLayout(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        // Closing (X) minimizes to the tray instead of quitting; the tray
        // icon's right-click menu has a real Exit. If the icon cannot be
        // added, stay visible and say so: hiding with no icon would leave
        // the user nothing to click.
        if (!HideToTray()) {
            SetStatus(L"Could not add a tray icon, so the window has stayed open. "
                      L"Use Exit from the tray menu, or close it again to retry.");
        }
        return 0;
    case WM_TRAYICON: {
        if (lp == WM_LBUTTONUP) {
            RestoreFromTray();
        } else if (lp == WM_RBUTTONUP) {
            POINT p; GetCursorPos(&p);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_RESTORE, L"Open");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
            SetForegroundWindow(hwnd); // required for the menu to dismiss correctly
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, NULL);
            DestroyMenu(menu);
        }
        return 0;
    }
    case WM_DESTROY:
        RemoveClipboardFormatListener(hwnd);
        trayRemove(&g_tray, Shell_NotifyIconW);
        if (g_foregroundHook) UnhookWinEvent(g_foregroundHook);
        if (g_labelFont)   DeleteObject(g_labelFont);
        if (g_previewFont) DeleteObject(g_previewFont);
        if (g_buttonFont)  DeleteObject(g_buttonFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ----------------------------------------------------------------------
// Entry point
// ----------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, PWSTR cmdLine, int cmdShow) {
    (void)hPrevInst; (void)cmdLine; (void)cmdShow;

    g_ownPid = GetCurrentProcessId();
    targetInit(&g_target);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Pulls in themed rendering for BUTTON/EDIT/STATIC per the current
    // visual style, as declared in app.manifest (comctl32 v6).
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = WND_CLASS_NAME;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    // Plain white background; child controls' backgrounds are matched to
    // this in WM_CTLCOLORSTATIC below.
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassExW(&wc);

    // WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX gives a normal, native
    // titlebar with real minimize and close boxes - no custom-drawn
    // chrome, and no WS_EX_TOOLWINDOW (that style would hide the minimize
    // glyph entirely). This does mean a normal taskbar entry appears,
    // even though clicking minimize doesn't send it there (see the
    // WM_SYSCOMMAND handler in WndProc) - a reasonable tradeoff for
    // genuinely native-looking buttons. WS_EX_NOACTIVATE is kept because
    // it's load-bearing for Copy's logic: clicking the widget must never
    // steal foreground focus away from whatever window the user is
    // actually copying from.
    RECT wr = { 0, 0, CLIENT_W, CLIENT_H };
    AdjustWindowRectEx(&wr, WIN_STYLE, FALSE, WIN_EXSTYLE);

    // Bottom-right corner of the primary monitor, like the original widget.
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = screenW - (wr.right - wr.left) - 24;
    int y = screenH - (wr.bottom - wr.top) - 48;

    g_hwnd = CreateWindowExW(
        WIN_EXSTYLE, WND_CLASS_NAME, APP_TITLE, WIN_STYLE,
        x, y, wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;

    // Built once, here, rather than rebuilt from zero on every hide. The tray
    // icon is a system icon because the project has no icon resource of its
    // own; app.rc embeds only the manifest. Sized to the small-icon metric,
    // which is what the notification area draws at.
    trayInit(&g_tray, g_hwnd, TRAY_ICON_ID, WM_TRAYICON,
        (HICON)LoadImageW(NULL, IDI_APPLICATION, IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED),
        APP_TITLE L" \x2014 click to reopen");

    // Out-of-context, so the callback is delivered to this thread through the
    // message loop below rather than injected into other processes.
    g_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, ForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);

    // Whatever is in front as we start is the window the user was last using.
    ForegroundChanged(NULL, EVENT_SYSTEM_FOREGROUND, GetForegroundWindow(),
        OBJID_WINDOW, CHILDID_SELF, 0, 0);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
