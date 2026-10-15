#include "preview.h"

#define ELLIPSIS L'\x2026'

/* TRUE for the first half of a surrogate pair. */
static BOOL isHighSurrogate(wchar_t c) {
    return c >= 0xD800 && c <= 0xDBFF;
}

size_t previewBuild(const wchar_t *src, size_t srcChars, wchar_t *out, size_t outCap) {
    if (out == NULL || outCap == 0) return 0;

    out[0] = L'\0';
    if (src == NULL || srcChars == 0) return 0;

    /* How long the payload really is: the first NUL, or the end of the block.
       Never wcslen(src) -- see the note in preview.h. */
    size_t len = 0;
    while (len < srcChars && src[len] != L'\0') len++;
    if (len == 0) return 0;

    BOOL truncated = (len > PREVIEW_MAX_CHARS);
    size_t keep = truncated ? (size_t)PREVIEW_MAX_CHARS : len;

    /* Now fit that into the destination, which may be smaller than the
       character limit. Doing this second, rather than folding it into the
       limit above, is what keeps the ellipsis: a cut forced by the buffer is
       still a cut, and has to be signalled like any other. */
    if (keep > outCap - 1) truncated = TRUE;
    size_t maxKeep = truncated ? (outCap >= 2 ? outCap - 2 : 0) : outCap - 1;
    if (keep > maxKeep) keep = maxKeep;

    /* A cut that lands between the two halves of a surrogate pair would leave
       a lone high surrogate at the end, which is not a character. Drop it. */
    if (truncated && keep > 0 && isHighSurrogate(src[keep - 1])) keep--;

    for (size_t i = 0; i < keep; i++) out[i] = src[i];

    size_t written = keep;
    if (truncated && written + 1 < outCap) out[written++] = ELLIPSIS;
    out[written] = L'\0';
    return written;
}
