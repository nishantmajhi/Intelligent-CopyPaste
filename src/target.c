#include "target.h"

void targetInit(TargetTracker *tracker) {
    if (tracker == NULL) return;
    tracker->last = NULL;
}

void targetNoteForeground(TargetTracker *tracker, HWND hwnd, DWORD pid, DWORD ownPid) {
    if (tracker == NULL || hwnd == NULL) return;
    if (pid == 0 || pid == ownPid) return;
    tracker->last = hwnd;
}

HWND targetPick(const TargetTracker *tracker, HWND foreground, DWORD foregroundPid, DWORD ownPid) {
    if (foreground != NULL && foregroundPid != 0 && foregroundPid != ownPid) {
        return foreground;
    }
    /* The foreground is ours, or there isn't one. Fall back to the last window
       that wasn't -- which is the window the user was working in before they
       touched the widget, and so still the one they meant. */
    return tracker != NULL ? tracker->last : NULL;
}

void targetForget(TargetTracker *tracker, HWND hwnd) {
    if (tracker == NULL) return;
    if (tracker->last == hwnd) tracker->last = NULL;
}

TargetAction targetDecide(const TargetTracker *tracker, HWND foreground,
                          DWORD foregroundPid, DWORD ownPid, HWND *window) {
    if (window == NULL) return TARGET_NONE;

    HWND pick = targetPick(tracker, foreground, foregroundPid, ownPid);
    if (pick == NULL) {
        *window = NULL;
        return TARGET_NONE;
    }

    *window = pick;
    return (pick == foreground) ? TARGET_USE_FOREGROUND : TARGET_RESTORE_FIRST;
}
