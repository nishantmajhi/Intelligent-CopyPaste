/*
 * target.h -- choosing the window a Copy or Paste should aim at.
 *
 * The widget is WS_EX_NOACTIVATE so that clicking its buttons never takes the
 * foreground away from whatever the user is copying from, which is what makes
 * GetForegroundWindow() the right question to ask at click time. That holds
 * for clicks on the widget -- but WS_EX_NOACTIVATE only stops activation by
 * clicking, it does not stop SetForegroundWindow, and the widget calls
 * SetForegroundWindow on itself every time the tray icon is right-clicked
 * (the call is not optional: without it a tray popup menu will not dismiss).
 *
 * So after any right-click on the tray icon the widget itself is the
 * foreground window, and a Copy from that state sends Ctrl+C to the widget --
 * which has no text and no selection, so nothing is copied, the clipboard
 * keeps whatever was already on it, and the preview panel shows that stale
 * content as though the copy had worked.
 *
 * The fix is to remember the last foreground window that was not ours and aim
 * at that instead. The decision is pure and lives here; src/target.c has no
 * hook and no window in it. The caller feeds it foreground changes from
 * whatever mechanism it likes and asks targetPick which window to use.
 */
#ifndef ICP_TARGET_H
#define ICP_TARGET_H

#include "winver.h"
#include <windows.h>

typedef struct {
    HWND last; /* most recent foreground window known to belong to someone else */
} TargetTracker;

void targetInit(TargetTracker *tracker);

/* Records a foreground change. Windows belonging to ownPid are ignored, which
   is the whole point: the remembered window is never ours. */
void targetNoteForeground(TargetTracker *tracker, HWND hwnd, DWORD pid, DWORD ownPid);

/* Which window a Copy or Paste should be sent to.
 *
 *   foreground     what GetForegroundWindow() returns right now
 *   foregroundPid  the process that owns it
 *   ownPid         this process
 *
 * Returns foreground when it belongs to somebody else, the last remembered
 * window when it does not, and NULL when there has never been one -- in which
 * case the caller must not send anything, because the only window available to
 * receive it is the widget's own. */
HWND targetPick(const TargetTracker *tracker, HWND foreground, DWORD foregroundPid, DWORD ownPid);

/* Drops a remembered window. Called when it is destroyed, so that a Copy does
   not aim at a handle that has since been reused for something else. */
void targetForget(TargetTracker *tracker, HWND hwnd);

typedef enum {
    TARGET_USE_FOREGROUND, /* the right window is already in front: just send */
    TARGET_RESTORE_FIRST,  /* bring the named window forward, then send */
    TARGET_NONE            /* there is nowhere to send: do not send anything */
} TargetAction;

/* The decision the caller actually needs, and the reason targetPick alone is
 * not enough: SendInput has no target parameter. It delivers to whatever has
 * the foreground, so knowing which window should receive the chord is only
 * half an answer -- if that window is not in front, it has to be put there
 * first or the keystroke goes to the widget and does nothing.
 *
 * On TARGET_RESTORE_FIRST, *window is the window to bring forward. On
 * TARGET_USE_FOREGROUND it is the foreground window, for the caller's
 * terminal check. On TARGET_NONE it is NULL. window may not be NULL. */
TargetAction targetDecide(const TargetTracker *tracker, HWND foreground,
                          DWORD foregroundPid, DWORD ownPid, HWND *window);

#endif /* ICP_TARGET_H */
