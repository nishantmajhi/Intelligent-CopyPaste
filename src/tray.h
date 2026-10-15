/*
 * tray.h -- the notification-area icon, as a state machine.
 *
 * Closing the widget's window hides it to the tray rather than quitting, and
 * the tray icon is then the only way to get it back. That makes adding the
 * icon a load-bearing operation, and the original code treated it as one that
 * cannot fail:
 *
 *   Shell_NotifyIconW(NIM_ADD, &g_nid);
 *   ShowWindow(g_hwnd, SW_HIDE);
 *
 * NIM_ADD does fail. It fails while Explorer is restarting, and it fails when
 * the notification area is not yet accepting icons at the moment it is asked.
 * When it does, the window is hidden anyway and there is no icon to click:
 * the widget is still running, still holding its hotkeys and its clipboard
 * listener, and completely unreachable short of Task Manager.
 *
 * Separately, an icon that was added successfully is discarded when Explorer
 * restarts, and the shell asks every application to put its icons back by
 * broadcasting a "TaskbarCreated" message. The original never registered for
 * it, so an Explorer restart while hidden reached the same dead end.
 *
 * So: trayShow reports whether the icon is actually there, the caller hides
 * the window only if it is, and trayRestore puts it back after a restart.
 * Shell_NotifyIconW arrives as a function pointer so that all of this can be
 * tested without a notification area -- the failure being fixed is one that
 * nothing here can provoke in a real shell on demand.
 */
#ifndef ICP_TRAY_H
#define ICP_TRAY_H

#include "winver.h"
#include <windows.h>
#include <shellapi.h>

/* The signature of Shell_NotifyIconW. */
typedef BOOL (WINAPI *TrayNotifyFn)(DWORD message, PNOTIFYICONDATAW data);

typedef struct {
    NOTIFYICONDATAW nid;
    BOOL present; /* the icon is in the notification area as far as we know */
} TrayState;

/* Fills in the NOTIFYICONDATAW once, at startup. The original rebuilt it from
   zero on every hide, which is how a second icon can appear if a hide ever
   runs without a matching restore. */
void trayInit(TrayState *state, HWND hwnd, UINT uID, UINT callbackMessage,
              HICON icon, const wchar_t *tip);

/* Adds the icon if it is not already there. Returns TRUE when the icon is in
 * the notification area on return -- which is the question the caller actually
 * has, and the reason this returns anything at all. Do not hide the window
 * unless this returned TRUE. */
BOOL trayShow(TrayState *state, TrayNotifyFn notify);

/* Removes the icon if it is there. Safe to call when it is not. */
void trayRemove(TrayState *state, TrayNotifyFn notify);

/* Re-adds the icon after the shell has told us it lost it (TaskbarCreated).
 * Forgets that the icon was ever present first, because it is not: the
 * notification area that held it no longer exists. Returns TRUE if the icon
 * is back. */
BOOL trayRestore(TrayState *state, TrayNotifyFn notify);

#endif /* ICP_TRAY_H */
