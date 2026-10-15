/*
 * preview.h -- turning a clipboard payload into the "Recently copied" line.
 *
 * Pure: nothing here opens the clipboard or touches a window. The caller locks
 * the clipboard block, hands over the pointer and how many wchar_t that block
 * actually holds, and gets back a string fit to put in a STATIC control.
 *
 * The length argument is the point of this module. A clipboard payload is
 * supposed to be NUL-terminated, but nothing enforces that -- any process can
 * SetClipboardData a block that is not -- and the original code called
 * wcslen() on it, which reads until it happens to find a zero somewhere past
 * the end of the allocation. GlobalSize() gives the real bound, so it is a
 * required argument rather than an optional one.
 */
#ifndef ICP_PREVIEW_H
#define ICP_PREVIEW_H

#include "winver.h"
#include <windows.h>
#include <stddef.h>

/* The most clipboard text the preview keeps. Longer payloads get an ellipsis. */
#define PREVIEW_MAX_CHARS 512

/* What a buffer handed to previewBuild needs to be, to hold PREVIEW_MAX_CHARS
   plus the ellipsis plus the NUL. */
#define PREVIEW_BUFFER_CHARS (PREVIEW_MAX_CHARS + 2)

/* Copies at most PREVIEW_MAX_CHARS characters of src into out, appending a
 * horizontal-ellipsis character if src was longer than that, and always
 * NUL-terminating.
 *
 *   src       the locked clipboard block; may be NULL
 *   srcChars  how many wchar_t src is known to hold, from
 *             GlobalSize(handle) / sizeof(wchar_t). The scan stops at the
 *             first NUL or at srcChars, whichever comes first, so a payload
 *             that is not terminated is truncated rather than read past.
 *   out       destination, at least outCap wchar_t
 *   outCap    size of out in wchar_t, including room for the NUL
 *
 * Returns the number of characters written, not counting the NUL. Returns 0
 * and writes an empty string when there is nothing usable to show.
 *
 * Truncation never splits a surrogate pair: if the cut would land between a
 * high surrogate and its low half, the high surrogate is dropped too. Half a
 * pair is not a character, and a STATIC control draws it as a replacement
 * glyph. */
size_t previewBuild(const wchar_t *src, size_t srcChars, wchar_t *out, size_t outCap);

#endif /* ICP_PREVIEW_H */
