/*
 * keys.h -- building and sending the Ctrl(+Shift)+letter chord.
 *
 * Two things live here. Building the INPUT array is pure and is tested
 * directly. Sending it is not, so SendInput arrives as a function pointer:
 * that is what lets a test drive the "the injection was refused" path, which
 * is otherwise only reachable by putting an elevated window in the foreground.
 *
 * Why that path matters: SendInput cannot deliver to a window belonging to a
 * higher-integrity process. User Interface Privilege Isolation drops the
 * events and the call returns 0. An administrator PowerShell is exactly where
 * "Ctrl+C means interrupt, use Ctrl+Shift+C" is most useful, so the widget's
 * most valuable case is also the one where it silently does nothing -- unless
 * the return value is checked, which is what keysSendCombo is for.
 */
#ifndef ICP_KEYS_H
#define ICP_KEYS_H

#include "winver.h"
#include <windows.h>
#include <stddef.h>

/* The longest sequence keysBuildCombo produces: Ctrl down, Shift down, letter
   down, letter up, Shift up, Ctrl up. */
#define KEYS_MAX_INPUTS 6

typedef enum {
    KEYS_SENT = 0,   /* every event was inserted */
    KEYS_BLOCKED,    /* nothing was inserted -- UIPI, or a locked desktop */
    KEYS_PARTIAL,    /* some events went in: the target saw part of a chord */
    KEYS_BAD_ARGS    /* nothing was attempted */
} KeysResult;

/* Fills out with the chord's key-down and key-up events, in the order a
 * target application expects to see them: modifiers down, letter down, letter
 * up, modifiers up in reverse. Returns how many INPUT records were written,
 * or 0 if out is NULL or outCap is too small. */
int keysBuildCombo(BOOL withShift, WORD letterVk, INPUT *out, size_t outCap);

/* The signature of SendInput, so a test can substitute its own. */
typedef UINT (WINAPI *KeysSendFn)(UINT cInputs, LPINPUT pInputs, int cbSize);

/* Builds the chord and sends it through send.
 *
 * If inserted is not NULL it receives the number of events the send function
 * reported inserting, which is what distinguishes "refused outright" from
 * "half a chord got through" -- a partial send can leave Ctrl logically stuck
 * down in the target, so the caller needs to know the difference. */
KeysResult keysSendCombo(KeysSendFn send, BOOL withShift, WORD letterVk, UINT *inserted);

/* A short explanation of a result, for the status line. Never NULL. */
const wchar_t *keysResultMessage(KeysResult result);

#endif /* ICP_KEYS_H */
