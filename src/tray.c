#include "tray.h"

#include <wchar.h>

void trayInit(TrayState *state, HWND hwnd, UINT uID, UINT callbackMessage,
              HICON icon, const wchar_t *tip) {
    if (state == NULL) return;

    ZeroMemory(state, sizeof(*state));
    state->nid.cbSize = sizeof(state->nid);
    state->nid.hWnd = hwnd;
    state->nid.uID = uID;
    state->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    state->nid.uCallbackMessage = callbackMessage;
    state->nid.hIcon = icon;
    if (tip != NULL) {
        /* szTip is a fixed 128 characters. wcsncpy leaves it unterminated if
           the tip is exactly that long, so terminate it explicitly. */
        size_t cap = sizeof(state->nid.szTip) / sizeof(state->nid.szTip[0]);
        wcsncpy(state->nid.szTip, tip, cap - 1);
        state->nid.szTip[cap - 1] = L'\0';
    }
    state->present = FALSE;
}

BOOL trayShow(TrayState *state, TrayNotifyFn notify) {
    if (state == NULL || notify == NULL) return FALSE;
    if (state->present) return TRUE;

    if (notify(NIM_ADD, &state->nid)) {
        state->present = TRUE;
    }
    return state->present;
}

void trayRemove(TrayState *state, TrayNotifyFn notify) {
    if (state == NULL || notify == NULL) return;
    if (!state->present) return;

    notify(NIM_DELETE, &state->nid);
    state->present = FALSE;
}

BOOL trayRestore(TrayState *state, TrayNotifyFn notify) {
    if (state == NULL || notify == NULL) return FALSE;

    /* Whatever we thought we had is gone with the old notification area. */
    state->present = FALSE;
    return trayShow(state, notify);
}
