/*
 * winver.h -- the Windows API version every translation unit here targets.
 *
 * This has to be included before <windows.h> or the declarations it unlocks
 * are simply absent and the call sites compile to implicit declarations that
 * return int. Two things in this project need it: QueryFullProcessImageNameW
 * (Vista) in src/intelligent_copy_paste.c, and CompareStringOrdinal (Vista)
 * in src/terminal.c.
 */
#ifndef ICP_WINVER_H
#define ICP_WINVER_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 /* Windows 7 */
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#endif /* ICP_WINVER_H */
