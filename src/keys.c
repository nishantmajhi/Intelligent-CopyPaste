#include "keys.h"

int keysBuildCombo(BOOL withShift, WORD letterVk, INPUT *out, size_t outCap) {
    if (out == NULL) return 0;

    size_t needed = withShift ? 6 : 4;
    if (outCap < needed) return 0;

    ZeroMemory(out, needed * sizeof(INPUT));
    int n = 0;

    out[n].type = INPUT_KEYBOARD; out[n].ki.wVk = VK_CONTROL; n++;
    if (withShift) { out[n].type = INPUT_KEYBOARD; out[n].ki.wVk = VK_SHIFT; n++; }
    out[n].type = INPUT_KEYBOARD; out[n].ki.wVk = letterVk; n++;

    out[n].type = INPUT_KEYBOARD; out[n].ki.wVk = letterVk;
    out[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    if (withShift) {
        out[n].type = INPUT_KEYBOARD; out[n].ki.wVk = VK_SHIFT;
        out[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
    }
    out[n].type = INPUT_KEYBOARD; out[n].ki.wVk = VK_CONTROL;
    out[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;

    return n;
}

/* Lets go of Ctrl and Shift. Called only after a partial send, where the
   key-down events reached the target but the matching key-up events did not:
   as far as that application is concerned the user is still holding Ctrl, and
   every subsequent keystroke is a chord until something releases it. */
static void releaseModifiers(KeysSendFn send) {
    INPUT up[2];
    ZeroMemory(up, sizeof(up));
    up[0].type = INPUT_KEYBOARD; up[0].ki.wVk = VK_SHIFT;   up[0].ki.dwFlags = KEYEVENTF_KEYUP;
    up[1].type = INPUT_KEYBOARD; up[1].ki.wVk = VK_CONTROL; up[1].ki.dwFlags = KEYEVENTF_KEYUP;
    send(2, up, sizeof(INPUT));
}

KeysResult keysSendCombo(KeysSendFn send, BOOL withShift, WORD letterVk, UINT *inserted) {
    if (inserted != NULL) *inserted = 0;
    if (send == NULL) return KEYS_BAD_ARGS;

    INPUT events[KEYS_MAX_INPUTS];
    int n = keysBuildCombo(withShift, letterVk, events, KEYS_MAX_INPUTS);
    if (n == 0) return KEYS_BAD_ARGS;

    UINT sent = send((UINT)n, events, sizeof(INPUT));
    if (inserted != NULL) *inserted = sent;

    if (sent == (UINT)n) return KEYS_SENT;
    if (sent == 0) return KEYS_BLOCKED;

    releaseModifiers(send);
    return KEYS_PARTIAL;
}

const wchar_t *keysResultMessage(KeysResult result) {
    switch (result) {
    case KEYS_SENT:
        return L"";
    case KEYS_BLOCKED:
        return L"Windows refused the keystroke. The focused window is most "
               L"likely running as administrator; this widget has to be too.";
    case KEYS_PARTIAL:
        return L"Only part of the keystroke got through. Try again.";
    case KEYS_BAD_ARGS:
    default:
        return L"The keystroke could not be built.";
    }
}
