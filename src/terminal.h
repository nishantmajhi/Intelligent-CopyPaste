/*
 * terminal.h -- deciding whether a window is a terminal.
 *
 * Copy sends Ctrl+Shift+C to terminals and plain Ctrl+C everywhere else,
 * because in a console Ctrl+C means "send SIGINT", not "copy". That decision
 * is made entirely from two strings -- a window class name and a process
 * image path -- so it lives here, with no window handle and no process handle
 * in sight, and tests/copypaste_test.c calls it directly.
 *
 * The allowlist is deliberately conservative. A false negative just means the
 * universal Ctrl+C is sent, which is correct for almost every application; a
 * false positive sends a chord the target may have bound to something else.
 */
#ifndef ICP_TERMINAL_H
#define ICP_TERMINAL_H

#include "winver.h"
#include <windows.h>

/* Returns the file name portion of a path: the text after the last backslash
   or forward slash, or the whole string if it has neither. NULL in, NULL out. */
const wchar_t *termBaseName(const wchar_t *path);

/* TRUE when className is one of the known terminal window classes. */
BOOL termClassIsTerminal(const wchar_t *className);

/* TRUE when the executable at imagePath is a known terminal host. imagePath
   may be a full path or a bare file name; only the file name is compared. */
BOOL termImageIsTerminal(const wchar_t *imagePath);

/* The combined decision, for a window whose class name and owning process
   image are already known. Either may be NULL or empty. */
BOOL termIsTerminal(const wchar_t *className, const wchar_t *imagePath);

/* Case-insensitive comparison that does not depend on the CRT locale.
   Exposed because allowlist matching is the whole of this module, and a test
   that cannot see the comparison cannot pin its behaviour. */
BOOL termEqualsIgnoreCase(const wchar_t *a, const wchar_t *b);

#endif /* ICP_TERMINAL_H */
